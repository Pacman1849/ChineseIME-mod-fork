#include <windows.h>
#include <msctf.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// 回调函数指针
typedef void(*CandidateCallback)(const wchar_t* composition, const wchar_t** candidates, int count);
typedef void(*LayoutCallback)(int inputMethodType);
typedef void(*KeyStateCallback)(bool capsLockOn, bool shiftPressed);

// 全局回调函数
static CandidateCallback g_candidateCallback = nullptr;
static LayoutCallback g_layoutCallback = nullptr;
static KeyStateCallback g_keyStateCallback = nullptr;

// 全局状态
static std::atomic<bool> g_running(false);
static std::thread g_workerThread;
static std::mutex g_stateMutex;

// 当前状态
static std::wstring g_composition;
static std::vector<std::wstring> g_candidates;
static int g_inputMethodType = 0; // 0=英文, 1=拼音, 2=注音, 3=仓颉, 4=五笔
static bool g_capsLockOn = false;
static bool g_shiftPressed = false;

// 检测键盘布局对应的输入法类型
int detectInputMethodType() {
    HKL hkl = GetKeyboardLayout(0);
    int langId = LOWORD((DWORD_PTR)hkl);
    
    switch (langId) {
        case 0x0409: return 0; // 英文
        case 0x0804: return 1; // 简体中文 - 拼音
        case 0x0404: return 2; // 繁体中文 - 注音
        default: return 1; // 默认拼音
    }
}

// Worker thread函数
void WorkerThread() {
    // 初始化COM为STA
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        OutputDebugStringW(L"[ActiveIME] CoInitializeEx failed\n");
        return;
    }
    
    OutputDebugStringW(L"[ActiveIME] Worker thread started\n");
    
    int lastInputMethodType = -1;
    bool lastCapsLockOn = false;
    bool lastShiftPressed = false;
    
    // 消息循环
    MSG msg;
    while (g_running.load()) {
        // 处理Windows消息
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        // 检测键盘状态
        bool newCapsLockOn = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        bool newShiftPressed = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        
        // 检测输入法类型
        int newInputMethodType = detectInputMethodType();
        
        // 检查状态变化
        bool stateChanged = false;
        {
            std::lock_guard<std::mutex> lock(g_stateMutex);
            
            if (newCapsLockOn != g_capsLockOn || newShiftPressed != g_shiftPressed) {
                g_capsLockOn = newCapsLockOn;
                g_shiftPressed = newShiftPressed;
                stateChanged = true;
            }
            
            if (newInputMethodType != g_inputMethodType) {
                g_inputMethodType = newInputMethodType;
                stateChanged = true;
            }
        }
        
        // 通知Java状态变化
        if (stateChanged && g_keyStateCallback) {
            g_keyStateCallback(newCapsLockOn, newShiftPressed);
        }
        
        if (newInputMethodType != lastInputMethodType && g_layoutCallback) {
            g_layoutCallback(newInputMethodType);
            lastInputMethodType = newInputMethodType;
        }
        
        if ((newCapsLockOn != lastCapsLockOn || newShiftPressed != lastShiftPressed) && g_keyStateCallback) {
            g_keyStateCallback(newCapsLockOn, newShiftPressed);
            lastCapsLockOn = newCapsLockOn;
            lastShiftPressed = newShiftPressed;
        }
        
        // 短暂休眠
        Sleep(50);
    }
    
    CoUninitialize();
    OutputDebugStringW(L"[ActiveIME] Worker thread stopped\n");
}

// C接口函数
extern "C" {

// 注册回调函数
void RegisterCallbacks(void* candidateUpdate, void* layoutChange, void* keyStateChange) {
    g_candidateCallback = reinterpret_cast<CandidateCallback>(candidateUpdate);
    g_layoutCallback = reinterpret_cast<LayoutCallback>(layoutChange);
    g_keyStateCallback = reinterpret_cast<KeyStateCallback>(keyStateChange);
    OutputDebugStringW(L"[ActiveIME] Callbacks registered\n");
}

// 开始监听
BOOL StartActiveListen(void* gameWindow) {
    if (g_running.load()) {
        return TRUE;
    }
    
    g_running.store(true);
    
    // 启动worker线程
    g_workerThread = std::thread(WorkerThread);
    
    OutputDebugStringW(L"[ActiveIME] StartActiveListen called\n");
    return TRUE;
}

// 停止监听
void StopActiveListen() {
    if (!g_running.load()) {
        return;
    }
    
    g_running.store(false);
    
    if (g_workerThread.joinable()) {
        g_workerThread.join();
    }
    
    OutputDebugStringW(L"[ActiveIME] StopActiveListen called\n");
}

// 获取当前输入法类型
int GetCurrentInputMethodType() {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_inputMethodType;
}

// 获取键盘状态
void GetCurrentKeyState(bool* capsLockOn, bool* shiftPressed) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    *capsLockOn = g_capsLockOn;
    *shiftPressed = g_shiftPressed;
}

// 获取DLL版本
const char* GetActiveDllVersion() {
    return "1.0.0";
}

} // extern "C"