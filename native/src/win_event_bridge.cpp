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
}

void WinEventBridge::refreshCandidates() {
    if (!targetWindow_) return;

    HIMC himc = ImmGetContext(targetWindow_);
    if (himc) {
        readCandidates(himc);
        ImmReleaseContext(targetWindow_, himc);
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

    char dbg[256];
    sprintf_s(dbg, "[ChineseIME] readCandidates: himc=0x%llX, bufSize=%u\n",
        (unsigned long long)himc, (unsigned int)bufSize);
    OutputDebugStringA(dbg);

    if (bufSize == 0) {
        if (!lastCandidates_.empty()) {
            lastCandidates_.clear();
            lastSelectedIndex_ = 0;
            if (callbacks_.candidateCallback) {
                callbacks_.candidateCallback(nullptr, 0, 0);
            }
            ImeStateManager::get().updateCandidates(L"", {}, 0);
        }
        return;
    }

    std::vector<char> buf(bufSize);
    CANDIDATELIST* candList = reinterpret_cast<CANDIDATELIST*>(buf.data());
    if (!ImmGetCandidateListW(himc, 0, candList, bufSize)) {
        OutputDebugStringA("[ChineseIME] readCandidates: ImmGetCandidateListW failed\n");
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

    sprintf_s(dbg, "[ChineseIME] readCandidates: count=%u, sel=%d\n",
        (unsigned int)count, lastSelectedIndex_);
    OutputDebugStringA(dbg);

    if (callbacks_.candidateCallback && !lastCandidates_.empty()) {
        std::vector<const wchar_t*> ptrs;
        for (const auto& c : lastCandidates_) {
            ptrs.push_back(c.c_str());
        }
        callbacks_.candidateCallback(ptrs.data(), (int)ptrs.size(), lastSelectedIndex_);
    }

    ImeStateManager::get().updateCandidates(lastComposition_, lastCandidates_, lastSelectedIndex_);
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