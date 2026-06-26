// void_drift.cpp — Void Drift (Asteroids inspired, with inertial drift).
//
// A glowing vector field of jagged wireframe rocks tumbling through the void.
// You fly a neon triangle with real inertia — thrust, coast, drift, wrap. Shoot
// rocks; large split into medium, medium into small, small vaporise. Chain kills
// for a combo multiplier. A drifting void saucer crosses and snipes you for
// bonus points. Hyperspace (DOWN) is a panic teleport with a roll-the-dice risk.
// Every 5th wave a giant void-alien drifts in, spits rocks and aimed fire across
// telegraphed phases, then rains power-ups on death.
//
// Leans hard into the CRT bloom: everything is thin saturated vector lines on a
// near-black field, so the shader makes the whole screen glow like phosphor.
#include "games/void_drift/void_drift.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "engine/boss_art.h"
#include "engine/combo.h"
#include "engine/initials.h"
#include "engine/input.h"
#include "engine/juice.h"
#include "engine/particles.h"
#include "engine/pause.h"
#include "engine/rng.h"
#include "engine/scores.h"
#include "engine/sound.h"
#include "engine/sprite.h"
#include "engine/surface.h"
#include "engine/telemetry.h"
#include "engine/text.h"

namespace {
const Color BLACK(0, 0, 0), WHITE(255, 255, 255), GREEN(80, 255, 80);
const Color CYAN(80, 220, 255), RED(255, 60, 60), YELLOW(255, 220, 40);
const Color DIM(40, 80, 40), ORANGE(255, 140, 40), GOLD(255, 200, 60);
const Color PURPLE(190, 110, 255), ROCK(150, 235, 180);
constexpr float PI = 3.14159265358979323846f;
constexpr float TAU = 6.28318530717958647692f;

std::string zpad(int v, int width) {
    std::string s = std::to_string(v);
    if ((int)s.size() < width) s = std::string(width - s.size(), '0') + s;
    return s;
}

// Wrap a coordinate into [0,max) (toroidal screen). O(1) and finite-safe: a
// non-finite coord (from a degenerate velocity) used to spin the old while-loops
// forever and freeze the game — now it's clamped to a valid position instead.
float wrapf(float v, float max) {
    if (!(max > 0.0f)) return 0.0f;
    if (!std::isfinite(v)) return max * 0.5f;
    v = std::fmod(v, max);
    if (v < 0) v += max;
    return v;
}
// Shortest signed delta from a to b on a wrapped axis of length max.
float wrap_delta(float a, float b, float max) {
    float d = b - a;
    if (d > max * 0.5f) d -= max;
    if (d < -max * 0.5f) d += max;
    return d;
}

// Draw a closed wireframe poly with an additive-feeling glow halo (thick dim
// pass under a thin bright pass) — the bloom shader turns this into neon light.
void draw_wire(Surface& s, const std::vector<std::pair<int, int>>& pts, Color c,
               int glow = 5) {
    int n = (int)pts.size();
    if (n < 2) return;
    Color halo(c.r, c.g, c.b, 60);
    for (int i = 0; i < n; ++i) {
        auto [x1, y1] = pts[i];
        auto [x2, y2] = pts[(i + 1) % n];
        draw::line(s, halo, x1, y1, x2, y2, glow);
    }
    for (int i = 0; i < n; ++i) {
        auto [x1, y1] = pts[i];
        auto [x2, y2] = pts[(i + 1) % n];
        draw::line(s, c, x1, y1, x2, y2, 2);
    }
}

// ── Void saucer pixel art ─────────────────────────────────────────────────────
// A proper retro flying saucer: rounded dome on top with a porthole, a flared
// hull skirt, and a row of underside lights (animated separately, on top). Built
// once from a '#'/'.' grid and cached (with a glow halo) — never rebuilt per
// frame. Light positions are tracked so we can blink them and spin an underside
// accent over the static hull.
using SaucerLines = std::vector<std::string>;

// 21 wide × 9 tall hull: a rounded dome on top of a wide flared disc. Every row
// is exactly 21 chars (centred) so make_sprite_surface lays it out symmetrically.
// The dome shine, porthole and underside lights are drawn live on top (so they
// can animate) rather than baked in. Row 6 (the lower rim) carries the lights.
const SaucerLines SAUCER_GRID = {
    ".........###.........",  // 0 dome cap
    ".......#######.......",  // 1 dome
    "......#########......",  // 2 dome base
    "....#############....",  // 3 upper disc
    "..#################..",  // 4 disc
    "#####################",  // 5 widest rim
    ".###################.",  // 6 lower rim  (lights sit here)
    "...###############...",  // 7 skirt
    "......#########......",  // 8 skirt taper
};

// Cached hull sprite + its glow halo, one entry per (scale, color) variant. Two
// variants are enough (small / big saucer), so a tiny fixed cache suffices.
struct SaucerSprite {
    std::shared_ptr<Surface> hull;   // flat hull pixel art
    std::shared_ptr<Surface> glow;   // soft colored halo behind it
    int w = 0, h = 0, glow_r = 0;
    std::vector<std::pair<int, int>> lights;  // underside light centres (px)
};

// Build (once) the saucer sprite for a given pixel scale + hull colour.
const SaucerSprite& saucer_sprite(bool small) {
    static SaucerSprite cache[2];
    SaucerSprite& sp = cache[small ? 1 : 0];
    if (sp.hull) return sp;
    int scale = small ? 3 : 4;
    Color hull_col = small ? Color(170, 130, 235) : PURPLE;
    Surface base = make_sprite_surface(SAUCER_GRID, hull_col, scale);
    sp.w = base.width(); sp.h = base.height();
    sp.hull = std::make_shared<Surface>(std::move(base));
    sp.glow_r = small ? 7 : 9;
    sp.glow = glow_surface(*sp.hull, Color(220, 170, 255), sp.glow_r, 0.8f);
    // underside light slots sit along the lower rim (row 6), evenly spaced.
    int cell = scale;
    int row = 6;                              // lower-rim row index
    int y = row * cell + cell / 2;
    for (int c = 3; c <= 17; c += 4) {        // four lights across the rim
        sp.lights.push_back({c * cell + cell / 2, y});
    }
    return sp;
}

// ── Bullet ───────────────────────────────────────────────────────────────────
struct Bullet {
    float x, y, vx, vy, life;
    Color color;
    bool alive = true;
    bool enemy = false;
    void update(float dt, int sw, int sh) {
        x = wrapf(x + vx * dt, (float)sw);
        y = wrapf(y + vy * dt, (float)sh);
        life -= dt;
        if (life <= 0) alive = false;
    }
    Rect rect() const { return {int(x) - 3, int(y) - 3, 6, 6}; }
    void draw(Surface& s) const {
        if (!alive) return;
        draw::circle(s, Color(color.r, color.g, color.b, 80), int(x), int(y), 5);
        draw::circle(s, color, int(x), int(y), 2);
    }
};

// ── Asteroid ─────────────────────────────────────────────────────────────────
// Jagged wireframe polygon. size 3=large, 2=medium, 1=small.
struct Rock {
    float x, y, vx, vy;
    float ang = 0, spin;
    int size;
    float radius;
    bool alive = true;
    std::vector<float> verts;  // per-vertex radius factor, evenly spaced angles
    Color color;

    Rock(float px, float py, int sz, float speed_scale) : x(px), y(py), size(sz) {
        radius = sz == 3 ? 46.0f : sz == 2 ? 26.0f : 14.0f;
        int n = sz == 3 ? 12 : sz == 2 ? 10 : 8;
        for (int i = 0; i < n; ++i) verts.push_back(rnd_uniform(0.66f, 1.0f));
        float a = rnd_uniform(0, TAU);
        float spd = rnd_uniform(28, 64) * speed_scale * (sz == 1 ? 1.5f : sz == 2 ? 1.2f : 1.0f);
        vx = std::cos(a) * spd;
        vy = std::sin(a) * spd;
        spin = rnd_uniform(-1.6f, 1.6f);
        ang = rnd_uniform(0, TAU);
        // small rocks glow hotter (greener->whiter) so they read as "live".
        color = sz == 3 ? Color(120, 210, 160) : sz == 2 ? ROCK : Color(190, 255, 210);
    }
    void update(float dt, int sw, int sh) {
        x = wrapf(x + vx * dt, (float)sw);
        y = wrapf(y + vy * dt, (float)sh);
        ang += spin * dt;
    }
    int points() const { return size == 3 ? 20 : size == 2 ? 50 : 100; }
    std::vector<std::pair<int, int>> poly() const {
        std::vector<std::pair<int, int>> p;
        int n = (int)verts.size();
        for (int i = 0; i < n; ++i) {
            float a = ang + i * (TAU / n);
            float r = radius * verts[i];
            p.push_back({int(x + std::cos(a) * r), int(y + std::sin(a) * r)});
        }
        return p;
    }
    void draw(Surface& s) const {
        if (!alive) return;
        draw_wire(s, poly(), color, size == 3 ? 6 : size == 2 ? 5 : 4);
    }
};

// ── Ship ─────────────────────────────────────────────────────────────────────
struct Ship {
    static constexpr float TURN = 4.2f, THRUST = 320.0f, FRICTION = 0.62f, MAX_V = 480.0f;
    static constexpr float RESPAWN_T = 1.6f;
    float x, y, ang = -PI / 2;     // pointing up
    float vx = 0, vy = 0;
    bool alive = true, thrusting = false;
    float fire_cd = 0, respawn_t = 0, invincible = 0, flame = 0;
    int lives = 3;
    float radius = 13.0f;

    Ship(int px, int py) : x((float)px), y((float)py) {}
    void update(float dt, float turn, bool thrust, int sw, int sh) {
        if (!alive) { respawn_t -= dt; return; }
        if (invincible > 0) invincible -= dt;
        if (fire_cd > 0) fire_cd -= dt;
        ang += turn * TURN * dt;
        thrusting = thrust;
        if (thrust) {
            vx += std::cos(ang) * THRUST * dt;
            vy += std::sin(ang) * THRUST * dt;
            flame = 1.0f;
        } else {
            flame = std::max(0.0f, flame - dt * 6);
        }
        // exponential friction so you drift but never coast forever.
        float fr = std::pow(FRICTION, dt);
        vx *= fr; vy *= fr;
        float sp = std::hypot(vx, vy);
        if (sp > MAX_V) { vx *= MAX_V / sp; vy *= MAX_V / sp; }
        x = wrapf(x + vx * dt, (float)sw);
        y = wrapf(y + vy * dt, (float)sh);
    }
    Rect rect() const { int r = int(radius);
        return {int(x) - r, int(y) - r, r * 2, r * 2}; }
    void kill() { alive = false; lives--; respawn_t = RESPAWN_T; vx = vy = 0; }
    void respawn(int sw, int sh) {
        if (!alive && respawn_t <= 0) {
            alive = true; x = sw / 2.0f; y = sh / 2.0f;
            vx = vy = 0; ang = -PI / 2; invincible = 2.5f;
        }
    }
    // The three hull points in world space.
    std::vector<std::pair<int, int>> hull() const {
        float c = std::cos(ang), s = std::sin(ang);
        auto pt = [&](float lx, float ly) {
            return std::make_pair(int(x + lx * c - ly * s), int(y + lx * s + ly * c));
        };
        return {pt(16, 0), pt(-11, -10), pt(-6, 0), pt(-11, 10)};
    }
    void draw(Surface& s) const {
        if (!alive) return;
        if (invincible > 0 && int(invincible * 10) % 2 == 0) return;
        // flame first (under the hull)
        if (flame > 0.05f) {
            float c = std::cos(ang), s_ = std::sin(ang);
            auto pt = [&](float lx, float ly) {
                return std::make_pair(int(x + lx * c - ly * s_), int(y + lx * s_ + ly * c));
            };
            float len = 10 + 12 * flame + rnd_uniform(-3, 3);
            auto tip = pt(-6 - len, 0);
            auto a = pt(-6, -6), b = pt(-6, 6);
            Color fc = flame > 0.6f ? GOLD : ORANGE;
            draw::line(s, Color(fc.r, fc.g, fc.b, 90), a.first, a.second, tip.first, tip.second, 5);
            draw::line(s, fc, a.first, a.second, tip.first, tip.second, 2);
            draw::line(s, fc, b.first, b.second, tip.first, tip.second, 2);
        }
        draw_wire(s, hull(), CYAN, 6);
    }
};

// ── Void saucer (UFO) ─────────────────────────────────────────────────────────
struct Saucer {
    bool active = false;
    float x = 0, y = 0, vx = 0, vy = 0;
    float spawn_cd, fire_t = 0, anim_t = 0;
    int dir = 1;
    bool small = false;  // small saucer aims; big one sprays
    float radius = 18;
    int wave = 1;

    Saucer(int wv) : wave(wv) { spawn_cd = rnd_uniform(10, 18); }
    Rect rect() const { return {int(x) - int(radius), int(y) - int(radius),
                                int(radius * 2), int(radius * 2)}; }
    void reset_idle() { active = false; spawn_cd = rnd_uniform(12, 22); }
    // returns true the frame it spawns
    bool update(float dt, float px, float py, int sw, int sh, std::vector<Bullet>& out) {
        if (!active) {
            spawn_cd -= dt;
            if (spawn_cd <= 0) {
                active = true;
                anim_t = 0;
                dir = rnd01() < 0.5f ? 1 : -1;
                small = wave >= 3 && rnd01() < std::min(0.55f, 0.2f + wave * 0.04f);
                radius = small ? 13 : 20;
                x = dir == 1 ? -radius : sw + radius;
                y = rnd_uniform(60, sh - 120);
                vx = dir * (small ? 130.0f : 95.0f);
                vy = 0;
                fire_t = rnd_uniform(0.6f, 1.2f);
                return true;
            }
            return false;
        }
        anim_t += dt;
        x += vx * dt;
        y += vy * dt;
        // occasional vertical jink to feel alive
        if (rnd01() < 0.012f) vy = rnd_uniform(-60, 60);
        y = std::clamp(y, 40.0f, sh - 80.0f);
        fire_t -= dt;
        if (fire_t <= 0) {
            fire_t = rnd_uniform(small ? 1.0f : 1.6f, small ? 1.6f : 2.4f);
            float aim;
            if (small) {  // aimed straight at the player (wrapped)
                float dx = wrap_delta(x, px, (float)sw), dy = wrap_delta(y, py, (float)sh);
                aim = std::atan2(dy, dx) + rnd_uniform(-0.12f, 0.12f);
            } else {
                aim = rnd_uniform(0, TAU);
            }
            Bullet b;
            b.x = x; b.y = y; b.vx = std::cos(aim) * 250; b.vy = std::sin(aim) * 250;
            b.life = 2.2f; b.color = RED; b.enemy = true;
            out.push_back(b);
        }
        if (x < -radius * 2 || x > sw + radius * 2) { reset_idle(); }
        return false;
    }
    int points() const { return small ? 200 : 100; }
    void draw(Surface& s) const {
        if (!active) return;
        const SaucerSprite& sp = saucer_sprite(small);
        int cx = int(x), cy = int(y);
        int ox = cx - sp.w / 2, oy = cy - sp.h / 2;  // sprite top-left
        // soft halo behind the hull, then the hull itself.
        s.blit(*sp.glow, ox - sp.glow_r, oy - sp.glow_r);
        s.blit(*sp.hull, ox, oy);

        // dome porthole: a glowing eye on the dome that subtly pulses.
        Color dome_col = small ? CYAN : Color(150, 230, 255);
        float pulse = 0.6f + 0.4f * std::sin(anim_t * 6.0f);
        int dr = small ? 3 : 4, dy = oy + sp.h / 4;
        draw::circle(s, Color(dome_col.r, dome_col.g, dome_col.b, int(90 * pulse)),
                     cx, dy, dr + 3);
        draw::circle(s, dome_col, cx, dy, dr);
        draw::circle(s, WHITE, cx, dy, std::max(1, dr - 2));

        // underside lights: a chasing blink that scrolls along the skirt.
        const Color lcols[3] = {GOLD, CYAN, Color(255, 110, 200)};
        int phase = int(anim_t * 7.0f);
        for (size_t i = 0; i < sp.lights.size(); ++i) {
            bool on = ((int)i + phase) % 2 == 0;
            if (!on) continue;
            Color lc = lcols[i % 3];
            int lx = ox + sp.lights[i].first, ly = oy + sp.lights[i].second;
            draw::circle(s, Color(lc.r, lc.g, lc.b, 110), lx, ly, small ? 3 : 4);
            draw::circle(s, lc, lx, ly, small ? 1 : 2);
        }

        // rotating underside accent: a hot spark sweeping under the hull, so the
        // saucer reads as a spinning disc even while drifting straight.
        float a = anim_t * 5.0f * dir;
        int sweep = sp.w / 3;
        int sx = cx + int(std::cos(a) * sweep);
        int sy = oy + sp.h - (small ? 3 : 4);
        draw::circle(s, Color(255, 240, 180, 150), sx, sy, small ? 2 : 3);
        draw::circle(s, WHITE, sx, sy, 1);
    }
};

// ── Boss ─────────────────────────────────────────────────────────────────────
// A giant void-alien that drifts in over the field, runs 3 telegraphed phases
// (spit rocks / aimed bursts / radial storm), and rains power-ups on death.
struct VoidBoss {
    float x, y, vx, vy;
    int w, h, sprite;
    int hp, max_hp;
    Color color;
    std::string name;
    float bob = 0, hit_flash = 0;
    bool entering = true, dying = false, finished = false;
    float die_t = 0, enter_t = 1.8f, atk_t = 0, telegraph = 0, sub_t = 0, spin = 0;
    int phase = 1, cur_attack = -1, pending = -1;
    static constexpr float DEATH_DUR = 1.6f;

    VoidBoss(int wave, int sw) {
        int tier = wave / 5;            // 1,2,3,...
        sprite = (tier - 1) % 4;
        const char* names[4] = {"VOID WARDEN", "THE DEVOURER", "NULL SOVEREIGN", "ABYSS TITAN"};
        name = names[sprite];
        const Color cols[4] = {Color(120, 230, 160), PURPLE, Color(110, 180, 255), RED};
        color = cols[sprite];
        w = 200 + tier * 14; h = 150 + tier * 10;
        max_hp = hp = 60 + (tier - 1) * 30;
        x = (sw - w) / 2.0f; y = -(float)h;
        vx = (60 + tier * 8) * (rnd01() < 0.5f ? 1 : -1);
        vy = 0;
    }
    Rect rect() const { return {int(x), int(y), w, h}; }
    float hp_ratio() const { return std::max(0.0f, float(hp) / max_hp); }
    bool damageable() const { return !entering && !dying; }
    std::pair<float, float> mouth() const { return {x + w / 2.0f, y + h - 8.0f}; }

    static Bullet shot(float bx, float by, float ang, float spd) {
        Bullet b; b.x = bx; b.y = by; b.vx = std::cos(ang) * spd; b.vy = std::sin(ang) * spd;
        b.life = 3.0f; b.color = ORANGE; b.enemy = true;
        return b;
    }
    void take_damage(int d) {
        if (!damageable()) return;
        hp -= d; hit_flash = 0.14f;
        if (hp <= 0) { hp = 0; dying = true; die_t = 0; }
    }
    int pick_attack() {
        std::vector<int> pool = {0, 1};               // SPIT, AIMED
        if (phase >= 2) pool.push_back(2);            // RADIAL
        if (phase >= 3) { pool.push_back(2); pool.push_back(1); }  // weight the nasty ones
        return pool[rnd_int(0, (int)pool.size() - 1)];
    }
    // out_bullets for fire; new_rocks gets {x,y} requests the game turns into rocks.
    void update(float dt, float pcx, float pcy, int sw, int sh,
                std::vector<Bullet>& out, std::vector<std::pair<float, float>>& new_rocks,
                ParticleSystem& parts) {
        bob += dt;
        if (hit_flash > 0) hit_flash -= dt * 6;
        if (entering) {
            enter_t -= dt;
            y += 70 * dt;
            if (enter_t <= 0 && y >= 30) { y = 30; entering = false; }
            return;
        }
        if (dying) {
            die_t += dt;
            if (rnd01() < 0.6f)
                parts.ufo_explode(x + rnd_uniform(0, w), y + rnd_uniform(0, h), 10);
            if (die_t >= DEATH_DUR) finished = true;
            return;
        }
        x += vx * dt;
        if (x < 6) { x = 6; vx = std::fabs(vx); }
        if (x + w > sw - 6) { x = sw - 6 - w; vx = -std::fabs(vx); }
        phase = hp_ratio() > 0.6f ? 1 : hp_ratio() > 0.3f ? 2 : 3;
        float iv = (phase == 3 ? 1.0f : phase == 2 ? 1.4f : 1.9f);
        auto [mx, my] = mouth();
        if (telegraph > 0) {
            telegraph -= dt;
            if (telegraph <= 0) { cur_attack = pending; atk_t = 0; sub_t = 99; }
        } else if (cur_attack >= 0) {
            atk_t += dt; sub_t += dt;
            switch (cur_attack) {
                case 0:  // SPIT: lob a couple of fresh rocks into the field
                    new_rocks.push_back({mx + rnd_uniform(-40, 40), my});
                    new_rocks.push_back({mx + rnd_uniform(-40, 40), my});
                    cur_attack = -1; break;
                case 1: {  // AIMED burst at the player
                    float aim = std::atan2(pcy - my, pcx - mx);
                    for (int i = -2; i <= 2; ++i) out.push_back(shot(mx, my, aim + i * 0.16f, 270));
                    cur_attack = -1; break;
                }
                case 2:  // RADIAL storm (sustained spinning spray)
                    if (sub_t >= 0.09f) {
                        sub_t = 0; spin += 0.42f;
                        for (int k = 0; k < 3; ++k)
                            out.push_back(shot(mx, my, spin + k * (TAU / 3), 220));
                    }
                    if (atk_t >= 1.6f) cur_attack = -1;
                    break;
                default: cur_attack = -1; break;
            }
        } else {
            atk_t += dt;
            if (atk_t >= iv) {
                atk_t = 0; pending = pick_attack();
                telegraph = phase == 3 ? 0.35f : 0.6f;
            }
        }
        (void)sh;
    }
    void draw(Surface& s, float pcx, float pcy) const {
        if (y + h < 0) return;
        float sc = dying ? std::max(0.0f, 1 - die_t / DEATH_DUR) : 1.0f;
        int bw = int(w * sc), bh = int(h * sc);
        int bx = int(x + (w - bw) / 2), by = int(y + (h - bh) / 2 + std::sin(bob * 2) * 4);
        if (bw < 6 || bh < 6) return;
        Color body = hit_flash > 0 ? lerp_color(color, WHITE, 0.6f) : color;
        draw::rect(s, Color(body.r, body.g, body.b, 60), {bx - 8, by - 8, bw + 16, bh + 16}, 0, 14);
        draw_boss_alien(s, sprite, int(bob * 3) % 2, bx, by, bw, bh, body);
        if (telegraph > 0) {
            float p = 0.4f + 0.6f * std::sin(bob * 30);
            draw::rect(s, Color(255, 60, 60, int(150 * p)), {bx - 7, by - 7, bw + 14, bh + 14}, 3, 16);
        }
        // googly eyes that track the player
        int er = std::max(6, bh / 8);
        int eyl = bx + bw / 3, eyr = bx + 2 * bw / 3, eyy = by + int(bh * 0.36f);
        auto eye = [&](int ex) {
            draw::circle(s, WHITE, ex, eyy, er);
            float dx = pcx - ex, dy = pcy - eyy, d = std::max(1.0f, std::hypot(dx, dy));
            draw::circle(s, BLACK, ex + int(dx / d * er / 2), eyy + int(dy / d * er / 2),
                         std::max(2, er / 2));
        };
        eye(eyl); eye(eyr);
    }
};

// ── power-ups (kept simple: a few drift-relevant boons) ───────────────────────
enum class PU { Spread, Rapid, Shield, Life };
struct PUInfo { const char* label; Color color; };
PUInfo pu_info(PU k) {
    switch (k) {
        case PU::Spread: return {"3", ORANGE};
        case PU::Rapid:  return {"R", YELLOW};
        case PU::Shield: return {"O", CYAN};
        default:         return {"+1", WHITE};
    }
}
struct PowerUp {
    float x, y, vx, vy, t = 0;
    PU kind;
    bool alive = true;
    Rect rect() const { return {int(x) - 14, int(y) - 14, 28, 28}; }
    void update(float dt, int sw, int sh) {
        x = wrapf(x + vx * dt, (float)sw);
        y = wrapf(y + vy * dt, (float)sh);
        t += dt;
        if (t > 12) alive = false;  // power-ups expire
    }
    void draw(Surface& s, Font& f) const {
        if (!alive) return;
        PUInfo in = pu_info(kind);
        float pulse = 0.7f + 0.3f * std::sin(t * 8);
        // fade out as it expires
        int a = t > 9 ? int(255 * std::max(0.0f, (12 - t) / 3.0f)) : 255;
        draw::circle(s, Color(in.color.r, in.color.g, in.color.b, int(70 * pulse * a / 255)),
                     int(x), int(y), 18);
        draw::circle(s, Color(8, 12, 18, a), int(x), int(y), 13);
        draw::circle(s, Color(in.color.r, in.color.g, in.color.b, a), int(x), int(y), 13, 2);
        draw_text_centered(s, f, in.label, int(x), int(y) - 7,
                           Color(in.color.r, in.color.g, in.color.b, a));
    }
};

struct Flash { std::string text; float x, y, life, max_life; Color color; bool big; };

bool overlap(Rect a, Rect b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

enum class St { Intro, Play, Dead, Win, GameOver };

// ── Game ─────────────────────────────────────────────────────────────────────
class VoidDriftGame : public Scene {
public:
    explicit VoidDriftGame(const Context& ctx)
        : Scene(ctx), pause_(ctx.W, ctx.H, ctx.fonts), initials_(ctx.W, ctx.H, ctx.fonts) {
        high_score_ = ctx.scores ? ctx.scores->high_score("void_drift") : 0;
        new_game();
        if (ctx_.sfx) ctx_.sfx->start_ambient(0);
    }
    void update(float dt, const Input& in) override;
    void draw(Surface& s) override;
    void on_exit() override {
        submit_scores();
        if (ctx_.sfx) { ctx_.sfx->stop_ufo(); ctx_.sfx->stop_ambient(); }
    }

private:
    static constexpr float AMBIENT_STAR_T = 0.18f;
    static constexpr int MAX_BULLETS = 5;
    PauseMenu pause_;
    InitialsEntry initials_;
    bool entry_started_ = false;
    ParticleSystem particles_;
    int high_score_ = 0;
    float star_t_ = 0;

    int score_ = 0, wave_ = 0;
    std::unique_ptr<Ship> ship_;
    std::vector<Rock> rocks_;
    std::vector<Rock> rock_spawn_;  // rocks split off this frame, merged post-loop
    std::vector<Bullet> bullets_, enemy_bullets_;
    std::unique_ptr<Saucer> saucer_;
    std::unique_ptr<VoidBoss> boss_;
    std::vector<PowerUp> powerups_;
    St state_ = St::Intro;
    float dead_t_ = 0, win_t_ = 0, intro_t_ = 0;
    bool boss_wave_ = false;
    std::vector<Flash> flash_;
    bool submitted_ = false;

    // weapon state (persists the run; reset on death like space_invaders)
    int w_spread_ = 0, w_rapid_ = 0;
    float shield_ = 0;
    float hyper_cd_ = 0;

    bool bot_ = false;
    float game_t_ = 0;

    void tele(const J& j) { if (ctx_.tele) ctx_.tele->event(j); }
    void bot_decide(float& turn, bool& thrust, bool& fire, bool& hyper);

    void new_game() {
        score_ = 0; wave_ = 0;
        entry_started_ = false; submitted_ = false;
        w_spread_ = w_rapid_ = 0; shield_ = 0; hyper_cd_ = 0;
        ship_ = std::make_unique<Ship>(ctx_.W / 2, ctx_.H / 2);
        particles_.clear();
        flash_.clear();
        bot_ = std::getenv("VOID_BOT") != nullptr || ctx_.demo;
        int start = 1;
        if (const char* e = std::getenv("VOID_WAVE")) start = std::max(1, atoi(e));
        start_wave(start);
    }

    void spawn_wave_rocks(int n, float speed_scale) {
        for (int i = 0; i < n; ++i) {
            // spawn on edges, away from the ship's centre
            float px, py;
            if (rnd01() < 0.5f) { px = rnd01() < 0.5f ? 0 : (float)ctx_.W; py = rnd_uniform(0, ctx_.H); }
            else { px = rnd_uniform(0, ctx_.W); py = rnd01() < 0.5f ? 0 : (float)ctx_.H; }
            rocks_.emplace_back(px, py, 3, speed_scale);
        }
    }

    void start_wave(int w) {
        wave_ = w;
        boss_wave_ = (w % 5 == 0);
        rocks_.clear();
        bullets_.clear(); enemy_bullets_.clear(); powerups_.clear();
        saucer_ = std::make_unique<Saucer>(w);
        if (ctx_.sfx) { ctx_.sfx->stop_ufo(); ctx_.sfx->start_ambient(0); }
        if (boss_wave_) {
            boss_ = std::make_unique<VoidBoss>(w, ctx_.W);
            spawn_wave_rocks(2 + w / 10, 0.9f);   // a few rocks to dodge while fighting
        } else {
            boss_.reset();
            float speed_scale = 1.0f + (w - 1) * 0.08f;
            int n = std::min(11, 3 + w);          // more rocks each wave
            spawn_wave_rocks(n, speed_scale);
        }
        // per-wave hue drift; boss waves push warm/menacing
        if (ctx_.fx)
            ctx_.fx->palette_hue = 0.18f * std::sin(w * 0.5f) + (boss_wave_ ? -0.12f : 0.0f);
        state_ = St::Intro;
        intro_t_ = 1.8f;
        tele(J().add("ev", "wave_start").add("wave", wave_).add("boss", boss_wave_ ? 1 : 0)
                .add("t", (double)game_t_).add("lives", ship_->lives));
    }

    void add_flash(const std::string& t, float x, float y, Color c, float life = 0.9f,
                   bool big = false) {
        flash_.push_back({t, x, y, life, life, c, big});
    }
    void submit_scores() {
        if (ctx_.demo) return;
        if (submitted_) return;
        submitted_ = true;
        tele(J().add("ev", "game_over").add("wave", wave_).add("score", score_)
                .add("t", (double)game_t_));
        if (ctx_.scores) {
            bool hi = ctx_.scores->submit_void_drift(score_, wave_);
            if (hi) add_flash("NEW HI-SCORE!", ctx_.W / 2, ctx_.H / 2 - 40, YELLOW, 2.5f);
            high_score_ = ctx_.scores->high_score("void_drift");
        }
    }

    void fire() {
        if (!ship_->alive || ship_->fire_cd > 0) return;
        if ((int)bullets_.size() >= MAX_BULLETS + w_spread_ * 2) return;
        ship_->fire_cd = std::max(0.12f, 0.30f - w_rapid_ * 0.05f);
        auto h = ship_->hull();
        float tx = (float)h[0].first, ty = (float)h[0].second;
        Color col = GOLD;
        auto add = [&](float off) {
            Bullet b;
            b.x = tx; b.y = ty;
            b.vx = std::cos(ship_->ang + off) * 620 + ship_->vx;
            b.vy = std::sin(ship_->ang + off) * 620 + ship_->vy;
            b.life = 0.85f; b.color = col;
            bullets_.push_back(b);
        };
        add(0);
        for (int i = 1; i <= w_spread_; ++i) { add(i * 0.16f); add(-i * 0.16f); }
        if (ctx_.sfx) { ctx_.sfx->pan_next(tx / float(ctx_.W)); ctx_.sfx->shoot(); }
    }

    void hyperspace() {
        if (!ship_->alive || hyper_cd_ > 0) return;
        hyper_cd_ = 1.2f;
        auto [cx, cy] = std::pair<int, int>{int(ship_->x), int(ship_->y)};
        particles_.ufo_explode((float)cx, (float)cy, 14);
        ship_->x = rnd_uniform(60, ctx_.W - 60);
        ship_->y = rnd_uniform(60, ctx_.H - 60);
        ship_->vx = ship_->vy = 0;
        particles_.ufo_explode(ship_->x, ship_->y, 14);
        if (ctx_.sfx) { ctx_.sfx->pan_next(ship_->x / float(ctx_.W)); ctx_.sfx->warp(); }
        if (ctx_.fx) ctx_.fx->shake(5);
        // small panic risk: ~12% chance you rematerialise inside a rock (classic)
        if (rnd01() < 0.12f) {
            for (auto& r : rocks_) {
                float d = std::hypot(wrap_delta(ship_->x, r.x, (float)ctx_.W),
                                     wrap_delta(ship_->y, r.y, (float)ctx_.H));
                if (d < r.radius + ship_->radius) { kill_ship("BAD JUMP!"); break; }
            }
        }
    }

    // Takes the rock BY VALUE, and defers child rocks into rock_spawn_ instead of
    // pushing straight into rocks_. The caller iterates `for (auto& r : rocks_)`,
    // so mutating rocks_ here would reallocate it and invalidate that loop's
    // iterators — which froze the game. rock_spawn_ is merged after the loop.
    void split_rock(Rock r) {
        auto [cx, cy] = std::pair<int, int>{int(r.x), int(r.y)};
        particles_.explode_invader((float)cx, (float)cy, r.color, r.size == 3 ? 30 : 18);
        if (r.size > 1) {
            float ss = 1.0f + (wave_ - 1) * 0.08f;
            for (int i = 0; i < 2; ++i) {
                Rock nr(r.x, r.y, r.size - 1, ss);
                // inherit a little of the parent momentum so chunks fly apart
                nr.vx = r.vx * 0.5f + nr.vx;
                nr.vy = r.vy * 0.5f + nr.vy;
                rock_spawn_.push_back(nr);
            }
        }
        if (ctx_.sfx) { ctx_.sfx->pan_next(r.x / float(ctx_.W)); ctx_.sfx->invader_hit(); }
        if (ctx_.fx) ctx_.fx->shake(r.size == 3 ? 5.0f : 3.0f);
    }

    void maybe_drop(float x, float y, float chance) {
        if (rnd01() >= chance) return;
        int r = rnd_int(0, 99);
        PU k = r < 38 ? PU::Spread : r < 70 ? PU::Rapid : r < 92 ? PU::Shield : PU::Life;
        PowerUp p; p.x = x; p.y = y;
        p.vx = rnd_uniform(-30, 30); p.vy = rnd_uniform(-30, 30);
        p.kind = k;
        powerups_.push_back(p);
    }
    void apply_powerup(PU k) {
        const char* msg = "";
        switch (k) {
            case PU::Spread: w_spread_ = std::min(3, w_spread_ + 1); msg = "TRI-SHOT!"; break;
            case PU::Rapid:  w_rapid_ = std::min(3, w_rapid_ + 1); msg = "RAPID FIRE!"; break;
            case PU::Shield: shield_ = 8.0f; msg = "SHIELD UP!"; break;
            case PU::Life:   ship_->lives++; msg = "EXTRA LIFE!"; break;
        }
        add_flash(msg, ship_->x, ship_->y - 30, YELLOW, 1.2f);
        if (ctx_.sfx) ctx_.sfx->powerup_collect();
        if (ctx_.fx) ctx_.fx->rumble_pickup();
    }

    void kill_ship(const char* reason = nullptr) {
        if (ship_->invincible > 0 || shield_ > 0) return;
        auto [cx, cy] = std::pair<int, int>{int(ship_->x), int(ship_->y)};
        particles_.explode_player((float)cx, (float)cy);
        bool had = w_spread_ || w_rapid_;
        w_spread_ = w_rapid_ = 0;
        if (had) add_flash("WEAPONS LOST!", (float)cx, cy - 40, RED, 1.6f, true);
        if (reason) add_flash(reason, (float)cx, cy - 64, RED, 1.4f, true);
        ship_->kill();
        state_ = St::Dead; dead_t_ = 2.0f;
        enemy_bullets_.clear();
        if (ctx_.sfx) { ctx_.sfx->stop_ufo(); ctx_.sfx->player_death(); }
        if (ctx_.fx) { ctx_.fx->shake(16); ctx_.fx->hitstop(0.09f); ctx_.fx->rumble_death(); }
        tele(J().add("ev", "death").add("wave", wave_).add("t", (double)game_t_)
                .add("lives_left", ship_->lives));
    }

    void update_play(float dt, const Input& in);
    void draw_hud(Surface& s);
    void draw_flash(Surface& s);
    void draw_banner(Surface& s);
    int rocks_alive() const { int n = 0; for (auto& r : rocks_) if (r.alive) ++n; return n; }
};

void VoidDriftGame::update(float dt, const Input& in) {
    for (auto k : in.pressed)
        if (k == SDLK_p && state_ == St::Play) {
            pause_.toggle();
            if (ctx_.sfx && pause_.active()) ctx_.sfx->stop_ufo();
        }
    if (pause_.active()) {
        if (pause_.update(in) == PauseResult::QuitToMenu) quit_to_menu = true;
        return;
    }
    for (auto k : in.pressed)
        if (k == SDLK_ESCAPE) { quit_to_menu = true; return; }

    game_t_ += dt;
    star_t_ += dt;
    if (star_t_ >= AMBIENT_STAR_T) { star_t_ = 0; particles_.ambient_star(ctx_.W, ctx_.H); }
    particles_.update(dt);
    for (auto& f : flash_) f.life -= dt;
    flash_.erase(std::remove_if(flash_.begin(), flash_.end(),
                 [](const Flash& f) { return f.life <= 0; }), flash_.end());

    if (state_ == St::Intro) {
        intro_t_ -= dt;
        // let the ship drift gently during the intro
        ship_->update(dt, 0, false, ctx_.W, ctx_.H);
        for (auto& r : rocks_) r.update(dt, ctx_.W, ctx_.H);
        if (intro_t_ <= 0) state_ = St::Play;
    } else if (state_ == St::Play) {
        update_play(dt, in);
    } else if (state_ == St::Dead) {
        dead_t_ -= dt;
        // Tick the (dead) ship so its respawn timer actually counts down — without
        // this, respawn() below no-ops forever and the game softlocks on a death.
        ship_->update(dt, 0, false, ctx_.W, ctx_.H);
        for (auto& r : rocks_) r.update(dt, ctx_.W, ctx_.H);
        if (dead_t_ <= 0) {
            if (ship_->lives > 0) {
                ship_->respawn(ctx_.W, ctx_.H);
                if (ctx_.sfx) ctx_.sfx->spawn_chime();
                state_ = St::Play;
            } else {
                state_ = St::GameOver; submit_scores();
            }
        }
    } else if (state_ == St::Win) {
        win_t_ -= dt;
        for (auto& r : rocks_) r.update(dt, ctx_.W, ctx_.H);
        if (win_t_ <= 0) start_wave(wave_ + 1);
    } else if (state_ == St::GameOver) {
        if (bot_) { new_game(); return; }
        if (ctx_.scores) {
            const std::string gid = "void_drift";
            if (!entry_started_ && ctx_.scores->qualifies(gid, score_)) {
                entry_started_ = true;
                int rank = 1;
                for (auto& e : ctx_.scores->leaderboard(gid)) if (e.score > score_) rank++;
                initials_.begin("VOID DRIFT", score_, rank);
            }
            if (initials_.active()) {
                if (initials_.update(in)) ctx_.scores->add_score(gid, initials_.name(), score_);
                return;
            }
        }
        for (auto k : in.pressed)
            if (k == SDLK_RETURN || k == SDLK_SPACE) new_game();
    }
}

void VoidDriftGame::update_play(float dt, const Input& in) {
    float turn = 0; bool thrust = false, want_fire = false, want_hyper = false;
    if (bot_) {
        bot_decide(turn, thrust, want_fire, want_hyper);
    } else {
        turn = (in.down(SDLK_RIGHT) || in.down(SDLK_d)) - (in.down(SDLK_LEFT) || in.down(SDLK_a));
        thrust = in.down(SDLK_UP) || in.down(SDLK_w);
        want_fire = in.down(SDLK_SPACE);
        for (auto k : in.pressed) if (k == SDLK_DOWN || k == SDLK_s) want_hyper = true;
    }

    if (hyper_cd_ > 0) hyper_cd_ -= dt;
    if (shield_ > 0) shield_ -= dt;
    if (want_hyper) hyperspace();
    ship_->update(dt, turn, thrust, ctx_.W, ctx_.H);
    if (thrust && ctx_.sfx && rnd01() < 0.15f) {}  // (thrust is silent; flame is the feedback)
    if (!ship_->alive) return;
    if (want_fire) fire();

    for (auto& b : bullets_) {
        b.update(dt, ctx_.W, ctx_.H);
        if (rnd01() < 0.4f) particles_.bullet_trail(b.x, b.y, b.color, true);
    }

    for (auto& r : rocks_) r.update(dt, ctx_.W, ctx_.H);

    // saucer
    bool saucer_was = saucer_->active;
    size_t saucer_eb_before = enemy_bullets_.size();
    bool spawned = saucer_->update(dt, ship_->x, ship_->y, ctx_.W, ctx_.H, enemy_bullets_);
    if (spawned) add_flash("⚠ VOID SAUCER ⚠", ctx_.W / 2, 56, PURPLE, 1.6f, true);
    if (ctx_.sfx) {
        if (saucer_->active && !saucer_was) {
            ctx_.sfx->start_ufo();
            // a little vocalisation on arrival, panned by where it slides in.
            ctx_.sfx->pan_next(saucer_->x / float(ctx_.W));
            ctx_.sfx->alien_chirp(saucer_->small ? 2 : 0);
        } else if (!saucer_->active && saucer_was) {
            ctx_.sfx->stop_ufo();
        }
        if (saucer_->active) ctx_.sfx->set_ufo_pan(saucer_->x / float(ctx_.W));
        // the saucer just fired at the player — distinct from our own shot.
        if (saucer_->active && enemy_bullets_.size() > saucer_eb_before) {
            ctx_.sfx->pan_next(saucer_->x / float(ctx_.W));
            ctx_.sfx->enemy_shot();
        }
    }

    // boss
    if (boss_) {
        std::vector<std::pair<float, float>> new_rocks;
        size_t eb_before = enemy_bullets_.size();
        boss_->update(dt, ship_->x, ship_->y, ctx_.W, ctx_.H, enemy_bullets_, new_rocks, particles_);
        for (auto& nr : new_rocks) {
            Rock rk(nr.first, nr.second, 2, 1.0f);
            rk.vy = std::fabs(rk.vy) + 40;  // bias rocks downward into the field
            rocks_.push_back(rk);
        }
        if (enemy_bullets_.size() > eb_before && ctx_.sfx) {
            ctx_.sfx->pan_next((boss_->x + boss_->w / 2) / float(ctx_.W));
            ctx_.sfx->enemy_shot();
        }
    }

    for (auto& b : enemy_bullets_) b.update(dt, ctx_.W, ctx_.H);

    // ── player bullets → rocks / saucer / boss ──
    for (auto& b : bullets_) {
        if (!b.alive) continue;
        for (auto& r : rocks_) {
            if (!r.alive) continue;
            float d = std::hypot(wrap_delta(b.x, r.x, (float)ctx_.W),
                                 wrap_delta(b.y, r.y, (float)ctx_.H));
            if (d <= r.radius) {
                r.alive = false; b.alive = false;
                int pts = r.points() * (ctx_.combo ? ctx_.combo->hit() : 1);
                score_ += pts;
                add_flash("+" + std::to_string(pts), r.x, r.y - 16, YELLOW);
                if (r.size == 1) maybe_drop(r.x, r.y, 0.10f);  // small rocks rarely gift
                split_rock(r);
                break;
            }
        }
        if (!b.alive) continue;
        if (saucer_->active && overlap(b.rect(), saucer_->rect())) {
            b.alive = false;
            int pts = saucer_->points() * (ctx_.combo ? ctx_.combo->hit() : 1);
            score_ += pts;
            auto [sx, sy] = std::pair<int, int>{int(saucer_->x), int(saucer_->y)};
            particles_.ufo_explode((float)sx, (float)sy);
            add_flash("+" + std::to_string(pts) + "!", (float)sx, sy - 20, PURPLE);
            maybe_drop((float)sx, (float)sy, 0.6f);
            saucer_->reset_idle();
            if (ctx_.sfx) { ctx_.sfx->stop_ufo(); ctx_.sfx->boss_explosion(); }
            if (ctx_.fx) { ctx_.fx->shake(8); ctx_.fx->hitstop(0.04f); }
            continue;
        }
        if (boss_ && boss_->damageable() && overlap(b.rect(), boss_->rect())) {
            b.alive = false;
            boss_->take_damage(1);
            score_ += 5;
            particles_.explode_invader(b.x, b.y, boss_->color, 6);
            if (ctx_.sfx) ctx_.sfx->invader_hit();
            if (ctx_.fx) ctx_.fx->shake(2);
        }
    }
    // Merge any rocks split off this frame now that the rocks_ loop has finished
    // (mutating rocks_ mid-loop would invalidate the iterators above).
    if (!rock_spawn_.empty()) {
        rocks_.insert(rocks_.end(), rock_spawn_.begin(), rock_spawn_.end());
        rock_spawn_.clear();
    }
    bullets_.erase(std::remove_if(bullets_.begin(), bullets_.end(),
                   [](const Bullet& b) { return !b.alive; }), bullets_.end());
    rocks_.erase(std::remove_if(rocks_.begin(), rocks_.end(),
                 [](const Rock& r) { return !r.alive; }), rocks_.end());

    // ── boss death → win + power-up rain ──
    if (boss_ && boss_->finished) {
        score_ += 1000 * (wave_ / 5);
        float bx = boss_->x + boss_->w / 2, by = boss_->y + boss_->h / 2;
        for (int i = 0; i < 4; ++i) {
            PowerUp p; p.x = bx + rnd_uniform(-60, 60); p.y = by;
            p.vx = rnd_uniform(-40, 40); p.vy = rnd_uniform(-40, 40);
            p.kind = rnd_choice<PU>({PU::Spread, PU::Rapid, PU::Shield, PU::Life});
            powerups_.push_back(p);
        }
        add_flash(boss_->name + " DESTROYED!", ctx_.W / 2, ctx_.H / 2 - 20, YELLOW, 2.2f, true);
        if (ctx_.fx) {
            ctx_.fx->shake(30); ctx_.fx->hitstop(0.10f);
            ctx_.fx->rumble_explosion();
            ctx_.fx->shockwave((boss_->x + boss_->w / 2) / float(ctx_.W),
                               (boss_->y + boss_->h / 2) / float(ctx_.H));
        }
        if (ctx_.sfx) {
            ctx_.sfx->stop_ufo();
            ctx_.sfx->pan_next(bx / float(ctx_.W));
            ctx_.sfx->big_explosion();   // boss death — bigger than boss_explosion
            ctx_.sfx->level_clear();
        }
        tele(J().add("ev", "boss_clear").add("wave", wave_).add("name", boss_->name)
                .add("lives", ship_->lives));
        boss_.reset();
        state_ = St::Win; win_t_ = 2.6f;
        return;
    }

    // ── power-ups ──
    for (auto& p : powerups_) {
        p.update(dt, ctx_.W, ctx_.H);
        if (p.alive && ship_->alive && overlap(p.rect(), ship_->rect())) {
            p.alive = false; apply_powerup(p.kind);
        }
    }
    powerups_.erase(std::remove_if(powerups_.begin(), powerups_.end(),
                    [](const PowerUp& p) { return !p.alive; }), powerups_.end());
    // ── threats → ship ──
    bool safe = ship_->invincible > 0 || shield_ > 0;
    if (ship_->alive) {
        // rocks
        for (auto& r : rocks_) {
            if (!r.alive) continue;
            float d = std::hypot(wrap_delta(ship_->x, r.x, (float)ctx_.W),
                                 wrap_delta(ship_->y, r.y, (float)ctx_.H));
            if (d < r.radius + ship_->radius - 3) {
                if (safe) {  // shield smashes the rock instead of dying
                    if (shield_ > 0) { score_ += r.points(); split_rock(r); r.alive = false; }
                    break;
                }
                kill_ship(); return;
            }
        }
    }
    if (ship_->alive && !safe) {
        for (auto& b : enemy_bullets_) {
            if (!b.alive) continue;
            if (overlap(b.rect(), ship_->rect())) { b.alive = false; kill_ship(); return; }
        }
    }
    if (ship_->alive && boss_ && boss_->damageable() && !safe &&
        overlap(ship_->rect(), boss_->rect())) { kill_ship(); return; }

    enemy_bullets_.erase(std::remove_if(enemy_bullets_.begin(), enemy_bullets_.end(),
                         [](const Bullet& b) { return !b.alive; }), enemy_bullets_.end());
    // ── wave clear (non-boss): field emptied ──
    if (!boss_ && rocks_alive() == 0) {
        tele(J().add("ev", "wave_clear").add("wave", wave_).add("t", (double)game_t_)
                .add("lives", ship_->lives));
        add_flash("WAVE CLEARED!", ctx_.W / 2, ctx_.H / 2 - 20, YELLOW, 1.6f, true);
        if (ctx_.sfx) { ctx_.sfx->big_explosion(); ctx_.sfx->level_clear(); }
        state_ = St::Win; win_t_ = 1.8f;
    }
}

// Competent attract bot: rotate to face the nearest threat, dodge if a rock is
// closing in, fire when roughly aimed, hyperspace as a last resort.
void VoidDriftGame::bot_decide(float& turn, bool& thrust, bool& fire, bool& hyper) {
    turn = 0; thrust = false; fire = false; hyper = false;
    if (!ship_->alive) return;

    // find nearest rock and nearest imminent danger
    float best_d = 1e9f; const Rock* target = nullptr;
    float danger_d = 1e9f;
    float dvx = 0, dvy = 0;  // direction away from closest danger
    for (auto& r : rocks_) {
        if (!r.alive) continue;
        float dx = wrap_delta(ship_->x, r.x, (float)ctx_.W);
        float dy = wrap_delta(ship_->y, r.y, (float)ctx_.H);
        float d = std::hypot(dx, dy) - r.radius;
        if (d < best_d) { best_d = d; target = &r; }
        if (d < danger_d) { danger_d = d; dvx = -dx; dvy = -dy; }
    }
    // also treat the saucer as a target
    float sd = 1e9f;
    if (saucer_->active) {
        float dx = wrap_delta(ship_->x, saucer_->x, (float)ctx_.W);
        float dy = wrap_delta(ship_->y, saucer_->y, (float)ctx_.H);
        sd = std::hypot(dx, dy);
    }
    if (boss_ && boss_->damageable()) {
        // aim at the boss body centre
        float dx = wrap_delta(ship_->x, boss_->x + boss_->w / 2.0f, (float)ctx_.W);
        float dy = wrap_delta(ship_->y, boss_->y + boss_->h / 2.0f, (float)ctx_.H);
        float ang_to = std::atan2(dy, dx);
        float da = std::remainder(ang_to - ship_->ang, TAU);
        turn = da > 0.05f ? 1 : da < -0.05f ? -1 : 0;
        fire = std::fabs(da) < 0.25f;
    } else if (sd < 220 && saucer_->active) {
        float dx = wrap_delta(ship_->x, saucer_->x, (float)ctx_.W);
        float dy = wrap_delta(ship_->y, saucer_->y, (float)ctx_.H);
        float ang_to = std::atan2(dy, dx);
        float da = std::remainder(ang_to - ship_->ang, TAU);
        turn = da > 0.05f ? 1 : da < -0.05f ? -1 : 0;
        fire = std::fabs(da) < 0.22f;
    } else if (target) {
        float dx = wrap_delta(ship_->x, target->x, (float)ctx_.W);
        float dy = wrap_delta(ship_->y, target->y, (float)ctx_.H);
        float ang_to = std::atan2(dy, dx);
        float da = std::remainder(ang_to - ship_->ang, TAU);
        turn = da > 0.05f ? 1 : da < -0.05f ? -1 : 0;
        fire = std::fabs(da) < 0.20f;
    }

    // dodge: if something is very close, thrust away from it
    if (danger_d < 70 && (dvx || dvy)) {
        float away = std::atan2(dvy, dvx);
        float da = std::remainder(away - ship_->ang, TAU);
        if (std::fabs(da) < 1.2f) thrust = true;  // already roughly facing escape
        else { turn = da > 0 ? 1 : -1; }          // else rotate toward escape
        fire = false;
        if (danger_d < 26 && hyper_cd_ <= 0) hyper = true;  // last-ditch teleport
    } else if (std::hypot(ship_->vx, ship_->vy) < 40 && best_d > 140) {
        thrust = true;  // creep toward the field if drifting too slow & far
    }
}

// ── drawing ──────────────────────────────────────────────────────────────────
void VoidDriftGame::draw(Surface& s) {
    s.fill(Color(3, 6, 12));  // near-black, faint blue void
    // subtle grid
    for (int x = 0; x < ctx_.W; x += 60) draw::line(s, Color(8, 14, 22), x, 0, x, ctx_.H, 1);
    for (int y = 0; y < ctx_.H; y += 60) draw::line(s, Color(8, 14, 22), 0, y, ctx_.W, y, 1);

    particles_.draw(s);
    for (auto& r : rocks_) r.draw(s);
    for (auto& p : powerups_) p.draw(s, *ctx_.fonts.small);
    saucer_->draw(s);
    if (boss_) boss_->draw(s, ship_->x, ship_->y);
    for (auto& b : enemy_bullets_) b.draw(s);
    for (auto& b : bullets_) b.draw(s);
    ship_->draw(s);

    // shield bubble
    if (shield_ > 0 && ship_->alive) {
        float pulse = 0.6f + 0.4f * std::sin(game_t_ * 20 + shield_ * 4);
        draw::circle(s, Color(80, 220, 255, int(90 * pulse)), int(ship_->x), int(ship_->y), 22, 2);
        draw::circle(s, Color(80, 220, 255, int(30 * pulse)), int(ship_->x), int(ship_->y), 19, 1);
    }

    draw_hud(s);
    draw_flash(s);
    draw_banner(s);

    if (state_ == St::GameOver) {
        draw_text_centered(s, *ctx_.fonts.big, "GAME  OVER", ctx_.W / 2, ctx_.H / 2 - 50, RED);
        draw_text_centered(s, *ctx_.fonts.med, "SCORE  " + zpad(score_, 6), ctx_.W / 2,
                           ctx_.H / 2 + 10, WHITE);
        draw_text_centered(s, *ctx_.fonts.small, "PRESS SPACE TO RETRY", ctx_.W / 2,
                           ctx_.H / 2 + 60, DIM);
        if (initials_.active()) initials_.draw(s);
    }
    pause_.draw(s);
}

void VoidDriftGame::draw_banner(Surface& s) {
    if (state_ == St::Intro) {
        float a = std::min(1.0f, intro_t_ * 1.5f);
        draw_text_centered(s, *ctx_.fonts.big, "WAVE " + std::to_string(wave_), ctx_.W / 2,
                           ctx_.H / 2 - 20, Color(int(80 + 175 * a), 255, int(120 + 60 * a)));
        if (boss_wave_)
            draw_text_centered(s, *ctx_.fonts.med, "SOMETHING STIRS IN THE VOID", ctx_.W / 2,
                               ctx_.H / 2 + 28, RED);
        else
            draw_text_centered(s, *ctx_.fonts.small,
                               "↑ THRUST   ← → TURN   SPACE FIRE   ↓ HYPERSPACE",
                               ctx_.W / 2, ctx_.H / 2 + 30, DIM);
    }
}

void VoidDriftGame::draw_hud(Surface& s) {
    draw_text(s, *ctx_.fonts.med, "SCORE  " + zpad(score_, 6), 16, 8, GREEN);
    draw_text_centered(s, *ctx_.fonts.med, "HI  " + zpad(high_score_, 6), ctx_.W / 2, 8, YELLOW);
    draw_text_right(s, *ctx_.fonts.med, "WAVE  " + std::to_string(wave_), ctx_.W - 16, 8, CYAN);

    if (ctx_.combo && ctx_.combo->active() && state_ == St::Play) {
        int m = ctx_.combo->mult();
        Color cc = m >= 6 ? RED : m >= 4 ? ORANGE : YELLOW;
        draw_text_centered(s, *ctx_.fonts.med, "x" + std::to_string(m) + " COMBO", ctx_.W / 2, 34, cc);
        int bw = 120, bx = ctx_.W / 2 - bw / 2;
        draw::rect(s, Color(40, 40, 40), {bx, 58, bw, 4});
        draw::rect(s, cc, {bx, 58, int(bw * ctx_.combo->ratio()), 4});
    }

    // boss HP bar
    if (boss_ && !boss_->dying && !boss_->entering) {
        int bw = 420, bx = ctx_.W / 2 - bw / 2, by = 76;
        draw_text_centered(s, *ctx_.fonts.small, boss_->name, ctx_.W / 2, by - 16, boss_->color);
        draw::rect(s, Color(20, 20, 30), {bx, by, bw, 12}, 0, 4);
        Color hc = boss_->hp_ratio() > 0.5f ? GREEN : boss_->hp_ratio() > 0.25f ? YELLOW : RED;
        draw::rect(s, hc, {bx, by, int(bw * boss_->hp_ratio()), 12}, 0, 4);
        draw::rect(s, boss_->color, {bx, by, bw, 12}, 1, 4);
    }

    // weapon pips (bottom-left)
    int hx = 16, hy = ctx_.H - 46;
    draw_text(s, *ctx_.fonts.small, "PWR", hx, hy, DIM);
    auto pip = [&](int n, Color c, int slot) {
        for (int i = 0; i < 3; ++i)
            draw::rect(s, i < n ? c : Color(30, 40, 30), {hx + 44 + slot * 16, hy + 11 - i * 4, 12, 3});
    };
    pip(w_spread_, ORANGE, 0); pip(w_rapid_, YELLOW, 1);

    // lives as little ships
    draw_text(s, *ctx_.fonts.small, "LIVES", 16, ctx_.H - 28, DIM);
    for (int i = 0; i < ship_->lives; ++i) {
        int lx = 80 + i * 22, ly = ctx_.H - 18;
        draw::polygon(s, CYAN, {{lx + 8, ly - 8}, {lx, ly + 6}, {lx + 4, ly + 2}, {lx + 8, ly + 6}});
    }

    if (shield_ > 0)
        draw_text_right(s, *ctx_.fonts.small, "SHIELD " + std::to_string(int(shield_ + 0.99f)) + "s",
                        ctx_.W - 12, ctx_.H - 28, CYAN);
    else
        draw_text_right(s, *ctx_.fonts.small, "P PAUSE", ctx_.W - 12, ctx_.H - 28,
                        Color(30, 60, 30));
}

void VoidDriftGame::draw_flash(Surface& s) {
    for (auto& f : flash_) {
        Color c = f.color;
        float a = std::max(0.0f, std::min(1.0f, f.life / std::max(0.1f, f.max_life)));
        c.a = uint8_t(255 * a);
        Font& font = f.big ? *ctx_.fonts.med : *ctx_.fonts.small;
        draw_text_centered(s, font, f.text, int(f.x), int(f.y), c);
    }
}
}  // namespace

std::unique_ptr<Scene> make_void_drift(const Context& ctx) {
    return std::make_unique<VoidDriftGame>(ctx);
}
