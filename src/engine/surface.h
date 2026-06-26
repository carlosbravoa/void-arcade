// surface.h — a CPU RGBA8 pixel buffer with a pygame.Surface-like API.
//
// Games draw onto a Surface exactly the way the original Python drew onto a
// pygame.Surface; the engine then uploads the finished 900x720 surface as a GL
// texture and runs it through the CRT shader. Keeping this API faithful to
// pygame makes each game port a near-mechanical translation.
#pragma once

#include <cstdint>
#include <vector>

#include "color.h"

enum class Blend {
    Alpha,      // default: source-over alpha compositing
    RgbaMult,   // pygame.BLEND_RGBA_MULT: dst = dst * src / 255 (per channel)
};

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

class Surface {
public:
    Surface(int w, int h, bool srcalpha = false);

    int width()  const { return w_; }
    int height() const { return h_; }

    // Raw RGBA bytes, row-major, top-left origin (matches pygame tobytes RGBA).
    const uint8_t* data() const { return px_.data(); }
    uint8_t*       data()       { return px_.data(); }

    // Per-surface alpha applied during blit (-1 == opaque/255), like set_alpha.
    void set_alpha(int a) { surf_alpha_ = a; }
    int  get_alpha() const { return surf_alpha_; }

    void fill(Color c);                 // whole surface
    void fill(Color c, const Rect& r);  // sub-rect

    void  set_at(int x, int y, Color c);
    Color get_at(int x, int y) const;

    // Blit src onto this surface with top-left at (dx,dy).
    void blit(const Surface& src, int dx, int dy, Blend mode = Blend::Alpha);

    // Nearest-neighbour scaled copy (always SRCALPHA), like pygame.transform.scale.
    Surface scaled(int nw, int nh) const;

private:
    friend void blend_pixel(Surface&, int, int, Color);
    int w_, h_;
    int surf_alpha_ = -1;
    std::vector<uint8_t> px_;  // RGBA8
};

// ── pygame.draw.* equivalents ────────────────────────────────────────────────
namespace draw {

// width==0 fills; width>0 draws an outline of that thickness.
void rect(Surface& s, Color c, Rect r, int width = 0, int border_radius = 0);
void circle(Surface& s, Color c, int cx, int cy, int radius, int width = 0);
void line(Surface& s, Color c, int x1, int y1, int x2, int y2, int width = 1);
void polygon(Surface& s, Color c, const std::vector<std::pair<int, int>>& pts,
             int width = 0);

}  // namespace draw
