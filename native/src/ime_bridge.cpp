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
    chineseime::InputMethodType type = chineseime::detectInputMethodTypeFromImeId(imeId, langId);

    // For TSF IMEs, imeId equals langId, which means it's not a real IME ID
    // We need to extract the actual IME ID from klName
    bool isValidImeId = (imeId != 0 && imeId != langId);

    WCHAR klName[16] = {0};
    if (GetKeyboardLayoutNameW(klName) && klName[0] && (!isValidImeId || type == chineseime::InputMethodType::OTHER_CHINESE || type == chineseime::InputMethodType::UNKNOWN) && IsChineseLangId(langId)) {
        // Extract IME ID from positions 4-7
        WCHAR layoutLow = klName[7];
        WCHAR layoutHigh = klName[6];
        WORD extractedId = 0;

        switch (layoutLow) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            extractedId = static_cast<WORD>(layoutLow - L'0' + ((layoutHigh - L'0') << 4));
            break;
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': {
            WORD lowNibble = (layoutLow >= L'a') ? (WORD)(layoutLow - L'a' + 10) : (WORD)(layoutLow - L'A' + 10);
            WORD highNibble = (layoutHigh >= L'a') ? (WORD)(layoutHigh - L'a' + 10) : (WORD)(layoutHigh - L'A' + 10);
            extractedId = static_cast<WORD>(lowNibble + (highNibble << 4));
            break;
        }
        }

        // Reject pure language IDs
        bool isLanguageIdOnly = (extractedId == 0x0804 || extractedId == 0x0404 ||
                                 extractedId == 0x0C04 || extractedId == 0x1404 ||
                                 extractedId == 0x1004 || extractedId == 0);

        if (!isLanguageIdOnly) {
            type = chineseime::detectInputMethodTypeFromImeId(extractedId, langId);
        } else {
            // Try first 4 chars for TSF IMEs with longer layout names
            WCHAR fullLow = klName[3];
            WCHAR fullHigh = klName[2];
            WORD fullExtractedId = 0;

            switch (fullLow) {
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                fullExtractedId = static_cast<WORD>(fullLow - L'0' + ((fullHigh - L'0') << 4));
                break;
            case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
            case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': {
                WORD lowNibble = (fullLow >= L'a') ? (WORD)(fullLow - L'a' + 10) : (WORD)(fullLow - L'A' + 10);
                WORD highNibble = (fullHigh >= L'a') ? (WORD)(fullHigh - L'a' + 10) : (WORD)(fullHigh - L'A' + 10);
                fullExtractedId = static_cast<WORD>(lowNibble + (highNibble << 4));
                break;
            }
            }

            bool isValidFullId = (fullExtractedId != 0 &&
                fullExtractedId != 0x0804 && fullExtractedId != 0x0404 &&
                fullExtractedId != 0x0C04 && fullExtractedId != 0x1404 &&
                fullExtractedId != 0x1004);

            if (isValidFullId) {
                type = chineseime::detectInputMethodTypeFromImeId(fullExtractedId, langId);
            } else {
                // Use language default
                if (langId == 0x0804) {
                    type = chineseime::InputMethodType::PINYIN;
                } else if (langId == 0x0404 || langId == 0x0C04 || langId == 0x1404 || langId == 0x1004) {
                    type = chineseime::InputMethodType::CANGJIE;
                }
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

    if (verbose) {
        char dbgHkl[256];
        sprintf_s(dbgHkl, "[ChineseIME] PollIME: hkl=0x%IX, klName=%S\n", (DWORD64)hkl, klName);
        OutputDebugStringA(dbgHkl);
    }

    if (hkl) {
        DWORD_PTR hklValue = reinterpret_cast<DWORD_PTR>(hkl);
        imeId = HIWORD(hklValue);
        LANGID langId = LOWORD(hklValue);

        // For TSF IMEs, HIWORD(hkl) often returns just the language ID (e.g., 0x0804 for zh-CN),
        // not a real IME ID. Check if imeId == langId to detect this case.
        // A valid IME ID should NOT equal the language ID.
        bool isValidImeId = (imeId != 0 && imeId != langId);

        if (verbose) {
            char dbgHkl[256];
            sprintf_s(dbgHkl, "[ChineseIME] PollIME: hkl=0x%IX, imeId=0x%X, langId=0x%X, isValidImeId=%d, klName=%S\n",
                (DWORD64)hkl, imeId, langId, isValidImeId ? 1 : 0, klName);
            OutputDebugStringA(dbgHkl);
        }

        // Try direct IME ID from HKL first (for legacy IMM32 IMEs that have real IME IDs)
        if (isValidImeId) {
            detectedType = chineseime::detectInputMethodTypeFromImeId(imeId, langId);
            if (verbose) {
                char dbg[256];
                sprintf_s(dbg, "[ChineseIME] PollIME: valid IME ID 0x%X from HKL -> type=%d\n", imeId, (int)detectedType);
                OutputDebugStringA(dbg);
            }
        }

        // For TSF IMEs, HKL returns just the language ID (e.g., 0x00000804 for zh-CN)
        // In this case, imeId == langId, so we need to extract the real IME ID from klName[4:7].
        if ((imeId == 0 || !isValidImeId) && IsChineseLangId(langId) && klName[0]) {
            {
                char dbgTsf[256];
                sprintf_s(dbgTsf, "[ChineseIME] DEBUG: Entering TSF block, imeId=0x%X, langId=0x%X, isValidImeId=%d, klName=%S\n",
                    imeId, langId, isValidImeId ? 1 : 0, klName);
                OutputDebugStringA(dbgTsf);
            }
            // Try extracting IME ID from positions 4-7 (standard 8-char format)
            WCHAR imeIdStr[5] = {klName[4], klName[5], klName[6], klName[7], 0};
            WORD extractedId = 0;
            swscanf_s(imeIdStr, L"%4hx", &extractedId);

            // Reject pure language IDs - if extracted ID equals the current language ID,
            // it's not a real IME ID, just the language identifier
            bool isLanguageIdOnly = (extractedId == 0x0804 || extractedId == 0x0404 ||
                                     extractedId == 0x0C04 || extractedId == 0x1404 ||
                                     extractedId == 0x1004 || extractedId == 0);

            if (!isLanguageIdOnly) {
                detectedType = chineseime::detectInputMethodTypeFromImeId(extractedId, langId);
                if (verbose) {
                    char dbgTsf[256];
                    sprintf_s(dbgTsf, "[ChineseIME] PollIME: TSF IME (HKL=0, klName=%S), extracted IME ID 0x%X -> type=%d\n",
                        klName, extractedId, (int)detectedType);
                    OutputDebugStringA(dbgTsf);
                }
            } else {
                // Pure language ID layout - try longer layout names for TSF IMEs
                // TSF IME layout names can be longer than 8 chars (e.g., "E0030804")
                // The IME ID is in the first 4 chars (e.g., "E003")
                WCHAR fullImeIdStr[5] = {klName[0], klName[1], klName[2], klName[3], 0};
                WORD fullExtractedId = 0;
                swscanf_s(fullImeIdStr, L"%4hx", &fullExtractedId);

                if (verbose) {
                    char dbgTsf[256];
                    sprintf_s(dbgTsf, "[ChineseIME] PollIME: TSF IME, full klName=%S, first4=%S (0x%X), last4=%S (0x%X)\n",
                        klName, fullImeIdStr, fullExtractedId, imeIdStr, extractedId);
                    OutputDebugStringA(dbgTsf);
                }

                // If the first 4 chars give a valid (non-zero, non-language-id) IME ID, use it
                bool isValidFullId = (fullExtractedId != 0 &&
                    fullExtractedId != 0x0804 && fullExtractedId != 0x0404 &&
                    fullExtractedId != 0x0C04 && fullExtractedId != 0x1404 &&
                    fullExtractedId != 0x1004);

                if (isValidFullId) {
                    detectedType = chineseime::detectInputMethodTypeFromImeId(fullExtractedId, langId);
                    if (verbose) {
                        char dbgTsf[256];
                        sprintf_s(dbgTsf, "[ChineseIME] PollIME: TSF IME, full IME ID 0x%X from first4 -> type=%d\n",
                            fullExtractedId, (int)detectedType);
                        OutputDebugStringA(dbgTsf);
                    }
                } else {
                    // Pure language ID layout - default based on language
                    // For zh-TW (0x0404, 0x0C04, 0x1404), the default is CANGJIE/速成
                    // For zh-CN (0x0804), the default is PINYIN
                    {
                        char dbgTsf[256];
                        sprintf_s(dbgTsf, "[ChineseIME] DEBUG2: TSF pure lang, langId=0x%X, setting default\n", langId);
                        OutputDebugStringA(dbgTsf);
                    }
                    if (langId == 0x0804) {
                        detectedType = chineseime::InputMethodType::PINYIN;
                        {
                            char dbgTsf[256];
                            sprintf_s(dbgTsf, "[ChineseIME] DEBUG2: TSF set to PINYIN (zh-CN)\n");
                            OutputDebugStringA(dbgTsf);
                        }
                    } else if (langId == 0x0404 || langId == 0x0C04 || langId == 0x1404 || langId == 0x1004) {
                        // For zh-TW, default to CANGJIE (most common), but we should check
                        // if it's 速成 by trying other detection methods
                        detectedType = chineseime::InputMethodType::CANGJIE;
                        {
                            char dbgTsf[256];
                            sprintf_s(dbgTsf, "[ChineseIME] DEBUG2: TSF set to CANGJIE (zh-TW)\n");
                            OutputDebugStringA(dbgTsf);
                        }
                    } else {
                        detectedType = chineseime::InputMethodType::UNKNOWN;
                    }
                    if (verbose) {
                        char dbgTsf[256];
                        sprintf_s(dbgTsf, "[ChineseIME] PollIME: TSF IME (HKL=0, klName=%S), pure lang ID, default type=%d\n",
                            klName, (int)detectedType);
                        OutputDebugStringA(dbgTsf);
                    }
                }
            }
        } else if ((imeId == 0 || !isValidImeId) && IsChineseLangId(langId)) {
            // No klName available, use language default
            if (langId == 0x0804) {
                detectedType = chineseime::InputMethodType::PINYIN;
            } else {
                detectedType = chineseime::InputMethodType::CANGJIE;
            }
            if (verbose) {
                char dbgTsf[256];
                sprintf_s(dbgTsf, "[ChineseIME] PollIME: TSF IME (HKL=0, no klName), default type=%d\n",
                    (int)detectedType);
                OutputDebugStringA(dbgTsf);
            }
        }

        // Try extracting IME ID from layout name (positions 4-7) for any remaining UNKNOWN/OTHER_CHINESE
        if ((detectedType == chineseime::InputMethodType::OTHER_CHINESE ||
             detectedType == chineseime::InputMethodType::UNKNOWN) && klName[0]) {
            WCHAR imeIdStr[5] = {klName[4], klName[5], klName[6], klName[7], 0};
            WORD extractedId = 0;
            swscanf_s(imeIdStr, L"%4hx", &extractedId);
            // Only accept IME IDs that are NOT pure language IDs
            if (extractedId != 0 && extractedId != 0x0804 && extractedId != 0x0404 &&
                extractedId != 0x0C04 && extractedId != 0x1404) {
                detectedType = chineseime::detectInputMethodTypeFromImeId(extractedId, langId);
                if (verbose) {
                    char dbgExt[256];
                    sprintf_s(dbgExt, "[ChineseIME] PollIME: extracted IME ID 0x%X from klName -> type=%d\n",
                        extractedId, (int)detectedType);
                    OutputDebugStringA(dbgExt);
                }
            }
        }
    }

    auto cachedType = mgr.getSnapshot().inputMethodType;

    if (verbose) {
        char dbg[256];
        sprintf_s(dbg, "[ChineseIME] PollIME #%d: kl=%S, imeId=0x%X, detected=%d, cached=%d, imeOpen=%d\n",
            pollCount, klName, imeId, (int)detectedType, (int)cachedType, imeOpen ? 1 : 0);
        OutputDebugStringA(dbg);
    }

    if (attached) {
        AttachThreadInput(pollThreadId, fgThreadId, FALSE);
    }

    // Only update type if detection is meaningful and differs from cached
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
    // Use ImmGetCandidateListW for Unicode support
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
                // Offset is in bytes, so we need to cast to wchar_t* correctly
                wchar_t* pStr = (wchar_t*)(candBuf.data() + candList->dwOffset[j]);
                candidates.push_back(pStr);
            }
            char dbgBuf[256];
            sprintf_s(dbgBuf, "[ChineseIME] PollIME: got %d candidates, sel=%d\n", (int)count, selectedIndex);
            OutputDebugStringA(dbgBuf);
        }
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