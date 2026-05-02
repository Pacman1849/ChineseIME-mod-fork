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
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.DrawContext;
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
    private boolean ctrlShiftTPressed = false;
    private boolean prevLeftPressed = false;
    private boolean prevRightPressed = false;
    private boolean prevUpPressed = false;
    private boolean prevDownPressed = false;

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

        this.imeManager.showTestCandidates();

        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            this.keyBindingManager.tick();
            this.imeManager.tick();

            long window = client.getWindow().getHandle();
            boolean leftPressed = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_LEFT) == GLFW.GLFW_PRESS;
            boolean rightPressed = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_RIGHT) == GLFW.GLFW_PRESS;
            boolean upPressed = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_UP) == GLFW.GLFW_PRESS;
            boolean downPressed = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_DOWN) == GLFW.GLFW_PRESS;

            if (this.imeManager.hasInput()) {
                if ((leftPressed && !prevLeftPressed) || (upPressed && !prevUpPressed)) {
                    this.imeManager.selectPrev();
                } else if ((rightPressed && !prevRightPressed) || (downPressed && !prevDownPressed)) {
                    this.imeManager.selectNext();
                }
            }

            prevLeftPressed = leftPressed;
            prevRightPressed = rightPressed;
            prevUpPressed = upPressed;
            prevDownPressed = downPressed;

            boolean ctrl = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_LEFT_CONTROL) == GLFW.GLFW_PRESS ||
                    GLFW.glfwGetKey(window, GLFW.GLFW_KEY_RIGHT_CONTROL) == GLFW.GLFW_PRESS;
            boolean shift = GLFW.glfwGetKey(window, GLFW.GLFW_KEY_LEFT_SHIFT) == GLFW.GLFW_PRESS ||
                    GLFW.glfwGetKey(window, GLFW.GLFW_KEY_RIGHT_SHIFT) == GLFW.GLFW_PRESS;
            if (ctrl && shift && GLFW.glfwGetKey(window, GLFW.GLFW_KEY_T) == GLFW.GLFW_PRESS) {
                if (!this.ctrlShiftTPressed) {
                    this.imeManager.showTestCandidates();
                    this.ctrlShiftTPressed = true;
                }
            } else {
                this.ctrlShiftTPressed = false;
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

    public void renderHud(DrawContext ctx) {
        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc == null) return;

        boolean inChatScreen = mc.currentScreen instanceof ChatScreen;
        this.statusIndicator.setInChatScreen(inChatScreen);

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
                this.statusIndicator.setDllInitialized(true);
            }
        }

        InputMode detectedMode = this.imeManager.getDetectedInputMode();
        this.statusIndicator.update(chineseMode, detectedMode, capsLockOn, inShiftMode, true, layoutChanged);

        this.candidateHud.render(ctx);
        this.statusIndicator.render(ctx);
    }
}