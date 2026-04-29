#pragma once

#include "ime_state_manager.h"
#include <windows.h>
#include <string>
#include <vector>

namespace chineseime {

class Imm32Monitor {
public:
    Imm32Monitor();
    ~Imm32Monitor();

    bool initialize();
    void shutdown();
    void update();

    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    void processComposition(HWND hwnd, HIMC himc);
    void processCandidate(HWND hwnd, HIMC himc);
    void detectInputMethodType(HKL hkl);

    HWND hwnd_ = nullptr;
    HWND parentWnd_ = nullptr;
    bool isInitialized_ = false;
    bool isTracking_ = false;
    HIMC lastHimc_ = nullptr;
};

} // namespace chineseime