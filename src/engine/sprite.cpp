#include "sprite.h"

#include <algorithm>
#include <cmath>

std::pair<int, int> sprite_size(const std::vector<std::string>& lines, int scale) {
    size_t w = 0;
    for (auto& r : lines) w = std::max(w, r.size());
    return {int(w) * scale, int(lines.size()) * scale};
}

Surface make_sprite_surface(const std::vector<std::string>& lines, Color color,
                            int scale) {
    auto [w, h] = sprite_size(lines, scale);
    Surface surf(w, h, /*srcalpha=*/true);
    for (size_t r = 0; r < lines.size(); ++r)
        for (size_t c = 0; c < lines[r].size(); ++c)
            if (lines[r][c] == '#')
                surf.fill(Color(color.r, color.g, color.b, 255),
                          {int(c) * scale, int(r) * scale, scale, scale});
    return surf;
}

// Separable box blur of a float field (a few passes ≈ gaussian). Small sprites,
// so a direct O(N·r) implementation is plenty fast and runs once at load.
static void box_blur(std::vector<float>& m, int W, int H, int r, int passes) {
    if (r < 1) return;
    std::vector<float> tmp(m.size());
    float inv = 1.0f / (2 * r + 1);
    for (int pass = 0; pass < passes; ++pass) {
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float acc = 0;
                for (int dx = -r; dx <= r; ++dx)
                    acc += m[y * W + std::clamp(x + dx, 0, W - 1)];
                tmp[y * W + x] = acc * inv;
            }
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float acc = 0;
                for (int dy = -r; dy <= r; ++dy)
                    acc += tmp[std::clamp(y + dy, 0, H - 1) * W + x];
                m[y * W + x] = acc * inv;
            }
    }
}

// Soft radial glow: build an alpha mask from the sprite's lit pixels, blur it
// into a smooth falloff, paint it in the glow colour at low opacity, then drop
// the crisp sprite on top. Reads as a real phosphor halo, not chunky pixels.
std::shared_ptr<Surface> glow_surface(const Surface& base, Color color,
                                      int radius, float intensity) {
    int w = base.width(), h = base.height();
    int R = radius;
    int W = w + R * 2, H = h + R * 2;
    auto g = std::make_shared<Surface>(W, H, true);

    std::vector<float> solid(size_t(W) * H, 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (base.get_at(x, y).a > 0) solid[size_t(y + R) * W + (x + R)] = 1.0f;

    // Flood-fill the "outside" (transparent region reachable from the border).
    // Enclosed gaps inside the sprite — the invaders' eyes/mouths — are NOT
    // reachable, so they never receive glow and stay dark.
    std::vector<char> outside(size_t(W) * H, 0);
    std::vector<int> stk;
    auto push = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        int i = y * W + x;
        if (outside[i] || solid[i] > 0.5f) return;
        outside[i] = 1; stk.push_back(i);
    };
    for (int x = 0; x < W; ++x) { push(x, 0); push(x, H - 1); }
    for (int y = 0; y < H; ++y) { push(0, y); push(W - 1, y); }
    while (!stk.empty()) {
        int i = stk.back(); stk.pop_back();
        push(i % W - 1, i / W); push(i % W + 1, i / W);
        push(i % W, i / W - 1); push(i % W, i / W + 1);
    }

    std::vector<float> blur = solid;
    int br = std::max(1, int(R * 0.5f));
    box_blur(blur, W, H, br, 2);

    // Outer halo only: paint glow where it's outside the silhouette. The crisp
    // sprite covers the interior; enclosed eye-gaps remain dark.
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int i = y * W + x;
            if (!outside[i]) continue;
            float a = blur[i] * intensity;
            if (a <= 0.01f) continue;
            g->set_at(x, y, Color(color.r, color.g, color.b, int(std::min(1.0f, a) * 255)));
        }

    g->blit(base, R, R);  // crisp sprite on top
    return g;
}

void draw_glow_bolt(Surface& s, Color color, int x, int y0, int y1, int half_w) {
    int lo = std::min(y0, y1), hi = std::max(y0, y1);
    int gr = half_w * 2 + 2;
    // soft outer halo
    for (int yy = lo; yy <= hi; yy += 2)
        draw::circle(s, Color(color.r, color.g, color.b, 24), x, yy, gr);
    // inner glow
    for (int yy = lo; yy <= hi; yy += 2)
        draw::circle(s, Color(color.r, color.g, color.b, 64), x, yy, half_w + 1);
    // rounded core
    for (int yy = lo; yy <= hi; ++yy) draw::circle(s, color, x, yy, half_w);
    // hot rounded tip
    Color hot = lerp_color(color, Color(255, 255, 255), 0.7f);
    draw::circle(s, hot, x, y0, std::max(1, half_w - 1));
}
