// color.h — RGBA color, mirroring pygame's (r,g,b[,a]) tuples.
#pragma once

#include <algorithm>
#include <cstdint>

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;

    constexpr Color() = default;
    constexpr Color(int r_, int g_, int b_, int a_ = 255)
        : r(uint8_t(r_)), g(uint8_t(g_)), b(uint8_t(b_)), a(uint8_t(a_)) {}
};

inline Color lerp_color(Color c1, Color c2, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color(int(c1.r + (c2.r - c1.r) * t),
                 int(c1.g + (c2.g - c1.g) * t),
                 int(c1.b + (c2.b - c1.b) * t));
}

inline Color brighten(Color c, float factor = 1.4f) {
    auto cl = [&](int v) { return std::min(255, int(v * factor)); };
    return Color(cl(c.r), cl(c.g), cl(c.b), c.a);
}
