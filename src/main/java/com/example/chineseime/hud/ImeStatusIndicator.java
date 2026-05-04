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

    private static final int BG_NORMAL = 0x99000000;
    private static final int BG_CAPS = 0x994466AA;
    private static final int BORDER_COLOR = 0xFF4466AA;
    private static final int TEXT_COLOR = 0xFFFFFFFF;
    private static final int SHIFT_INDICATOR = 0xFFFFAA00;

    private static final int SIZE_1080P = 40;
    private static final int SHIFT_INDICATOR_SIZE_1080P = 8;

    public ImeStatusIndicator() {
        this.inputMode = InputMode.PINYIN;
        this.capsLockOn = false;
        this.inShiftMode = false;
        this.visible = false;
    }

    public void update(boolean chineseMode, InputMode inputMode, boolean capsLockOn, boolean inShiftMode) {
        boolean changed = this.chineseMode != chineseMode
            || this.inputMode != inputMode
            || this.capsLockOn != capsLockOn
            || this.inShiftMode != inShiftMode;

        this.chineseMode = chineseMode;
        this.inputMode = inputMode;
        this.capsLockOn = capsLockOn;
        this.inShiftMode = inShiftMode;
        this.visible = true;

        if (changed) {
            com.example.chineseime.ChineseIMEInitializer.LOGGER.info(
                "[ChineseIME] Indicator: IME={}, CapsLock={}, ShiftMode={}, ChineseMode={}",
                getDisplayText(), capsLockOn, inShiftMode, chineseMode);
        }
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

    public void render(DrawContext ctx) {
        if (!this.visible) return;

        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc == null) return;

        TextRenderer font = mc.textRenderer;
        int scaledW = ctx.getScaledWindowWidth();
        int scaledH = ctx.getScaledWindowHeight();
        int physicalW = mc.getWindow().getWidth();
        float scale = physicalW > 0 ? (float) physicalW / (float) scaledW : 2.0f;

        int size = (int)(SIZE_1080P / scale);
        int shiftSize = (int)(SHIFT_INDICATOR_SIZE_1080P / scale);
        int margin = (int)(8 / scale);
        int chatInputTop = scaledH - 22 - 14;
        int x = margin;
        int y = chatInputTop - 2 - size;

        int bgColor = (!this.inShiftMode && this.capsLockOn) ? BG_CAPS : BG_NORMAL;

        ctx.fill(x, y, x + size, y + size, bgColor);
        ctx.drawBorder(x, y, size, size, BORDER_COLOR);

        String text = this.getDisplayText();
        int textWidth = font.getWidth(text);
        int textX = x + (size - textWidth) / 2;
        int textY = y + (size - font.fontHeight) / 2 + 1;
        ctx.drawText(font, text, textX, textY, TEXT_COLOR, false);

        if (this.inShiftMode) {
            int shiftPad = (int)(2 / ((float) size / SIZE_1080P));
            int shiftX = x + size - shiftSize - shiftPad;
            int shiftY = y + shiftPad;
            ctx.fill(shiftX, shiftY, shiftX + shiftSize, shiftY + shiftSize, SHIFT_INDICATOR);
        }
    }

    public boolean isVisible() { return this.visible; }
    public boolean isChineseMode() { return this.chineseMode; }
    public InputMode getInputMode() { return this.inputMode; }
    public boolean isInShiftMode() { return this.inShiftMode; }

    public static int getHeight(float scale) {
        return (int)(SIZE_1080P / scale);
    }
}