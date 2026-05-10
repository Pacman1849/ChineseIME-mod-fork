# ChineseIME-mod cangjie/hud 分支 修復報告

**日期**：2026-05-05  
**作者**：Grok  
**目標**：解決「只有速成正常、倉頡候選詞不顯示、拼音被其他模式干擾」等問題

---

## 問題總結

- 只有**速成**輸入法能正常顯示候選詞
- **倉頡**及其他輸入法候選詞不顯示，且呼叫錯誤的 HUD 邏輯
- **拼音**輸入法容易被倉頡/速成模式干擾，Shift 狀態異常，候選詞不同步

**核心原因**：`InputMethodType` 辨識不完整 + HUD 未區分輸入法類型 + 狀態更新邏輯太粗糙

---

## 1. C++ 側修復（最高優先）

### 檔案：`ime_bridge.cpp`

#### 替換 `detectInputMethodTypeFromImeId` 函數

```cpp
InputMethodType detectInputMethodTypeFromImeId(WORD imeId, LANGID langId) {
    if (!IsChineseLangId(langId)) {
        return InputMethodType::ENGLISH;
    }

    switch (imeId) {
        case 0x0000:
        case 0x0001: case 0x0010: case 0xE010: case 0xE020:
            return InputMethodType::PINYIN;

        case 0x0002: case 0xE011:
            return InputMethodType::WUBI;

        case 0x0003: case 0xE001:
            return InputMethodType::ZHUYIN;

        case 0x0004: case 0xE002: case 0xE012: case 0xE022: case 0xE032:
            return InputMethodType::CANGJIE;           // 倉頡

        case 0x0005: case 0xE003: case 0xE013: case 0xE023:
            return InputMethodType::SUCHENG;           // 速成

        default:
            // 透過鍵盤布局名稱二次判斷（非常重要）
            WCHAR klName[KL_NAMELENGTH] = {0};
            if (GetKeyboardLayoutNameW(klName)) {
                if (wcsstr(klName, L"Cangjie") || wcsstr(klName, L"倉頡") || 
                    wcsstr(klName, L"SCangjie") || wcsstr(klName, L"ChangJie")) {
                    return InputMethodType::CANGJIE;
                }
                if (wcsstr(klName, L"Sucheng") || wcsstr(klName, L"速成") || 
                    wcsstr(klName, L"Quick")) {
                    return InputMethodType::SUCHENG;
                }
                if (wcsstr(klName, L"Wubi") || wcsstr(klName, L"五筆")) {
                    return InputMethodType::WUBI;
                }
            }
            return InputMethodType::OTHER_CHINESE;
    }
}
```

#### 修改 `PollIMEState()` 中的輸入法類型更新邏輯

```cpp
// 取代原本的：
if (!imeOpen) {
    mgr.updateInputMethod(chineseime::InputMethodType::ENGLISH);
} else {
    mgr.updateInputMethod(chineseime::InputMethodType::PINYIN);  // ← 錯誤
}

// 改成：
if (!imeOpen) {
    mgr.updateInputMethod(chineseime::InputMethodType::ENGLISH);
} else {
    HKL hkl = GetKeyboardLayout(0);
    chineseime::InputMethodType type = DetectInputMethodTypeFromHkl(hkl);
    mgr.updateInputMethod(type);
}
```

---

## 2. Java 側修復

### 檔案：`CandidateHud.java`

- 新增欄位：`private InputMethodType currentInputMethod = InputMethodType.PINYIN;`
- 修改 `update` 方法，增加 `InputMethodType` 參數（見上方報告細節）
- `render()` 可依 `currentInputMethod` 調整顏色提示

### 檔案：`ImeStatusIndicator.java`

更新 `getDisplayText()`：

```java
private String getDisplayText() {
    return switch (inputMode) {
        case PINYIN  -> "拼";
        case CANGJIE -> "仓";
        case SUCHENG -> "速";
        case WUBI    -> "五";
        case ZHUYIN  -> "注";
        case LATIN   -> "En";
        default      -> "??";
    };
}
```

### 呼叫端修正（ChineseIMEInitializer 或 Hud 管理類）

```java
InputMethodType currentType = NativeImeBridge.getCurrentInputMethodType();

candidateHud.update(candidates, composition, selectedIndex, currentType);

imeStatusIndicator.update(
    NativeImeBridge.isChineseMode(),
    currentType,
    NativeImeBridge.isCapsLockOn(),
    NativeImeBridge.isShiftMode()
);
```

---

## 套用後測試項目

1. 微軟拼音（預設）
2. 倉頡輸入法（重點測試候選詞）
3. 速成輸入法
4. 拼音模式下的 Shift 中英文切換
5. Caps Lock 狀態是否正常

---

**建議套用順序**：
1. 先修改 C++ 部分並重新編譯 DLL
2. 測試倉頡是否能顯示候選詞
3. 再修改 Java 部分

---

測試完後請告訴我結果（尤其是倉頡與拼音的表現），我會繼續幫你優化下一階段！