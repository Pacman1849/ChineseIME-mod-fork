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
        if (instance == null || instance.getImeManager() == null) return;

        var imeManager = instance.getImeManager();
        var h = imeManager.getHud();
        var v = imeManager.getVerticalHud();
        boolean hasCandidates = (h != null && h.isVisible() && !h.getCandidates().isEmpty()) ||
                               (v != null && v.isVisible() && !v.getCandidates().isEmpty());

        if (!hasCandidates) return;

        if (keyCode == GLFW.GLFW_KEY_ENTER || keyCode == GLFW.GLFW_KEY_KP_ENTER) {
            instance.handleEnterKey();
            cir.setReturnValue(true);
            return;
        }

        if (keyCode == GLFW.GLFW_KEY_BACKSPACE) {
            instance.handleBackspace();
            cir.setReturnValue(true);
            return;
        }

        if (keyCode >= GLFW.GLFW_KEY_1 && keyCode <= GLFW.GLFW_KEY_9) {
            int num = keyCode - GLFW.GLFW_KEY_1 + 1;
            instance.handleNumberKey(num);
            cir.setReturnValue(true);
            return;
        }
    }
}