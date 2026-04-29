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
    private boolean visible = false;
    private int x;
    private int y;
    private int width;
    private int height;
    private static final int BG = 0x80000000;
    private static final int BORDER_COLOR = 0xFF4466AA;
    private static final int SEL_BAR = 0xFF4488FF;
    private static final int SEL_BG = 0x80505050;
    private static final int TEXT_COLOR = 0xFFFFFFFF;
    private static final int NUM_COLOR = 0xFF888888;
    private static final int INPUT_COLOR = 0xFFB991FF;
    private static final int ARROW_COLOR = 0xFFAAAAAA;

    private static final int HUD_HEIGHT_PX = 40;
    private static final int CHAR_WIDTH_PX = 70;
    private static final int CHAR_EXTRA_PX = 20;
    private static final int DEFAULT_WIDTH_PX = 630;
    private static final int MAX_WIDTH_PX = 960;
    private static final int MARGIN_PX = 8;

    public void updateCandidates(List<String> candidates, String composition) {
        this.candidates = candidates != null ? new ArrayList<>(candidates) : new ArrayList<>();
        this.composition = composition != null ? composition : "";
        this.selected = 0;
        this.page = 0;
        this.visible = !this.candidates.isEmpty();
    }

    public void clear() {
        this.candidates.clear();
        this.composition = "";
        this.selected = 0;
        this.page = 0;
        this.visible = false;
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
        int maxPage = (this.candidates.size() - 1) / this.perPage;
        if (this.page < maxPage) {
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
        return true;
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
        return (this.page + 1) * this.perPage < this.candidates.size();
    }

    public boolean handleClick(double mouseX, double mouseY) {
        if (!this.visible) return false;
        int mx = (int) mouseX;
        int my = (int) mouseY;

        if (my >= this.y && my <= this.y + this.height) {
            int arrowAreaW = 30;
            if (this.hasPrevPage() && mx >= this.x + 4 && mx <= this.x + 4 + arrowAreaW) {
                this.prevPage();
                return true;
            } else if (this.hasNextPage() && mx >= this.x + this.width - 4 - arrowAreaW && mx <= this.x + this.width - 4) {
                this.nextPage();
                return true;
            }
        }
        return false;
    }

    public void render(DrawContext ctx) {
        MinecraftClient mc = MinecraftClient.getInstance();
        TextRenderer font = mc.textRenderer;

        int scaledW = mc.getWindow().getScaledWidth();
        int scaledH = mc.getWindow().getScaledHeight();
        int actualW = mc.getWindow().getWidth();
        int actualH = mc.getWindow().getHeight();
        float scaleFactor = (float) actualW / scaledW;

        List<String> displayCandidates = this.candidates;
        if (displayCandidates.isEmpty()) {
            displayCandidates = java.util.Arrays.asList("测试词语1", "测试词语2", "测试词语3", "测试词语4", "测试词语5", "测试词语6", "测试词语7", "测试词语8", "测试词语9");
            this.composition = "测试";
        }

        int h = (int) (HUD_HEIGHT_PX * scaleFactor);
        int pad = (int) (6 * scaleFactor);
        int arrowW = (int) (24 * scaleFactor);
        int charW = (int) (CHAR_WIDTH_PX * scaleFactor);
        int charExtra = (int) (CHAR_EXTRA_PX * scaleFactor);

        int inputW = (int) (font.getWidth(this.composition) * scaleFactor);
        int inputAreaW = (!this.composition.isEmpty()) ? inputW + pad * 2 : 0;

        int defaultW = (int) (DEFAULT_WIDTH_PX * scaleFactor);
        int maxW = (int) (MAX_WIDTH_PX * scaleFactor);

        int contentW = Math.min(displayCandidates.size() * (charW + charExtra), maxW);
        contentW = Math.max(contentW, defaultW);

        this.width = contentW + pad * 2 + inputAreaW;
        this.width = Math.min(this.width, maxW + (int) (100 * scaleFactor));
        this.width = Math.max(this.width, defaultW);

        this.x = (int) (MARGIN_PX * scaleFactor);
        this.y = actualH - h - (int) (MARGIN_PX * scaleFactor);

        this.height = h;

        ctx.fill(this.x, this.y, this.x + this.width, this.y + h, BG);
        ctx.drawBorder(this.x, this.y, this.width, h, BORDER_COLOR);

        int cx = this.x + pad;

        int totalPages = (displayCandidates.size() + this.perPage - 1) / this.perPage;
        boolean hasPrev = this.page > 0;
        boolean hasNext = this.page < totalPages - 1;

        if (hasPrev) {
            ctx.drawText(font, "<", cx, this.y + (h - (int) (16 * scaleFactor)) / 2, ARROW_COLOR, false);
            cx += arrowW;
        }

        if (!this.composition.isEmpty()) {
            ctx.drawText(font, this.composition, cx, this.y + (h - (int) (16 * scaleFactor)) / 2, INPUT_COLOR, false);
            cx += inputW + pad;
        }

        int start = this.page * this.perPage;
        int actualPerPage = Math.min(this.perPage, displayCandidates.size());

        int availableW = this.width - pad - (hasPrev ? arrowW : 0) - (hasNext ? arrowW : 0) - inputAreaW;
        int itemW = charW + charExtra;
        int maxItems = Math.min(actualPerPage, availableW / itemW);

        int end = Math.min(start + maxItems, displayCandidates.size());

        int textY = this.y + (h - (int) (16 * scaleFactor)) / 2;
        int textPadLeft = (int) (8 * scaleFactor);

        for (int i = start; i < end; ++i) {
            String cand = displayCandidates.get(i);
            boolean isSelected = i == this.selected;

            if (isSelected) {
                ctx.fill(cx, this.y + 2, cx + charW + charExtra, this.y + h - 2, SEL_BG);
                ctx.fill(cx, this.y + 2, cx + 3, this.y + h - 2, SEL_BAR);
            }

            ctx.drawText(font, cand, cx + textPadLeft, textY, TEXT_COLOR, false);
            cx += charW + charExtra;
        }

        if (hasNext) {
            cx = this.x + this.width - pad - arrowW;
            ctx.drawText(font, ">", cx, textY, ARROW_COLOR, false);
        }
    }

    public void clearInput() {
        this.candidates.clear();
        this.composition = "";
        this.selected = 0;
        this.page = 0;
        this.visible = false;
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