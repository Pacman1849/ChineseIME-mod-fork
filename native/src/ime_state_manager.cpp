#include "ime_state_manager.h"
#include <algorithm>
#include <windows.h>

namespace chineseime {

ImeStateManager& ImeStateManager::get() {
    static ImeStateManager instance;
    return instance;
}

InputMethodType detectInputMethodTypeFromImeId(WORD imeId, LANGID langId) {
    // First check if it's a Chinese language
    bool isZhCN = (langId == 0x0804);
    bool isZhTW = (langId == 0x0404 || langId == 0x0C04 || langId == 0x1404 || langId == 0x1004);
    bool isChinese = isZhCN || isZhTW;

    if (!isChinese) {
        return InputMethodType::ENGLISH;
    }

    // Get keyboard layout name for pattern matching
    WCHAR klName[64] = {0};
    GetKeyboardLayoutNameW(klName);
    char klNameA[64] = {0};
    WideCharToMultiByte(CP_ACP, 0, klName, -1, klNameA, sizeof(klNameA), NULL, NULL);

    // For TSF IMEs: imeId often equals the language ID (e.g., 0x0804 for zh-CN, 0x0404 for zh-TW)
    // Check if imeId is just a language ID (not a real IME ID) - this is a TSF IME
    if (imeId == langId) {
        // TSF IME - use language default
        if (isZhCN) {
            // zh-CN defaults to PINYIN
            return InputMethodType::PINYIN;
        } else if (isZhTW) {
            // zh-TW defaults to CANGJIE (most common)
            return InputMethodType::CANGJIE;
        }
    }

    // Check for IME-specific patterns in layout name (only for full word matches)
    // Check Cangjie first (vertical layout IMEs)
    if (strstr(klNameA, "Cangjie") || strstr(klNameA, "SCangjie") ||
        strstr(klNameA, "ChangJie")) {
        return InputMethodType::CANGJIE;
    }
    // Check Sucheng (Quick)
    if (strstr(klNameA, "Sucheng") || strstr(klNameA, "SQuick") ||
        strstr(klNameA, "Quick")) {
        return InputMethodType::SUCHENG;
    }
    // Check other IMEs
    if (strstr(klNameA, "Pinyin") || strstr(klNameA, "MSPY")) {
        return InputMethodType::PINYIN;
    }
    if (strstr(klNameA, "Wubi") || strstr(klNameA, "WUBI")) {
        return InputMethodType::WUBI;
    }
    if (strstr(klNameA, "Zhuyin") || strstr(klNameA, "New Phonetic")) {
        return InputMethodType::ZHUYIN;
    }

    // Check by IME ID for legacy IMM32 IMEs
    switch (imeId) {
    case 0x0000:
    case 0x0001: case 0x0010: case 0xE010: case 0xE020:
        return InputMethodType::PINYIN;
    case 0x0002: case 0xE011:
        return InputMethodType::WUBI;
    case 0x0003: case 0xE001:
        return InputMethodType::ZHUYIN;
    case 0x0004: case 0xE002: case 0xE012: case 0xE022: case 0xE032:
        return InputMethodType::CANGJIE;
    case 0x0005: case 0xE003: case 0xE013: case 0xE023:
        return InputMethodType::SUCHENG;
    case 0x0011:
        return InputMethodType::PINYIN;
    case 0x0012:
        return InputMethodType::WUBI;
    case 0x0013:
        return InputMethodType::ZHUYIN;
    case 0x0014:
        return InputMethodType::CANGJIE;
    case 0x0015:
        return InputMethodType::SUCHENG;
    default:
        break;
    }

    // Fallback based on language
    if (isZhCN) {
        return InputMethodType::PINYIN;
    } else if (isZhTW) {
        return InputMethodType::CANGJIE;
    }

    return InputMethodType::OTHER_CHINESE;
}

void ImeStateManager::updateInputMethod(InputMethodType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.inputMethodType != type) {
        state_.inputMethodType = type;
        state_.layoutChangeCount++;
        changes_.inputMethodChanged = true;
        changes_.shiftModeChanged = true;
    }
}

void ImeStateManager::updateChineseMode(bool isChinese) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.chineseMode != isChinese) {
        state_.chineseMode = isChinese;
        changes_.chineseModeChanged = true;
        changes_.shiftModeChanged = true;
    }
}

void ImeStateManager::updateImeOpen(bool isOpen) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.imeOpen != isOpen) {
        state_.imeOpen = isOpen;
    }
}

void ImeStateManager::updateCapsLock(bool isOn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.capsLockOn != isOn) {
        state_.capsLockOn = isOn;
        changes_.capsLockChanged = true;
    }
}

void ImeStateManager::updateShiftPressed(bool isPressed) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.shiftPressed != isPressed) {
        state_.shiftPressed = isPressed;
        changes_.shiftChanged = true;
    }
}

void ImeStateManager::updateComposition(const std::wstring& comp) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.composition != comp) {
        state_.composition = comp;
        changes_.compositionChanged = true;
    }
}

void ImeStateManager::updateCandidates(const std::wstring& comp, const std::vector<std::wstring>& cands, int selectedIndex) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.composition != comp) {
        state_.composition = comp;
        changes_.compositionChanged = true;
    }
    if (state_.candidates != cands || state_.selectedIndex != selectedIndex) {
        state_.candidates = cands;
        state_.selectedIndex = selectedIndex;
        changes_.candidatesChanged = true;
    }
}

void ImeStateManager::updateKeyboardState(bool capsLock, bool shiftPressed) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.capsLockOn != capsLock) {
        state_.capsLockOn = capsLock;
        changes_.capsLockChanged = true;
    }
    if (state_.shiftPressed != shiftPressed) {
        state_.shiftPressed = shiftPressed;
        changes_.shiftChanged = true;
    }
}

IMEState ImeStateManager::getSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

ChangeFlags ImeStateManager::checkChanges() {
    std::lock_guard<std::mutex> lock(mutex_);
    ChangeFlags result = changes_;
    changes_ = ChangeFlags();
    return result;
}

void ImeStateManager::clearChanges() {
    std::lock_guard<std::mutex> lock(mutex_);
    changes_ = ChangeFlags();
}

bool ImeStateManager::checkLayoutChanged() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.layoutChangeCount > 0) {
        state_.layoutChangeCount = 0;
        return true;
    }
    return false;
}

bool ImeStateManager::isChineseInputMethod() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto type = state_.inputMethodType;
    return type != InputMethodType::ENGLISH && type != InputMethodType::UNKNOWN;
}

long ImeStateManager::getKeyboardLayout() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_.hkl;
}

void ImeStateManager::updateHklState(long hkl) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.hkl != hkl) {
        state_.hkl = hkl;
        state_.layoutChangeCount++;
    }
}

void ImeStateManager::clearLayoutChanged() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.layoutChangeCount = 0;
}

} // namespace chineseime