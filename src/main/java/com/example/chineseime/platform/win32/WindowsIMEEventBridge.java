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
        return NativeImeBridge.getInputMethodTypeAsEnum();
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

    public void updateFromNativeState() {
        String composition = NativeImeBridge.getCompositionString();
        java.util.List<String> candidates = NativeImeBridge.getCandidates();
        int selectedIndex = NativeImeBridge.getSelectedCandidateIndex();

        if (!composition.isEmpty() || !candidates.isEmpty()) {
            if (isVerticalLayout()) {
                verticalHud.updateCandidatesKeepSelection(candidates, composition, selectedIndex, 0);
                horizontalHud.setVisible(false);
                verticalHud.setVisible(true);
            } else {
                horizontalHud.updateCandidatesKeepSelection(candidates, composition, selectedIndex, 0);
                horizontalHud.setVisible(true);
                verticalHud.setVisible(false);
            }
        } else {
            horizontalHud.setVisible(false);
            verticalHud.setVisible(false);
        }
    }

    public void clearInput() {
        horizontalHud.clearInput();
        verticalHud.clearInput();
    }
}