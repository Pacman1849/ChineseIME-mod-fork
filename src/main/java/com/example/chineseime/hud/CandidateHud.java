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
    private static final int SEL_BAR = 0xFF4488FF;
    private static final int SEL_BG = 0x50505050;
    private static final int TEXT_COLOR = 0xFFFFFFFF;
    private static final int INPUT_COLOR = 0xFFB991FF;
    private static final int ARROW_COLOR = 0xFFAAAAAA;
    private static final int ARROW_HOVER_COLOR = 0xFFFFFFFF;

    private static final int HUD_HEIGHT_PX = 40;
    private static final int CHAR_WIDTH_PX = 70;
    private static final int CHAR_EXTRA_PX = 20;
    private static final int DEFAULT_WIDTH_PX = 630;
    private static final int MAX_WIDTH_PX = 960;
    private static final int MARGIN_PX = 8;
    private static final int SEL_BAR_WIDTH_PX = 3;

    private boolean prevArrowHovered = false;
    private boolean nextArrowHovered = false;
    private boolean pageArrowClicked = false;

    public void updateCandidates(List<String> candidates, String composition) {
        this.candidates = candidates != null ? new ArrayList<>(candidates) : new ArrayList<>();
        this.composition = composition != null ? composition : "";
        this.selected = 0;
        this.page = 0;
        this.visible = !this.candidates.isEmpty();
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

    public boolean handleClick(double mouseX, double mouseY) {
        if (!this.visible) return false;
        int mx = (int) mouseX;
        int my = (int) mouseY;

        if (my < this.y || my > this.y + this.height) return false;

        MinecraftClient mc = MinecraftClient.getInstance();
        TextRenderer font = mc.textRenderer;
        int scaledW = mc.getWindow().getScaledWidth();
        int actualW = mc.getWindow().getWidth();
        float scale = (float) actualW / scaledW;
        int pad = (int) (6 * scale);
        int arrowW = (int) (24 * scale);
        int itemW = (int) (CHAR_WIDTH_PX + CHAR_EXTRA_PX);

        boolean hasPrev = hasPrevPage();
        boolean hasNext = hasNextPage();
        int cx = this.x + pad;
        if (hasPrev) cx += arrowW;

        if (hasPrev && mx >= this.x + pad && mx < this.x + pad + arrowW) {
            this.pageArrowClicked = true;
            this.prevPage();
            return true;
        }

        if (hasNext && mx >= this.x + this.width - pad - arrowW && mx <= this.x + this.width - pad) {
            this.pageArrowClicked = true;
            this.nextPage();
            return true;
        }

        int start = this.page * this.perPage;
        int end = Math.min(start + this.perPage, this.candidates.size());
        for (int i = start; i < end; i++) {
            int itemX = cx + (i - start) * itemW;
            if (mx >= itemX && mx < itemX + itemW) {
                this.selected = i;
                return true;
            }
        }

        return false;
    }

    public void onMouseMove(double mouseX, double mouseY) {
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

        MinecraftClient mc = MinecraftClient.getInstance();
        int scaledW = mc.getWindow().getScaledWidth();
        int actualW = mc.getWindow().getWidth();
        float scale = (float) actualW / scaledW;
        int pad = (int) (6 * scale);
        int arrowW = (int) (24 * scale);

        this.prevArrowHovered = hasPrevPage() && mx >= this.x + pad && mx < this.x + pad + arrowW;
        this.nextArrowHovered = hasNextPage() && mx >= this.x + this.width - pad - arrowW && mx <= this.x + this.width - pad;
    }

    public void render(DrawContext ctx) {
        if (!this.visible) {
            this.pageArrowClicked = false;
            return;
        }

        MinecraftClient mc = MinecraftClient.getInstance();
        TextRenderer font = mc.textRenderer;

        int scaledW = mc.getWindow().getScaledWidth();
        int scaledH = mc.getWindow().getScaledHeight();
        int actualW = mc.getWindow().getWidth();
        int actualH = mc.getWindow().getHeight();
        float scale = (float) actualW / scaledW;

        List<String> displayCandidates = this.candidates;

        int h = (int) (HUD_HEIGHT_PX * scale);
        int pad = (int) (6 * scale);
        int arrowW = (int) (24 * scale);
        int itemW = (int) (CHAR_WIDTH_PX + CHAR_EXTRA_PX);

        boolean hasPrev = hasPrevPage();
        boolean hasNext = hasNextPage();

        int start = this.page * this.perPage;
        int end = Math.min(start + this.perPage, displayCandidates.size());
        int visibleCount = end - start;

        int candidatesW = visibleCount * itemW;
        if (hasPrev) candidatesW += arrowW;
        if (hasNext) candidatesW += arrowW;

        int inputW = 0;
        int inputAreaW = 0;
        if (!this.composition.isEmpty()) {
            inputW = font.getWidth(this.composition) + pad * 2;
            inputAreaW = inputW + pad;
        }

        int contentW = candidatesW + inputAreaW;
        contentW = Math.max(contentW, (int) (DEFAULT_WIDTH_PX * scale));
        contentW = Math.min(contentW, (int) (MAX_WIDTH_PX * scale));

        this.width = contentW + pad * 2;
        this.x = (int) (MARGIN_PX * scale);
        this.y = actualH - h - (int) (MARGIN_PX * scale);
        this.height = h;

        ctx.fill(this.x, this.y, this.x + this.width, this.y + h, BG);

        int cx = this.x + pad;

        if (hasPrev) {
            int arrowColor = this.prevArrowHovered ? ARROW_HOVER_COLOR : ARROW_COLOR;
            ctx.drawText(font, "<", cx + (arrowW - font.getWidth("<")) / 2,
                this.y + (h - font.fontHeight) / 2, arrowColor, false);
            cx += arrowW;
        }

        for (int i = start; i < end; i++) {
            String cand = displayCandidates.get(i);
            boolean isSelected = i == this.selected;

            if (isSelected) {
                ctx.fill(cx, this.y + 2, cx + itemW, this.y + h - 2, SEL_BG);
                ctx.fill(cx, this.y + 2, cx + SEL_BAR_WIDTH_PX, this.y + h - 2, SEL_BAR);
            }

            ctx.drawText(font, cand,
                cx + SEL_BAR_WIDTH_PX + 4,
                this.y + (h - font.fontHeight) / 2,
                TEXT_COLOR, false);
            cx += itemW;
        }

        if (hasNext) {
            int arrowColor = this.nextArrowHovered ? ARROW_HOVER_COLOR : ARROW_COLOR;
            ctx.drawText(font, ">", cx + (arrowW - font.getWidth(">")) / 2,
                this.y + (h - font.fontHeight) / 2, arrowColor, false);
            cx += arrowW;
        }

        if (!this.composition.isEmpty()) {
            int inputX = this.x + this.width - pad - inputW;
            ctx.drawText(font, this.composition, inputX, this.y + (h - font.fontHeight) / 2, INPUT_COLOR, false);
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