#pragma once

#include <cstdint>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

#ifdef _WIN32
#ifdef CHINESEIME_EXPORTS
#define CHINESEIME_API __declspec(dllexport)
#else
#define CHINESEIME_API __declspec(dllimport)
#endif
#else
#define CHINESEIME_API
#endif

namespace chineseime {

enum class InputMethodType {
    UNKNOWN = 0,
    ENGLISH = 1,
    PINYIN = 2,
    ZHUYIN = 3,
    CANGJIE = 4,
    WUBI = 5,
    SUCHENG = 6,
    OTHER_CHINESE = 99
};

struct IMEState {
    bool isValid = false;
    long hkl = 0;
    InputMethodType inputMethodType = InputMethodType::UNKNOWN;
    bool imeOpen = false;
    bool chineseMode = false;
    bool inShiftMode = false;
    bool capsLockOn = false;
    bool shiftPressed = false;
    std::wstring composition;
    std::vector<std::wstring> candidates;
    int selectedIndex = 0;
    int layoutChangeCount = 0;
};

struct ChangeFlags {
    bool inputMethodChanged = false;
    bool chineseModeChanged = false;
    bool capsLockChanged = false;
    bool candidatesChanged = false;
    bool compositionChanged = false;
    bool shiftChanged = false;
    bool shiftModeChanged = false;
};

class ImeStateManager {
public:
    static ImeStateManager& get();

    void updateInputMethod(InputMethodType type);
    void updateChineseMode(bool isChinese);
    void updateImeOpen(bool isOpen);
    void updateCapsLock(bool isOn);
    void updateShiftPressed(bool isPressed);
    void updateComposition(const std::wstring& comp);
    void updateCandidates(const std::wstring& comp, const std::vector<std::wstring>& cands, int selectedIndex);
    void updateKeyboardState(bool capsLock, bool shiftPressed);

    IMEState getSnapshot() const;
    ChangeFlags checkChanges();
    void clearChanges();
    bool checkLayoutChanged();
    void clearLayoutChanged();

private:
    ImeStateManager() = default;
    ~ImeStateManager() = default;
    ImeStateManager(const ImeStateManager&) = delete;
    ImeStateManager& operator=(const ImeStateManager&) = delete;

    mutable std::mutex mutex_;
    IMEState state_;
    ChangeFlags changes_;
    bool isInitialized_ = false;
};

InputMethodType detectInputMethodTypeFromImeId(WORD imeId, LANGID langId);

} // namespace chineseime