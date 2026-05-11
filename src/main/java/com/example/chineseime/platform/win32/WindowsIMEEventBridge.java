package com.example.chineseime.platform.win32;

import com.example.chineseime.ChineseIMEInitializer;
import com.example.chineseime.engine.InputMode;
import com.example.chineseime.hud.CandidateHud;
import com.example.chineseime.hud.ImeStatusIndicator;
import com.example.chineseime.hud.VerticalCandidateHud;

public class WindowsIMEEventBridge {
    private final CandidateHud horizontalHud;
    private final VerticalCandidateHud verticalHud;
    private final ImeStatusIndicator statusIndicator;

    private InputMode currentMode = InputMode.PINYIN;
    private boolean currentChineseMode = false;
    private boolean currentCapsLock = false;
    private boolean currentShiftMode = false;
    private boolean initialized = false;

    public WindowsIMEEventBridge(CandidateHud horizontalHud, VerticalCandidateHud verticalHud, ImeStatusIndicator statusIndicator) {
        this.horizontalHud = horizontalHud;
        this.verticalHud = verticalHud;
        this.statusIndicator = statusIndicator;
    }

    public void initialize(long hwnd) {
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] WindowsIMEEventBridge.initialize() called with hwnd={}", String.format("0x%X", hwnd));

        if (hwnd == 0) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] HWND is 0, cannot initialize");
            return;
        }

        if (!NativeImeBridge.isAvailable()) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Native library not available");
            return;
        }

        ChineseIMEInitializer.LOGGER.info("[ChineseIME] Native library available, proceeding with initialization");

        try {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Calling hookWindowProc with hwnd={}", String.format("0x%X", hwnd));
            NativeImeBridge.hookWindowProc(hwnd);
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] hookWindowProc returned");

            // Start listening for IME state changes
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Starting IME listening...");
            NativeImeBridge.startListening(hwnd);
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] IME listening started");

            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Starting TSF listening...");
            int tsfResult = NativeImeBridge.startTsfListening();
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] TSF listening started: {}", tsfResult);
        } catch (Throwable e) {
            ChineseIMEInitializer.LOGGER.error("[ChineseIME] hookWindowProc threw exception", e);
            return;
        }

        initialized = NativeImeBridge.isWindowHooked();
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] WindowsIMEEventBridge initialize complete, hooked={}", initialized);
    }

    public void shutdown() {
        if (NativeImeBridge.isAvailable()) {
            NativeImeBridge.unhookWindowProc();
        }
        initialized = false;
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] WindowsIMEEventBridge shutdown");
    }

    public void refreshCandidates() {
        if (NativeImeBridge.isAvailable()) {
            NativeImeBridge.refreshCandidates();
        }
    }

    public boolean isInitialized() {
        return initialized;
    }

    public boolean isHooked() {
        return NativeImeBridge.isWindowHooked();
    }

    public InputMode getDetectedInputMode() {
        int rawType = NativeImeBridge.getInputMethodType();
        InputMode mode = NativeImeBridge.getInputMethodTypeAsEnum(rawType);
        // Debug log every 60 frames
        if (debugFrame % 60 == 0) {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] getDetectedInputMode: raw={}, enum={}", rawType, mode);
        }
        return mode;
    }

    public boolean isChineseMode() {
        return NativeImeBridge.isChineseMode();
    }

    public boolean isCapsLockOn() {
        return NativeImeBridge.getCapsLockState();
    }

    public boolean isInShiftMode() {
        return NativeImeBridge.getShiftMode();
    }

    public boolean isImeOpen() {
        if (NativeImeBridge.isAvailable()) {
            return NativeImeBridge.getImeOpenStatus();
        }
        return false;
    }

    public boolean hasInput() {
        return horizontalHud.isVisible() || verticalHud.isVisible();
    }

    public boolean isVerticalLayout() {
        InputMode mode = getDetectedInputMode();
        return (mode == InputMode.CANGJIE ||
                mode == InputMode.ZHUYIN ||
                mode == InputMode.SUCHENG);
    }

    private InputMode lastMode = null;
    private boolean lastVerticalLayout = false;
    private int debugFrame = 0;

    public void updateFromNativeState() {
        String composition = NativeImeBridge.getCompositionString();
        java.util.List<String> candidates = NativeImeBridge.getCandidates();
        int selectedIndex = NativeImeBridge.getSelectedCandidateIndex();
        InputMode currentMode = getDetectedInputMode();  // Get IME type

        boolean verticalLayout = isVerticalLayout();

        // Debug logging every 60 frames
        if (++debugFrame % 60 == 0) {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] DEBUG: currentMode={}, verticalLayout={}, candidates={}, composition='{}'",
                currentMode, verticalLayout, candidates.size(), composition);
        }

        if (verticalLayout) {
            // Pass IME type to vertical HUD (Grok fix)
            verticalHud.updateKeepSelection(candidates, composition, selectedIndex, 0, currentMode);
            horizontalHud.clearInput();
            if (verticalHud.isVisible() != !composition.isEmpty() || !candidates.isEmpty()) {
                verticalHud.setVisible(!composition.isEmpty() || !candidates.isEmpty());
            }
            if (horizontalHud.isVisible()) {
                horizontalHud.setVisible(false);
            }
        } else {
            // Pass IME type to horizontal HUD (Grok fix)
            horizontalHud.updateKeepSelection(candidates, composition, selectedIndex, 0, currentMode);
            verticalHud.clearInput();
            if (horizontalHud.isVisible() != !composition.isEmpty() || !candidates.isEmpty()) {
                horizontalHud.setVisible(!composition.isEmpty() || !candidates.isEmpty());
            }
            if (verticalHud.isVisible()) {
                verticalHud.setVisible(false);
            }
        }

        lastVerticalLayout = verticalLayout;

        if (currentMode != lastMode) {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] IME type changed: {} -> {}", lastMode, currentMode);
            lastMode = currentMode;
        }
    }

    public void clearInput() {
        horizontalHud.clearInput();
        verticalHud.clearInput();
    }
}