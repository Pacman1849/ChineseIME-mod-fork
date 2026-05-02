package com.example.chineseime.hud;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.font.TextRenderer;
import net.minecraft.client.gui.DrawContext;

public class CandidateHud {
    private List<String> candidates = new ArrayList<>();
    private String composition = "";
    private int selected = 0;
    private int page = 0;
    private int perPage = 9;
    private boolean visible = true;
    private int x;
    private int y;
    private int width;
    private int height;

    private static final int BG = 0xCC000000;
    private static final int SEL_BG = 0x66B1B4B6;
    private static final int SEL_BAR = 0xFF4488FF;
    private static final int TEXT_COLOR = 0xFFFFFFFF;
    private static final int NUM_COLOR = 0xFF929194;
    private static final int INPUT_COLOR = 0xFFB991FF;
    private static final int ARROW_COLOR = 0xFFAAAAAA;
    private static final int ARROW_HOVER_COLOR = 0xFFFFFFFF;

    private static final int HUD_HEIGHT_1080P = 36;
    private static final int ITEM_WIDTH_1080P = 60;
    private static final int ITEM_GAP_1080P = 0;
    private static final int MAX_WIDTH_1080P = 960;
    private static final int MARGIN_1080P = 8;
    private static final int ARROW_W_1080P = 20;
    private static final int ARROW_GAP_1080P = 6;
    private static final int BLUE_BAR_W_1080P = 3;
    private static final int HIGHLIGHT_MARGIN_1080P = 2;
    private static final int COMPO_MIN_GAP_1080P = 10;
    private static final int COMPO_PAD_1080P = 8;

    private boolean prevArrowHovered = false;
    private boolean nextArrowHovered = false;
    private boolean pageArrowClicked = false;

    private boolean permanentShow = true;

    public void updateCandidates(List<String> candidates, String composition) {
        this.candidates = candidates != null ? new ArrayList<>(candidates) : new ArrayList<>();
        this.composition = composition != null ? composition : "";
        this.selected = 0;
        this.page = 0;
        this.visible = permanentShow || !this.candidates.isEmpty();
    }

    public void updateCandidatesKeepSelection(List<String> candidates, String composition, int selectedIndex, int page) {
        this.candidates = candidates != null ? new ArrayList<>(candidates) : new ArrayList<>();
        this.composition = composition != null ? composition : "";
        int totalPages = this.candidates.isEmpty() ? 1 : (this.candidates.size() + this.perPage - 1) / this.perPage;
        this.page = Math.max(0, Math.min(page, totalPages - 1));
        this.selected = this.page * this.perPage + Math.max(0, Math.min(selectedIndex % this.perPage, this.candidates.size() - 1));
        if (this.selected >= this.candidates.size()) {
            this.selected = Math.max(0, this.candidates.size() - 1);
            int newPage = this.selected / this.perPage;
            if (newPage != this.page) this.page = newPage;
        }
        this.visible = permanentShow || !this.candidates.isEmpty();
    }

    public void clear() {
        this.candidates.clear();
        this.composition = "";
        this.selected = 0;
        this.page = 0;
    }

    public void selectPrevious() {
        if (this.candidates.isEmpty()) return;
        if (this.selected > 0) {
            this.selected--;
            int newPage = this.selected / this.perPage;
            if (newPage != this.page) {
                this.page = newPage;
            }
        }
    }

    public void selectNext() {
        if (this.candidates.isEmpty()) return;
        if (this.selected < this.candidates.size() - 1) {
            this.selected++;
            int newPage = this.selected / this.perPage;
            if (newPage != this.page) {
                this.page = newPage;
            }
        }
    }

    public void prevPage() {
        if (this.page > 0) {
            this.page--;
            this.selected = this.page * this.perPage;
            if (this.selected >= this.candidates.size()) {
                this.selected = Math.max(0, this.candidates.size() - 1);
            }
        }
    }

    public void nextPage() {
        int totalPages = (this.candidates.size() + this.perPage - 1) / this.perPage;
        if (this.page < totalPages - 1) {
            this.page++;
            this.selected = this.page * this.perPage;
            if (this.selected >= this.candidates.size()) {
                this.selected = Math.max(0, this.candidates.size() - 1);
            }
        }
    }

    public String getSelected() {
        return this.selected >= 0 && this.selected < this.candidates.size() ? this.candidates.get(this.selected) : "";
    }

    public boolean isVisible() {
        return this.visible;
    }

    public int getSelectedIndex() {
        return this.selected;
    }

    public List<String> getCandidates() {
        return this.candidates;
    }

    public String getInput() {
        return this.composition;
    }

    public int getPage() {
        return this.page;
    }

    public int getTotalPages() {
        return (this.candidates.size() + this.perPage - 1) / this.perPage;
    }

    public boolean hasPrevPage() {
        return this.page > 0;
    }

    public boolean hasNextPage() {
        return this.page < getTotalPages() - 1;
    }

    public boolean handleClick(double mouseX, double mouseY, float scale) {
        if (!this.visible) return false;
        int mx = (int) mouseX;
        int my = (int) mouseY;

        if (my < this.y || my > this.y + this.height) return false;

        int itemW = (int)(ITEM_WIDTH_1080P / scale);
        int gap = (int)(ITEM_GAP_1080P / scale);
        int start = this.page * this.perPage;
        int end = Math.min(start + this.perPage, this.candidates.size());
        int compoW = computeCompositionWidth(scale);
        int arrowW = (int)(ARROW_W_1080P / scale);
        int arrowGap = (int)(ARROW_GAP_1080P / scale);

        int compoCx = this.x + computePad(scale) + (compoW > 0 ? compoW + gap : 0);
        for (int i = start; i < end; i++) {
            int itemX = compoCx + (i - start) * (itemW + gap);
            if (mx >= itemX && mx < itemX + itemW) {
                this.selected = i;
                return true;
            }
        }

        int visibleCount = end - start;
        int arrowsX = compoCx + visibleCount * (itemW + gap) + arrowGap;
        if (visibleCount > 0) {
            if (mx >= arrowsX && mx < arrowsX + arrowW) {
                this.pageArrowClicked = true;
                this.prevPage();
                return true;
            }
            if (mx >= arrowsX + arrowW && mx < arrowsX + arrowW * 2) {
                this.pageArrowClicked = true;
                this.nextPage();
                return true;
            }
        }

        return false;
    }

    public void onMouseMove(double mouseX, double mouseY, float scale) {
        if (!this.visible) {
            this.prevArrowHovered = false;
            this.nextArrowHovered = false;
            return;
        }
        int mx = (int) mouseX;
        int my = (int) mouseY;

        if (my < this.y || my > this.y + this.height) {
            this.prevArrowHovered = false;
            this.nextArrowHovered = false;
            return;
        }

        int itemW = (int)(ITEM_WIDTH_1080P / scale);
        int gap = (int)(ITEM_GAP_1080P / scale);
        int compoW = computeCompositionWidth(scale);
        int arrowW = (int)(ARROW_W_1080P / scale);
        int arrowGap = (int)(ARROW_GAP_1080P / scale);

        int start = this.page * this.perPage;
        int end = Math.min(start + this.perPage, this.candidates.size());
        int visibleCount = end - start;

        int compoCx = this.x + computePad(scale) + (compoW > 0 ? compoW + gap : 0);
        int arrowsX = compoCx + visibleCount * (itemW + gap) + arrowGap;
        this.prevArrowHovered = visibleCount > 0 && mx >= arrowsX && mx < arrowsX + arrowW;
        this.nextArrowHovered = visibleCount > 0 && mx >= arrowsX + arrowW && mx < arrowsX + arrowW * 2;
    }

    private int computePad(float scale) {
        return (int)(8 / scale);
    }

    private int computeCompositionWidth(float scale) {
        if (this.composition.isEmpty()) return 0;
        TextRenderer font = MinecraftClient.getInstance().textRenderer;
        int pad = (int)(COMPO_PAD_1080P / scale);
        return font.getWidth(this.composition) + pad * 2;
    }

    private int computeCompositionMinGap(float scale) {
        return (int)(COMPO_MIN_GAP_1080P / scale);
    }

    public void render(DrawContext ctx) {
        if (!this.visible) {
            this.pageArrowClicked = false;
            return;
        }

        MinecraftClient mc = MinecraftClient.getInstance();
        TextRenderer font = mc.textRenderer;

        int scaledW = ctx.getScaledWindowWidth();
        int scaledH = ctx.getScaledWindowHeight();
        int physicalW = mc.getWindow().getWidth();
        float scale = physicalW > 0 ? (float) physicalW / (float) scaledW : 2.0f;

        int h = (int)(HUD_HEIGHT_1080P / scale);
        int itemW = (int)(ITEM_WIDTH_1080P / scale);
        int gap = (int)(ITEM_GAP_1080P / scale);
        int pad = computePad(scale);
        int rightPad = (int)(8 / scale);
        int highlightMargin = (int)(HIGHLIGHT_MARGIN_1080P / scale);
        int blueBarW = (int)(BLUE_BAR_W_1080P / scale);
        int arrowW = (int)(ARROW_W_1080P / scale);
        int arrowGap = (int)(ARROW_GAP_1080P / scale);

        int start = this.page * this.perPage;
        int end = Math.min(start + this.perPage, this.candidates.size());
        int visibleCount = Math.max(end - start, 0);

        int compoW = computeCompositionWidth(scale);
        int compoMinGap = computeCompositionMinGap(scale);

        int candidatesW = visibleCount * itemW + Math.max(0, visibleCount - 1) * gap;
        int arrowsW = visibleCount > 0 ? arrowW * 2 + arrowGap * 2 : 0;
        int compoAreaW = compoW > 0 ? compoW + compoMinGap : 0;
        int rawW = pad + compoAreaW + candidatesW + arrowsW + rightPad;
        int maxW = (int)(MAX_WIDTH_1080P / scale);

        boolean compositionScroll = false;
        int compoDisplayW = compoW;

        if (rawW > maxW && compoW > 0) {
            compositionScroll = true;
            int fixedCandidatesW = Math.min(candidatesW, maxW - pad - compoMinGap - compoW - arrowsW - rightPad - arrowW * 2);
            compoDisplayW = maxW - pad - compoMinGap - fixedCandidatesW - arrowsW - rightPad - arrowW * 2;
            if (compoDisplayW < (int)(40 / scale)) compoDisplayW = (int)(40 / scale);
            compoW = compoDisplayW;
            rawW = maxW;
        }

        int contentW = rawW;
        if (contentW > maxW) contentW = maxW;

        this.width = contentW;
        this.x = (int)(MARGIN_1080P / scale);
        this.y = scaledH - (int)(78 / scale);
        this.height = h;

        int px = this.x;
        int py = this.y;
        int pw = this.width;
        int ph = this.height;

        ctx.fill(px, py, px + pw, py + ph, BG);

        int cx = px + pad;
        int textY = py + (ph - font.fontHeight + 1) / 2;

        if (compoW > 0) {
            int compoTextW = font.getWidth(this.composition);
            int compoPadInside = (int)(COMPO_PAD_1080P / scale);

            if (compositionScroll) {
                int scrollArrowY = textY;
                int scrollArrowColor = ARROW_COLOR;
                ctx.drawText(font, "<", cx, scrollArrowY, scrollArrowColor, false);
                cx += arrowW;
            }

            int compoTextX = cx;
            if (compositionScroll && compoTextW > compoW - compoPadInside * 2) {
                compoTextX = cx;
            } else {
                compoTextX = cx + (compoW - compoTextW) / 2;
            }
            ctx.drawText(font, this.composition, compoTextX, textY, INPUT_COLOR, false);

            if (compositionScroll) {
                int scrollArrowX = cx + compoW - arrowW;
                ctx.drawText(font, ">", scrollArrowX, textY, ARROW_COLOR, false);
            }

            cx += compoW + compoMinGap;
        }

        for (int i = start; i < end; i++) {
            String cand = this.candidates.get(i);
            boolean isSelected = i == this.selected;
            int localIndex = (i - start) + 1;

            int itemLeft = cx + (i - start) * (itemW + gap);

            if (isSelected) {
                ctx.fill(itemLeft, py + highlightMargin,
                    itemLeft + itemW, py + ph - highlightMargin, SEL_BG);
                ctx.fill(itemLeft, py + highlightMargin,
                    itemLeft + blueBarW, py + ph - highlightMargin, SEL_BAR);
            }

            String numStr = String.valueOf(localIndex);
            int numW = font.getWidth(numStr);
            int candW = font.getWidth(cand);
            int spacer = font.getWidth(" ");
            int totalTextW = numW + 1 + candW;
            int textX = itemLeft + (itemW - totalTextW) / 2;

            ctx.drawText(font, numStr, textX, textY, NUM_COLOR, false);
            ctx.drawText(font, cand, textX + numW + 1, textY, TEXT_COLOR, false);
        }

        if (visibleCount > 0) {
            int arrowsX = cx + visibleCount * (itemW + gap) + arrowGap;
            int arrowColorLeft = this.prevArrowHovered ? ARROW_HOVER_COLOR : ARROW_COLOR;
            int arrowColorRight = this.nextArrowHovered ? ARROW_HOVER_COLOR : ARROW_COLOR;
            ctx.drawText(font, "<", arrowsX, textY, arrowColorLeft, false);
            ctx.drawText(font, ">", arrowsX + arrowW, textY, arrowColorRight, false);
        }
    }

    public void clearInput() {
        this.candidates.clear();
        this.composition = "";
        this.selected = 0;
        this.page = 0;
    }

    public int getX() {
        return this.x;
    }

    public int getY() {
        return this.y;
    }

    public int getWidth() {
        return this.width;
    }

    public int getHeight() {
        return this.height;
    }
}