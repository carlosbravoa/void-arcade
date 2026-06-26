#include "surface.h"

#include <algorithm>
#include <cmath>

Surface::Surface(int w, int h, bool srcalpha) : w_(w), h_(h) {
    px_.assign(size_t(w) * h * 4, 0);
    if (!srcalpha) {
        // Opaque black, like a plain pygame.Surface.
        for (size_t i = 3; i < px_.size(); i += 4) px_[i] = 255;
    }
}

void Surface::set_at(int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    uint8_t* p = &px_[(size_t(y) * w_ + x) * 4];
    p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a;
}

Color Surface::get_at(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return Color(0, 0, 0, 0);
    const uint8_t* p = &px_[(size_t(y) * w_ + x) * 4];
    return Color(p[0], p[1], p[2], p[3]);
}

void Surface::fill(Color c) { fill(c, Rect{0, 0, w_, h_}); }

void Surface::fill(Color c, const Rect& r) {
    int x0 = std::max(0, r.x), y0 = std::max(0, r.y);
    int x1 = std::min(w_, r.x + r.w), y1 = std::min(h_, r.y + r.h);
    for (int y = y0; y < y1; ++y) {
        uint8_t* row = &px_[(size_t(y) * w_ + x0) * 4];
        for (int x = x0; x < x1; ++x) {
            *row++ = c.r; *row++ = c.g; *row++ = c.b; *row++ = c.a;
        }
    }
}

// Source-over compositing of color c (with its own alpha) onto (x,y).
void blend_pixel(Surface& s, int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= s.w_ || y >= s.h_) return;
    if (c.a == 0) return;
    uint8_t* p = &s.px_[(size_t(y) * s.w_ + x) * 4];
    if (c.a == 255) {
        p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 255;
        return;
    }
    float sa = c.a / 255.0f;
    float ia = 1.0f - sa;
    p[0] = uint8_t(c.r * sa + p[0] * ia + 0.5f);
    p[1] = uint8_t(c.g * sa + p[1] * ia + 0.5f);
    p[2] = uint8_t(c.b * sa + p[2] * ia + 0.5f);
    p[3] = uint8_t(c.a + p[3] * ia + 0.5f);
}

void Surface::blit(const Surface& src, int dx, int dy, Blend mode) {
    int sx0 = std::max(0, -dx), sy0 = std::max(0, -dy);
    int sx1 = std::min(src.w_, w_ - dx), sy1 = std::min(src.h_, h_ - dy);
    int sa_scale = src.surf_alpha_;  // the source's set_alpha() value, if any
    for (int sy = sy0; sy < sy1; ++sy) {
        const uint8_t* sp = &src.px_[(size_t(sy) * src.w_ + sx0) * 4];
        for (int sx = sx0; sx < sx1; ++sx, sp += 4) {
            Color c(sp[0], sp[1], sp[2], sp[3]);
            if (mode == Blend::RgbaMult) {
                Color d = get_at(dx + sx, dy + sy);
                set_at(dx + sx, dy + sy,
                       Color(d.r * c.r / 255, d.g * c.g / 255,
                             d.b * c.b / 255, d.a * c.a / 255));
                continue;
            }
            if (sa_scale >= 0) c.a = uint8_t(c.a * sa_scale / 255);
            blend_pixel(*this, dx + sx, dy + sy, c);
        }
    }
}

Surface Surface::scaled(int nw, int nh) const {
    Surface out(nw, nh, true);
    if (nw <= 0 || nh <= 0) return out;
    for (int y = 0; y < nh; ++y) {
        int sy = int(int64_t(y) * h_ / nh);
        for (int x = 0; x < nw; ++x) {
            int sx = int(int64_t(x) * w_ / nw);
            const uint8_t* sp = &px_[(size_t(sy) * w_ + sx) * 4];
            uint8_t* dp = &out.px_[(size_t(y) * nw + x) * 4];
            dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3];
        }
    }
    return out;
}

// ── draw namespace ───────────────────────────────────────────────────────────
namespace draw {

static void hline(Surface& s, Color c, int x0, int x1, int y) {
    if (x0 > x1) std::swap(x0, x1);
    for (int x = x0; x <= x1; ++x) blend_pixel(s, x, y, c);
}

static void fill_rect_plain(Surface& s, Color c, Rect r) {
    if (c.a == 255) { s.fill(c, r); return; }
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x) blend_pixel(s, x, y, c);
}

// Per-pixel rounded-rect test: pixel is inside if within the inset rect or
// within `rad` of the nearest corner centre.
static bool in_rounded(int px, int py, Rect r, int rad) {
    int x0 = r.x, y0 = r.y, x1 = r.x + r.w - 1, y1 = r.y + r.h - 1;
    int cx = std::clamp(px, x0 + rad, x1 - rad);
    int cy = std::clamp(py, y0 + rad, y1 - rad);
    int dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= rad * rad;
}

void rect(Surface& s, Color c, Rect r, int width, int border_radius) {
    if (r.w <= 0 || r.h <= 0) return;
    int rad = std::min({border_radius, r.w / 2, r.h / 2});
    if (rad <= 0) {
        if (width <= 0) { fill_rect_plain(s, c, r); return; }
        fill_rect_plain(s, c, {r.x, r.y, r.w, width});                       // top
        fill_rect_plain(s, c, {r.x, r.y + r.h - width, r.w, width});         // bottom
        fill_rect_plain(s, c, {r.x, r.y, width, r.h});                       // left
        fill_rect_plain(s, c, {r.x + r.w - width, r.y, width, r.h});         // right
        return;
    }
    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            if (!in_rounded(x, y, r, rad)) continue;
            if (width > 0) {
                Rect inner{r.x + width, r.y + width, r.w - 2 * width,
                           r.h - 2 * width};
                int irad = std::max(0, rad - width);
                if (inner.w > 0 && inner.h > 0 && in_rounded(x, y, inner, irad))
                    continue;
            }
            blend_pixel(s, x, y, c);
        }
    }
}

void circle(Surface& s, Color c, int cx, int cy, int radius, int width) {
    if (radius <= 0) return;
    int r2 = radius * radius;
    int inner2 = (width > 0) ? (radius - width) * (radius - width) : -1;
    for (int dy = -radius; dy <= radius; ++dy) {
        int yy = cy + dy;
        int dy2 = dy * dy;
        for (int dx = -radius; dx <= radius; ++dx) {
            int d2 = dx * dx + dy2;
            if (d2 > r2) continue;
            if (width > 0 && d2 < inner2) continue;
            blend_pixel(s, cx + dx, yy, c);
        }
    }
}

static void disk(Surface& s, Color c, int cx, int cy, int radius) {
    circle(s, c, cx, cy, radius, 0);
}

void line(Surface& s, Color c, int x1, int y1, int x2, int y2, int width) {
    int dx = std::abs(x2 - x1), dy = std::abs(y2 - y1);
    int sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;
    int half = width / 2;
    while (true) {
        if (width <= 1) blend_pixel(s, x1, y1, c);
        else disk(s, c, x1, y1, half);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 <  dx) { err += dx; y1 += sy; }
    }
}

void polygon(Surface& s, Color c, const std::vector<std::pair<int, int>>& pts,
             int width) {
    if (pts.size() < 2) return;
    if (width > 0) {
        for (size_t i = 0; i < pts.size(); ++i) {
            auto a = pts[i];
            auto b = pts[(i + 1) % pts.size()];
            line(s, c, a.first, a.second, b.first, b.second, width);
        }
        return;
    }
    int ymin = pts[0].second, ymax = pts[0].second;
    for (auto& p : pts) { ymin = std::min(ymin, p.second); ymax = std::max(ymax, p.second); }
    ymin = std::max(ymin, 0);
    ymax = std::min(ymax, s.height() - 1);
    std::vector<float> xs;
    for (int y = ymin; y <= ymax; ++y) {
        xs.clear();
        for (size_t i = 0; i < pts.size(); ++i) {
            auto a = pts[i];
            auto b = pts[(i + 1) % pts.size()];
            int ay = a.second, by = b.second;
            if ((ay <= y && by > y) || (by <= y && ay > y)) {
                float t = float(y - ay) / float(by - ay);
                xs.push_back(a.first + t * (b.first - a.first));
            }
        }
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2)
            hline(s, c, int(std::ceil(xs[i])), int(std::floor(xs[i + 1])), y);
    }
}

}  // namespace draw
