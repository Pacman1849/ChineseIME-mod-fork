#include "ime_state_manager.h"
#include <algorithm>

namespace chineseime {

ImeStateManager& ImeStateManager::get() {
    static ImeStateManager instance;
    return instance;
}

InputMethodType detectInputMethodTypeFromImeId(WORD imeId, LANGID langId) {
    if (langId != 0x0804 && langId != 0x0404 && langId != 0x0C04 && langId != 0x1404) {
        return InputMethodType::ENGLISH;
    }
    switch (imeId) {
    case 0x0000: return InputMethodType::PINYIN;
    case 0x0001: case 0x0010: case 0xE010: case 0xE020: return InputMethodType::PINYIN;
    case 0x0002: case 0xE011: return InputMethodType::WUBI;
    case 0x0003: case 0xE001: return InputMethodType::ZHUYIN;
    case 0x0004: case 0xE002: return InputMethodType::CANGJIE;
    case 0x0005: case 0xE003: return InputMethodType::SUCHENG;
    case 0x0011: return InputMethodType::PINYIN;
    case 0x0012: return InputMethodType::WUBI;
    case 0x0013: return InputMethodType::ZHUYIN;
    case 0x0014: return InputMethodType::CANGJIE;
    case 0x0015: return InputMethodType::SUCHENG;
    default: {
        // Try to detect by IME ID ranges
        // 0x08xx range for zh-CN (Simplified Chinese) IMEs
        if (langId == 0x0804) {
            BYTE highByte = (imeId >> 8) & 0xFF;
            BYTE lowByte = imeId & 0xFF;
            // 0x08 = Simplified Chinese range
            // 0x10 = Microsoft Pinyin variants
            // 0x20 = Wubi variants
            // 0x21 = Wubi 86
            // 0x23 = Wubi 98
            // 0x24-0x2F = various IMEs
            if (highByte == 0x08 || highByte == 0x09) {
                // For 0x08xx range, check the low byte to determine IME type
                switch (lowByte) {
                case 0x00: case 0x01: case 0x02: case 0x03:
                case 0x04: case 0x05: case 0x06: case 0x07:
                case 0x08: case 0x09: case 0x0A: case 0x0B:
                case 0x0C: case 0x0D: case 0x0E: case 0x0F:
                    return InputMethodType::PINYIN;  // Most common zh-CN IMEs default to PINYIN
                case 0x10: case 0x11: case 0x12: case 0x13:
                case 0x14: case 0x15: case 0x16: case 0x17:
                case 0x18: case 0x19: case 0x1A: case 0x1B:
                case 0x1C: case 0x1D: case 0x1E: case 0x1F:
                    return InputMethodType::PINYIN;
                case 0x20: case 0x21: case 0x22: case 0x23:
                case 0x24: case 0x25: case 0x26: case 0x27:
                case 0x28: case 0x29: case 0x2A: case 0x2B:
                case 0x2C: case 0x2D: case 0x2E: case 0x2F:
                    return InputMethodType::WUBI;
                default:
                    // For any other 0x08xx IME, assume PINYIN for zh-CN
                    return InputMethodType::PINYIN;
                }
            }
        }
        // 0x09xx range for zh-TW (Traditional Chinese) IMEs
        if (langId == 0x0404 || langId == 0x0C04 || langId == 0x1404) {
            BYTE highByte = (imeId >> 8) & 0xFF;
            if (highByte == 0x09 || highByte == 0x08) {
                // For zh-TW, most IMEs are CANGJIE or ZHUYIN
                BYTE lowByte = imeId & 0xFF;
                if (lowByte >= 0x00 && lowByte <= 0x0F) {
                    return InputMethodType::CANGJIE;
                }
                // ZHUYIN is 0x03 range in some systems
                if (lowByte >= 0x20 && lowByte <= 0x2F) {
                    return InputMethodType::ZHUYIN;
                }
                return InputMethodType::CANGJIE;
            }
        }
        return InputMethodType::OTHER_CHINESE;
    }
    }
}

void ImeStateManager::updateInputMethod(InputMethodType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.inputMethodType != type) {
        char dbg[128];
        sprintf_s(dbg, "[ChineseIME] updateInputMethod: %d -> %d\n",
            (int)state_.inputMethodType, (int)type);
        OutputDebugStringA(dbg);
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