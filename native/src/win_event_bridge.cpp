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

// Static state
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

    // CRITICAL: Only monitor THIS process, not all processes!
    // Using process ID 0 hooks ALL processes, which breaks other apps' IME.
    DWORD currentProcessId = GetCurrentProcessId();

    // Hook only this process for foreground changes
    g_winEventHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND,
        EVENT_SYSTEM_FOREGROUND,
        nullptr, // dll
        WinEventHookCallback,
        currentProcessId, // ONLY this process
        0, // all threads in process
        WINEVENT_OUTOFCONTEXT);

    // Hook for focus changes (only this process)
    g_objectEventHook = SetWinEventHook(
        EVENT_OBJECT_FOCUS,
        EVENT_OBJECT_FOCUS,
        nullptr,
        ObjectEventHookCallback,
        currentProcessId,
        0,
        WINEVENT_OUTOFCONTEXT);

    if (g_winEventHook) {
        OutputDebugStringA("[ChineseIME] WinEventHook: started (process-local only)\n");
    } else {
        OutputDebugStringA("[ChineseIME] WinEventHook: FAILED to start\n");
    }
    if (g_objectEventHook) {
        OutputDebugStringA("[ChineseIME] ObjectEventHook: started (process-local only)\n");
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

// Global function to get the target window from WinEventBridge
HWND WinEventBridge::GetWinEventTargetWindow() {
    return WinEventBridge::get().getTargetWindow();
}

void WinEventBridge::setCallbacks(EventCallbacks&& callbacks) {
    callbacks_ = std::move(callbacks);
    g_callbacksRegistered = true;
}

void WinEventBridge::hookWindow(HWND hwnd) {
    // CRITICAL: DO NOT replace the game's WndProc!
    // Replacing WndProc breaks IME for all applications, including system-wide.
    // 
    // For TSF IMEs (Microsoft Pinyin, Wubi), the TSF UIElementProvider handles
    // all IME event monitoring. No WndProc hook needed.
    //
    // For legacy IMM32 IMEs, we can detect them but they are rare now.
    // If needed, we can add a separate non-invasive monitoring approach.

    targetWindow_ = hwnd;
    g_targetWindow = hwnd;
    hooked_ = true;  // Mark as "active" but don't actually hook WndProc

    char dbg[128];
    sprintf_s(dbg, "[ChineseIME] WinEventBridge: registered HWND 0x%p (WndProc NOT hooked - using TSF instead)\n", (void*)hwnd);
    OutputDebugStringA(dbg);
}

void WinEventBridge::unhookWindow() {
    targetWindow_ = nullptr;
    g_targetWindow = nullptr;
    hooked_ = false;
    OutputDebugStringA("[ChineseIME] WinEventBridge: unregistered\n");
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
    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] readCandidates: called with lastComposition_='%S'\n", lastComposition_.c_str());
    OutputDebugStringA(dbg);

    DWORD bufSize = ImmGetCandidateListW(himc, 0, nullptr, 0);
    sprintf_s(dbg, "[ChineseIME] readCandidates: bufSize=%u\n", bufSize);
    OutputDebugStringA(dbg);

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
        // Verify the update
        {
            char dbg[256];
            auto state = ImeStateManager::get().getSnapshot();
            sprintf_s(dbg, "[ChineseIME] readCandidates: UPDATED ImeStateManager, composition='%S', candidates=%d\n",
                state.composition.c_str(), (int)state.candidates.size());
            OutputDebugStringA(dbg);
        }
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

// Helper function to check if a string contains Chinese characters
static bool containsChinese(const wchar_t* str) {
    if (!str) return false;
    while (*str) {
        wchar_t c = *str;
        // Check for CJK Unified Ideographs (U+4E00 - U+9FFF)
        // Also include CJK Compatibility (U+3400 - U+4DBF) and Extension B+
        if ((c >= 0x4E00 && c <= 0x9FFF) ||
            (c >= 0x3400 && c <= 0x4DBF) ||
            (c >= 0x20000 && c <= 0x2A6DF)) {
            return true;
        }
        // Check for punctuation and common symbols used in Chinese
        // Full-width punctuation
        if ((c >= 0xFF00 && c <= 0xFFEF)) {
            return true;
        }
        str++;
    }
    return false;
}

void WinEventBridge::processImeComposition(HWND hwnd, LPARAM lParam) {
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) {
        OutputDebugStringA("[ChineseIME] processImeComposition: IMM context is null!\n");
        return;
    }

    // Only process messages that have composition or result data
    // Ignore messages that only have cursor position or other flags
    bool hasCompStr = (lParam & GCS_COMPSTR) != 0;
    bool hasResultStr = (lParam & GCS_RESULTSTR) != 0;
    bool hasCompRead = (lParam & GCS_COMPREADSTR) != 0;

    // Debug logging
    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] processImeComposition: hasComp=%d, hasResult=%d, hasCompRead=%d, lParam=0x%lX\n",
        hasCompStr ? 1 : 0, hasResultStr ? 1 : 0, hasCompRead ? 1 : 0, lParam);
    OutputDebugStringA(dbg);

    if (hasCompStr) {
        readComposition(himc, lParam);
    }

    if (lParam & GCS_CURSORPOS) {
        readCompositionCursor(himc);
    }

    sprintf_s(dbg, "[ChineseIME] processImeComposition: lastComposition_='%S', after read, size=%d\n",
        lastComposition_.c_str(), (int)lastComposition_.size());
    OutputDebugStringA(dbg);

    if (hasCompStr && !lastComposition_.empty()) {
        readCandidates(himc);
    }

    if (hasResultStr) {
        LONG len = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
        if (len > 0) {
            std::vector<wchar_t> buf(len / sizeof(wchar_t) + 1);
            LONG actual = ImmGetCompositionStringW(himc, GCS_RESULTSTR, buf.data(), len);
            if (actual > 0) {
                int wcharLen = actual / sizeof(wchar_t);
                buf[wcharLen] = 0;

                // Filter: only commit if result contains Chinese characters
                // This prevents raw pinyin or partial input from being committed
                if (containsChinese(buf.data())) {
                    if (callbacks_.commitCallback) {
                        callbacks_.commitCallback(buf.data());
                    }
                } else {
                    // Result doesn't contain Chinese - this might be an error
                    // Log it for debugging
                    char dbg[256];
                    sprintf_s(dbg, "[ChineseIME] WARNING: GCS_RESULTSTR without Chinese: '%S'\n", buf.data());
                    OutputDebugStringA(dbg);
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

} // namespace chineseime