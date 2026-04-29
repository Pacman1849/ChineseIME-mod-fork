package com.example.chineseime.keybind;

import com.example.chineseime.config.ConfigScreen;
import com.example.chineseime.config.ModConfig;
import com.example.chineseime.platform.PlatformIMEManager;
import net.fabricmc.fabric.api.client.keybinding.v1.KeyBindingHelper;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.option.KeyBinding;
import net.minecraft.client.util.InputUtil;
import org.lwjgl.glfw.GLFW;

public class KeyBindingManager {
    private final ModConfig config;
    private final PlatformIMEManager ime;
    private KeyBinding toggleChinese;
    private KeyBinding toggleScript;
    private boolean ctrlGPressed = false;
    private boolean ctrlShiftFPressed = false;
    private boolean prevToggleImePressed = false;
    private boolean prevToggleModePressed = false;

    public KeyBindingManager(ModConfig config, PlatformIMEManager ime) {
        this.config = config;
        this.ime = ime;
    }

    public void register() {
        String cat = "key.categories.chineseime";
        this.toggleChinese = KeyBindingHelper.registerKeyBinding(new KeyBinding(
                "key.chineseime.toggle_chinese",
                InputUtil.Type.KEYSYM,
                GLFW.GLFW_KEY_COMMA,
                cat
        ));
        this.toggleScript = KeyBindingHelper.registerKeyBinding(new KeyBinding(
                "key.chineseime.toggle_script",
                InputUtil.Type.KEYSYM,
                -1,
                cat
        ));
    }

    public void tick() {
        MinecraftClient mc = MinecraftClient.getInstance();
        long win = mc.getWindow().getHandle();

        if (this.toggleChinese.wasPressed()) {
            this.ime.toggleChineseMode();
        }

        boolean ctrl = isCtrlPressed(win);
        boolean shift = isShiftPressed(win);

        if (ctrl && shift && GLFW.glfwGetKey(win, GLFW.GLFW_KEY_F) == GLFW.GLFW_PRESS) {
            if (!this.ctrlShiftFPressed) {
                this.ime.toggleScriptType();
                this.ctrlShiftFPressed = true;
            }
        } else {
            this.ctrlShiftFPressed = false;
        }

        if (ctrl && GLFW.glfwGetKey(win, GLFW.GLFW_KEY_G) == GLFW.GLFW_PRESS) {
            if (!this.ctrlGPressed) {
                mc.setScreen(new ConfigScreen(mc.currentScreen, this.config));
                this.ctrlGPressed = true;
            }
        } else {
            this.ctrlGPressed = false;
        }

        int toggleImeKey = this.config.getToggleImeKey();
        if (toggleImeKey != -1) {
            boolean currentToggleImePressed = GLFW.glfwGetKey(win, toggleImeKey) == GLFW.GLFW_PRESS;
            if (currentToggleImePressed && !this.prevToggleImePressed) {
                this.ime.toggleInputMethod();
            }
            this.prevToggleImePressed = currentToggleImePressed;
        }

        if (!PlatformIMEManager.getPlatform().equals(PlatformIMEManager.OS.WINDOWS)) {
            int toggleModeKey = this.config.getToggleChineseModeKey();
            if (toggleModeKey != -1) {
                boolean currentToggleModePressed = GLFW.glfwGetKey(win, toggleModeKey) == GLFW.GLFW_PRESS;
                if (currentToggleModePressed && !this.prevToggleModePressed) {
                    this.ime.toggleChineseMode();
                }
                this.prevToggleModePressed = currentToggleModePressed;
            }
        }
    }

    private boolean isCtrlPressed(long win) {
        return GLFW.glfwGetKey(win, GLFW.GLFW_KEY_LEFT_CONTROL) == GLFW.GLFW_PRESS ||
                GLFW.glfwGetKey(win, GLFW.GLFW_KEY_RIGHT_CONTROL) == GLFW.GLFW_PRESS;
    }

    private boolean isShiftPressed(long win) {
        return GLFW.glfwGetKey(win, GLFW.GLFW_KEY_LEFT_SHIFT) == GLFW.GLFW_PRESS ||
                GLFW.glfwGetKey(win, GLFW.GLFW_KEY_RIGHT_SHIFT) == GLFW.GLFW_PRESS;
    }
}