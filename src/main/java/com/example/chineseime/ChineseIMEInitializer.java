package com.example.chineseime;

import com.example.chineseime.config.ModConfig;
import com.example.chineseime.engine.InputMode;
import com.example.chineseime.hud.CandidateHud;
import com.example.chineseime.hud.ImeStatusIndicator;
import com.example.chineseime.keybind.KeyBindingManager;
import com.example.chineseime.platform.PlatformIMEManager;
import com.example.chineseime.platform.win32.WindowsIMEBridgeNative;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.api.EnvType;
import net.fabricmc.api.Environment;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.rendering.v1.HudRenderCallback;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.screen.ChatScreen;
import org.lwjgl.glfw.GLFW;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Environment(EnvType.CLIENT)
public class ChineseIMEInitializer implements ClientModInitializer {
    public static final String MOD_ID = "chineseime";
    public static final Logger LOGGER = LoggerFactory.getLogger("chineseime");
    private static ChineseIMEInitializer instance;
    private ModConfig config;
    private CandidateHud candidateHud;
    private ImeStatusIndicator statusIndicator;
    private PlatformIMEManager imeManager;
    private KeyBindingManager keyBindingManager;

    @Override
    public void onInitializeClient() {
        instance = this;
        LOGGER.info("[ChineseIME] Initializing...");
        this.config = ModConfig.load();
        this.candidateHud = new CandidateHud();
        this.statusIndicator = new ImeStatusIndicator();
        this.imeManager = new PlatformIMEManager(this.config, this.candidateHud);
        this.keyBindingManager = new KeyBindingManager(this.config, this.imeManager);
        this.keyBindingManager.register();

HudRenderCallback.EVENT.register((ctx, tickDelta) -> {
    MinecraftClient mc = MinecraftClient.getInstance();
    boolean inChatScreen = mc != null && mc.currentScreen instanceof ChatScreen;
    this.statusIndicator.setInChatScreen(inChatScreen);

    boolean isTyping = this.imeManager.hasInput();
    boolean layoutChanged = this.imeManager.checkAndClearLayoutChanged();
    boolean capsLockOn = false;
    boolean inShiftMode = false;
    boolean chineseMode = this.config.isChineseMode();

    if (this.imeManager.isWindowsSync()) {
        Object bridge = this.imeManager.getWindowsBridge();
        if (bridge instanceof WindowsIMEBridgeNative windowsBridge) {
            capsLockOn = windowsBridge.isCapsLockOn();
            inShiftMode = windowsBridge.isInShiftMode();
            chineseMode = windowsBridge.isChineseMode();
            LOGGER.debug("[ChineseIME] HUD: chat={}, capsLock={}, shift={}, chinese={}, detected={}",
                inChatScreen, capsLockOn, inShiftMode, chineseMode, windowsBridge.getDetectedInputMode());
        }
    }

    InputMode detectedMode = this.imeManager.getDetectedInputMode();
    this.statusIndicator.update(chineseMode, detectedMode, capsLockOn, inShiftMode, isTyping, layoutChanged);

    if (isTyping) {
        this.candidateHud.render(ctx);
    }

    this.statusIndicator.render(ctx);
});

ClientTickEvents.END_CLIENT_TICK.register(client -> {
        this.keyBindingManager.tick();
        this.imeManager.tick();

        if (this.imeManager.hasInput()) {
            long window = client.getWindow().getHandle();
            boolean leftPressed = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_LEFT) == GLFW.GLFW_PRESS;
            boolean rightPressed = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_RIGHT) == GLFW.GLFW_PRESS;
            boolean upPressed = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_UP) == GLFW.GLFW_PRESS;
            boolean downPressed = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_DOWN) == GLFW.GLFW_PRESS;

            if (leftPressed || upPressed) {
                this.imeManager.selectPrev();
            } else if (rightPressed || downPressed) {
                this.imeManager.selectNext();
            }
        }
    });

        LOGGER.info("[ChineseIME] Initialization complete! Platform: {}, isWindowsSync: {}", PlatformIMEManager.getPlatform(), this.imeManager.isWindowsSync());
    }

    public static ChineseIMEInitializer getInstance() {
        return instance;
    }

    public ModConfig getConfig() {
        return this.config;
    }

    public CandidateHud getCandidateHud() {
        return this.candidateHud;
    }

    public ImeStatusIndicator getStatusIndicator() {
        return this.statusIndicator;
    }

    public PlatformIMEManager getImeManager() {
        return this.imeManager;
    }

    public boolean hasLayoutChanged() {
        return this.imeManager.hasLayoutChanged();
    }
}