// sprite.h — pixel-art sprite rendering + glow halo, shared by the games.
// Ported from the make_surface / glow_surface helpers in the games' entities.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "color.h"
#include "surface.h"

// Render a '#'/'.' pixel grid into an SRCALPHA Surface, `scale` px per cell.
Surface make_sprite_surface(const std::vector<std::string>& lines, Color color,
                            int scale);

std::pair<int, int> sprite_size(const std::vector<std::string>& lines, int scale);

// Build a glowing version of `base` with a soft colored halo of `radius` px.
std::shared_ptr<Surface> glow_surface(const Surface& base, Color color,
                                      int radius = 8, float intensity = 0.7f);

// Draw a glowing projectile: a rounded capsule core from (x,y0) to (x,y1) with
// a soft radial halo and a hot rounded tip at (x,y0). Replaces square bolts.
void draw_glow_bolt(Surface& s, Color color, int x, int y0, int y1, int half_w);
