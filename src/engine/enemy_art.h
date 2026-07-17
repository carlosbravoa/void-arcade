// enemy_art.h — cached two-frame pixel-art enemy sprites with a glow halo and a
// shape-correct hit-flash. Extracted from void_storm so other games (void_raid)
// share the same richer look instead of raw polygons.
#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "engine/sprite.h"
#include "engine/surface.h"

// A pixel-art grid: one string per row. '#' = body colour, '@' = hot near-white
// cockpit/highlight cell, '.' = transparent. (Same alias the games already use.)
using SL = std::vector<std::string>;

// Two animation frames, each pre-rendered as a glowing Surface, plus a white
// silhouette per frame for the hit-flash flicker.
struct EnemyArt {
    std::shared_ptr<Surface> frame[2];
    std::shared_ptr<Surface> flash[2];
    int cw = 0, ch = 0;   // unscaled cell dims (for centring)
    int scale = 0;
};

// Render one frame: body cells in `body`, '@' cells as a brighter highlight.
inline Surface build_enemy_frame(const SL& grid, Color body, int scale) {
    Surface s = make_sprite_surface(grid, body, scale);
    Color hi(std::min(255, body.r + 110), std::min(255, body.g + 110),
             std::min(255, body.b + 110));
    for (int r = 0; r < (int)grid.size(); ++r)
        for (int c = 0; c < (int)grid[r].size(); ++c)
            if (grid[r][c] == '@')
                draw::rect(s, hi, {c * scale, r * scale, scale, scale});
    return s;
}

// Build the cached two-frame art (glowing body + white flash silhouettes) once.
inline EnemyArt make_enemy_art(const SL& f0, const SL& f1, Color body, int scale,
                               int glow_r = 6, float glow_i = 0.8f) {
    const Color white(255, 255, 255);
    EnemyArt a;
    a.scale = scale;
    a.cw = (int)f0[0].size();
    a.ch = (int)f0.size();
    Surface s0 = build_enemy_frame(f0, body, scale);
    Surface s1 = build_enemy_frame(f1, body, scale);
    a.frame[0] = glow_surface(s0, body, glow_r, glow_i);
    a.frame[1] = glow_surface(s1, body, glow_r, glow_i);
    Surface w0 = make_sprite_surface(f0, white, scale);
    Surface w1 = make_sprite_surface(f1, white, scale);
    a.flash[0] = glow_surface(w0, white, glow_r, glow_i);
    a.flash[1] = glow_surface(w1, white, glow_r, glow_i);
    return a;
}

// Blit a cached frame centred on (cx,cy); overlay the white silhouette at the
// hit-flash alpha for a clean, shape-correct impact flicker.
inline void blit_enemy(Surface& s, const EnemyArt& art, int frame, int cx, int cy,
                       float hit_flash) {
    const Surface& g = *art.frame[frame & 1];
    int gx = cx - g.width() / 2, gy = cy - g.height() / 2;
    s.blit(g, gx, gy);
    if (hit_flash > 0) {
        Surface& f = *art.flash[frame & 1];
        f.set_alpha(int(170 * std::min(1.0f, hit_flash)));
        s.blit(f, gx, gy);
        f.set_alpha(-1);
    }
}
