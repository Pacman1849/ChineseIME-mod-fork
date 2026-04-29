package com.example.chineseime.platform.win32;

import com.example.chineseime.ChineseIMEInitializer;
import com.example.chineseime.engine.InputMode;
import java.util.ArrayList;
import java.util.List;

public class WindowsIMEBridgeNative {
    private final IMEState state = new IMEState();
    private boolean initialized = false;

    public WindowsIMEBridgeNative() {
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] Initializing WindowsIMEBridgeNative");
    }

public boolean initialize() {
    NativeImeBridge.getInstance();

    if (!NativeImeBridge.isAvailable()) {
        ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Native library not available");
        return false;
    }

    ChineseIMEInitializer.LOGGER.info("[ChineseIME] DLL available, starting TSF listen...");

    NativeImeBridge.startListening(0L);
    int tsfResult = NativeImeBridge.startTsfListening();
    ChineseIMEInitializer.LOGGER.info("[ChineseIME] TSF listen result: {}", tsfResult);

    initialized = true;

    updateState();
    ChineseIMEInitializer.LOGGER.info("[ChineseIME] WindowsIMEBridgeNative initialized successfully");
    return true;
}

public void update() {
    if (!initialized) return;

    NativeImeBridge.refreshImeState();

    state.imeOpen = NativeImeBridge.getImeOpenStatus();
    state.chineseMode = NativeImeBridge.getTsfChineseMode();
    state.capsLockOn = NativeImeBridge.getCapsLockState();
    state.inShiftMode = NativeImeBridge.getShiftMode();

    ChineseIMEInitializer.LOGGER.debug("[ChineseIME] DLL: imeOpen={}, chineseMode={}, capsLock={}, shiftMode={}",
        state.imeOpen, state.chineseMode, state.capsLockOn, state.inShiftMode);

        String comp = NativeImeBridge.getCompositionString();
        List<String> cands = NativeImeBridge.getCandidates();
        int selIndex = NativeImeBridge.getSelectedCandidateIndex();

        if (!comp.isEmpty()) {
            state.composition = comp;
            state.candidates = cands;
            state.selectedIndex = selIndex;
        } else if (comp.isEmpty() && !state.composition.isEmpty()) {
            state.composition = "";
            state.candidates.clear();
            state.selectedIndex = 0;
        }

        int inputMethodType = NativeImeBridge.getInputMethodType();
        InputMode newMode = NativeImeBridge.getInputMethodTypeAsEnum(inputMethodType);
        String newDesc = NativeImeBridge.getInputMethodTypeString(inputMethodType);

        if (state.detectedInputMode != newMode || !newDesc.equals(state.imeDescription)) {
            state.detectedInputMode = newMode;
            state.imeDescription = newDesc;
            state.layoutChanged = true;
        }

        if (NativeImeBridge.hasTsfLayoutChanged()) {
            state.layoutChanged = true;
        }

        state.wasInChineseMode = state.chineseMode;
    }

    private void updateState() {
        state.detectedInputMode = NativeImeBridge.getInputMethodTypeAsEnum();
        state.imeDescription = NativeImeBridge.getInputMethodTypeString();
        state.chineseMode = NativeImeBridge.isChineseMode();
        state.imeOpen = NativeImeBridge.getImeOpenStatus();
    }

    public void shutdown() {
        if (initialized) {
            NativeImeBridge.stopTsfListening();
            NativeImeBridge.stopListening();
            initialized = false;
        }
    }

    public boolean isImeOpen() {
        return state.imeOpen;
    }

    public boolean isChineseMode() {
        return state.chineseMode;
    }

    public String getComposition() {
        return state.composition;
    }

    public List<String> getCandidates() {
        return state.candidates;
    }

    public int getSelectedIndex() {
        return state.selectedIndex;
    }

    public boolean hasCandidates() {
        return !state.candidates.isEmpty();
    }

    public boolean hasComposition() {
        return state.composition != null && !state.composition.isEmpty();
    }

    public boolean isCapsLockOn() {
        return state.capsLockOn;
    }

    public boolean isInShiftMode() {
        return state.inShiftMode;
    }

    public InputMode getDetectedInputMode() {
        return state.detectedInputMode;
    }

    public String getImeDescription() {
        return state.imeDescription;
    }

    public boolean hasLayoutChanged() {
        boolean changed = state.layoutChanged;
        state.layoutChanged = false;
        return changed;
    }

    public void resetShiftMode() {
        state.wasInChineseMode = state.chineseMode;
        state.inShiftMode = false;
    }

    private static class IMEState {
        boolean imeOpen = false;
        boolean chineseMode = false;
        String composition = "";
        List<String> candidates = new ArrayList<>();
        int selectedIndex = 0;
        boolean layoutChanged = false;
        InputMode detectedInputMode = InputMode.PINYIN;
        String imeDescription = "";
        boolean capsLockOn = false;
        boolean inShiftMode = false;
        boolean wasInChineseMode = false;
    }
}