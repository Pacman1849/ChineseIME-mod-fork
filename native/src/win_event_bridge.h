#pragma once

#include "ime_state_manager.h"
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace chineseime {

struct WinEventBridge {
    struct EventCallbacks {
        std::function<void(const wchar_t* text, int cursorPos, int selStart, int selLen)> preeditCallback;
        std::function<void(const wchar_t* text)> commitCallback;
        std::function<void(const wchar_t** candidates, int count, int selIdx)> candidateCallback;
    };

    static WinEventBridge& get();

    void setCallbacks(EventCallbacks&& callbacks);
    void hookWindow(HWND hwnd);
    void unhookWindow();
    void refreshCandidates();

    bool isHooked() const { return hooked_; }
    HWND getTargetWindow() const { return targetWindow_; }
    void onForegroundChanged(HWND hwnd);
    void onFocusChanged(HWND hwnd);

    // Get the target window from WinEventBridge (for use by other modules)
    static HWND GetWinEventTargetWindow();

private:
    WinEventBridge() = default;

    void readComposition(HIMC himc, LPARAM lParam);
    void readCompositionCursor(HIMC himc);
    void readCandidates(HIMC himc);
    void processImeComposition(HWND hwnd, LPARAM lParam);
    void processImeEndComposition(HWND hwnd);
    void processImeNotify(WPARAM wParam, LPARAM lParam);

    static LRESULT CALLBACK ImeWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    EventCallbacks callbacks_;
    HWND targetWindow_ = nullptr;
    std::wstring lastComposition_;
    std::vector<std::wstring> lastCandidates_;
    int lastSelectedIndex_ = 0;
    int lastCursorPos_ = 0;
    bool hooked_ = false;
};

} // namespace chineseime