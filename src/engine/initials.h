// initials.h — classic 3-letter arcade high-score entry overlay.
// Keyboard: type A-Z, arrows adjust/navigate, SPACE/ENTER confirm.
// Gamepad: d-pad up/down change letter, left/right move, A confirms.
#pragma once

#include <string>

#include "game.h"

class Surface;
struct Input;

class InitialsEntry {
public:
    InitialsEntry(int w, int h, Fonts fonts);

    void begin(const std::string& title, int score, int rank);
    bool active() const { return active_; }
    bool update(const Input& in);   // returns true the frame it's confirmed
    void draw(Surface& s);

    std::string name() const { return std::string(chars_, 3); }

private:
    int W_, H_;
    Fonts f_;
    bool active_ = false;
    char chars_[3] = {'A', 'A', 'A'};
    int  cursor_ = 0;
    int  rank_ = 0, score_ = 0;
    float t_ = 0.0f;
    std::string title_;
};
