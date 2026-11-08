#pragma once

// Only the hook thread owns this state. Never query async key state in the hook:
// Windows has not updated it for the event currently being delivered.
class TabFilter {
public:
    void Reset(bool alreadyHeld = false) {
        held_ = alreadyHeld;
        suppressedPress_ = false;
    }

    bool Suppress(bool keyDown, bool altDown) {
        if (keyDown) {
            if (!held_) {
                held_ = true;
                suppressedPress_ = altDown;
            }
            // Alt can be pressed after a normal Tab down; suppress its repeats,
            // but still deliver that original press's key-up to avoid a stuck Tab.
            return suppressedPress_ || altDown;
        }
        const bool suppress = held_ && suppressedPress_;
        Reset();
        return suppress;
    }

private:
    bool held_ = false;
    bool suppressedPress_ = false;
};
