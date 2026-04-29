package com.example.chineseime.hud;

import com.example.chineseime.engine.InputMode;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.font.TextRenderer;
import net.minecraft.client.gui.DrawContext;

public class ImeStatusIndicator {
    private boolean chineseMode = true;
    private InputMode inputMode;
    private boolean capsLockOn;
    private boolean inShiftMode;
    private boolean visible;
    private boolean lastChineseMode;
    private InputMode lastInputMode;
    private boolean lastCapsLockOn;
    private boolean inChatScreen;

    private static final int BG_NORMAL = 0x99000000;
    private static final int BG_CAPS = 0x994466AA;
    private static final int BORDER_COLOR = 0xFF4466AA;
    private static final int TEXT_COLOR = 0xFFFFFFFF;
    private static final int SHIFT_INDICATOR = 0xFFFFAA00;

    private static final int SIZE = 22;
    private static final int SHIFT_INDICATOR_SIZE = 8;

    public ImeStatusIndicator() {
        this.inputMode = InputMode.PINYIN;
        this.capsLockOn = false;
        this.inShiftMode = false;
        this.visible = false;
        this.lastChineseMode = true;
        this.lastInputMode = InputMode.PINYIN;
        this.lastCapsLockOn = false;
        this.inChatScreen = false;
    }

    public void setInChatScreen(boolean inChatScreen) {
        this.inChatScreen = inChatScreen;
    }

    public void update(boolean chineseMode, InputMode inputMode, boolean capsLockOn, boolean inShiftMode, boolean isTyping, boolean layoutChanged) {
        this.chineseMode = chineseMode;
        this.inputMode = inputMode;
        this.capsLockOn = capsLockOn;
        this.inShiftMode = inShiftMode;

        this.visible = this.inChatScreen || true;
        this.lastChineseMode = chineseMode;
        this.lastInputMode = inputMode;
        this.lastCapsLockOn = capsLockOn;
    }

    public void hide() {
        this.visible = false;
    }

    private String getDisplayText() {
        return switch (this.inputMode) {
            case LATIN -> "En";
            case PINYIN -> "拼";
            case ZHUYIN -> "注";
            case CANGJIE -> "仓";
            case SUCHENG -> "速";
            case WUBI -> "五";
            default -> "?";
        };
    }

    private int getShiftIndicatorSize() {
        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc == null) return SHIFT_INDICATOR_SIZE;
        int screenHeight = mc.getWindow().getHeight();
        return Math.round(8.0f * screenHeight / 1080.0f);
    }

    public void render(DrawContext ctx) {
        if (!this.visible) return;

        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc == null) return;

        TextRenderer font = mc.textRenderer;
        int screenHeight = mc.getWindow().getHeight();

        int x = 4;
        int y = screenHeight - SIZE - 48;

        int bgColor = (!this.inShiftMode && this.capsLockOn) ? BG_CAPS : BG_NORMAL;

        ctx.fill(x, y, x + SIZE, y + SIZE, bgColor);
        ctx.drawBorder(x, y, SIZE, SIZE, BORDER_COLOR);

        String text = this.getDisplayText();
        int textWidth = font.getWidth(text);
        int textX = x + (SIZE - textWidth) / 2;
        int textY = y + (SIZE - 8) / 2;
        ctx.drawText(font, text, textX, textY, TEXT_COLOR, false);

        if (this.inShiftMode) {
            int shiftIndicatorSize = getShiftIndicatorSize();
            int shiftX = x + SIZE - shiftIndicatorSize - 1;
            int shiftY = y + 1;
            ctx.fill(shiftX, shiftY, shiftX + shiftIndicatorSize, shiftY + shiftIndicatorSize, SHIFT_INDICATOR);
        }
    }

    public boolean isVisible() {
        return this.visible;
    }

    public boolean isChineseMode() {
        return this.chineseMode;
    }

    public InputMode getInputMode() {
        return this.inputMode;
    }

    public boolean isInShiftMode() {
        return this.inShiftMode;
    }

    public static int getHeight() {
        return SIZE;
    }
}