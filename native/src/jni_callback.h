#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*CandidateUpdateFunc)(const wchar_t*, const wchar_t**, int, int);
typedef void (*LayoutChangeFunc)(int);
typedef void (*ModeChangeFunc)(int);
typedef void (*KeyboardStateChangeFunc)(int, int);

#ifdef __cplusplus
}
#endif

namespace chineseime {

void setCandidateCallback(CandidateUpdateFunc func);
void setLayoutChangeCallback(LayoutChangeFunc func);
void setModeChangeCallback(ModeChangeFunc func);
void setKeyboardStateCallback(KeyboardStateChangeFunc func);

void onCandidateChanged(const wchar_t* composition, const wchar_t** candidates, int count, int selectedIndex);
void onImeStateChanged(int inputMethodType, bool chineseMode);
void onKeyboardStateChanged(int capsLockOn, int inShiftMode);

} // namespace chineseime