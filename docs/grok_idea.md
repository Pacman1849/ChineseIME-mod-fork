
以下是專門針對 **Phase 1（拼音吞字母 + 莫名 Commit）** 的完整修復報告：

---

```markdown
# ChineseIME-mod cangjie/hud 分支 - Phase 1 修復報告

**日期**：2026-05-05  
**目標**：修復「拼音輸入吞字母 + 莫名 Commit」問題  
**優先級**：最高（最影響使用者體驗）

---

## 問題原因總結

- `CommitCallback` 在 composition 還沒真正結束時就過早觸發
- `insertTextToFocusedField` 直接用反射修改 `chatField`，容易與 Windows IME 本身的 composition 機制衝突
- 狀態清除不徹底，導致舊 composition 殘留
- Native 與 Java 側同步時機不一致

---

## Phase 1 修復 Patch（直接替換）

### 1. ChineseIMEInitializer.java

#### 替換 `registerCallbacks()` 方法

```java
private void registerCallbacks() {
    NativeImeBridge.CommitCallback commitCB = text -> {
        if (text == null || text.length() == 0) return;

        String committed = text.toString();
        LOGGER.info("[ChineseIME] CommitCallback triggered: '{}'", committed);

        // === 關鍵防護：只有沒有活躍 composition 時才真正 commit ===
        boolean hasActiveComposition = false;
        if (this.imeManager != null) {
            CandidateHud hud = this.imeManager.getHud();
            VerticalCandidateHud vhud = this.imeManager.getVerticalHud();

            hasActiveComposition = 
                (hud != null && !hud.getInput().isEmpty()) ||
                (vhud != null && !vhud.getInput().isEmpty());
        }

        if (!hasActiveComposition) {
            insertTextToFocusedField(committed);
            if (this.imeManager != null) {
                this.imeManager.clearInput();   // 徹底清除所有狀態
            }
            LOGGER.info("[ChineseIME] Commit accepted and inserted: '{}'", committed);
        } else {
            LOGGER.warn("[ChineseIME] Commit BLOCKED - Still has active composition: {}", committed);
            // 可選：暫存 committed，等待 composition 清空後再插入
        }
    };

    // 其他 callback 保持不變...
    NativeImeBridge.setEventCallbacks(null, commitCB, candidateCB, imeChangeCB, keyboardCB);
    LOGGER.info("[ChineseIME] Event callbacks registered with strengthened commit protection");
}
```

#### 替換 `insertTextToFocusedField` 方法（更安全版）

```java
public void insertTextToFocusedField(String text) {
    if (text == null || text.isEmpty()) return;

    MinecraftClient mc = MinecraftClient.getInstance();
    if (mc == null || !(mc.currentScreen instanceof ChatScreen chatScreen)) return;

    try {
        java.lang.reflect.Field field = ChatScreen.class.getDeclaredField("chatField");
        field.setAccessible(true);
        TextFieldWidget chatField = (TextFieldWidget) field.get(chatScreen);

        if (chatField != null) {
            String current = chatField.getText();
            int cursor = chatField.getCursor();

            String newText = current.substring(0, cursor) + text + current.substring(cursor);
            chatField.setText(newText);
            chatField.setCursor(cursor + text.length(), false);

            LOGGER.info("[ChineseIME] Inserted '{}' at cursor {}, new text: '{}'", 
                       text, cursor + text.length(), newText);
        }
    } catch (Exception e) {
        LOGGER.error("[ChineseIME] Failed to insert text to chat field", e);
    }
}
```

---

### 2. PlatformIMEManager.java（建議加入）

在 `tick()` 方法中加入以下強制清除邏輯：

```java
public void tick() {
    // 強制同步最新狀態
    NativeImeBridge.RefreshImeState();

    // 強制清除已結束的 composition
    if (candidateHud != null && candidateHud.getInput().isEmpty()) {
        candidateHud.clear();
    }
    if (verticalCandidateHud != null && verticalCandidateHud.getInput().isEmpty()) {
        verticalCandidateHud.clear();
    }

    // ... 其他原有 tick 邏輯
}
```

---

### 3. CandidateHud.java & VerticalCandidateHud.java

在兩個類的 `clear()` 方法確保徹底清除：

```java
public void clear() {
    this.candidates.clear();
    this.composition = "";
    this.selected = 0;
    this.page = 0;
    this.visible = false;
    this.currentInputMethod = InputMode.PINYIN;   // 或保持原本
}
```

---

## 測試步驟

1. 替換以上程式碼
2. 重新編譯 JAR + DLL
3. 使用**微軟拼音**測試以下輸入：
   - `nihaoma`
   - `woxihuanni`
   - `zhonghuarenmin`
   - 較長的句子
4. 觀察是否還有吞字母或突然 Commit 的情況

---

## 預期效果

- Commit 只會在真正完成輸入時觸發
- 大幅減少吞字母現象
- 輸入過程更穩定

---

**測試完請告訴我結果**：
- 吞字母問題是否明顯改善？
- 是否還有 `[ChineseIME] Commit BLOCKED` 的 log？
- 候選詞是否仍然不顯示？

如果 Phase 1 修復後仍有問題，我們再進入 **Phase 2（候選詞顯示）**。

需要我再補充其他檔案的對應修改嗎？
```

---

**使用方式**：直接複製上方全部內容，存成 `Phase1-Commit-Fix-20260505.md`

需要我繼續準備 **Phase 2（候選詞不顯示）** 的修復報告嗎？