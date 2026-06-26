// combo.h — a chain/multiplier shared across games. Each kill/brick within the
// window bumps the chain; the score multiplier rises with it and decays on idle.
#pragma once

#include <algorithm>

struct Combo {
    static constexpr float WINDOW = 1.8f;  // seconds to keep the chain alive
    static constexpr int   MAX_MULT = 8;
    int   count = 0;
    int   best = 0;
    float timer = 0.0f;

    void reset() { count = 0; timer = 0; }
    void update(float dt) {
        if (timer > 0) { timer -= dt; if (timer <= 0) count = 0; }
    }
    int mult() const { return std::min(MAX_MULT, 1 + count / 4); }  // +1x per 4 chained
    float ratio() const { return WINDOW > 0 ? std::max(0.0f, timer / WINDOW) : 0.0f; }
    bool active() const { return count >= 4; }  // start showing at x2

    // Register a hit and return the multiplier to apply to its points.
    int hit() {
        ++count;
        best = std::max(best, count);
        timer = WINDOW;
        return mult();
    }
};
