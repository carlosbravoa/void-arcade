// text.h — convenience helpers for rendering Font text onto a Surface.
#pragma once

#include <string>

#include "color.h"
#include "font.h"
#include "surface.h"

inline void draw_text(Surface& s, Font& f, const std::string& txt, int x, int y,
                      Color c) {
    auto t = f.render(txt, c);
    s.blit(*t, x, y);
}

// Horizontally centered around cx.
inline void draw_text_centered(Surface& s, Font& f, const std::string& txt,
                               int cx, int y, Color c) {
    auto t = f.render(txt, c);
    s.blit(*t, cx - t->width() / 2, y);
}

// Right-aligned so the text ends at x_right.
inline void draw_text_right(Surface& s, Font& f, const std::string& txt,
                            int x_right, int y, Color c) {
    auto t = f.render(txt, c);
    s.blit(*t, x_right - t->width(), y);
}
