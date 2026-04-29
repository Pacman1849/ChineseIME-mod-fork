#include "common.h"
#include <algorithm>

namespace chineseime {

// g_cache is defined in ime_bridge.cpp

void CandidateCache::update(const IMEState& newState) {
std::lock_guard<std::mutex> lock(mutex_);

// Check for changes
if (state_.inputMethodType != newState.inputMethodType) {
changes_.inputMethodChanged = true;
}
if (state_.chineseMode != newState.chineseMode) {
changes_.chineseModeChanged = true;
}
if (state_.capsLockOn != newState.capsLockOn) {
changes_.capsLockChanged = true;
}
if (state_.shiftPressed != newState.shiftPressed) {
changes_.shiftChanged = true;
}
if (state_.inShiftMode != newState.inShiftMode) {
changes_.shiftModeChanged = true;
}
if (state_.composition != newState.composition) {
changes_.compositionChanged = true;
}
if (state_.candidates != newState.candidates || state_.selectedIndex != newState.selectedIndex) {
changes_.candidatesChanged = true;
}

// Always update layout change count when HKL changes
if (state_.hkl != newState.hkl) {
changes_.inputMethodChanged = true;
state_.layoutChangeCount++;
}

state_ = newState;
state_.isValid = true;
}

IMEState CandidateCache::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

CandidateCache::ChangeFlags CandidateCache::checkChanges() {
    std::lock_guard<std::mutex> lock(mutex_);
    ChangeFlags result = changes_;
    changes_ = ChangeFlags(); // Clear after returning
    return result;
}

void CandidateCache::clearChanges() {
    std::lock_guard<std::mutex> lock(mutex_);
    changes_ = ChangeFlags();
}

bool CandidateCache::checkLayoutChanged() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.layoutChangeCount > 0) {
        state_.layoutChangeCount = 0;
        return true;
    }
    return false;
}

void CandidateCache::clearLayoutChanged() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.layoutChangeCount = 0;
}

} // namespace chineseime