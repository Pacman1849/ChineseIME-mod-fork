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

const char* VERSION = "2.5.0";

bool IsChineseLangId(LANGID langId) {
    return langId == 0x0804 || langId == 0x0404 || langId == 0x0C04 || langId == 0x1404;
}

chineseime::InputMethodType DetectInputMethodTypeFromHkl(HKL hkl) {
    LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
    DWORD_PTR hklValue = reinterpret_cast<DWORD_PTR>(hkl);
    WORD imeId = HIWORD(hklValue);

    // For TSF IMEs, imeId equals langId
    bool isTsfIme = (imeId == langId);

    if (isTsfIme) {
        // TSF IME - use language default
        if (langId == 0x0804) {
            return chineseime::InputMethodType::PINYIN;
        } else if (langId == 0x0404 || langId == 0x0C04 || langId == 0x1404 || langId == 0x1004) {
            return chineseime::InputMethodType::CANGJIE;
        }
    }

    // IMM32 IME - use IME ID
    return chineseime::detectInputMethodTypeFromImeId(imeId, langId);
}

void PollKeyboardState() {
    // Use GetAsyncKeyState for reliable physical key state detection
    // GetAsyncKeyState doesn't require thread attachment and always returns
    // the current state of the physical keyboard keys
    bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool capsLockOn = (GetKeyState(VK_CAPITAL) & 0x01) != 0;

    chineseime::ImeStateManager::get().updateKeyboardState(capsLockOn, shiftPressed);
}

void PollIMEState() {
    chineseime::ImeStateManager& mgr = chineseime::ImeStateManager::get();

    static int pollCount = 0;
    pollCount++;

    HWND fgWnd = g_targetWindow;
    if (!fgWnd) fgWnd = chineseime::WinEventBridge::GetWinEventTargetWindow();
    if (!fgWnd) fgWnd = GetForegroundWindow();
    if (!fgWnd) fgWnd = GetActiveWindow();
    if (!fgWnd) return;

    // CRITICAL: Attach to the target window's thread BEFORE calling ImmGetContext!
    DWORD fgThreadId = GetWindowThreadProcessId(fgWnd, nullptr);
    DWORD pollThreadId = GetCurrentThreadId();
    bool attached = false;
    if (fgThreadId != pollThreadId) {
        attached = AttachThreadInput(pollThreadId, fgThreadId, TRUE) != 0;
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] PollIMEState: AttachThreadInput to fgThreadId=%u, result=%d\n", fgThreadId, attached ? 1 : 0);
        OutputDebugStringA(dbg);
    }

    // Now safe to call ImmGetContext
    HIMC himc = ImmGetContext(fgWnd);
    if (!himc) {
        HWND testWnd = chineseime::WinEventBridge::GetWinEventTargetWindow();
        if (!testWnd) testWnd = GetForegroundWindow();
        if (testWnd && testWnd != fgWnd) {
            // Also attach to this window's thread
            DWORD testThreadId = GetWindowThreadProcessId(testWnd, nullptr);
            if (testThreadId != pollThreadId) {
                AttachThreadInput(pollThreadId, testThreadId, TRUE);
            }
            HIMC testHimc = ImmGetContext(testWnd);
            if (testHimc) {
                himc = testHimc;
                fgWnd = testWnd;
                fgThreadId = testThreadId;
            }
        }
    }
    if (!himc) {
        // Detach if we attached
        if (attached && fgThreadId != pollThreadId) {
            AttachThreadInput(pollThreadId, fgThreadId, FALSE);
        }
        return;
    }

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

    // Use fgThreadId to get the keyboard layout of the foreground window's thread
    HKL hkl = GetKeyboardLayout(fgThreadId);
    chineseime::InputMethodType detectedType = chineseime::InputMethodType::UNKNOWN;
    LANGID langId = 0;

    if (hkl) {
        DWORD_PTR hklValue = reinterpret_cast<DWORD_PTR>(hkl);
        WORD imeId = HIWORD(hklValue);
        langId = LOWORD(hklValue);

        // For TSF IMEs: imeId often equals the language ID (e.g., 0x0804 for zh-CN, 0x0404 for zh-TW)
        bool isTsfIme = (imeId == langId);

        // Try to detect IME type from HKL (for legacy IMM32 IMEs)
        // Only check if imeId is not equal to langId (i.e., not TSF IME)
        if (!isTsfIme && imeId != 0) {
            detectedType = chineseime::detectInputMethodTypeFromImeId(imeId, langId);
            char dbg[256];
            sprintf_s(dbg, "[ChineseIME] PollIME: IMM32 imeId=0x%X, detected=%d\n", imeId, (int)detectedType);
            OutputDebugStringA(dbg);
        }

        // For TSF IMEs (imeId == langId), use language-based defaults
        if (isTsfIme || detectedType == chineseime::InputMethodType::UNKNOWN ||
            detectedType == chineseime::InputMethodType::OTHER_CHINESE) {
            if (langId == 0x0804) {
                detectedType = chineseime::InputMethodType::PINYIN;
                char dbg[64];
                sprintf_s(dbg, "[ChineseIME] PollIME: TSF zh-CN -> PINYIN\n");
                OutputDebugStringA(dbg);
            } else if (langId == 0x0404 || langId == 0x0C04 || langId == 0x1404 || langId == 0x1004) {
                detectedType = chineseime::InputMethodType::CANGJIE;
                char dbg[64];
                sprintf_s(dbg, "[ChineseIME] PollIME: TSF zh-TW -> CANGJIE\n");
                OutputDebugStringA(dbg);
            }
        }

        // Log the final detected type
        char dbg[256];
        sprintf_s(dbg, "[ChineseIME] PollIME: hkl=0x%IX, imeId=0x%X, langId=0x%X, isTsf=%d, finalType=%d, open=%d\n",
            (DWORD64)hkl, imeId, langId, isTsfIme ? 1 : 0, (int)detectedType, imeOpen ? 1 : 0);
        OutputDebugStringA(dbg);

        // Update type based on HKL language
        // If HKL is not a Chinese keyboard, force ENGLISH
        bool isChineseHkl = (langId == 0x0804 || langId == 0x0404 || langId == 0x0C04 || langId == 0x1404 || langId == 0x1004);

        if (!isChineseHkl) {
            // Non-Chinese keyboard layout - force ENGLISH
            auto cachedType = mgr.getSnapshot().inputMethodType;
            if (cachedType != chineseime::InputMethodType::ENGLISH) {
                mgr.updateInputMethod(chineseime::InputMethodType::ENGLISH);
            }
        } else if (imeOpen) {
            // Chinese keyboard and IME is open - update detected type
            if (detectedType != chineseime::InputMethodType::UNKNOWN &&
                detectedType != chineseime::InputMethodType::ENGLISH &&
                detectedType != chineseime::InputMethodType::OTHER_CHINESE) {
                auto cachedType = mgr.getSnapshot().inputMethodType;
                if (detectedType != cachedType) {
                    mgr.updateInputMethod(detectedType);
                }
            }
        }
        // If Chinese HKL but IME closed, don't change - let TSF monitor handle it
    } else {
        OutputDebugStringA("[ChineseIME] PollIME: hkl is NULL\n");
    }

    // Keep thread attached during IMM32 reads!
    // Detach AFTER reading all IME data.

    // Read composition and candidates — BOTH must be read before deciding what to update.
    // Reading them sequentially causes a race: composition could be empty mid-frame while
    // candidates are still active, wiping the cached composition and breaking IsComposing().
    //
    // GCS_COMPSTR gives the converted (Chinese) composition string.
    // We no longer fall back to GCS_COMPREADSTR — that returns raw pinyin which should
    // never appear as a composition (it would bypass the containsChinese filter).
    std::wstring composition;
    LONG compLen = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
    if (compLen > 0) {
        int wcharLen = compLen / sizeof(wchar_t);
        std::vector<wchar_t> compBuf(wcharLen + 1);
        LONG actualLen = ImmGetCompositionStringW(himc, GCS_COMPSTR, compBuf.data(), compLen);
        if (actualLen > 0) {
            int actualWcharLen = actualLen / sizeof(wchar_t);
            compBuf[actualWcharLen] = 0;
            composition.assign(compBuf.data(), actualWcharLen);
        }
    }

    std::vector<std::wstring> candidates;
    int selectedIndex = 0;
    DWORD bufSize = ImmGetCandidateListW(himc, 0, nullptr, 0);

    if (bufSize > 0) {
        std::vector<char> candBuf(bufSize);
        CANDIDATELIST* candList = reinterpret_cast<CANDIDATELIST*>(candBuf.data());
        DWORD result = ImmGetCandidateListW(himc, 0, candList, bufSize);
        if (result > 0) {
            DWORD count = candList->dwCount;
            selectedIndex = (int)candList->dwSelection;
            if (count > 10) count = 10;
            for (DWORD j = 0; j < count; j++) {
                wchar_t* pStr = (wchar_t*)(candBuf.data() + candList->dwOffset[j]);
                candidates.push_back(pStr);
            }
        }
    }

    // KEY FIX: Only update if at least one of comp/candidates is non-empty.
    // If both are empty, skip this update entirely — don't wipe the cached state.
    // An empty-poll means the IME session might not have updated yet; the previous
    // cached values (from a prior frame) are still valid until we confirm they're gone.
    if (!composition.empty() || !candidates.empty()) {
        mgr.updateCandidates(composition, candidates, selectedIndex);
    }

    char dbg2[256];
    sprintf_s(dbg2, "[ChineseIME] PollIMEState: comp='%S', candCnt=%d, open=%d\n",
        composition.c_str(), (int)candidates.size(), imeOpen ? 1 : 0);
    OutputDebugStringA(dbg2);

    ImmReleaseContext(fgWnd, himc);

    // Now detach AFTER all IME reads are complete
    if (attached && fgThreadId != pollThreadId) {
        AttachThreadInput(pollThreadId, fgThreadId, FALSE);
    }
}

} // anonymous namespace

extern "C" {

__declspec(dllexport) void SetCallbacks(void* candidateUpdate, void* layoutChange, void* modeChange, void* keyboardState) {
}

__declspec(dllexport) int StartListen(void* hwnd) {
    if (g_tsfInitialized.load() || g_imm32Initialized.load()) return 1;

    g_targetWindow = hwnd ? reinterpret_cast<HWND>(hwnd) : nullptr;

    // Use the target window's thread to get the keyboard layout
    HWND targetWnd = g_targetWindow;
    HKL hkl = nullptr;
    if (targetWnd) {
        DWORD targetThreadId = GetWindowThreadProcessId(targetWnd, nullptr);
        hkl = GetKeyboardLayout(targetThreadId);
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] StartListen: targetThreadId=%u, hkl=0x%IX\n", targetThreadId, (DWORD64)(DWORD_PTR)hkl);
        OutputDebugStringA(dbg);
    }

    if (!hkl) {
        hkl = GetKeyboardLayout(0);  // Fallback
        OutputDebugStringA("[ChineseIME] StartListen: using GetKeyboardLayout(0) as fallback\n");
    }

    if (hkl) {
        chineseime::InputMethodType type = DetectInputMethodTypeFromHkl(hkl);
        chineseime::ImeStateManager::get().updateInputMethod(type);
        LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
        bool isChineseLang = IsChineseLangId(langId);
        chineseime::ImeStateManager::get().updateChineseMode(isChineseLang);
        chineseime::ImeStateManager::get().updateImeOpen(isChineseLang);

        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] StartListen: detected type=%d, langId=0x%X, isChinese=%d\n",
            (int)type, langId, isChineseLang ? 1 : 0);
        OutputDebugStringA(dbg);
    }

    return 1;
}

__declspec(dllexport) void StopListen(void) {
}

__declspec(dllexport) int IsListening(void) {
    return (g_tsfInitialized.load() || g_imm32Initialized.load()) ? 1 : 0;
}

__declspec(dllexport) int StartTsfListen(void) {
    OutputDebugStringA("[ChineseIME] StartTsfListen called\n");
    if (g_tsfInitialized.load()) {
        OutputDebugStringA("[ChineseIME] StartTsfListen: already initialized");
        return 1;
    }

    g_staThread = std::make_unique<chineseime::StaThread>();
    if (!g_staThread->start()) {
        OutputDebugStringA("[ChineseIME] StartTsfListen: StaThread start failed");
        return 0;
    }
    OutputDebugStringA("[ChineseIME] StartTsfListen: StaThread started");

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
    int count = (int)chineseime::ImeStateManager::get().getSnapshot().candidates.size();
    char dbg[64];
    sprintf_s(dbg, "[ChineseIME] GetCandidateCount: returning %d\n", count);
    OutputDebugStringA(dbg);
    return count;
}

__declspec(dllexport) int GetCandidate(int index, wchar_t* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return 0;
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    if (index < 0 || index >= (int)state.candidates.size()) {
        buffer[0] = 0;
        return 0;
    }
    const std::wstring& cand = state.candidates[index];
    char dbg[256];
    int candLen = (int)cand.length();
    sprintf_s(dbg, "[ChineseIME] GetCandidate[%d]: len=%d\n", index, candLen);
    OutputDebugStringA(dbg);
    int len = candLen;
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
    // Shift mode should be determined by actual shift key state from keyboard
    // The old logic (!chineseMode && imeOpen) is incorrect - it was detecting
    // when IME is in "half-width" or English mode, not shift state
    // Use the actual shiftPressed state instead
    return state.shiftPressed ? 1 : 0;
}

__declspec(dllexport) int GetCapsLockState(void) {
    return chineseime::ImeStateManager::get().getSnapshot().capsLockOn ? 1 : 0;
}

__declspec(dllexport) int GetKeyboardStateForPolling(int vKey) {
    // KEY FIX: Use GetAsyncKeyState (same as PollKeyboardState) instead of
    // GetKeyboardState. GetKeyboardState reads the thread's keyboard state table
    // which may be stale/desynced when called from the Java game thread.
    // GetAsyncKeyState reads the hardware keyboard state and works from any thread.
    return (GetAsyncKeyState(vKey) & 0x8000) ? 1 : 0;
}

__declspec(dllexport) void SetTargetWindow(void* hwnd) {
    g_targetWindow = hwnd ? reinterpret_cast<HWND>(hwnd) : nullptr;
}

__declspec(dllexport) void RefreshImeState(void) {
    OutputDebugStringA("[ChineseIME] RefreshImeState: entry\n");
    PollKeyboardState();
    PollIMEState();
    if (g_tsfMonitor) {
        g_tsfMonitor->refreshState();
        OutputDebugStringA("[ChineseIME] RefreshImeState: refreshState done\n");
    }
}

__declspec(dllexport) void FreeBuffer(void* ptr) {
    if (ptr) CoTaskMemFree(ptr);
}

__declspec(dllexport) const wchar_t* GetDllVersion(void) {
    return L"2.5.0";
}

__declspec(dllexport) int IsComposing(void) {
    // Check if there's a non-empty composition string
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    bool composing = !state.composition.empty();

    // Debug logging (will show in DebugView)
    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] IsComposing: composing=%d, composition='%S', candidates=%d\n",
        composing ? 1 : 0, state.composition.c_str(), (int)state.candidates.size());
    OutputDebugStringA(dbg);

    return composing ? 1 : 0;
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

    chineseime::WinEventBridge::EventCallbacks evCallbacks;

    if (preedit) {
        PreeditCallback pc = reinterpret_cast<PreeditCallback>(preedit);
        evCallbacks.preeditCallback = [pc](const wchar_t* text, int cursorPos, int selStart, int selLen) {
            pc(text, cursorPos, selStart, selLen);
        };
    }
    if (commit) {
        CommitCallback cc = reinterpret_cast<CommitCallback>(commit);
        evCallbacks.commitCallback = [cc](const wchar_t* text) {
            cc(text);
        };
    }
    if (candidate) {
        CandidateCallback candc = reinterpret_cast<CandidateCallback>(candidate);
        evCallbacks.candidateCallback = [candc](const wchar_t** cands, int count, int selIdx) {
            candc(cands, count, selIdx);
        };
    }

    chineseime::WinEventBridge::get().setCallbacks(std::move(evCallbacks));
    OutputDebugStringA("[ChineseIME] SetEventCallbacks: stored in globals AND WinEventBridge\n");
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

__declspec(dllexport) int GetCurrentKeyboardLayoutName(void* buffer, int bufferSize) {
    if (!buffer || bufferSize < 16) return 0;
    WCHAR klName[16] = {0};
    if (!GetKeyboardLayoutNameW(klName)) return 0;

    int len = 0;
    while (klName[len] && len < 15) len++;
    if (len * (int)sizeof(WCHAR) >= bufferSize) {
        len = (bufferSize / (int)sizeof(WCHAR)) - 1;
    }

    wmemcpy((wchar_t*)buffer, klName, len);
    ((wchar_t*)buffer)[len] = 0;

    char dbg[128];
    sprintf_s(dbg, "[ChineseIME] GetCurrentKeyboardLayoutName: %S\n", klName);
    OutputDebugStringA(dbg);

    return len * sizeof(WCHAR);
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