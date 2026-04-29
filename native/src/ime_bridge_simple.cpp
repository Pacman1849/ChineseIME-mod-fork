#include "ime_bridge.h"
#include "common.h"
#include "sta_thread.h"
#include "tsf_sink.h"
#include <windows.h>
#include <imm.h>
#include <string>
#include <algorithm>
#include <thread>
#include <atomic>
#include <future>

// 调试宏，可以通过编译选项控制调试级别
#ifdef CHINESEIME_DEBUG
    #define DEBUG_LOG(format, ...) do { \
        wchar_t buf[512]; \
        swprintf_s(buf, sizeof(buf)/sizeof(wchar_t), format, __VA_ARGS__); \
        OutputDebugStringW(buf); \
    } while(0)
    #define DEBUG_LOG_SIMPLE(msg) OutputDebugStringW(msg)
#else
    #define DEBUG_LOG(format, ...) 
    #define DEBUG_LOG_SIMPLE(msg) 
#endif

namespace {

// Thread-safe global state
struct GlobalState {
    // 上一次的键盘状态，用于检测变化
    bool lastCapsLockOn = false;
    bool lastShiftPressed = false;
    // TSF components
    std::unique_ptr<chineseime::StaThread> staThread;
    std::unique_ptr<chineseime::TsfSink> tsfSink;
    std::atomic<bool> tsfInitialized{false};
    DWORD tfClientId{TF_CLIENTID_NULL};
    
    // IMM32 components
    std::atomic<bool> imm32Initialized{false};
    HWND imm32Window{nullptr};
    
    // Active polling thread
    std::thread pollingThread;
    std::atomic<bool> pollingRunning{false};
    
    // Synchronization
    std::mutex mutex;
    
    ~GlobalState() {
        cleanup();
    }
    
    void cleanup() {
        // Stop polling thread
        if (pollingRunning.load()) {
            pollingRunning.store(false);
            if (pollingThread.joinable()) {
                pollingThread.join();
            }
        }
        
        // Clean up IMM32
        if (imm32Window) {
            DestroyWindow(imm32Window);
            imm32Window = nullptr;
            UnregisterClassW(L"ChineseIME_Imm32Window", GetModuleHandleW(nullptr));
        }
        
        imm32Initialized.store(false);
        tsfInitialized.store(false);
    }
};

GlobalState g_globalState;

const char* VERSION = "1.2.0";

void PollKeyboardLayout() {
    // Use TSF sink for real IME data if available (拼音和注音)
    if (g_globalState.tsfSink && g_globalState.tsfInitialized.load()) {
        g_globalState.tsfSink->refreshState();
        return;
    }
    
    // Use IMM32 if available (仓颉和五笔)
    if (g_globalState.imm32Initialized.load()) {
        // IMM32-based IME detection
        HWND fgWnd = GetForegroundWindow();
        DWORD fgThread = 0;
        
        // 检查是否是Minecraft窗口
        bool isMinecraftWindow = false;
        if (fgWnd) {
            wchar_t className[256];
            if (GetClassNameW(fgWnd, className, sizeof(className)/sizeof(wchar_t))) {
                if (wcsstr(className, L"GLFW") != nullptr || wcsstr(className, L"Minecraft") != nullptr) {
                    isMinecraftWindow = true;
                }
            }
            fgThread = GetWindowThreadProcessId(fgWnd, nullptr);
        }
        
        // Get keyboard layout
        HKL hkl = GetKeyboardLayout(fgThread);
        if (!hkl) {
            hkl = GetKeyboardLayout(0);
        }
        
        if (hkl) {
            long hklValue = static_cast<long>(reinterpret_cast<DWORD_PTR>(hkl));
            long oldHkl = chineseime::g_state.lastHkl.exchange(hklValue);
            
            LANGID langId = LOWORD(hklValue);
            bool isChinese = (langId == 0x0804 || langId == 0x0404);
            
            // Check Caps Lock state (只在变化时检测)
            bool capsLockOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
            
            // Check Shift key state (只在中文模式下检测，避免英文模式下频繁检测)
            bool shiftPressed = false;
            if (isChinese) {
                shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            }
            
            // Check IME open status using IMM32
            bool imeOpen = false;
            std::wstring composition;
            std::vector<std::wstring> candidates;
            int selectedIndex = 0;
            
            if (g_globalState.imm32Window && isChinese) {
                HIMC himc = ImmGetContext(g_globalState.imm32Window);
                if (himc) {
                    imeOpen = ImmGetOpenStatus(himc);
                    
                    // Get composition string
                    wchar_t compBuf[256];
                    int compLen = ImmGetCompositionStringW(himc, GCS_COMPSTR, compBuf, sizeof(compBuf)/sizeof(wchar_t)-1);
                    if (compLen > 0) {
                        compBuf[compLen] = 0;
                        composition = compBuf;
                        DEBUG_LOG(L"[ChineseIME] IMM32 composition detected: '%s'", compBuf);
                    }
                    
                    // Get candidate list
                    DWORD candCount = ImmGetCandidateListW(himc, 0, nullptr, 0);
                    if (candCount > 0) {
                        CANDIDATELIST* candList = (CANDIDATELIST*)malloc(candCount);
                        if (candList) {
                            if (ImmGetCandidateListW(himc, 0, candList, candCount)) {
                                DEBUG_LOG(L"[ChineseIME] IMM32 candidates detected: %d", candList->dwCount);
                                for (DWORD i = 0; i < candList->dwCount && i < 20; i++) {
                                    wchar_t* candStr = (wchar_t*)((BYTE*)candList + candList->dwOffset[i]);
                                    if (candStr && wcslen(candStr) > 0) {
                                        candidates.push_back(candStr);
                                    }
                                }
                                selectedIndex = candList->dwSelection;
                            }
                            free(candList);
                        }
                    }
                    
                    ImmReleaseContext(g_globalState.imm32Window, himc);
                }
            }
            
            // 如果检测到Minecraft窗口且有中文输入法，强制检查候选词
            if (isMinecraftWindow && isChinese && imeOpen) {
                DEBUG_LOG(L"[ChineseIME] Minecraft window detected with Chinese IME active");
            }
            
            // Update state
            chineseime::g_state.chineseMode.store(isChinese);
            
            auto state = chineseime::g_cache.snapshot();
            bool stateChanged = false;
            
            if (state.chineseMode != isChinese) {
                state.chineseMode = isChinese;
                stateChanged = true;
            }
            if (state.imeOpen != imeOpen) {
                state.imeOpen = imeOpen;
                stateChanged = true;
            }
            if (state.hkl != hklValue) {
                state.hkl = hklValue;
                stateChanged = true;
            }
            if (state.composition != composition) {
                state.composition = composition;
                stateChanged = true;
            }
            if (state.candidates != candidates) {
                state.candidates = candidates;
                stateChanged = true;
            }
            if (state.selectedIndex != selectedIndex) {
                state.selectedIndex = selectedIndex;
                stateChanged = true;
            }
            
        // 新增状态检测 (只在变化时更新)
        if (capsLockOn != g_globalState.lastCapsLockOn) {
            state.capsLockOn = capsLockOn;
            g_globalState.lastCapsLockOn = capsLockOn;
            stateChanged = true;
            DEBUG_LOG(L"[ChineseIME] Caps Lock changed to %d", capsLockOn ? 1 : 0);
        }
        if (isChinese && shiftPressed != g_globalState.lastShiftPressed) {
            state.shiftPressed = shiftPressed;
            g_globalState.lastShiftPressed = shiftPressed;
            stateChanged = true;
            DEBUG_LOG(L"[ChineseIME] Shift state changed to %d", shiftPressed ? 1 : 0);
        }
            
            if (oldHkl != hklValue) {
                chineseime::g_state.layoutChanged.store(true);
                state.layoutChangeCount++;
                stateChanged = true;
            }
            
            if (stateChanged) {
                chineseime::g_cache.update(state);
            }
        }
        return;
    }
    
    // Fallback: basic keyboard layout detection
    HWND fgWnd = GetForegroundWindow();
    DWORD fgThread = 0;
    if (fgWnd) {
        fgThread = GetWindowThreadProcessId(fgWnd, nullptr);
    }
    
    HKL hkl = GetKeyboardLayout(fgThread);
    if (!hkl) {
        hkl = GetKeyboardLayout(0);
    }
    
    if (hkl) {
        long hklValue = static_cast<long>(reinterpret_cast<DWORD_PTR>(hkl));
        long oldHkl = chineseime::g_state.lastHkl.exchange(hklValue);
        
        LANGID langId = LOWORD(hklValue);
        bool isChinese = (langId == 0x0804 || langId == 0x0404);
        
        // Check Caps Lock state (只在变化时检测)
        static bool lastCapsLockOn = false;
        bool capsLockOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        
        // Check Shift key state (只在中文模式下检测，避免英文模式下频繁检测)
        static bool lastShiftPressed = false;
        bool shiftPressed = false;
        if (isChinese) {
            shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        }
        
        // Update state
        chineseime::g_state.chineseMode.store(isChinese);
        
        auto state = chineseime::g_cache.snapshot();
        bool stateChanged = false;
        
        if (state.chineseMode != isChinese) {
            state.chineseMode = isChinese;
            state.imeOpen = isChinese;
            stateChanged = true;
        }
        if (state.hkl != hklValue) {
            state.hkl = hklValue;
            stateChanged = true;
        }
        if (state.capsLockOn != capsLockOn) {
            state.capsLockOn = capsLockOn;
            stateChanged = true;
        }
        if (state.shiftPressed != shiftPressed) {
            state.shiftPressed = shiftPressed;
            stateChanged = true;
        }
        
        if (oldHkl != hklValue) {
            chineseime::g_state.layoutChanged.store(true);
            state.layoutChangeCount++;
            stateChanged = true;
        }
        
        if (stateChanged) {
            chineseime::g_cache.update(state);
        }
    }
}

// IMM32 initialization
bool InitializeImm32() {
    OutputDebugStringW(L"[ChineseIME] Initializing IMM32 support\n");
    
    // Create a hidden window for IMM32
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ChineseIME_Imm32Window";
    
    RegisterClassExW(&wc);
    
    g_globalState.imm32Window = CreateWindowExW(
        0, L"ChineseIME_Imm32Window", L"", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    
    if (!g_globalState.imm32Window) {
        OutputDebugStringW(L"[ChineseIME] Failed to create IMM32 window\n");
        return false;
    }
    
    OutputDebugStringW(L"[ChineseIME] IMM32 support initialized\n");
    return true;
}

// Polling thread function
void PollingThread() {
    OutputDebugStringW(L"[ChineseIME] Active polling thread started\n");
    
    auto lastState = chineseime::g_cache.snapshot();
    
    while (g_globalState.pollingRunning.load()) {
        // Refresh state
        PollKeyboardLayout();
        
        // Get current state
        auto newState = chineseime::g_cache.snapshot();
        
        // Check for changes and notify Java
        bool hasCandidateChange = lastState.composition != newState.composition || 
                                 lastState.candidates != newState.candidates;
        bool hasLayoutChange = lastState.inputMethodType != newState.inputMethodType;
        bool hasModeChange = lastState.chineseMode != newState.chineseMode;
        
        // 强制检查候选词变化，确保Minecraft能收到通知
        bool hasActiveComposition = !newState.composition.empty();
        bool hasActiveCandidates = newState.candidates.size() > 0;
        
        // 如果有组合字符串或候选词，就通知Java
        // 或者如果是中文模式且有IME打开，也通知Java（确保Minecraft能收到通知）
        if (hasCandidateChange || hasLayoutChange || hasModeChange || 
            hasActiveComposition || hasActiveCandidates || newState.chineseMode) {
            
            // 只在真正有变化时输出日志
            if (hasCandidateChange || hasActiveComposition || hasActiveCandidates) {
                DEBUG_LOG(L"[ChineseIME] Candidate state changed, composition='%s', candidates=%d", 
                         newState.composition.c_str(), newState.candidates.size());
            }
            if (hasLayoutChange) {
                DEBUG_LOG(L"[ChineseIME] Layout changed to %d", static_cast<int>(newState.inputMethodType));
            }
            if (hasModeChange) {
                DEBUG_LOG(L"[ChineseIME] Chinese mode changed to %d", newState.chineseMode ? 1 : 0);
            }
            if (hasCandidateChange || hasActiveComposition || hasActiveCandidates) {
                DEBUG_LOG(L"[ChineseIME] Candidate state changed, composition='%s', candidates=%d", 
                         newState.composition.c_str(), newState.candidates.size());
            }
            
            // Prepare candidate array
            std::vector<const wchar_t*> candidatePtrs;
            for (const auto& cand : newState.candidates) {
                candidatePtrs.push_back(cand.c_str());
            }
            
            // Notify candidate update
            if (chineseime::g_callbacks.onCandidateUpdate) {
                chineseime::g_callbacks.onCandidateUpdate(
                    newState.composition.c_str(),
                    candidatePtrs.empty() ? nullptr : candidatePtrs.data(),
                    static_cast<int>(newState.candidates.size()),
                    newState.selectedIndex
                );
            }
            
            // Notify layout change
            if (hasLayoutChange) {
                if (chineseime::g_callbacks.onLayoutChange) {
                    chineseime::g_callbacks.onLayoutChange(static_cast<int>(newState.inputMethodType));
                }
            }
            
            // Notify mode change
            if (hasModeChange) {
                if (chineseime::g_callbacks.onModeChange) {
                    chineseime::g_callbacks.onModeChange(newState.chineseMode);
                }
            }
            
            lastState = newState;
        }
        
        Sleep(100); // Poll every 100ms (降低频率减少日志)
    }
    
    OutputDebugStringW(L"[ChineseIME] Active polling thread stopped\n");
}

} // anonymous namespace

namespace chineseime {
State g_state;
CandidateCache g_cache;
CallbackFunctions g_callbacks;
}

extern "C" {

// ========== Legacy WinEvent Hook API ==========

int StartListen(void* hwnd) {
    OutputDebugStringW(L"[ChineseIME] StartListen called\n");
    return 1;
}

void StopListen(void) {
    OutputDebugStringW(L"[ChineseIME] StopListen called\n");
}

int IsListening(void) {
    return g_globalState.pollingRunning.load() ? 1 : 0;
}

int IsChineseMode(void) {
    auto state = chineseime::g_cache.snapshot();
    return state.chineseMode ? 1 : 0;
}

int HasLayoutChanged(void) {
    return chineseime::g_cache.checkLayoutChanged() ? 1 : 0;
}

// ========== Callback API ==========

extern "C" __declspec(dllexport) void WINAPI SetCallbacks(void* candidateUpdate, void* layoutChange, void* modeChange) {
    OutputDebugStringW(L"[ChineseIME] SetCallbacks called\n");
    
    if (candidateUpdate) {
        chineseime::g_callbacks.onCandidateUpdate = reinterpret_cast<decltype(chineseime::g_callbacks.onCandidateUpdate)>(candidateUpdate);
        OutputDebugStringW(L"[ChineseIME] Candidate callback registered\n");
    }
    if (layoutChange) {
        chineseime::g_callbacks.onLayoutChange = reinterpret_cast<decltype(chineseime::g_callbacks.onLayoutChange)>(layoutChange);
        OutputDebugStringW(L"[ChineseIME] Layout callback registered\n");
    }
    if (modeChange) {
        chineseime::g_callbacks.onModeChange = reinterpret_cast<decltype(chineseime::g_callbacks.onModeChange)>(modeChange);
        OutputDebugStringW(L"[ChineseIME] Mode callback registered\n");
    }
    
    OutputDebugStringW(L"[ChineseIME] All callbacks registered successfully\n");
}

// ========== TSF-based API ==========

int StartTsfListen(void) {
    if (g_globalState.tsfInitialized.load() || g_globalState.imm32Initialized.load()) {
        return 1;
    }
    
    OutputDebugStringW(L"[ChineseIME] StartTsfListen called\n");
    
    bool tsfSuccess = false;
    bool imm32Success = false;
    
    // Detect current input method type first
    HKL hkl = GetKeyboardLayout(0);
    if (hkl) {
        LANGID langId = LOWORD(reinterpret_cast<DWORD_PTR>(hkl));
        bool isChinese = (langId == 0x0804 || langId == 0x0404);
        
        if (isChinese) {
            // For Chinese input methods, try to determine specific type
            // 拼音和注音使用TSF，仓颉和五笔使用IMM32
            OutputDebugStringW(L"[ChineseIME] Chinese IME detected, determining type\n");
            
            // Try TSF first for Pinyin and Zhuyin
            if (!g_globalState.tsfInitialized.load()) {
                OutputDebugStringW(L"[ChineseIME] Attempting TSF initialization for Pinyin/Zhuyin\n");
                
                // Create STA thread (required for TSF)
                g_globalState.staThread = std::make_unique<chineseime::StaThread>();
                if (g_globalState.staThread->start()) {
                    // Create TSF sink in STA thread
                    std::promise<bool> tsfInitPromise;
                    std::future<bool> tsfInitFuture = tsfInitPromise.get_future();
                    
                    g_globalState.staThread->submitTask([&tsfInitPromise]() {
                        bool initialized = false;
                        g_globalState.tsfSink = std::make_unique<chineseime::TsfSink>();
                        
                        // Get thread manager
                        ITfThreadMgr* pThreadMgr = nullptr;
                        HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, 
                                                      IID_ITfThreadMgr, (void**)&pThreadMgr);
                        if (SUCCEEDED(hr) && pThreadMgr) {
                            hr = pThreadMgr->Activate(&g_globalState.tfClientId);
                            if (SUCCEEDED(hr)) {
                                initialized = g_globalState.tsfSink->initialize(pThreadMgr);
                            }
                            pThreadMgr->Release();
                        }
                        
                        tsfInitPromise.set_value(initialized);
                    });
                    
                    tsfSuccess = tsfInitFuture.get();
                    if (tsfSuccess) {
                        g_globalState.tsfInitialized.store(true);
                        OutputDebugStringW(L"[ChineseIME] TSF initialization successful for Pinyin/Zhuyin\n");
                    } else {
                        OutputDebugStringW(L"[ChineseIME] TSF initialization failed\n");
                        g_globalState.staThread->stop();
                    }
                }
            }
            
            // Try IMM32 for Cangjie and Wubi (or as fallback)
            if (!g_globalState.tsfInitialized.load() && !g_globalState.imm32Initialized.load()) {
                OutputDebugStringW(L"[ChineseIME] Attempting IMM32 initialization for Cangjie/Wubi\n");
                imm32Success = InitializeImm32();
                if (imm32Success) {
                    g_globalState.imm32Initialized.store(true);
                    OutputDebugStringW(L"[ChineseIME] IMM32 initialization successful\n");
                } else {
                    OutputDebugStringW(L"[ChineseIME] IMM32 initialization failed\n");
                }
            }
        } else {
            // Non-Chinese input method, use IMM32
            OutputDebugStringW(L"[ChineseIME] Non-Chinese IME detected, using IMM32\n");
            imm32Success = InitializeImm32();
            if (imm32Success) {
                g_globalState.imm32Initialized.store(true);
                OutputDebugStringW(L"[ChineseIME] IMM32 initialization successful\n");
            }
        }
    }
    
    // Start active polling thread if either method succeeded
    if (g_globalState.tsfInitialized.load() || g_globalState.imm32Initialized.load()) {
        g_globalState.pollingRunning.store(true);
        g_globalState.pollingThread = std::thread(PollingThread);
        
        OutputDebugStringW(L"[ChineseIME] Listening started\n");
        return 1;
    }
    
    OutputDebugStringW(L"[ChineseIME] All initialization methods failed\n");
    return 0;
}

void StopTsfListen(void) {
    if (!g_globalState.tsfInitialized.load() && !g_globalState.imm32Initialized.load()) {
        return;
    }
    
    OutputDebugStringW(L"[ChineseIME] StopTsfListen called\n");
    
    // Stop polling thread
    if (g_globalState.pollingRunning.load()) {
        g_globalState.pollingRunning.store(false);
        if (g_globalState.pollingThread.joinable()) {
            g_globalState.pollingThread.join();
        }
    }
    
    // Clean up TSF sink
    if (g_globalState.staThread) {
        g_globalState.staThread->submitTask([]() {
            if (g_globalState.tsfSink) {
                g_globalState.tsfSink->shutdown();
                g_globalState.tsfSink.reset();
            }
            
            // Deactivate TSF client
            if (g_globalState.tfClientId != TF_CLIENTID_NULL) {
                ITfThreadMgr* pThreadMgr = nullptr;
                HRESULT hr = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER, 
                                              IID_ITfThreadMgr, (void**)&pThreadMgr);
                if (SUCCEEDED(hr) && pThreadMgr) {
                    pThreadMgr->Deactivate();
                    pThreadMgr->Release();
                }
                g_globalState.tfClientId = TF_CLIENTID_NULL;
            }
        });
        
        g_globalState.staThread->stop();
        g_globalState.staThread.reset();
    }
    
    // Clean up IMM32
    if (g_globalState.imm32Initialized.load()) {
        g_globalState.cleanup();
    }
    
    g_globalState.tsfInitialized.store(false);
    OutputDebugStringW(L"[ChineseIME] Listening stopped\n");
}

int IsTsfListening(void) {
    return (g_globalState.tsfInitialized.load() || g_globalState.imm32Initialized.load()) ? 1 : 0;
}

int GetCompositionString(wchar_t* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return 0;
    
    auto state = chineseime::g_cache.snapshot();
    if (state.composition.empty()) {
        buffer[0] = 0;
        return 0;
    }
    
    int len = (std::min)(static_cast<int>(state.composition.length()), bufferSize - 1);
    wcsncpy_s(buffer, bufferSize, state.composition.c_str(), len);
    buffer[len] = 0;
    return len;
}

int GetCandidateCount(void) {
    auto state = chineseime::g_cache.snapshot();
    return static_cast<int>(state.candidates.size());
}

int GetCandidate(int index, wchar_t* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0) return 0;
    
    auto state = chineseime::g_cache.snapshot();
    if (index < 0 || index >= static_cast<int>(state.candidates.size())) {
        buffer[0] = 0;
        return 0;
    }
    
    const auto& cand = state.candidates[index];
    int len = (std::min)(static_cast<int>(cand.length()), bufferSize - 1);
    wcsncpy_s(buffer, bufferSize, cand.c_str(), len);
    buffer[len] = 0;
    return len;
}

int GetSelectedCandidateIndex(void) {
    auto state = chineseime::g_cache.snapshot();
    return state.selectedIndex;
}

int GetImeOpenStatus(void) {
    auto state = chineseime::g_cache.snapshot();
    return state.imeOpen ? 1 : 0;
}

int GetTsfChineseMode(void) {
    auto state = chineseime::g_cache.snapshot();
    return state.chineseMode ? 1 : 0;
}

int HasTsfLayoutChanged(void) {
    return chineseime::g_cache.checkLayoutChanged() ? 1 : 0;
}

int GetInputMethodType(void) {
    auto state = chineseime::g_cache.snapshot();
    return static_cast<int>(state.inputMethodType);
}

void RefreshImeState(void) {
    PollKeyboardLayout();
    
    // Also refresh TSF state if available
    if (g_globalState.tsfSink && g_globalState.tsfInitialized.load()) {
        g_globalState.tsfSink->refreshState();
    }
}

void FreeBuffer(void* ptr) {
    if (ptr) {
        CoTaskMemFree(ptr);
    }
}

long GetKeyboardLayoutHKL(void) {
    HKL hkl = GetKeyboardLayout(0);
    return static_cast<long>(reinterpret_cast<DWORD_PTR>(hkl));
}

const char* GetDllVersion(void) {
    return VERSION;
}

} // extern "C"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            OutputDebugStringW(L"[ChineseIME] DLL loaded\n");
            break;
        case DLL_PROCESS_DETACH:
            StopTsfListen();
            OutputDebugStringW(L"[ChineseIME] DLL unloaded\n");
            break;
    }
    return TRUE;
}