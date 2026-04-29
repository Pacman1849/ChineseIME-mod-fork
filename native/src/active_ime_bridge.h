#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// C接口函数
BOOL RegisterCallbacks(void* candidateUpdate, void* layoutChange, void* keyStateChange);
BOOL StartActiveListen(void* gameWindow);
void StopActiveListen(void);
int GetCurrentInputMethodType(void);
void GetCurrentKeyState(BOOL* capsLockOn, BOOL* shiftPressed);
const char* GetActiveDllVersion(void);

#ifdef __cplusplus
}
#endif