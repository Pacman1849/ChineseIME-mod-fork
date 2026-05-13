**我們先集中火力修復「拼音吞字母 + 莫名 Commit」這個最影響體驗的問題。**

這個問題在拼音輸入法上特別明顯，主要原因是 **CommitCallback 被過早觸發**，或是 `insertTextToFocusedField` 直接強制插入文字，與 Windows IME 的 composition 機制衝突。

---

### **主要修復方向**

#### **1. ChineseIMEInitializer.java 修復版（重點修改）**

請替換以下兩個方法：

```java
private void registerCallbacks() {
    NativeImeBridge.CommitCallback commitCB = text -> {
        if (text == null || text.length() == 0) return;

        String committed = text.toString();
        LOGGER.info("[ChineseIME] Commit: '{}'", committed);

        // 重要：只在沒有 composition 時才真正 commit（避免吞字）
        if (this.imeManager != null) {
            CandidateHud hud = this.imeManager.getHud();
            VerticalCandidateHud vHud = this.imeManager.getVerticalHud();
            
            boolean hasComposition = (hud != null && !hud.getInput().isEmpty()) ||
                                   (vHud != null && !vHud.getInput().isEmpty());

            if (!hasComposition) {
                insertTextToFocusedField(committed);
                this.imeManager.clearInput();
            } else {
                LOGGER.warn("[ChineseIME] Commit blocked because composition still exists: {}", committed);
            }
        }
    };

    // ... 其他 callback 保持不變

    NativeImeBridge.setEventCallbacks(null, commitCB, candidateCB, imeChangeCB, keyboardCB);
}
```

#### **2. 優化 insertTextToFocusedField 方法**

```java
public void insertTextToFocusedField(String text) {
    if (text == null || text.isEmpty()) return;

    MinecraftClient mc = MinecraftClient.getInstance();
    if (mc == null || mc.currentScreen == null) return;

    try {
        if (mc.currentScreen instanceof ChatScreen chatScreen) {
            java.lang.reflect.Field field = chatScreen.getClass().getDeclaredField("chatField");
            field.setAccessible(true);
            TextFieldWidget chatField = (TextFieldWidget) field.get(chatScreen);

            if (chatField != null) {
                String current = chatField.getText();
                int cursor = chatField.getCursor();

                // 更安全的插入方式
                String newText = current.substring(0, cursor) + text + current.substring(cursor);
                chatField.setText(newText);
                chatField.setCursor(cursor + text.length(), false);

                LOGGER.info("[ChineseIME] Inserted '{}', cursor moved to {}", text, cursor + text.length());
            }
        }
    } catch (Exception e) {
        LOGGER.error("[ChineseIME] Failed to insert text", e);
    }
}
```

---

### **額外重要修正建議**

1. **在 `PlatformIMEManager.tick()` 中加入強制清除**（如果你還沒做）：

```java
public void tick() {
    // ... 原有程式碼

    // 新增：如果 composition 變空，就強制清除 HUD
    if (candidateHud != null && candidateHud.getInput().isEmpty()) {
        candidateHud.clear();
    }
    if (verticalCandidateHud != null && verticalCandidateHud.getInput().isEmpty()) {
        verticalCandidateHud.clear();
    }
}
```

2. **在 Native 層（ime_bridge.cpp）的 Commit 邏輯中加強保護**（如果有暴露 commit 事件）

   確保只有當 composition 真的結束時才觸發 commit。
