package com.example.chineseime.platform.win32;

import com.example.chineseime.ChineseIMEInitializer;
import com.example.chineseime.engine.InputMode;
import com.example.chineseime.hud.CandidateHud;
import com.sun.jna.Native;
import com.sun.jna.WString;
import com.sun.jna.Pointer;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ConcurrentLinkedQueue;

public class WindowsIMEBridgeNative {
    private final IMEState state = new IMEState();
    private boolean initialized = false;
    private CandidateHud candidateHud;

    private static class CandidateEvent {
        List<String> candidates;
        String composition;
        int selectedIndex;
        CandidateEvent(List<String> c, String comp, int sel) {
            this.candidates = c; this.composition = comp; this.selectedIndex = sel;
        }
    }

    private static class KeyboardEvent {
        int capsLockOn; int inShiftMode;
        KeyboardEvent(int c, int s) { this.capsLockOn = c; this.inShiftMode = s; }
    }

    private static class StateEvent {
        int inputMethodType; boolean chineseMode;
        StateEvent(int t, boolean m) { this.inputMethodType = t; this.chineseMode = m; }
    }

    private final ConcurrentLinkedQueue<CandidateEvent> candidateQueue = new ConcurrentLinkedQueue<>();
    private final ConcurrentLinkedQueue<KeyboardEvent> keyboardQueue = new ConcurrentLinkedQueue<>();
    private final ConcurrentLinkedQueue<StateEvent> stateQueue = new ConcurrentLinkedQueue<>();

    public WindowsIMEBridgeNative(CandidateHud candidateHud) {
        this.candidateHud = candidateHud;
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] Initializing WindowsIMEBridgeNative");
    }

    public boolean initialize() {
        NativeImeBridge.getInstance();

        if (!NativeImeBridge.isAvailable()) {
            ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Native library not available");
            return false;
        }

        ChineseIMEInitializer.LOGGER.info("[ChineseIME] DLL available, registering callbacks and starting TSF listen...");

        NativeImeBridge.registerCallbacks(
            (WString composition, Pointer candidatesPtr, int count, int selectedIndex) -> {
                try {
                    String comp = composition != null ? composition.toString() : "";
                    List<String> cands = new ArrayList<>();
                    if (candidatesPtr != null && count > 0) {
                        Pointer[] ptrArray = new Pointer[count];
                        candidatesPtr.read(0, ptrArray, 0, count);
                        for (int i = 0; i < count; i++) {
                            String cand = ptrArray[i].getWideString(0);
                            if (cand != null && !cand.isEmpty()) {
                                cands.add(cand);
                            }
                        }
                    }
                    candidateQueue.add(new CandidateEvent(cands, comp, selectedIndex));
                } catch (Exception e) {
                    ChineseIMEInitializer.LOGGER.warn("[ChineseIME] Error in candidate callback: {}", e.getMessage());
                }
            },

            (int inputMethodType) -> {
                stateQueue.add(new StateEvent(inputMethodType, state.chineseMode));
            },

            (int chineseMode) -> {
                stateQueue.add(new StateEvent(NativeImeBridge.getInputMethodType(), chineseMode != 0));
            },

            (int capsLockOn, int inShiftMode) -> {
                keyboardQueue.add(new KeyboardEvent(capsLockOn, inShiftMode));
            }
        );

        NativeImeBridge.startListening(0L);
        int tsfResult = NativeImeBridge.startTsfListening();
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] TSF listen result: {}", tsfResult);

        initialized = true;

        updateState();
        ChineseIMEInitializer.LOGGER.info("[ChineseIME] WindowsIMEBridgeNative initialized successfully (callback mode)");
        return true;
    }

    public void update() {
        if (!initialized) return;

        NativeImeBridge.refreshImeState();

        CandidateEvent candEvent;
        while ((candEvent = candidateQueue.poll()) != null) {
            state.composition = candEvent.composition;
            state.candidates = candEvent.candidates;
            state.selectedIndex = candEvent.selectedIndex;

            if (candidateHud != null) {
                if (!candEvent.candidates.isEmpty()) {
                    candidateHud.updateCandidatesKeepSelection(
                        candEvent.candidates, candEvent.composition,
                        candEvent.selectedIndex, candidateHud.getPage());
                } else if (!candEvent.composition.isEmpty()) {
                    candidateHud.updateCandidatesKeepSelection(
                        new ArrayList<>(), candEvent.composition, 0, 0);
                }
            }
        }

        KeyboardEvent kbdEvent;
        while ((kbdEvent = keyboardQueue.poll()) != null) {
            state.capsLockOn = kbdEvent.capsLockOn != 0;
            state.inShiftMode = kbdEvent.inShiftMode != 0;
        }

        StateEvent stEvent;
        while ((stEvent = stateQueue.poll()) != null) {
            InputMode newMode = NativeImeBridge.getInputMethodTypeAsEnum(stEvent.inputMethodType);
            if (state.detectedInputMode != newMode) {
                state.detectedInputMode = newMode;
                state.layoutChanged = true;
            }
            if (state.chineseMode != stEvent.chineseMode) {
                state.chineseMode = stEvent.chineseMode;
                state.layoutChanged = true;
            }
        }

        state.imeOpen = NativeImeBridge.getImeOpenStatus();
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