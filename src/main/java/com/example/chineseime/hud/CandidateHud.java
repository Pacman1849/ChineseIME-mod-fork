package com.example.chineseime.hud;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.font.TextRenderer;
import net.minecraft.client.gui.DrawContext;

public class CandidateHud {
    private List<String> candidates = new ArrayList<>();
    private String input = "";
    private int selected = 0;
    private int page = 0;
    private int perPage = 9;
    private boolean visible = false;
    private int x;
    private int y;
    private int width;
    private int height;
    private static final int BG = 0x99000000;
    private static final int BORDER_COLOR = 0xFF4466AA;
    private static final int SEL_BAR = 0xFF4466AA;
    private static final int SEL_BG = 0x334466AA;
    private static final int TEXT_COLOR = 0xFFFFFFFF;
    private static final int NUM_COLOR = 0xFF888888;
    private static final int INPUT_COLOR = 0xFF6699FF;
    private static final int ARROW_COLOR = 0xFFAAAAAA;

    public void updateCandidates(List<String> candidates, String input) {
        this.candidates = candidates != null ? new ArrayList<>(candidates) : new ArrayList<>();
        this.input = input != null ? input : "";
        this.selected = 0;
        this.page = 0;
        this.visible = !this.candidates.isEmpty();
    }

    public void clear() {
        this.candidates.clear();
        this.input = "";
        this.selected = 0;
        this.page = 0;
        this.visible = false;
    }

    public void selectPrevious() {
        if (!this.candidates.isEmpty()) {
            int pageStart = this.page * this.perPage;
            if (this.selected > pageStart) {
                --this.selected;
            } else if (this.page > 0) {
                --this.page;
                this.selected = Math.min((this.page + 1) * this.perPage - 1, this.candidates.size() - 1);
            }
        }
    }

    public void selectNext() {
        if (!this.candidates.isEmpty()) {
            int pageEnd = Math.min((this.page + 1) * this.perPage, this.candidates.size()) - 1;
            if (this.selected < pageEnd) {
                ++this.selected;
            } else if ((this.page + 1) * this.perPage < this.candidates.size()) {
                ++this.page;
                this.selected = this.page * this.perPage;
            }
        }
    }

    public void prevPage() {
        if (this.page > 0) {
            --this.page;
            this.selected = this.page * this.perPage;
        }
    }

    public void nextPage() {
        if ((this.page + 1) * this.perPage < this.candidates.size()) {
            ++this.page;
            this.selected = this.page * this.perPage;
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
        return this.input;
    }

    public int getPage() {
        return this.page;
    }

    public boolean hasPrevPage() {
        return this.page > 0;
    }

    public boolean hasNextPage() {
        return (this.page + 1) * this.perPage < this.candidates.size();
    }

    public boolean handleClick(double mouseX, double mouseY) {
        if (!this.visible) return false;
        int mx = (int)mouseX;
        int my = (int)mouseY;
        if (my >= this.y && my <= this.y + this.height) {
            if (this.hasPrevPage() && mx >= this.x + 4 && mx <= this.x + 20) {
                this.prevPage();
                return true;
            } else if (this.hasNextPage() && mx >= this.x + this.width - 20 && mx <= this.x + this.width - 4) {
                this.nextPage();
                return true;
            }
        }
        return false;
    }

    public void render(DrawContext ctx) {
        if (this.visible) {
            MinecraftClient mc = MinecraftClient.getInstance();
            TextRenderer font = mc.textRenderer;
            int screenH = mc.getWindow().getHeight();
            int h = 40;
            int pad = 6;
            int charPad = 6;
            int arrowW = 20;
            int w = pad * 2;

            int inputW = font.getWidth(this.input);
            if (!this.input.isEmpty()) {
                w += inputW + charPad * 2;
            }

            int start = this.page * this.perPage;
            int end = Math.min((this.page + 1) * this.perPage, this.candidates.size());
            int cx = pad;

            for (int i = start; i < end; ++i) {
                String cand = this.candidates.get(i);
                int numW = font.getWidth((i - start + 1) + ".");
                int txtW = font.getWidth(cand);
                cx += numW + txtW + charPad * 2;
            }

            int minW = 300;
            int maxW = 960;
            w = Math.max(w, cx + pad);
            w = Math.min(w, Math.max(minW, maxW));
            w = Math.min(w, 400);
            this.x = 4;
            this.y = screenH - h - 16;
            this.width = w;
            this.height = h;

            ctx.fill(this.x, this.y, this.x + w, this.y + h, BG);
        ctx.drawBorder(this.x, this.y, w, h, BORDER_COLOR);

            cx = this.x + pad;

            if (this.hasPrevPage()) {
                ctx.drawText(font, "<", cx, this.y + (h - 8) / 2, ARROW_COLOR, false);
                cx += arrowW;
            }

            if (!this.input.isEmpty()) {
                ctx.drawText(font, "[" + this.input + "]", cx, this.y + (h - 8) / 2, INPUT_COLOR, false);
                cx += inputW + charPad * 2;
            }

            for (int i = start; i < end; ++i) {
                String cand = this.candidates.get(i);
                boolean isSelected = i == this.selected;
                int num = i - start + 1;
                String numStr = num + ".";
                int numW = font.getWidth(numStr);
                int txtW = font.getWidth(cand);
                int itemW = numW + txtW + charPad * 2;

                if (cx + itemW > this.x + w - pad - (this.hasNextPage() ? arrowW : 0)) {
                    break;
                }

                if (isSelected) {
                    ctx.fill(cx, this.y + 2, cx + itemW + 2, this.y + h - 2, SEL_BG);
                    ctx.fill(cx, this.y + 2, cx + 3, this.y + h - 2, SEL_BAR);
                }

                ctx.drawText(font, numStr, cx + 5, this.y + (h - 8) / 2, NUM_COLOR, false);
                ctx.drawText(font, cand, cx + 5 + numW, this.y + (h - 8) / 2, TEXT_COLOR, false);
                cx += itemW + charPad;
            }

            if (this.hasNextPage()) {
                ctx.drawText(font, ">", this.x + w - pad - 10, this.y + (h - 8) / 2, ARROW_COLOR, false);
            }
        }
    }

    public void clearInput() {
        this.candidates.clear();
        this.input = "";
        this.selected = 0;
        this.page = 0;
        this.visible = false;
    }
}