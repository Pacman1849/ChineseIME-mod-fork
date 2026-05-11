#include "win_event_bridge.h"
#include <algorithm>
#include <vector>
#include <windows.h>
#include <imm.h>
#include <debugapi.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

static WNDPROC g_originalWndProc = nullptr;
static HWND g_targetWindow = nullptr;
static bool g_callbacksRegistered = false;

// WinEvent Hook for IME message capture
static HWINEVENTHOOK g_winEventHook = nullptr;
static HWINEVENTHOOK g_objectEventHook = nullptr;

// WinEvent callback function (global scope, outside namespace)
void CALLBACK WinEventHookCallback(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD idEventThread, DWORD dwmsEventTime) {
    (void)hook; // unused
    
    if (hwnd == nullptr || idObject != OBJID_WINDOW) return;

    // Only process relevant system events
    // Note: There are no EVENT_SYSTEM_IME_* constants - IME messages are delivered via WM_IME_* window messages
    // We hook foreground and focus changes to detect when the target window changes
    if (event == EVENT_SYSTEM_FOREGROUND || event == EVENT_SYSTEM_SWITCHSTART || event == EVENT_SYSTEM_SWITCHEND) {
        
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] WinEventHook: event=0x%X, hwnd=0x%p\n", event, (void*)hwnd);
        OutputDebugStringA(dbg);

        // Forward to WinEventBridge if it's the hooked window
        auto& bridge = chineseime::WinEventBridge::get();
        if (bridge.isHooked() && bridge.getTargetWindow() == hwnd) {
            if (event == EVENT_SYSTEM_FOREGROUND) {
                bridge.onForegroundChanged(hwnd);
            }
        }
    }
}

// Object event callback for focus changes
void CALLBACK ObjectEventHookCallback(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD idEventThread, DWORD dwmsEventTime) {
    (void)hook; // unused
    
    if (hwnd == nullptr || idObject != OBJID_WINDOW) return;

    // Capture focus changes to detect chat window
    if (event == EVENT_OBJECT_FOCUS && idChild == OBJID_CLIENT) {
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] ObjectEventHook: FOCUS hwnd=0x%p\n", (void*)hwnd);
        OutputDebugStringA(dbg);

        // Forward to WinEventBridge
        chineseime::WinEventBridge::get().onFocusChanged(hwnd);
    }
}

// Start WinEvent Hook (called once with static guard)
static void startWinEventHook() {
    if (g_winEventHook) return;
    
    // Hook into all desktop processes for window focus changes
    // EVENT_SYSTEM_FOREGROUND = 0x0005, EVENT_SYSTEM_SWITCHEND = 0x0017
    // This covers system-level focus and window switch events
    g_winEventHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_SWITCHEND,
        nullptr, // hook in all processes
        WinEventHookCallback,
        0, // all processes
        0, // all threads
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    // Also hook object events for focus changes (EVENT_OBJECT_FOCUS = 0x8005)
    g_objectEventHook = SetWinEventHook(
        EVENT_OBJECT_FOCUS,
        EVENT_OBJECT_FOCUS,
        nullptr, // hook in all processes
        ObjectEventHookCallback,
        0, // all processes
        0, // all threads
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

    if (g_winEventHook) {
        OutputDebugStringA("[ChineseIME] WinEventHook: started successfully\n");
    } else {
        OutputDebugStringA("[ChineseIME] WinEventHook: FAILED to start\n");
    }
    if (g_objectEventHook) {
        OutputDebugStringA("[ChineseIME] ObjectEventHook: started successfully\n");
    } else {
        OutputDebugStringA("[ChineseIME] ObjectEventHook: FAILED to start\n");
    }
}

// Stop WinEvent Hook
static void stopWinEventHook() {
    if (g_winEventHook) {
        UnhookWinEvent(g_winEventHook);
        g_winEventHook = nullptr;
        OutputDebugStringA("[ChineseIME] WinEventHook: stopped\n");
    }
    if (g_objectEventHook) {
        UnhookWinEvent(g_objectEventHook);
        g_objectEventHook = nullptr;
        OutputDebugStringA("[ChineseIME] ObjectEventHook: stopped\n");
    }
}

namespace chineseime {

WinEventBridge& WinEventBridge::get() {
    static WinEventBridge instance;
    return instance;
}

void WinEventBridge::setCallbacks(EventCallbacks&& callbacks) {
    callbacks_ = std::move(callbacks);
    g_callbacksRegistered = true;
}

void WinEventBridge::hookWindow(HWND hwnd) {
    // Start WinEvent Hook once (static guard ensures it runs only once)
    static bool hookInitialized = []() {
        startWinEventHook();
        return true;
    }();
    (void)hookInitialized; // suppress unused variable warning

    if (hooked_ && targetWindow_ == hwnd) {
        return;
    }

    if (hooked_ && g_targetWindow && g_originalWndProc) {
        SetWindowLongPtr(g_targetWindow, GWLP_WNDPROC, (LONG_PTR)g_originalWndProc);
        g_originalWndProc = nullptr;
    }

    targetWindow_ = hwnd;
    g_targetWindow = hwnd;
    if (hwnd) {
        g_originalWndProc = (WNDPROC)GetWindowLongPtr(hwnd, GWLP_WNDPROC);
        SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)ImeWndProc);
        hooked_ = true;
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] WinEventBridge: hooked HWND 0x%p\n", (void*)hwnd);
        OutputDebugStringA(dbg);
    } else {
        hooked_ = false;
    }
}

void WinEventBridge::unhookWindow() {
    if (hooked_ && g_targetWindow && g_originalWndProc) {
        SetWindowLongPtr(g_targetWindow, GWLP_WNDPROC, (LONG_PTR)g_originalWndProc);
        g_originalWndProc = nullptr;
        hooked_ = false;
        targetWindow_ = nullptr;
        g_targetWindow = nullptr;
        OutputDebugStringA("[ChineseIME] WinEventBridge: unhooked\n");
    }
    stopWinEventHook();
}

void WinEventBridge::refreshCandidates() {
    if (!targetWindow_) return;

    HIMC himc = ImmGetContext(targetWindow_);
    if (himc) {
        readCandidates(himc);
        ImmReleaseContext(targetWindow_, himc);
    }
}

void WinEventBridge::onForegroundChanged(HWND hwnd) {
    if (!hwnd) return;
    char dbg[128];
    sprintf_s(dbg, "[ChineseIME] WinEventBridge::onForegroundChanged: hwnd=0x%p\n", (void*)hwnd);
    OutputDebugStringA(dbg);
    
    // If not already hooked to this window, set it as target and hook it
    if (!hooked_ || targetWindow_ != hwnd) {
        hookWindow(hwnd);
    }
}

void WinEventBridge::onFocusChanged(HWND hwnd) {
    if (!hwnd) return;
    char dbg[128];
    sprintf_s(dbg, "[ChineseIME] WinEventBridge::onFocusChanged: hwnd=0x%p\n", (void*)hwnd);
    OutputDebugStringA(dbg);
    
    // Check if this is the Minecraft window (we could enhance this with class name detection)
    // For now, we set it as target if we don't have one yet, or if it becomes foreground
    if (!targetWindow_) {
        hookWindow(hwnd);
    }
}

void WinEventBridge::readComposition(HIMC himc, LPARAM lParam) {
    std::wstring newComposition;
    int cursorPos = 0;

    if (lParam & GCS_COMPSTR) {
        LONG len = ImmGetCompositionStringW(himc, GCS_COMPSTR, nullptr, 0);
        if (len > 0) {
            std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
            LONG actual = ImmGetCompositionStringW(himc, GCS_COMPSTR, buf.data(), len);
            if (actual > 0) {
                int wcharLen = actual / sizeof(wchar_t);
                buf[wcharLen] = 0;
                newComposition.assign(buf.data(), wcharLen);

                if (lParam & GCS_CURSORPOS) {
                    cursorPos = ImmGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0);
                }
            }
        }
    }

    if (callbacks_.preeditCallback) {
        callbacks_.preeditCallback(newComposition.empty() ? nullptr : newComposition.c_str(),
                                   cursorPos, 0, 0);
    }

    lastComposition_ = std::move(newComposition);
}

void WinEventBridge::readCompositionCursor(HIMC himc) {
    LONG cursorPos = ImmGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0);
    if (cursorPos >= 0) {
        lastCursorPos_ = (int)cursorPos;
    }
}

void WinEventBridge::readCandidates(HIMC himc) {
    DWORD bufSize = ImmGetCandidateListW(himc, 0, nullptr, 0);

    if (bufSize > 0) {
        std::vector<char> buf(bufSize);
        CANDIDATELIST* candList = reinterpret_cast<CANDIDATELIST*>(buf.data());
        if (!ImmGetCandidateListW(himc, 0, candList, bufSize)) {
            return;
        }

        lastCandidates_.clear();
        DWORD count = candList->dwCount;
        lastSelectedIndex_ = (int)candList->dwSelection;

        if (count > 20) count = 20;

        for (DWORD i = 0; i < count; i++) {
            wchar_t* str = (wchar_t*)(buf.data() + candList->dwOffset[i]);
            lastCandidates_.push_back(str);
        }

        if (!lastCandidates_.empty()) {
            std::vector<const wchar_t*> ptrs;
            for (const auto& c : lastCandidates_) {
                ptrs.push_back(c.c_str());
            }
            if (callbacks_.candidateCallback) {
                callbacks_.candidateCallback(ptrs.data(), (int)ptrs.size(), lastSelectedIndex_);
            }
        }

        ImeStateManager::get().updateCandidates(lastComposition_, lastCandidates_, lastSelectedIndex_);
    } else {
        // Fallback to TSF cached candidates
        auto state = ImeStateManager::get().getSnapshot();
        if (!state.candidates.empty()) {
            lastCandidates_ = state.candidates;
            lastSelectedIndex_ = state.selectedIndex;
            ImeStateManager::get().updateCandidates(lastComposition_, lastCandidates_, lastSelectedIndex_);
        }
    }
}

void WinEventBridge::processImeComposition(HWND hwnd, LPARAM lParam) {
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) return;

    if (lParam & GCS_COMPSTR) {
        readComposition(himc, lParam);
    }

    if (lParam & GCS_CURSORPOS) {
        readCompositionCursor(himc);
    }

    if ((lParam & GCS_COMPSTR) && !lastComposition_.empty()) {
        readCandidates(himc);
    }

    if (lParam & GCS_RESULTSTR) {
        LONG len = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
        if (len > 0) {
            std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
            LONG actual = ImmGetCompositionStringW(himc, GCS_RESULTSTR, buf.data(), len);
            if (actual > 0) {
                int wcharLen = actual / sizeof(wchar_t);
                buf[wcharLen] = 0;

                if (callbacks_.commitCallback) {
                    callbacks_.commitCallback(buf.data());
                }

                lastComposition_.clear();
                lastCandidates_.clear();
                lastSelectedIndex_ = 0;

                if (callbacks_.preeditCallback) {
                    callbacks_.preeditCallback(nullptr, 0, 0, 0);
                }
                if (callbacks_.candidateCallback) {
                    callbacks_.candidateCallback(nullptr, 0, 0);
                }

                ImeStateManager::get().updateCandidates(L"", {}, 0);
            }
        }
    }

    ImmReleaseContext(hwnd, himc);
}

void WinEventBridge::processImeEndComposition(HWND hwnd) {
    if (!lastComposition_.empty() || !lastCandidates_.empty()) {
        lastComposition_.clear();
        lastCandidates_.clear();
        lastSelectedIndex_ = 0;

        if (callbacks_.preeditCallback) {
            callbacks_.preeditCallback(nullptr, 0, 0, 0);
        }
        if (callbacks_.candidateCallback) {
            callbacks_.candidateCallback(nullptr, 0, 0);
        }

        ImeStateManager::get().updateCandidates(L"", {}, 0);
    }
}

void WinEventBridge::processImeNotify(WPARAM wParam, LPARAM) {
    if (wParam == IMN_OPENCANDIDATE || wParam == IMN_CHANGECANDIDATE || wParam == IMN_CLOSECANDIDATE) {
        if (targetWindow_) {
            HIMC himc = ImmGetContext(targetWindow_);
            if (himc) {
                if (wParam == IMN_CLOSECANDIDATE) {
                    if (!lastCandidates_.empty()) {
                        lastCandidates_.clear();
                        lastSelectedIndex_ = 0;
                        if (callbacks_.candidateCallback) {
                            callbacks_.candidateCallback(nullptr, 0, 0);
                        }
                        ImeStateManager::get().updateCandidates(lastComposition_, {}, 0);
                    }
                } else {
                    readCandidates(himc);
                }
                ImmReleaseContext(targetWindow_, himc);
            }
        }
    }
}

LRESULT CALLBACK WinEventBridge::ImeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto& bridge = WinEventBridge::get();

    // Log all IME-related messages
    if (msg >= WM_IME_STARTCOMPOSITION && msg <= WM_IME_KEYLAST) {
        char dbg[128];
        const char* msgName = "?";
        if (msg == WM_IME_STARTCOMPOSITION) msgName = "STARTCOMP";
        else if (msg == WM_IME_COMPOSITION) msgName = "COMP";
        else if (msg == WM_IME_ENDCOMPOSITION) msgName = "ENDCOMP";
        else if (msg == WM_IME_NOTIFY) msgName = "NOTIFY";
        else if (msg == WM_IME_SETCONTEXT) msgName = "SETCTX";
        else if (msg == WM_INPUTLANGCHANGE) msgName = "INPUTLANG";
        sprintf_s(dbg, "[ChineseIME] ImeWndProc: hwnd=0x%p msg=%s(0x%04X) wp=0x%llX lp=0x%llX\n",
            (void*)hWnd, msgName, msg, (unsigned long long)wParam, (unsigned long long)lParam);
        OutputDebugStringA(dbg);
    }

    switch (msg) {
        case WM_IME_STARTCOMPOSITION: {
            bridge.processImeEndComposition(hWnd);
            break;
        }

        case WM_IME_COMPOSITION: {
            bridge.processImeComposition(hWnd, lParam);
            break;
        }

        case WM_IME_ENDCOMPOSITION: {
            bridge.processImeEndComposition(hWnd);
            break;
        }

        case WM_IME_NOTIFY: {
            bridge.processImeNotify(wParam, lParam);
            break;
        }

        case WM_IME_SETCONTEXT:
        case WM_INPUTLANGCHANGE:
            break;
    }

    if (g_originalWndProc) {
        return CallWindowProc(g_originalWndProc, hWnd, msg, wParam, lParam);
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

} // namespace chineseime