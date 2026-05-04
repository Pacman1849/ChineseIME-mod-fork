package com.example.chineseime.platform.win32;

import com.example.chineseime.ChineseIMEInitializer;
import com.example.chineseime.engine.InputMode;
import com.example.chineseime.hud.CandidateHud;
import com.example.chineseime.hud.ImeStatusIndicator;
import java.util.ArrayList;
import java.util.List;

public class WindowsIMEBridgeNative {
    private boolean initialized = false;
    private CandidateHud candidateHud;
    private ImeStatusIndicator statusIndicator;

    private int prevInputMethodType = -1;
    private boolean prevChineseMode = false;
    private boolean prevCapsLock = false;
    private boolean prevShiftMode = false;
    private List<String> prevCandidates = new ArrayList<>();
    private String prevComposition = "";
    private int tickCounter = 0;

    public WindowsIMEBridgeNative(CandidateHud candidateHud) {
        this.candidateHud = candidateHud;
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] Initializing WindowsIMEBridgeNative (polling mode)");
    }

    public void setStatusIndicator(ImeStatusIndicator indicator) {
        this.statusIndicator = indicator;
    }

    public boolean initialize() {
        NativeImeBridge.getInstance();

        if (!NativeImeBridge.isAvailable()) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Native library not available");
            return false;
        }

        NativeImeBridge.startListening(0L);
        int tsfResult = NativeImeBridge.startTsfListening();
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] TSF listen result: {}", tsfResult);

        initialized = true;
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] WindowsIMEBridgeNative initialized (polling mode)");
        return true;
    }

    public void update() {
        if (!initialized) return;

        NativeImeBridge.refreshImeState();

        int inputMethodType = NativeImeBridge.getInputMethodType();
        InputMode currentMode = NativeImeBridge.getInputMethodTypeAsEnum(inputMethodType);
        boolean chineseMode = NativeImeBridge.isChineseMode();
        boolean capsLockOn = NativeImeBridge.getCapsLockState();
        boolean imeOpen = NativeImeBridge.getImeOpenStatus();
        boolean inShiftMode = NativeImeBridge.getShiftMode();

        String composition = NativeImeBridge.getCompositionString();
        List<String> candidates = NativeImeBridge.getCandidates();
        int selectedIndex = NativeImeBridge.getSelectedCandidateIndex();

        boolean stateChanged = prevInputMethodType != inputMethodType
            || prevChineseMode != chineseMode
            || prevCapsLock != capsLockOn
            || prevShiftMode != inShiftMode;

        prevInputMethodType = inputMethodType;
        prevChineseMode = chineseMode;
        prevCapsLock = capsLockOn;
        prevShiftMode = inShiftMode;

        boolean candidatesChanged = !prevCandidates.equals(candidates) || !prevComposition.equals(composition);
        if (candidatesChanged) {
            prevCandidates = new ArrayList<>(candidates);
            prevComposition = composition;

            if (candidateHud != null) {
                if (!candidates.isEmpty()) {
                    candidateHud.updateCandidatesKeepSelection(
                        candidates, composition, selectedIndex, candidateHud.getPage());
                } else if (!composition.isEmpty()) {
                    candidateHud.updateCandidatesKeepSelection(
                        new ArrayList<>(), composition, 0, 0);
                } else {
                    candidateHud.clearInput();
                }
            }
        }

        tickCounter++;
        if (tickCounter % 20 == 0) {
            ChineseIMEInitializer.LOGGER.info("[ChineseIME] Poll: IME={}({}), CMode={}, Caps={}, ShiftM={}, ImeOpen={}, CandCnt={}, Comp='{}', stateChg={}, candChg={}, hudVis={}",
                currentMode, inputMethodType, chineseMode, capsLockOn, inShiftMode, imeOpen,
                candidates.size(), composition, stateChanged, candidatesChanged,
                candidateHud != null ? candidateHud.isVisible() : "null");
        }

        if (stateChanged && statusIndicator != null) {
            statusIndicator.update(chineseMode, currentMode, capsLockOn, inShiftMode);
        }

        if (candidateHud != null && candidateHud.isVisible() && statusIndicator != null) {
            statusIndicator.hide();
        }
    }

    public void shutdown() {
        if (initialized) {
            NativeImeBridge.stopTsfListening();
            NativeImeBridge.stopListening();
            initialized = false;
        }
    }

    public boolean isImeOpen() { return NativeImeBridge.getImeOpenStatus(); }
    public boolean isChineseMode() { return NativeImeBridge.isChineseMode(); }
    public boolean isCapsLockOn() { return NativeImeBridge.getCapsLockState(); }
    public boolean isInShiftMode() { return NativeImeBridge.getShiftMode(); }
    public InputMode getDetectedInputMode() { return NativeImeBridge.getInputMethodTypeAsEnum(); }
    public boolean hasLayoutChanged() { return false; }
}