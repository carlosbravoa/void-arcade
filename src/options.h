// options.h — in-game Options screen. Edits a Settings struct live (the whole
// screen is rendered through the CRT, so glow/scanline/afterglow changes preview
// in real time). The app applies Settings to the renderer/sound/window and saves
// on exit.
#pragma once

#include "engine/game.h"  // Fonts

struct Settings;
struct Input;
class Surface;

class OptionsMenu {
public:
    OptionsMenu(int w, int h, Fonts fonts, Settings& s)
        : W_(w), H_(h), f_(fonts), settings_(s) {}

    void update(float dt, const Input& in);
    void draw(Surface& s);

    bool done = false;  // user backed out → caller saves Settings and returns to menu

private:
    int W_, H_;
    Fonts f_;
    Settings& settings_;
    int sel_ = 0;
    float t_ = 0.0f;
};
