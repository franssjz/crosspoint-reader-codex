#pragma once
#include <cstdint>

// Allocation-free arbitration for CPR's fixed screenshot chord and optional
// short-Power input lock. Consume both release orders after a screenshot.
class PowerShortcutState {
 public:
  enum class Event { None, Screenshot, LockChanged };
  struct Result {
    Event event = Event::None;
    bool consume = false;
  };
  Result update(uint32_t now, bool powerDown, bool sideDown, bool shortPowerRelease, bool enableQuickLock,
                bool anyButtonPressed = false) {
    if (draining_) {
      if (!anyButtonPressed) draining_ = false;
      return {Event::None, true};
    }
    if (screenshotChord_) {
      if (!powerDown && !sideDown) screenshotChord_ = false;
      return {Event::None, true};
    }
    if (powerDown && sideDown) {
      screenshotChord_ = true;
      return {locked_ ? Event::None : Event::Screenshot, true};
    }
    if (shortPowerRelease && (locked_ || enableQuickLock)) {
      locked_ = !locked_;
      if (locked_)
        lockedAt_ = now;
      else
        draining_ = true;
      return {Event::LockChanged, true};
    }
    return {Event::None, locked_};
  }
  bool isLocked() const { return locked_; }
  bool shouldSleep(uint32_t now, uint32_t timeout) const {
    return locked_ && timeout != 0 && static_cast<uint32_t>(now - lockedAt_) >= timeout;
  }

 private:
  bool draining_ = false;
  bool screenshotChord_ = false;
  bool locked_ = false;
  uint32_t lockedAt_ = 0;
};
