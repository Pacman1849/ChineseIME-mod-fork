#include "ime_bridge.h"
#include "ime_state_manager.h"
#include "tsf_monitor.h"
#include "imm32_monitor.h"
#include "jni_callback.h"
#include "sta_thread.h"
#include <windows.h>
#include <imm.h>
#include <string>
#include <algorithm>
#include <vector>
#include <thread>
#include <atomic>
#include <future>

#pragma comment(lib, "imm32.lib")

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace {

std::unique_ptr<chineseime::StaThread> g_staThread;
std::unique_ptr<chineseime::TsfMonitor> g_tsfMonitor;
std::unique_ptr<chineseime::Imm32Monitor> g_imm32Monitor;
std::atomic<bool> g_tsfInitialized{false};
std::atomic<bool> g_imm32Initialized{false};
static DWORD g_dwTfClientId = TF_CLIENTID_NULL;

std::thread g_pollingThread;
std::atomic<bool> g_pollingRunning{false};
HWND g_targetWindow = nullptr;

const char* VERSION = "2.0.0";

chineseime::InputMethodType DetectInputMethodTypeFromHkl(HKL hkl) {
    LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
    DWORD_PTR hklValue = reinterpret_cast<DWORD_PTR>(hkl);
    WORD imeId = HIWORD(hklValue);
    return chineseime::detectInputMethodTypeFromImeId(imeId, langId);
}

bool IsChineseLangId(LANGID langId) {
    return langId == 0x0804 || langId == 0x0404 || langId == 0x0C04 || langId == 0x1404;
}

void PollKeyboardState() {
    BYTE keyboardState[256] = {0};
    if (GetKeyboardState(keyboardState)) {
        bool capsLockOn = (keyboardState[VK_CAPITAL] & 0x01) != 0;
        bool shiftPressed = (keyboardState[VK_SHIFT] & 0x80) != 0;
        chineseime::ImeStateManager::get().updateKeyboardState(capsLockOn, shiftPressed);
    }
}

void PollIMEState() {
    chineseime::ImeStateManager& mgr = chineseime::ImeStateManager::get();

    HKL hkl = GetKeyboardLayout(0);
    if (!hkl) return;

    LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
    DWORD_PTR hklValue = reinterpret_cast<DWORD_PTR>(hkl);
    WORD imeId = HIWORD(hklValue);

    bool isChineseLang = IsChineseLangId(langId);
    mgr.updateImeOpen(isChineseLang);

    if (!isChineseLang) {
        mgr.updateInputMethod(chineseime::InputMethodType::ENGLISH);
        mgr.updateChineseMode(false);
        return;
    }

    if (g_tsfMonitor) {
        chineseime::InputMethodType tsfType = g_tsfMonitor->getInputMethodType();
        if (tsfType != chineseime::InputMethodType::UNKNOWN &&
            tsfType != chineseime::InputMethodType::ENGLISH) {
            mgr.updateInputMethod(tsfType);
        } else {
            mgr.updateInputMethod(DetectInputMethodTypeFromHkl(hkl));
        }
    } else {
        mgr.updateInputMethod(DetectInputMethodTypeFromHkl(hkl));
    }

    HWND fgWnd = g_targetWindow;
    if (!fgWnd) fgWnd = GetForegroundWindow();
    if (!fgWnd) fgWnd = GetActiveWindow();

    if (fgWnd) {
        HIMC himc = ImmGetContext(fgWnd);
        if (himc) {
            bool imeOpen = ImmGetOpenStatus(himc) != 0;
            mgr.updateImeOpen(imeOpen);

            if (imeOpen) {
                DWORD conversion = 0;
                DWORD sentence = 0;
                if (ImmGetConversionStatus(himc, &conversion, &sentence)) {
                    bool chineseMode = (conversion & IME_CMODE_NATIVE) != 0;
                    mgr.updateChineseMode(chineseMode);
                }
            } else {
                mgr.updateChineseMode(false);
            }

            std::wstring composition;
            LONG compLen = ImmGetCompositionString(himc, GCS_COMPREADSTR, nullptr, 0);
            if (compLen > 0) {
                wchar_t* compBuf = new wchar_t[compLen + 1];
                ImmGetCompositionString(himc, GCS_COMPREADSTR, compBuf, compLen);
                compBuf[compLen] = 0;
                composition = compBuf;
                delete[] compBuf;
            }

            std::vector<std::wstring> candidates;
            int selectedIndex = 0;
            size_t bufSize = ImmGetCandidateList(himc, 0, nullptr, 0);
            if (bufSize > 0) {
                CANDIDATELIST* candList = (CANDIDATELIST*)new char[bufSize];
                ImmGetCandidateList(himc, 0, candList, bufSize);
                DWORD count = candList->dwCount;
                selectedIndex = candList->dwSelection;
                if (count > 10) count = 10;
                for (DWORD j = 0; j < count; j++) {
                    wchar_t* pStr = (wchar_t*)((char*)candList + candList->dwOffset[j]);
                    candidates.push_back(pStr);
                }
                delete[] (char*)candList;
            }

mgr.updateCandidates(composition, candidates, selectedIndex);

            ImmReleaseContext(fgWnd, himc);
        }
    }
}

} // anonymous namespace

extern "C" {

__declspec(dllexport) void SetCallbacks(void* candidateUpdate, void* layoutChange, void* modeChange, void* keyboardState) {
    using CandFunc = void(*)(const wchar_t*, const wchar_t**, int, int);
    using LayoutFunc = void(*)(int);
    using ModeFunc = void(*)(int);
    using KbdFunc = void(*)(int, int);
    chineseime::setCandidateCallback(reinterpret_cast<CandFunc>(candidateUpdate));
    chineseime::setLayoutChangeCallback(reinterpret_cast<LayoutFunc>(layoutChange));
    chineseime::setModeChangeCallback(reinterpret_cast<ModeFunc>(modeChange));
    chineseime::setKeyboardStateCallback(reinterpret_cast<KbdFunc>(keyboardState));
}

__declspec(dllexport) int StartListen(void* hwnd) {
    if (g_tsfInitialized.load()) return 1;

    HKL hkl = GetKeyboardLayout(0);
    if (hkl) {
        chineseime::ImeStateManager::get().updateInputMethod(DetectInputMethodTypeFromHkl(hkl));
        LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
        chineseime::ImeStateManager::get().updateChineseMode(IsChineseLangId(langId));
    }

    return 1;
}

__declspec(dllexport) void StopListen(void) {
}

__declspec(dllexport) int IsListening(void) {
    return (g_tsfInitialized.load() || g_imm32Initialized.load()) ? 1 : 0;
}

__declspec(dllexport) int StartTsfListen(void) {
    if (g_tsfInitialized.load()) return 1;

    g_staThread = std::make_unique<chineseime::StaThread>();
    if (!g_staThread->start()) return 0;

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

    chineseime::IMEState state = chineseime::ImeStateManager::get().getSnapshot();
    state.isValid = true;
    HKL hkl = GetKeyboardLayout(0);
    if (hkl) {
        LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
        state.inputMethodType = DetectInputMethodTypeFromHkl(hkl);
        state.chineseMode = IsChineseLangId(langId);
        state.imeOpen = state.chineseMode;
    }
    chineseime::ImeStateManager::get().updateInputMethod(state.inputMethodType);
    chineseime::ImeStateManager::get().updateChineseMode(state.chineseMode);
    chineseime::ImeStateManager::get().updateImeOpen(state.imeOpen);

    g_pollingRunning.store(true);
    g_pollingThread = std::thread([]() {
        while (g_pollingRunning.load()) {
            PollKeyboardState();

            if (g_tsfMonitor) {
                g_tsfMonitor->refreshState();
            } else if (g_imm32Monitor) {
                g_imm32Monitor->update();
            }

            PollIMEState();

            auto changes = chineseime::ImeStateManager::get().checkChanges();
            auto state = chineseime::ImeStateManager::get().getSnapshot();

            if (changes.inputMethodChanged || changes.chineseModeChanged) {
                chineseime::onImeStateChanged(static_cast<int>(state.inputMethodType), state.chineseMode);
            }

            if (changes.candidatesChanged || changes.compositionChanged) {
                if (!state.candidates.empty()) {
                    std::vector<const wchar_t*> ptrs;
                    for (const auto& c : state.candidates) {
                        ptrs.push_back(c.c_str());
                    }
                    chineseime::onCandidateChanged(
                        state.composition.c_str(),
                        ptrs.data(),
                        static_cast<int>(ptrs.size()),
                        state.selectedIndex
                    );
                }
            }

            if (changes.capsLockChanged || changes.shiftModeChanged) {
                bool inShiftMode = (state.inputMethodType != chineseime::InputMethodType::ENGLISH &&
                    state.inputMethodType != chineseime::InputMethodType::UNKNOWN &&
                    !state.chineseMode);
                chineseime::onKeyboardStateChanged(state.capsLockOn ? 1 : 0, inShiftMode ? 1 : 0);
            }

            Sleep(16);
        }
    });

    return 1;
}

void StopTsfListen(void) {
    if (g_pollingRunning.load()) {
        g_pollingRunning.store(false);
        if (g_pollingThread.joinable()) {
            g_pollingThread.join();
        }
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

int IsTsfListening(void) {
    return g_tsfInitialized.load() ? 1 : 0;
}

__declspec(dllexport) int IsChineseMode(void) {
    return chineseime::ImeStateManager::get().getSnapshot().chineseMode ? 1 : 0;
}

int GetCompositionString(wchar_t* buffer, int bufferSize) {
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

int GetCandidateCount(void) {
    return (int)chineseime::ImeStateManager::get().getSnapshot().candidates.size();
}

int GetCandidate(int index, wchar_t* buffer, int bufferSize) {
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

int GetSelectedCandidateIndex(void) {
    return chineseime::ImeStateManager::get().getSnapshot().selectedIndex;
}

int GetImeOpenStatus(void) {
    return chineseime::ImeStateManager::get().getSnapshot().imeOpen ? 1 : 0;
}

int GetTsfChineseMode(void) {
    return chineseime::ImeStateManager::get().getSnapshot().chineseMode ? 1 : 0;
}

int HasTsfLayoutChanged(void) {
    return chineseime::ImeStateManager::get().checkLayoutChanged() ? 1 : 0;
}

int GetInputMethodType(void) {
    return (int)chineseime::ImeStateManager::get().getSnapshot().inputMethodType;
}

__declspec(dllexport) int GetShiftMode(void) {
    auto state = chineseime::ImeStateManager::get().getSnapshot();
    bool isChineseInputMethod = state.inputMethodType != chineseime::InputMethodType::ENGLISH &&
        state.inputMethodType != chineseime::InputMethodType::UNKNOWN;
    return (isChineseInputMethod && !state.chineseMode) ? 1 : 0;
}

__declspec(dllexport) int GetCapsLockState(void) {
    return chineseime::ImeStateManager::get().getSnapshot().capsLockOn ? 1 : 0;
}

__declspec(dllexport) void SetTargetWindow(void* hwnd) {
    g_targetWindow = hwnd ? reinterpret_cast<HWND>(hwnd) : nullptr;
}

void RefreshImeState(void) {
    PollIMEState();
}

void FreeBuffer(void* ptr) {
    if (ptr) CoTaskMemFree(ptr);
}

const char* GetDllVersion(void) {
    return VERSION;
}

int HasLayoutChanged(void) {
    return chineseime::ImeStateManager::get().checkLayoutChanged() ? 1 : 0;
}

long GetKeyboardLayoutHKL(void) {
    HKL hkl = GetKeyboardLayout(0);
    return (long)reinterpret_cast<DWORD_PTR>(hkl);
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        break;
    case DLL_PROCESS_DETACH:
        StopTsfListen();
        break;
    }
    return TRUE;
}