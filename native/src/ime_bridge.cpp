#include "ime_bridge.h"
#include "ime_state_manager.h"
#include "tsf_monitor.h"
#include "imm32_monitor.h"
#include "jni_callback.h"
#include "sta_thread.h"
#include "win_event_bridge.h"
#include "ime_callback.h"
#include <windows.h>
#include <imm.h>
#include <string>
#include <algorithm>
#include <vector>
#include <thread>
#include <atomic>
#include <future>
#include <debugapi.h>

#pragma comment(lib, "imm32.lib")

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#ifdef CHINESEIME_DEBUG
#define DEBUG_LOG(format, ...) do { \
    char buf[512]; \
    sprintf_s(buf, format, __VA_ARGS__); \
    OutputDebugStringA(buf); \
} while(0)
#define DEBUG_LOG_SIMPLE(msg) OutputDebugStringA(msg)
#else
#define DEBUG_LOG(format, ...)
#define DEBUG_LOG_SIMPLE(msg)
#endif

namespace {

PreeditCallback g_preeditCallback = nullptr;
CommitCallback g_commitCallback = nullptr;
CandidateCallback g_candidateCallback = nullptr;
ImeChangeCallback g_imeChangeCallback = nullptr;
KeyboardCallback g_keyboardCallback = nullptr;

std::unique_ptr<chineseime::StaThread> g_staThread;
std::unique_ptr<chineseime::TsfMonitor> g_tsfMonitor;
std::unique_ptr<chineseime::Imm32Monitor> g_imm32Monitor;
std::atomic<bool> g_tsfInitialized{false};
std::atomic<bool> g_imm32Initialized{false};
static DWORD g_dwTfClientId = TF_CLIENTID_NULL;

std::thread g_pollingThread;
std::atomic<bool> g_pollingRunning{false};
HWND g_targetWindow = nullptr;

const char* VERSION = "2.2.0";

bool IsChineseLangId(LANGID langId) {
    return langId == 0x0804 || langId == 0x0404 || langId == 0x0C04 || langId == 0x1404;
}

chineseime::InputMethodType DetectInputMethodTypeFromHkl(HKL hkl) {
    LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
    DWORD_PTR hklValue = reinterpret_cast<DWORD_PTR>(hkl);
    WORD imeId = HIWORD(hklValue);
    chineseime::InputMethodType type = chineseime::detectInputMethodTypeFromImeId(imeId, langId);

    WCHAR klName[16] = {0};
    if (GetKeyboardLayoutNameW(klName) && klName[0] && type == chineseime::InputMethodType::OTHER_CHINESE && IsChineseLangId(langId)) {
        WCHAR layoutLow = klName[7];
        WCHAR layoutHigh = klName[6];
        switch (layoutLow) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            type = chineseime::detectInputMethodTypeFromImeId(
                static_cast<WORD>(layoutLow - L'0' + ((layoutHigh - L'0') << 4)), langId);
            break;
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': {
            WORD lowNibble = (layoutLow >= L'a') ? (WORD)(layoutLow - L'a' + 10) : (WORD)(layoutLow - L'A' + 10);
            WORD highNibble = (layoutHigh >= L'a') ? (WORD)(layoutHigh - L'a' + 10) : (WORD)(layoutHigh - L'A' + 10);
            type = chineseime::detectInputMethodTypeFromImeId(
                static_cast<WORD>(lowNibble + (highNibble << 4)), langId);
            break;
        }
        }
    }
    return type;
}

void PollKeyboardState() {
    bool capsLockOn = false;
    bool shiftPressed = false;

    HWND fgWnd = g_targetWindow;
    if (!fgWnd) fgWnd = GetForegroundWindow();
    if (!fgWnd) fgWnd = GetActiveWindow();

    if (fgWnd) {
        DWORD fgThreadId = GetWindowThreadProcessId(fgWnd, nullptr);
        DWORD pollThreadId = GetCurrentThreadId();
        if (fgThreadId != pollThreadId) {
            AttachThreadInput(pollThreadId, fgThreadId, TRUE);
        }

        BYTE keyboardState[256];
        if (GetKeyboardState(keyboardState)) {
            capsLockOn = (keyboardState[VK_CAPITAL] & 0x01) != 0;
            shiftPressed = (keyboardState[VK_SHIFT] & 0x80) != 0;
        }

        if (fgThreadId != pollThreadId) {
            AttachThreadInput(pollThreadId, fgThreadId, FALSE);
        }
    } else {
        BYTE keyboardState[256];
        if (GetKeyboardState(keyboardState)) {
            capsLockOn = (keyboardState[VK_CAPITAL] & 0x01) != 0;
            shiftPressed = (keyboardState[VK_SHIFT] & 0x80) != 0;
        }
    }

    chineseime::ImeStateManager::get().updateKeyboardState(capsLockOn, shiftPressed);
}

void PollIMEState() {
    chineseime::ImeStateManager& mgr = chineseime::ImeStateManager::get();

    static int pollCount = 0;
    pollCount++;
    bool verbose = pollCount <= 10 || pollCount % 300 == 0;

    HWND fgWnd = g_targetWindow;
    if (!fgWnd) fgWnd = GetForegroundWindow();
    if (!fgWnd) fgWnd = GetActiveWindow();
    if (!fgWnd) return;

    HIMC himc = ImmGetContext(fgWnd);
    if (!himc) {
        HWND testWnd = GetForegroundWindow();
        if (testWnd && testWnd != fgWnd) {
            HIMC testHimc = ImmGetContext(testWnd);
            if (testHimc) {
                himc = testHimc;
                fgWnd = testWnd;
            }
        }
    }
    if (!himc) return;

    bool imeOpen = ImmGetOpenStatus(himc) != 0;
    mgr.updateImeOpen(imeOpen);

    bool chineseMode = false;
    DWORD conversion = 0;
    DWORD sentence = 0;
    if (ImmGetConversionStatus(himc, &conversion, &sentence)) {
        chineseMode = (conversion & IME_CMODE_NATIVE) != 0;
    }
    if (!imeOpen) {
        chineseMode = false;
    }
    mgr.updateChineseMode(chineseMode);

    DWORD fgThreadId = GetWindowThreadProcessId(fgWnd, nullptr);
    DWORD pollThreadId = GetCurrentThreadId();
    BOOL attached = FALSE;
    if (fgThreadId != pollThreadId) {
        attached = AttachThreadInput(pollThreadId, fgThreadId, TRUE);
    }

    HKL hkl = GetKeyboardLayout(0);
    WCHAR klName[16] = {0};
    GetKeyboardLayoutNameW(klName);
    chineseime::InputMethodType detectedType = chineseime::InputMethodType::UNKNOWN;
    WORD imeId = 0;
    if (hkl) {
        detectedType = DetectInputMethodTypeFromHkl(hkl);
        DWORD_PTR hklValue = reinterpret_cast<DWORD_PTR>(hkl);
        imeId = HIWORD(hklValue);
        
        // For TSF IMEs, imeId is often 0. Try to extract from layout name
        if (imeId == 0 && klName[0]) {
            WCHAR layoutLow = klName[7];
            WCHAR layoutHigh = klName[6];
            WORD extractedId = 0;
            if (layoutLow >= L'0' && layoutLow <= L'9' && layoutHigh >= L'0' && layoutHigh <= L'9') {
                extractedId = static_cast<WORD>((layoutHigh - L'0') * 16 + (layoutLow - L'0'));
            } else if (layoutLow >= L'A' && layoutLow <= L'F') {
                WORD lowNibble = static_cast<WORD>(layoutLow - L'A' + 10);
                WORD highNibble = static_cast<WORD>(layoutHigh - L'A' + 10);
                extractedId = static_cast<WORD>(lowNibble + (highNibble << 4));
            }
            if (extractedId > 0) {
                LANGID langId = LOWORD(hklValue);
                detectedType = chineseime::detectInputMethodTypeFromImeId(extractedId, langId);
            }
        }
    }

    auto cachedType = mgr.getSnapshot().inputMethodType;
    bool tsfHasSetType = (cachedType != chineseime::InputMethodType::UNKNOWN &&
                          cachedType != chineseime::InputMethodType::ENGLISH);

    if (verbose) {
        char dbg[256];
        sprintf_s(dbg, "[ChineseIME] PollIME #%d: kl=%S, imeId=0x%X, detected=%d, cached=%d, imeOpen=%d\n",
            pollCount, klName, imeId, (int)detectedType, (int)cachedType, imeOpen ? 1 : 0);
        OutputDebugStringA(dbg);
    }

    if (attached) {
        AttachThreadInput(pollThreadId, fgThreadId, FALSE);
    }

    // Always check for IME type changes when IME is open
    // Only skip update if types are the same
    if (imeOpen) {
        if (detectedType != chineseime::InputMethodType::UNKNOWN &&
            detectedType != chineseime::InputMethodType::ENGLISH &&
            detectedType != cachedType) {
            mgr.updateInputMethod(detectedType);
            char dbg[256];
            sprintf_s(dbg, "[ChineseIME] PollIME: updating IME type %d -> %d\n", (int)cachedType, (int)detectedType);
            OutputDebugStringA(dbg);
        }
    } else {
        // IME closed, check if we should set ENGLISH
        bool isChineseCached = (cachedType != chineseime::InputMethodType::ENGLISH &&
                                cachedType != chineseime::InputMethodType::UNKNOWN);
        if (isChineseCached) {
            mgr.updateInputMethod(chineseime::InputMethodType::ENGLISH);
            char dbg[128];
            sprintf_s(dbg, "[ChineseIME] PollIME: IME closed, setting ENGLISH\n");
            OutputDebugStringA(dbg);
        }
    }

    std::wstring composition;
    LONG compLen = ImmGetCompositionString(himc, GCS_COMPSTR, nullptr, 0);
    if (compLen <= 0) {
        compLen = ImmGetCompositionString(himc, GCS_COMPREADSTR, nullptr, 0);
    }
    if (compLen > 0) {
        int wcharLen = compLen / sizeof(wchar_t);
        std::vector<wchar_t> compBuf(wcharLen + 1);
        LONG actualLen = ImmGetCompositionString(himc, GCS_COMPSTR, compBuf.data(), compLen);
        if (actualLen <= 0) {
            actualLen = ImmGetCompositionString(himc, GCS_COMPREADSTR, compBuf.data(), compLen);
        }
        if (actualLen > 0) {
            int actualWcharLen = actualLen / sizeof(wchar_t);
            compBuf[actualWcharLen] = 0;
            composition.assign(compBuf.data(), actualWcharLen);
        }
    }

    std::vector<std::wstring> candidates;
    int selectedIndex = 0;
    size_t bufSize = ImmGetCandidateList(himc, 0, nullptr, 0);

    if (bufSize > 0) {
        std::vector<char> candBuf(bufSize);
        CANDIDATELIST* candList = reinterpret_cast<CANDIDATELIST*>(candBuf.data());
        ImmGetCandidateList(himc, 0, candList, bufSize);
        DWORD count = candList->dwCount;
        selectedIndex = candList->dwSelection;
        if (count > 10) count = 10;
        for (DWORD j = 0; j < count; j++) {
            wchar_t* pStr = (wchar_t*)(candBuf.data() + candList->dwOffset[j]);
            candidates.push_back(pStr);
        }
        char dbgBuf[256];
        sprintf_s(dbgBuf, "[ChineseIME] PollIME: got %d candidates, sel=%d\n", (int)count, selectedIndex);
        OutputDebugStringA(dbgBuf);
    }

    if (!composition.empty() || !candidates.empty()) {
        mgr.updateCandidates(composition, candidates, selectedIndex);
    }

    ImmReleaseContext(fgWnd, himc);
}

} // anonymous namespace

extern "C" {

__declspec(dllexport) void SetCallbacks(void* candidateUpdate, void* layoutChange, void* modeChange, void* keyboardState) {
}

__declspec(dllexport) int StartListen(void* hwnd) {
    if (g_tsfInitialized.load() || g_imm32Initialized.load()) return 1;

    g_targetWindow = hwnd ? reinterpret_cast<HWND>(hwnd) : nullptr;

    HKL hkl = GetKeyboardLayout(0);
    if (hkl) {
        chineseime::InputMethodType type = DetectInputMethodTypeFromHkl(hkl);
        chineseime::ImeStateManager::get().updateInputMethod(type);
        LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
        bool isChineseLang = IsChineseLangId(langId);
        chineseime::ImeStateManager::get().updateChineseMode(isChineseLang);
        chineseime::ImeStateManager::get().updateImeOpen(isChineseLang);
    }

    return 1;
}

__declspec(dllexport) void StopListen(void) {
}

__declspec(dllexport) int IsListening(void) {
    return (g_tsfInitialized.load() || g_imm32Initialized.load()) ? 1 : 0;
}

__declspec(dllexport) int StartTsfListen(void) {
    // Use a simple approach - try to create a file to verify this function is called
    FILE* f = fopen("C:\\Users\\user\\Documents\\ChineseIME-Fabric-1.21.4\\debug_log.txt", "a");
    if (f) {
        fprintf(f, "StartTsfListen called\n");
        fclose(f);
    }
    OutputDebugStringA("[ChineseIME] StartTsfListen: called");
    if (g_tsfInitialized.load()) {
        OutputDebugStringA("[ChineseIME] StartTsfListen: already initialized");
        return 1;
    }

    g_staThread = std::make_unique<chineseime::StaThread>();
    if (!g_staThread->start()) {
        OutputDebugStringA("[ChineseIME] StartTsfListen: StaThread start failed");
        FILE* f2 = fopen("C:\\Users\\user\\Documents\\ChineseIME-Fabric-1.21.4\\debug_log.txt", "a");
        if (f2) { fprintf(f2, "StaThread start failed\n"); fclose(f2); }
        return 0;
    }
    OutputDebugStringA("[ChineseIME] StartTsfListen: StaThread started");

    // Log to file to verify
    FILE* f3 = fopen("C:\\Users\\user\\Documents\\ChineseIME-Fabric-1.21.4\\debug_log.txt", "a");
    if (f3) { fprintf(f3, "StaThread started successfully\n"); fclose(f3); }

    std::promise<bool> initPromise;
    std::future<bool> initFuture = initPromise.get_future();

    g_staThread->submitTask([&initPromise]() {
        bool initialized = false;
        g_tsfMonitor = std::make_unique<chineseime::TsfMonitor>();

        ITfThreadMgr* pThreadMgr = nullptr;
        HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&pThreadMgr);
        if (SUCCEEDED(hr) && pThreadMgr) {
            hr = pThreadMgr->Activate(&g_dwTfClientId);
            if (SUCCEEDED(hr)) {
                initialized = g_tsfMonitor->initialize(pThreadMgr);
            }
            pThreadMgr->Release();
        }

        initPromise.set_value(initialized);
    });

    bool initialized = initFuture.get();
    if (!initialized) {
        g_staThread->stop();
        g_staThread.reset();
        g_tsfMonitor.reset();

        if (!g_imm32Initialized.load()) {
            g_imm32Monitor = std::make_unique<chineseime::Imm32Monitor>();
            if (g_imm32Monitor->initialize()) {
                g_imm32Initialized.store(true);
                initialized = true;
            }
        }

        if (!initialized) {
            return 0;
        }
    }

    g_tsfInitialized.store(true);

    HKL hkl = GetKeyboardLayout(0);
    if (hkl) {
        LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
        chineseime::InputMethodType type = DetectInputMethodTypeFromHkl(hkl);
        chineseime::ImeStateManager::get().updateInputMethod(type);
        chineseime::ImeStateManager::get().updateChineseMode(IsChineseLangId(langId));
        chineseime::ImeStateManager::get().updateImeOpen(IsChineseLangId(langId));
    }

    g_pollingRunning.store(true);
    g_pollingThread = std::thread([]() {
        DEBUG_LOG_SIMPLE("[ChineseIME] Polling thread started\n");
        PollKeyboardState();
        PollIMEState();
        {
            auto initialState = chineseime::ImeStateManager::get().getSnapshot();
            char buf[256];
            sprintf_s(buf, "[ChineseIME] Init: IME=%d, CMode=%d, Caps=%d\n",
                (int)initialState.inputMethodType, initialState.chineseMode ? 1 : 0,
                initialState.capsLockOn ? 1 : 0);
            OutputDebugStringA(buf);
        }
        while (g_pollingRunning.load()) {
            PollKeyboardState();
            PollIMEState();
            if (g_tsfMonitor) {
                g_tsfMonitor->pollUpdate();
            }
            Sleep(16);
        }
        DEBUG_LOG_SIMPLE("[ChineseIME] Polling thread stopped\n");
    });

    return 1;
}

__declspec(dllexport) void StopTsfListen(void) {
    g_pollingRunning.store(false);

    if (g_pollingThread.joinable()) {
        std::thread tmpThread = std::move(g_pollingThread);
        tmpThread.detach();
    }

    if (g_staThread && g_tsfMonitor) {
        g_staThread->submitTask([]() {
            if (g_tsfMonitor) {
                g_tsfMonitor->shutdown();
                g_tsfMonitor.reset();
            }
            if (g_dwTfClientId != TF_CLIENTID_NULL) {
                ITfThreadMgr* pThreadMgr = nullptr;
                HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, IID_ITfThreadMgr, (void**)&pThreadMgr);
                if (SUCCEEDED(hr) && pThreadMgr) {
                    pThreadMgr->Deactivate();
                    pThreadMgr->Release();
                }
                g_dwTfClientId = TF_CLIENTID_NULL;
            }
        });
    }

    if (g_staThread) {
        g_staThread->stop();
        g_staThread.reset();
    }

    if (g_imm32Monitor) {
        g_imm32Monitor->shutdown();
        g_imm32Monitor.reset();
    }

    g_tsfInitialized.store(false);
    g_imm32Initialized.store(false);
}

__declspec(dllexport) int IsTsfListening(void) {
    return g_tsfInitialized.load() ? 1 : 0;
}

__declspec(dllexport) int IsChineseMode(void) {
    return chineseime::ImeStateManager::get().getSnapshot().chineseMode ? 1 : 0;
}

__declspec(dllexport) int GetCompositionString(wchar_t* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return 0;
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    if (state.composition.empty()) {
        buffer[0] = 0;
        return 0;
    }
    int len = (int)state.composition.length();
    if (len >= bufferSize) len = bufferSize - 1;
    wcsncpy_s(buffer, (size_t)bufferSize, state.composition.c_str(), (size_t)len);
    buffer[len] = 0;
    return len;
}

__declspec(dllexport) int GetCandidateCount(void) {
    return (int)chineseime::ImeStateManager::get().getSnapshot().candidates.size();
}

__declspec(dllexport) int GetCandidate(int index, wchar_t* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return 0;
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    if (index < 0 || index >= (int)state.candidates.size()) {
        buffer[0] = 0;
        return 0;
    }
    const std::wstring& cand = state.candidates[index];
    int len = (int)cand.length();
    if (len >= bufferSize) len = bufferSize - 1;
    wcsncpy_s(buffer, (size_t)bufferSize, cand.c_str(), (size_t)len);
    buffer[len] = 0;
    return len;
}

__declspec(dllexport) int GetSelectedCandidateIndex(void) {
    return chineseime::ImeStateManager::get().getSnapshot().selectedIndex;
}

__declspec(dllexport) int GetImeOpenStatus(void) {
    return chineseime::ImeStateManager::get().getSnapshot().imeOpen ? 1 : 0;
}

__declspec(dllexport) int GetTsfChineseMode(void) {
    return chineseime::ImeStateManager::get().getSnapshot().chineseMode ? 1 : 0;
}

__declspec(dllexport) int HasTsfLayoutChanged(void) {
    return chineseime::ImeStateManager::get().checkLayoutChanged() ? 1 : 0;
}

__declspec(dllexport) int GetInputMethodType(void) {
    return (int)chineseime::ImeStateManager::get().getSnapshot().inputMethodType;
}

__declspec(dllexport) int GetShiftMode(void) {
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    bool isChineseInputMethod = state.inputMethodType != chineseime::InputMethodType::ENGLISH &&
        state.inputMethodType != chineseime::InputMethodType::UNKNOWN;
    bool inShiftMode = isChineseInputMethod && !state.chineseMode && state.imeOpen;
    return inShiftMode ? 1 : 0;
}

__declspec(dllexport) int GetCapsLockState(void) {
    return chineseime::ImeStateManager::get().getSnapshot().capsLockOn ? 1 : 0;
}

__declspec(dllexport) int GetKeyboardStateForPolling(int vKey) {
    BYTE keyboardState[256];
    if (GetKeyboardState(keyboardState)) {
        return (keyboardState[vKey] & 0x80) ? 1 : 0;
    }
    return 0;
}

__declspec(dllexport) void SetTargetWindow(void* hwnd) {
    g_targetWindow = hwnd ? reinterpret_cast<HWND>(hwnd) : nullptr;
}

__declspec(dllexport) void RefreshImeState(void) {
    PollKeyboardState();
    PollIMEState();
}

__declspec(dllexport) void FreeBuffer(void* ptr) {
    if (ptr) CoTaskMemFree(ptr);
}

__declspec(dllexport) const char* GetDllVersion(void) {
    return VERSION;
}

__declspec(dllexport) int HasLayoutChanged(void) {
    return chineseime::ImeStateManager::get().checkLayoutChanged() ? 1 : 0;
}

long GetKeyboardLayoutHKL(void) {
    HKL hkl = GetKeyboardLayout(0);
    return (long)reinterpret_cast<DWORD_PTR>(hkl);
}

__declspec(dllexport) void SetEventCallbacks(
    void* preedit,
    void* commit,
    void* candidate,
    void* imeChange,
    void* keyboard) {
    g_preeditCallback = reinterpret_cast<PreeditCallback>(preedit);
    g_commitCallback = reinterpret_cast<CommitCallback>(commit);
    g_candidateCallback = reinterpret_cast<CandidateCallback>(candidate);
    g_imeChangeCallback = reinterpret_cast<ImeChangeCallback>(imeChange);
    g_keyboardCallback = reinterpret_cast<KeyboardCallback>(keyboard);

    OutputDebugStringA("[ChineseIME] Event callbacks registered\n");
}

__declspec(dllexport) void HookWindowProc(void* hwnd) {
    char dbg[128];
    sprintf_s(dbg, "[ChineseIME] HookWindowProc called with hwnd=0x%llX\n", (unsigned long long)hwnd);
    OutputDebugStringA(dbg);

    HWND h = hwnd ? reinterpret_cast<HWND>(hwnd) : nullptr;
    sprintf_s(dbg, "[ChineseIME] HookWindowProc: HWND from void* = 0x%p\n", (void*)h);
    OutputDebugStringA(dbg);

    if (h) {
        SetTargetWindow(hwnd);
        sprintf_s(dbg, "[ChineseIME] HookWindowProc: calling hookWindow\n");
        OutputDebugStringA(dbg);
    }
    chineseime::WinEventBridge::get().hookWindow(h);
    sprintf_s(dbg, "[ChineseIME] HookWindowProc: hookWindow returned, hooked=%d\n",
        chineseime::WinEventBridge::get().isHooked() ? 1 : 0);
    OutputDebugStringA(dbg);
}

__declspec(dllexport) void UnhookWindowProc(void) {
    chineseime::WinEventBridge::get().unhookWindow();
}

__declspec(dllexport) void RefreshCandidates(void) {
    chineseime::WinEventBridge::get().refreshCandidates();
}

__declspec(dllexport) int IsWindowHooked(void) {
    return chineseime::WinEventBridge::get().isHooked() ? 1 : 0;
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        DEBUG_LOG_SIMPLE("[ChineseIME] DLL loaded\n");
        break;
    case DLL_PROCESS_DETACH:
        StopTsfListen();
        DEBUG_LOG_SIMPLE("[ChineseIME] DLL unloaded\n");
        break;
    }
    return TRUE;
}