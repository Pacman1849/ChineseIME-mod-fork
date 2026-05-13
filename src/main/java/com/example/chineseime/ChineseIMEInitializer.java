package com.example.chineseime;

import com.example.chineseime.config.ModConfig;
import com.example.chineseime.hud.CandidateHud;
import com.example.chineseime.hud.ImeStatusIndicator;
import com.example.chineseime.hud.VerticalCandidateHud;
import com.example.chineseime.keybind.KeyBindingManager;
import com.example.chineseime.platform.PlatformIMEManager;
import com.example.chineseime.platform.win32.NativeImeBridge;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.api.EnvType;
import net.fabricmc.api.Environment;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.client.MinecraftClient;
import net.minecraft.client.gui.DrawContext;
import net.minecraft.client.gui.screen.ChatScreen;
import net.minecraft.client.gui.widget.TextFieldWidget;
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
    private VerticalCandidateHud verticalCandidateHud;
    private ImeStatusIndicator statusIndicator;
    private PlatformIMEManager imeManager;
    private KeyBindingManager keyBindingManager;
    private boolean ctrlShiftTPressed = false;
    private boolean prevLeftPressed = false;
    private boolean prevRightPressed = false;
    private boolean prevUpPressed = false;
    private boolean prevDownPressed = false;
    private boolean prevBracketLeftPressed = false;
    private boolean prevBracketRightPressed = false;
    private boolean prevEnterPressed = false;
    private boolean prevBackspacePressed = false;
    private boolean prevEscPressed = false;
    private boolean prevNum1Pressed = false, prevNum2Pressed = false, prevNum3Pressed = false;
    private boolean prevNum4Pressed = false, prevNum5Pressed = false, prevNum6Pressed = false;
    private boolean prevNum7Pressed = false, prevNum8Pressed = false, prevNum9Pressed = false;

    @Override
    public void onInitializeClient() {
        instance = this;
        LOGGER.info("[ChineseIME] Initializing...");
        this.config = ModConfig.load();
        this.candidateHud = new CandidateHud();
        this.verticalCandidateHud = new VerticalCandidateHud();
        this.statusIndicator = new ImeStatusIndicator();

        ClientTickEvents.START_CLIENT_TICK.register(client -> {
            if (this.imeManager == null && PlatformIMEManager.getPlatform() == PlatformIMEManager.OS.WINDOWS) {
                long windowHandle = client.getWindow().getHandle();
                if (windowHandle != 0) {
                    long nativeHandle = NativeImeBridge.getGlfwWin32Window(windowHandle);
                    LOGGER.info("[ChineseIME] Got native window handle: {}", String.format("0x%X", nativeHandle));

                    this.imeManager = new PlatformIMEManager(this.config, this.candidateHud, this.verticalCandidateHud, this.statusIndicator, nativeHandle);

                    LOGGER.info("[ChineseIME] IME Manager initialized, syncEnabled={}", this.imeManager.isWindowsSync());

                    if (this.imeManager.isWindowsSync()) {
                        registerCallbacks();
                    }
                }
            }
        });

        ClientTickEvents.END_CLIENT_TICK.register(client -> {
            if (this.imeManager != null) {
                this.imeManager.tick();
            }

            long windowHandle = client.getWindow().getHandle();
            boolean leftPressed = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_LEFT) == GLFW.GLFW_PRESS;
            boolean rightPressed = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_RIGHT) == GLFW.GLFW_PRESS;
            boolean upPressed = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_UP) == GLFW.GLFW_PRESS;
            boolean downPressed = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_DOWN) == GLFW.GLFW_PRESS;

            boolean hasCandidates = false;
            if (this.imeManager != null) {
                CandidateHud h = this.imeManager.getHud();
                VerticalCandidateHud v = this.imeManager.getVerticalHud();
                hasCandidates = (h != null && h.isVisible() && !h.getCandidates().isEmpty()) ||
                               (v != null && v.isVisible() && !v.getCandidates().isEmpty());
            }

            boolean consumeArrowKeys = hasCandidates;

            if (consumeArrowKeys) {
                if ((leftPressed && !prevLeftPressed) || (upPressed && !prevUpPressed)) {
                    this.imeManager.selectPrev();
                } else if ((rightPressed && !prevRightPressed) || (downPressed && !prevDownPressed)) {
                    this.imeManager.selectNext();
                }
                boolean bracketLeftPressed = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_LEFT_BRACKET) == GLFW.GLFW_PRESS;
                boolean bracketRightPressed = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_RIGHT_BRACKET) == GLFW.GLFW_PRESS;
                if (bracketLeftPressed && !prevBracketLeftPressed) {
                    this.imeManager.prevPage();
                } else if (bracketRightPressed && !prevBracketRightPressed) {
                    this.imeManager.nextPage();
                }
                prevBracketLeftPressed = bracketLeftPressed;
                prevBracketRightPressed = bracketRightPressed;
            }

            prevLeftPressed = leftPressed;
            prevRightPressed = rightPressed;
            prevUpPressed = upPressed;
            prevDownPressed = downPressed;

            boolean ctrl = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_LEFT_CONTROL) == GLFW.GLFW_PRESS ||
                    GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_RIGHT_CONTROL) == GLFW.GLFW_PRESS;
            boolean shift = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_LEFT_SHIFT) == GLFW.GLFW_PRESS ||
                    GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_RIGHT_SHIFT) == GLFW.GLFW_PRESS;
            if (ctrl && shift && GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_T) == GLFW.GLFW_PRESS) {
                if (!this.ctrlShiftTPressed) {
                    if (this.imeManager != null) {
                        this.imeManager.showTestCandidates();
                    }
                    this.ctrlShiftTPressed = true;
                }
            } else {
                this.ctrlShiftTPressed = false;
            }

            // ESC: clear IME input
            boolean escPressed = GLFW.glfwGetKey(windowHandle, GLFW.GLFW_KEY_ESCAPE) == GLFW.GLFW_PRESS;
            if (escPressed && !this.prevEscPressed) {
                if (this.imeManager != null) {
                    LOGGER.info("[ChineseIME] ESC pressed, clearing input");
                    this.imeManager.clearInput();
                }
            }
            this.prevEscPressed = escPressed;
        });

        LOGGER.info("[ChineseIME] Initialization complete! Platform: {}", PlatformIMEManager.getPlatform());
    }

    private void registerCallbacks() {
        NativeImeBridge.CommitCallback commitCB = text -> {
            if (text == null || text.length() == 0) return;

            String committed = text.toString();
            LOGGER.info("[ChineseIME] Commit: '{}'", committed);

            // 重要：只在没有 composition 时才真正 commit（避免吞字）
            if (this.imeManager != null) {
                CandidateHud hud = this.imeManager.getHud();
                VerticalCandidateHud vHud = this.imeManager.getVerticalHud();

                boolean hasComposition = (hud != null && !hud.getInput().isEmpty()) ||
                        (vHud != null && !vHud.getInput().isEmpty());

                if (!hasComposition) {
                    insertTextToFocusedField(committed);
                    this.imeManager.clearInput();
                } else {
                    LOGGER.warn("[ChineseIME] Commit blocked because composition still exists: {}", committed);
                }
            }
        };

        NativeImeBridge.CandidateCallback candidateCB = (cands, count, selectedIdx) -> {
            // Candidates are handled via polling + updateFromNativeState
        };

        NativeImeBridge.ImeChangeCallback imeChangeCB = (int imeType, int chineseMode) -> {
            LOGGER.info("[ChineseIME] IME changed to type: {}, chineseMode: {}", imeType, chineseMode);
        };

        NativeImeBridge.KeyboardCallback keyboardCB = (int capsLock, int shiftMode) -> {
            // Keyboard state handled via polling
        };

        NativeImeBridge.setEventCallbacks(null, commitCB, candidateCB, imeChangeCB, keyboardCB);
        LOGGER.info("[ChineseIME] IME event callbacks registered");
    }

    public void insertTextToFocusedField(String text) {
        if (text == null || text.isEmpty()) {
            LOGGER.warn("[ChineseIME] insertTextToFocusedField: text is null or empty");
            return;
        }

        MinecraftClient mc = MinecraftClient.getInstance();
        if (mc == null) {
            LOGGER.warn("[ChineseIME] insertTextToFocusedField: mc is null");
            return;
        }
        if (mc.currentScreen == null) {
            LOGGER.warn("[ChineseIME] insertTextToFocusedField: currentScreen is null");
            return;
        }

        LOGGER.info("[ChineseIME] insertTextToFocusedField: screen={}", mc.currentScreen.getClass().getSimpleName());

        try {
            if (mc.currentScreen instanceof ChatScreen chatScreen) {
                java.lang.reflect.Field field = chatScreen.getClass().getDeclaredField("chatField");
                field.setAccessible(true);
                TextFieldWidget chatField = (TextFieldWidget) field.get(chatScreen);

                if (chatField == null) {
                    LOGGER.warn("[ChineseIME] insertTextToFocusedField: chatField is null");
                    return;
                }

                String current = chatField.getText();
                int cursor = chatField.getCursor();

                LOGGER.info("[ChineseIME] insertTextToFocusedField: current='{}', cursor={}, inserting='{}'",
                    current, cursor, text);

                String newText = current.substring(0, cursor) + text + current.substring(cursor);
                chatField.setText(newText);
                chatField.setCursor(cursor + text.length(), false);

                LOGGER.info("[ChineseIME] insertTextToFocusedField: success! newText='{}'", newText);
            } else {
                LOGGER.warn("[ChineseIME] insertTextToFocusedField: not a ChatScreen");
            }
        } catch (Exception e) {
            LOGGER.error("[ChineseIME] insertTextToFocusedField failed", e);
        }
    }

    public void handleEnterKey() {
        if (this.imeManager == null) {
            LOGGER.warn("[ChineseIME] handleEnterKey: imeManager is null");
            return;
        }
        CandidateHud h = this.imeManager.getHud();
        VerticalCandidateHud v = this.imeManager.getVerticalHud();

        LOGGER.info("[ChineseIME] handleEnterKey: h.visible={}, h.candidates={}, v.visible={}, v.candidates={}",
            h.isVisible(), h.getCandidates().size(),
            v.isVisible(), v.getCandidates().size());

        String selected = null;
        if (h.isVisible() && !h.getCandidates().isEmpty()) {
            selected = h.getSelected();
            LOGGER.info("[ChineseIME] handleEnterKey: selected from horizontal: '{}'", selected);
        } else if (v.isVisible() && !v.getCandidates().isEmpty()) {
            selected = v.getSelected();
            LOGGER.info("[ChineseIME] handleEnterKey: selected from vertical: '{}'", selected);
        }

        if (selected != null && !selected.isEmpty()) {
            LOGGER.info("[ChineseIME] handleEnterKey: selecting candidate '{}'", selected);
            insertTextToFocusedField(selected);
            this.imeManager.clearInput();
        } else {
            LOGGER.warn("[ChineseIME] handleEnterKey: no valid candidate selected!");
        }
    }

    public void handleNumberKey(int num) {
        if (this.imeManager == null || num < 1 || num > 9) {
            LOGGER.warn("[ChineseIME] handleNumberKey: invalid params, imeManager={}, num={}",
                this.imeManager != null, num);
            return;
        }
        CandidateHud h = this.imeManager.getHud();
        VerticalCandidateHud v = this.imeManager.getVerticalHud();

        LOGGER.info("[ChineseIME] handleNumberKey({}): h.visible={}, h.candidates={}, v.visible={}, v.candidates={}",
            num, h.isVisible(), h.getCandidates().size(),
            v.isVisible(), v.getCandidates().size());

        java.util.List<String> candidates = null;
        if (h.isVisible() && !h.getCandidates().isEmpty()) {
            candidates = h.getCandidates();
            LOGGER.info("[ChineseIME] handleNumberKey: using horizontal candidates");
        } else if (v.isVisible() && !v.getCandidates().isEmpty()) {
            candidates = v.getCandidates();
            LOGGER.info("[ChineseIME] handleNumberKey: using vertical candidates");
        }

        if (candidates != null && num <= candidates.size()) {
            String selected = candidates.get(num - 1);
            LOGGER.info("[ChineseIME] handleNumberKey({}): selecting '{}'", num, selected);
            insertTextToFocusedField(selected);
            this.imeManager.clearInput();
        } else {
            LOGGER.warn("[ChineseIME] handleNumberKey({}): index out of bounds, candidates={}",
                num, candidates != null ? candidates.size() : 0);
        }
    }

    public void handleBackspace() {
        if (this.imeManager == null) return;
        CandidateHud h = this.imeManager.getHud();
        VerticalCandidateHud v = this.imeManager.getVerticalHud();

        String comp = h.isVisible() ? h.getInput() : v.getInput();
        if (comp != null && !comp.isEmpty()) {
            if (comp.length() > 1) {
                String newComp = comp.substring(0, comp.length() - 1);
                java.util.List<String> suggestions = com.example.chineseime.engine.PinyinDictionary.getSuggestions(newComp);
                h.updateCandidates(suggestions, newComp);
                v.updateCandidates(suggestions, newComp);
            } else {
                this.imeManager.clearInput();
            }
        }
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

    public VerticalCandidateHud getVerticalCandidateHud() {
        return this.verticalCandidateHud;
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

        this.statusIndicator.render(ctx);
        if (this.imeManager != null) {
            this.imeManager.renderHud(ctx);
        }
    }
}