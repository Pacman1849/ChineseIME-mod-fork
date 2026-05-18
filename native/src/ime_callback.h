#pragma once

// ime_callback.h — 保留回调函数指针类型定义，供 ime_bridge.cpp 使用。
// 所有全局 g_* 回调指针已移除；回调统一通过 WinEventBridge::EventCallbacks 管理。

#ifdef _WIN32

#ifdef __cplusplus
extern "C" {
#endif

// C-style function pointer types for SetEventCallbacks
typedef void (__cdecl* PreeditCallback)(const wchar_t* text, int cursorPos, int selStart, int selLen);
typedef void (__cdecl* CommitCallback)(const wchar_t* text);
typedef void (__cdecl* CandidateCallback)(const wchar_t** candidates, int count, int selectedIndex);
typedef void (__cdecl* ImeChangeCallback)(int inputMethodType, int chineseMode);
typedef void (__cdecl* KeyboardCallback)(int capsLock, int shiftMode);

#ifdef __cplusplus
}
#endif

#endif