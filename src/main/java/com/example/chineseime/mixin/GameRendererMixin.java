package com.example.chineseime.mixin;

import com.example.chineseime.ChineseIMEInitializer;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.gui.screen.ChatScreen;
import net.minecraft.client.render.GameRenderer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(GameRenderer.class)
public class GameRendererMixin {

    @Inject(method = "render", at = @At("TAIL"))
    private void onRenderTail(float tickDelta, long startTime, boolean tick, CallbackInfo ci) {
        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc == null || mc.options == null) return;

        DrawContext ctx = new DrawContext(mc, mc.getBufferBuilders().getEntityVertexConsumers());
        ChineseIMEInitializer instance = ChineseIMEInitializer.getInstance();
        if (instance != null) {
            instance.renderHUD(ctx);
        }
    }
}