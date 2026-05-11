**✅ 已打包完成！以下是針對 `cangjie/hud` 分支的最新修復建議**

請直接複製下方內容，存成：

**檔案名稱建議**：`ChineseIME-cangjie-hud-Fix-Report-20260505.md`

---

```markdown
# ChineseIME-mod cangjie/hud 分支 最新修復建議

**日期**：2026-05-05  
**狀態**：倉頡/速成候選詞問題 + 輸入法切換干擾  
**作者**：Grok

---

## 目前核心問題

- 只有「速成」能正常顯示候選詞
- 倉頡輸入法候選詞不顯示（或顯示錯誤內容）
- 拼音輸入法容易被其他模式干擾（Shift 狀態異常、候選詞不同步）
- InputMethodType 辨識不足

---

## 1. C++ 側 必須修改（最高優先）

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
            return InputMethodType::CANGJIE;     // 倉頡

        case 0x0005: case 0xE003: case 0xE013: case 0xE023:
            return InputMethodType::SUCHENG;     // 速成

        default:
            // 二次判斷（極重要）
            WCHAR klName[KL_NAMELENGTH] = {0};
            if (GetKeyboardLayoutNameW(klName)) {
                if (wcsstr(klName, L"Cangjie") || wcsstr(klName, L"倉頡") || 
                    wcsstr(klName, L"SCangjie") || wcsstr(klName, L"ChangJie")) {
                    return InputMethodType::CANGJIE;
                }
                if (wcsstr(klName, L"Sucheng") || wcsstr(klName, L"速成")) {
                    return InputMethodType::SUCHENG;
                }
            }
            return InputMethodType::OTHER_CHINESE;
    }
}
```

#### 修改 `PollIMEState()` 中的模式更新

```cpp
if (!imeOpen) {
    mgr.updateInputMethod(InputMethodType::ENGLISH);
} else {
    HKL hkl = GetKeyboardLayout(0);
    InputMethodType type = DetectInputMethodTypeFromHkl(hkl);
    mgr.updateInputMethod(type);
}
```

---

## 2. Java 側 修改

### CandidateHud.java（重要）

新增成員變數：
```java
private InputMethodType currentInputMethod = InputMethodType.PINYIN;
```

修改 `update` 方法：
```java
public void update(List<String> newCandidates, String newComposition, 
                   int newSelectedIndex, InputMethodType inputMethodType) {
    
    this.currentInputMethod = inputMethodType != null ? inputMethodType : InputMethodType.PINYIN;
    // ... 其餘原本邏輯保持不變
}
```

### ImeStatusIndicator.java

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

---

## 3. 呼叫端修正（ChineseIMEInitializer.java 或對應類別）

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

## 測試清單

- [ ] 微軟拼音（正常）
- [ ] 倉頡輸入法（候選詞必須出現）
- [ ] 速成輸入法
- [ ] 拼音模式下 Shift 中英文切換
- [ ] 切換輸入法後舊候選詞是否正確清除

---

**建議執行順序**：
1. 先修改 C++ 部分並重新編譯 DLL
2. 測試倉頡是否能顯示候選詞
3. 再修改 Java 部分

---

需要我再提供**某個檔案的完整替換版本**（而非 patch），還是想加上「輸入法切換時強制清除 HUD」等額外強化功能？

把測試結果告訴我，我們繼續迭代！
```

---

直接複製存成 `.md` 即可使用。  

需要我再補充其他內容（例如完整 CandidateHud.java）隨時說！