#include "jni_callback.h"

namespace chineseime {

static CandidateUpdateFunc s_candidateCallback = nullptr;
static LayoutChangeFunc s_layoutChangeCallback = nullptr;
static ModeChangeFunc s_modeChangeCallback = nullptr;
static KeyboardStateChangeFunc s_keyboardStateCallback = nullptr;

void setCandidateCallback(CandidateUpdateFunc func) {
    s_candidateCallback = func;
}

void setLayoutChangeCallback(LayoutChangeFunc func) {
    s_layoutChangeCallback = func;
}

void setModeChangeCallback(ModeChangeFunc func) {
    s_modeChangeCallback = func;
}

void setKeyboardStateCallback(KeyboardStateChangeFunc func) {
    s_keyboardStateCallback = func;
}

void onCandidateChanged(const wchar_t* composition, const wchar_t** candidates, int count, int selectedIndex) {
    if (s_candidateCallback) {
        s_candidateCallback(composition, candidates, count, selectedIndex);
    }
}

void onImeStateChanged(int inputMethodType, bool chineseMode) {
    if (s_layoutChangeCallback) {
        s_layoutChangeCallback(inputMethodType);
    }
    if (s_modeChangeCallback) {
        s_modeChangeCallback(chineseMode ? 1 : 0);
    }
}

void onKeyboardStateChanged(int capsLockOn, int inShiftMode) {
    if (s_keyboardStateCallback) {
        s_keyboardStateCallback(capsLockOn, inShiftMode);
    }
}

} // namespace chineseime