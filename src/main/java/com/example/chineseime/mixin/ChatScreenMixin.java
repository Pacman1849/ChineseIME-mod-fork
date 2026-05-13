package com.example.chineseime.mixin;

import com.example.chineseime.ChineseIMEInitializer;
import net.minecraft.client.gui.screen.ChatScreen;
import org.lwjgl.glfw.GLFW;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(ChatScreen.class)
public abstract class ChatScreenMixin {

    @Inject(method = "mouseClicked", at = @At("HEAD"), cancellable = true)
    private void onMouseClicked(double mouseX, double mouseY, int button, CallbackInfoReturnable<Boolean> cir) {
        ChineseIMEInitializer instance = ChineseIMEInitializer.getInstance();
        if (instance == null) return;

        if (button == 0) {
            if (instance.getImeManager().isVerticalLayout()) {
                float scale = instance.getVerticalCandidateHud().getScaleForClick();
                if (instance.getVerticalCandidateHud().handleClick(mouseX, mouseY, scale)) {
                    cir.setReturnValue(true);
                }
            } else {
                float scale = instance.getCandidateHud().getScaleForClick();
                if (instance.getCandidateHud().handleClick(mouseX, mouseY, scale)) {
                    cir.setReturnValue(true);
                }
            }
        }
    }

    @Inject(method = "keyPressed", at = @At("HEAD"), cancellable = true)
    private void onKeyPressed(int keyCode, int scanCode, int modifiers, CallbackInfoReturnable<Boolean> cir) {
        ChineseIMEInitializer instance = ChineseIMEInitializer.getInstance();
        if (instance == null || instance.getImeManager() == null) {
            // Let all keys through if IME not initialized
            return;
        }

        var imeManager = instance.getImeManager();
        var h = imeManager.getHud();
        var v = imeManager.getVerticalHud();

        // Check if candidates are visible
        boolean horizontalHasCandidates = (h != null && h.isVisible() && !h.getCandidates().isEmpty());
        boolean verticalHasCandidates = (v != null && v.isVisible() && !v.getCandidates().isEmpty());
        boolean hasCandidates = horizontalHasCandidates || verticalHasCandidates;

        // CRITICAL: Never block letter keys (A-Z, a-z) or general typing!
        // For TSF IMEs, the system handles all input. We only intercept
        // special keys when candidates are visible.

        // Only intercept special keys when candidates are visible
        if (hasCandidates) {
            // Enter: select current/highlighted candidate
            if (keyCode == GLFW.GLFW_KEY_ENTER || keyCode == GLFW.GLFW_KEY_KP_ENTER) {
                ChineseIMEInitializer.LOGGER.info("[ChineseIME] Mixin: Enter -> select candidate");
                instance.handleEnterKey();
                cir.setReturnValue(true);
                return;
            }

            // Backspace: delete last character of composition
            if (keyCode == GLFW.GLFW_KEY_BACKSPACE) {
                ChineseIMEInitializer.LOGGER.info("[ChineseIME] Mixin: Backspace -> delete char");
                instance.handleBackspace();
                cir.setReturnValue(true);
                return;
            }

            // Number 1-9: select specific candidate
            if (keyCode >= GLFW.GLFW_KEY_1 && keyCode <= GLFW.GLFW_KEY_9) {
                int num = keyCode - GLFW.GLFW_KEY_1 + 1;
                ChineseIMEInitializer.LOGGER.info("[ChineseIME] Mixin: Number {} -> select candidate", num);
                instance.handleNumberKey(num);
                cir.setReturnValue(true);
                return;
            }

            // Space: select first candidate (common behavior)
            if (keyCode == GLFW.GLFW_KEY_SPACE) {
                ChineseIMEInitializer.LOGGER.info("[ChineseIME] Mixin: Space -> select first candidate");
                instance.handleNumberKey(1);
                cir.setReturnValue(true);
                return;
            }

            // Escape: close candidates without selection
            if (keyCode == GLFW.GLFW_KEY_ESCAPE) {
                ChineseIMEInitializer.LOGGER.info("[ChineseIME] Mixin: Escape -> clear");
                instance.getImeManager().clearInput();
                cir.setReturnValue(true);
                return;
            }
        }

        // IMPORTANT: Let ALL other keys pass through!
        // - Letter keys (A-Z): Normal typing, handled by system IME
        // - Number keys: Normal typing
        // - Punctuation: Normal typing
        // - Function keys: Normal behavior
        // - Arrow keys: Cursor movement
        // etc.
        //
        // DO NOT block anything here. The system IME handles all input.
    }
}