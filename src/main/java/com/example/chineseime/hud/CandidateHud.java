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
    private static final int SEL_BG = 0x664466AA;
    private static final int TEXT_COLOR = 0xFFFFFFFF;
    private static final int NUM_COLOR = 0xFF888888;
    private static final int INPUT_COLOR = 0xFFB991FF;
    private static final int ARROW_COLOR = 0xFFAAAAAA;
    private static final int CHAR_WIDTH = 70;
    private static final int EXTRA_WIDTH = 20;
    private static final int HUD_HEIGHT = 40;
    private static final int MAX_WIDTH = 960;
    private static final int DEFAULT_WIDTH = 630;

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
            int arrowAreaW = 24;
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
        int screenH = mc.getWindow().getHeight();
        int scaledH = mc.getWindow().getScaledHeight();

        List<String> displayCandidates = this.candidates;
        if (displayCandidates.isEmpty()) {
            displayCandidates = java.util.Arrays.asList("测试词语1", "测试词语2", "测试词语3", "测试词语4", "测试词语5", "测试词语6", "测试词语7", "测试词语8", "测试词语9");
            this.composition = "测试";
        }

        int h = HUD_HEIGHT;
        int pad = 6;
        int arrowW = 24;

        int contentW = displayCandidates.size() * (CHAR_WIDTH + EXTRA_WIDTH);
        contentW = Math.min(contentW, MAX_WIDTH);
        contentW = Math.max(contentW, DEFAULT_WIDTH);

        int inputW = font.getWidth(this.composition);
        int inputAreaW = (!this.composition.isEmpty()) ? inputW + 20 : 0;

        this.width = contentW + pad * 2 + inputAreaW;
        this.width = Math.min(this.width, MAX_WIDTH + 100);
        this.width = Math.max(this.width, DEFAULT_WIDTH);

        this.x = 4;
        this.y = screenH - h - 16;

        int maxCharsOnPage = (int) ((this.width - pad * 2 - inputAreaW - (this.hasPrevPage() ? arrowW : 0) - (this.hasNextPage() ? arrowW : 0)) / (CHAR_WIDTH + EXTRA_WIDTH));
        if (maxCharsOnPage <= 0) maxCharsOnPage = 9;

        int actualPerPage = Math.min(this.perPage, displayCandidates.size());
        this.height = h;

        ctx.fill(this.x, this.y, this.x + this.width, this.y + h, BG);
        ctx.drawBorder(this.x, this.y, this.width, h, BORDER_COLOR);

        int cx = this.x + pad;

        int totalPages = (displayCandidates.size() + this.perPage - 1) / this.perPage;
        boolean hasPrev = this.page > 0;
        boolean hasNext = this.page < totalPages - 1;

        if (hasPrev) {
            ctx.drawText(font, "<", cx, this.y + (h - 8) / 2, ARROW_COLOR, false);
            cx += arrowW;
        }

        int start = this.page * this.perPage;
        int end = Math.min(start + actualPerPage, displayCandidates.size());

        int availableW = this.width - pad - (hasPrev ? arrowW : 0) - (hasNext ? arrowW : 0) - inputAreaW;
        int itemW = CHAR_WIDTH + EXTRA_WIDTH;
        int maxItems = Math.min(actualPerPage, availableW / itemW);

        end = Math.min(start + maxItems, displayCandidates.size());

        for (int i = start; i < end; ++i) {
            String cand = displayCandidates.get(i);
            boolean isSelected = i == this.selected;

            if (isSelected) {
                ctx.fill(cx, this.y + 2, cx + CHAR_WIDTH + EXTRA_WIDTH, this.y + h - 2, SEL_BG);
                ctx.fill(cx, this.y + 2, cx + 3, this.y + h - 2, SEL_BAR);
            }

            ctx.drawText(font, cand, cx + 8, this.y + (h - 8) / 2, TEXT_COLOR, false);
            cx += CHAR_WIDTH + EXTRA_WIDTH;
        }

        if (hasNext) {
            cx = this.x + this.width - pad - arrowW;
            ctx.drawText(font, ">", cx, this.y + (h - 8) / 2, ARROW_COLOR, false);
        }

        if (!this.composition.isEmpty()) {
            int inputX = this.x + this.width - pad - inputAreaW;
            ctx.drawText(font, this.composition, inputX + 4, this.y + (h - 8) / 2, INPUT_COLOR, false);
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