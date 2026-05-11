# IME Type Detection - Root Cause Analysis & Fix

**Date**: 2026-05-10
**Status**: Debug version deployed - awaiting DebugView logs

---

## Problem Summary

- English IME → shows "En" ✅
- Pinyin IME → shows "倉" ❌
- Sucheng IME → shows "倉" ❌ (but typing works)
- Cangjie/Sucheng → Vertical HUD ✅
- Pinyin → uses Cangjie/Sucheng HUD ❌
- Once wrong type detected, switching IME never updates ❌

---

## Root Cause Analysis

### Bug 1: TSF GUID Detection Fails → Falls Back to Wrong Default

**File**: `tsf_monitor.cpp::updateInputMethodType()`

When the TSF profile CLSID/GUID doesn't match any known Microsoft IME GUID, the code falls back to `OTHER_CHINESE`, which gets mapped to `CANGJIE` in Java (`NativeImeBridge::getInputMethodTypeAsEnum()`).

```cpp
// OLD CODE (wrong):
if (!detected) {
    currentInputMethod_ = InputMethodType::OTHER_CHINESE;  // ← BUG: Java maps this to CANGJIE
}
```

**Fix**: Use language-based default instead of `OTHER_CHINESE`:
- zh-CN (0x0804) → default to `PINYIN`
- zh-TW/HK (0x0404/0x0C04/0x1404) → default to `CANGJIE`

### Bug 2: `queryCurrentInputMethod()` Enumerates ALL Profiles

**File**: `tsf_monitor.cpp::queryCurrentInputMethod()`

`ITfInputProcessorProfiles::EnumLanguageProfiles(0, &enumProfiles)` enumerates ALL installed Chinese profiles, not just the active one. If Cangjie is listed before Pinyin in the enumeration, it gets detected first and breaks out of the loop.

```cpp
// OLD CODE: accepts ANY non-UNKNOWN/non-ENGLISH type
if (currentInputMethod_ != UNKNOWN && currentInputMethod_ != ENGLISH) {
    typeFromTsf = true;  // ← BUG: breaks even if type is OTHER_CHINESE
    break;
}
```

**Fix**: Only break when a definite type is detected (not `OTHER_CHINESE`).

### Bug 3: `PollIMEState()` Ignores Type Changes After First Detection

**File**: `ime_bridge.cpp::PollIMEState()`

Once `tsfHasSetType` is true (any non-UNKNOWN/non-ENGLISH type), the polling loop stops updating even when the user switches IMEs:

```cpp
// OLD CODE:
if (tsfHasSetType) {  // Once CANGJIE is set, this is true forever
    if (detectedType != UNKNOWN && detectedType != ENGLISH && detectedType != cachedType) {
        // This never executes because TSF "has set" the type
    }
}
```

**Fix**: Always check for IME type changes when IME is open:
```cpp
// NEW CODE:
if (imeOpen) {
    if (detectedType != UNKNOWN && detectedType != ENGLISH && detectedType != cachedType) {
        mgr.updateInputMethod(detectedType);  // ← Now updates correctly
    }
}
```

### Bug 4: `PollIMEState()` vs `queryCurrentInputMethod()` Use Different Detection

**File**: `ime_bridge.cpp::PollIMEState()` vs `tsf_monitor.cpp::queryCurrentInputMethod()`

- `PollIMEState()` uses `DetectInputMethodTypeFromHkl(hkl)` - doesn't extract from layout name
- `queryCurrentInputMethod()` uses `detectInputMethodTypeFromHklSafe()` - does extract from layout name

For TSF IMEs where HKL IME ID = 0, the extraction from `GetKeyboardLayoutNameW()` may give different results.

**Fix**: Added consistent KL name extraction to `PollIMEState()` as well.

### Bug 5: `pollUpdate()` Only Queries When UNKNOWN/ENGLISH

**File**: `tsf_monitor.cpp::TsfMonitor::pollUpdate()`

```cpp
// OLD CODE:
if (currentInputMethod_ == UNKNOWN || currentInputMethod_ == ENGLISH) {
    queryCurrentInputMethod();  // ← Only called once!
}
```

**Fix**: Always query and compare. If HKL gives a different type, update:
```cpp
queryCurrentInputMethod();
if (currentInputMethod_ != UNKNOWN && currentInputMethod_ != ENGLISH) {
    ImeStateManager::get().updateInputMethod(currentInputMethod_);
}
```

---

## Current Architecture

```
┌─────────────────────────────────────────────────────┐
│  Java: WindowsIMEEventBridge                        │
│  - getDetectedInputMode() → NativeImeBridge         │
│  - updateFromNativeState() → Candidate Hud          │
└─────────────────────┬───────────────────────────────┘
                      │ JNA
┌─────────────────────┴───────────────────────────────┐
│  C++ DLL: ImeStateManager (singleton)               │
│  - Stores: inputMethodType, chineseMode, candidates │
│  - updateInputMethod(type) → callback Java         │
└─────────────┬─────────────────┬───────────────────┘
              │                 │
    ┌─────────┴───┐     ┌───────┴──────┐
    │  TsfMonitor │     │ PollIMEState │
    │  (TSF/     │     │  (Polling   │
    │   Event)   │     │   Thread)   │
    └─────────────┘     └──────────────┘
```

---

## Key Files Modified

| File | Change |
|------|--------|
| `native/src/ime_bridge.cpp` | `PollIMEState()` - always check type changes, add KL name extraction |
| `native/src/tsf_monitor.cpp` | `pollUpdate()` - always query, `updateInputMethodType()` - language default, `OnActivated()` - more logging |
| `native/src/ime_state_manager.cpp` | `detectInputMethodTypeFromImeId(0, langId)` → PINYIN (TSF fallback) |

---

## Type Mapping (Java)

```java
// NativeImeBridge.java
case IME_TYPE_ENGLISH → InputMode.LATIN ("En")
case IME_TYPE_PINYIN  → InputMode.PINYIN ("拼")
case IME_TYPE_CANGJIE → InputMode.CANGJIE ("倉")
case IME_TYPE_ZHUYIN   → InputMode.ZHUYIN ("注")
case IME_TYPE_WUBI     → InputMode.WUBI ("五")
case IME_TYPE_SUCHENG  → InputMode.SUCHENG ("速")
case IME_TYPE_OTHER_CHINESE → InputMode.OTHER ("其他")
```

---

## HUD Selection Logic

```java
// PlatformIMEManager.isVerticalLayout()
boolean useVertical = (mode == CANGJIE || mode == ZHUYIN || mode == SUCHENG);

// If Pinyin is detected as CANGJIE → wrong HUD is used
```

---

## Debug Log Tags (DebugView)

| Tag | Location | Meaning |
|-----|----------|---------|
| `PollIME #N: kl=XXX, imeId=0xXX, detected=N, cached=N` | `PollIMEState()` | Every 16ms polling |
| `Tsf pollUpdate #N: type=N` | `TsfMonitor::pollUpdate()` | TSF polling |
| `OnActivated: langid=0xXXXX, clsid=XXXXXXXX` | `TsfMonitor::OnActivated()` | TSF event |
| `updateInputMethodType: langid=0xXXXX, clsid=...` | `TsfMonitor::updateInputMethodType()` | GUID matching |
| `TSF Enum: profile langid=0xXXXX, clsid=...` | `queryCurrentInputMethod()` | Profile enumeration |
| `PollIME: updating IME type X -> Y` | `PollIMEState()` | Type change detected |

---

## Expected DebugView Output When Switching to Pinyin

1. `PollIME #N: kl=00000804, imeId=0x0000, detected=2, cached=4, imeOpen=1`
   → Detected type=2 (PINYIN), cached=4 (CANGJIE). Should trigger update.

2. `PollIME: updating IME type 4 -> 2`
   → State manager updated to PINYIN.

3. `ImeStateManager::updateInputMethod: 4 -> 2`
   → Change notification.

---

## TODO

1. [x] Add comprehensive debug logging
2. [x] Fix `PollIMEState()` to always check type changes
3. [x] Fix `queryCurrentInputMethod()` to not accept OTHER_CHINESE
4. [x] Fix `updateInputMethodType()` to use language-based defaults
5. [x] Fix `pollUpdate()` to always query
6. [ ] Verify with DebugView logs from user
7. [ ] If still broken, investigate if `GetKeyboardLayout()` returns same value across IME switches
8. [ ] Consider using `RegisterHotKey` or `WM_INPUTLANGCHANGE` for explicit IME switch notification