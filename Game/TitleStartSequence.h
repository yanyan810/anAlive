#pragma once
#include <cmath>

// Only the transition timer is title-specific; Enemy owns hits and destruction.
class TitleStartSequence {
public:
    float delay = .75f;
    bool Starting() const { return starting_; }
    bool Finished() const { return starting_ && elapsed_ >= delay; }
    float Elapsed() const { return elapsed_; }
    bool Begin() {
        if (starting_) return false;
        starting_ = true; elapsed_ = 0; return true;
    }
    void Update(float dt) {
        if (starting_ && std::isfinite(dt) && dt > 0) elapsed_ += dt;
    }
private:
    bool starting_ = false;
    float elapsed_ = 0;
};
