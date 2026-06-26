// pause.h — reusable pause menu overlay, ported from shared/pause.py.
#pragma once

#include "game.h"

class Surface;
struct Input;

enum class PauseResult { None, Resume, QuitToMenu };

class PauseMenu {
public:
    PauseMenu(int w, int h, Fonts fonts);

    bool active() const { return active_; }
    void toggle();
    PauseResult update(const Input& in);  // call only when active
    void draw(Surface& s);

private:
    int W_, H_;
    Fonts f_;
    bool  active_ = false;
    bool  swallow_open_ = false;  // ignore the very keypress that opened the menu
    int   selected_ = 0;
    float t_ = 0.0f;
    float alpha_ = 0.0f;
};
