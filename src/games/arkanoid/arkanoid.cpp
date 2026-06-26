// arkanoid.cpp — Void Breaker. Ported from games/arkanoid/*.py.
#include "arkanoid.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

// ── colours (game.py) ─────────────────────────────────────────────────────────
const Color BLACK(0, 0, 0), WHITE(255, 255, 255), CYAN(80, 200, 255);
const Color YELLOW(255, 220, 40), RED(255, 60, 60), GREEN(80, 255, 80);
const Color PURPLE(200, 80, 255), DIM(40, 60, 80);

constexpr float PI = 3.14159265358979323846f;

// ── brick colours (entities.py BRICK_COLORS) ──────────────────────────────────
Color brick_color(int hp) {
    switch (hp) {
        case 1: return Color(255, 80, 80);    // red    (1 hit)
        case 2: return Color(255, 160, 40);   // orange (2 hits)
        case 3: return Color(255, 220, 40);   // yellow (3 hits)
        case 4: return Color(80, 220, 80);     // green  (4 hits)
        case 5: return Color(80, 180, 255);    // blue   (5 hits)
        case 6: return Color(200, 80, 255);    // purple (6 hits)
        case 9: return Color(180, 180, 180);   // silver (indestructible)
        default: return Color(255, 80, 80);    // default to BRICK_COLORS[1]
    }
}

// ── powerup colours / labels (entities.py) ────────────────────────────────────
Color powerup_color(const std::string& k) {
    if (k == "expand") return Color(80, 255, 80);
    if (k == "shrink") return Color(255, 80, 80);
    if (k == "laser")  return Color(255, 80, 255);
    if (k == "multi")  return Color(80, 200, 255);
    if (k == "slow")   return Color(255, 220, 40);
    if (k == "fast")   return Color(255, 100, 40);
    if (k == "life")   return Color(255, 255, 255);
    if (k == "catch")  return Color(40, 255, 200);
    if (k == "fire")   return Color(255, 110, 30);
    return Color(200, 200, 200);
}
std::string powerup_label(const std::string& k) {
    if (k == "expand") return "EXP";
    if (k == "shrink") return "SHK";
    if (k == "laser")  return "LSR";
    if (k == "multi")  return "MLT";
    if (k == "slow")   return "SLW";
    if (k == "fast")   return "FST";
    if (k == "life")   return "UP!";
    if (k == "catch")  return "CTH";
    if (k == "fire")   return "FIRE";
    return "???";
}

// ── paddle/ball constants (entities.py) ───────────────────────────────────────
constexpr int PADDLE_W_NORMAL = 96, PADDLE_W_BIG = 140, PADDLE_W_SMALL = 56;
constexpr int PADDLE_H = 12, PADDLE_Y_OFF = 50;
constexpr int BALL_R = 6;

// ── brick layout constants (game.py) ──────────────────────────────────────────
constexpr int BRICK_W = 56, BRICK_H = 20, BRICK_GAP = 2, BRICK_COLS = 13, BRICK_TOP = 60;

// ── powerup spawn (game.py) ───────────────────────────────────────────────────
constexpr float POWERUP_CHANCE = 0.22f;
const std::vector<std::string> POWERUP_KINDS = {
    "expand", "shrink", "laser", "multi", "slow", "fast", "life", "catch", "fire"};
const std::vector<int> POWERUP_WEIGHTS = {20, 5, 15, 15, 15, 10, 8, 12, 10};

// ── levels.py ─────────────────────────────────────────────────────────────────
using Row = std::vector<int>;
using Layout = std::vector<Row>;
const std::vector<Layout> LEVELS = {
    // Level 1 – all one-hit bricks: fast, snappy, instant satisfaction
    {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
        {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
        {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
        {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
        {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
        {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
    },
    // Level 2 – diamond
    {
        {0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 3, 3, 3, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 2, 2, 2, 2, 2, 0, 0, 0, 0},
        {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0},
        {0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0},
        {0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0},
        {0, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0, 0},
        {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0},
        {0, 0, 0, 0, 2, 2, 2, 2, 2, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 3, 3, 3, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0},
    },
    // Level 3 – silver fortress
    {
        {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9},
        {9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9},
        {9, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 9},
        {9, 0, 3, 4, 4, 4, 4, 4, 4, 4, 3, 0, 9},
        {9, 0, 3, 4, 5, 5, 5, 5, 5, 4, 3, 0, 9},
        {9, 0, 3, 4, 5, 0, 0, 0, 5, 4, 3, 0, 9},
        {9, 0, 3, 4, 5, 0, 0, 0, 5, 4, 3, 0, 9},
        {9, 0, 3, 4, 5, 5, 5, 5, 5, 4, 3, 0, 9},
        {9, 0, 3, 4, 4, 4, 4, 4, 4, 4, 3, 0, 9},
        {9, 0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 9},
        {9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9},
        {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9},
    },
    // Level 4 – checkerboard chaos
    {
        {1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 1},
        {0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 1, 0},
        {2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 1, 0, 2},
        {0, 3, 0, 4, 0, 5, 0, 6, 0, 1, 0, 2, 0},
        {3, 0, 4, 0, 5, 0, 6, 0, 1, 0, 2, 0, 3},
        {0, 4, 0, 5, 0, 6, 0, 1, 0, 2, 0, 3, 0},
        {4, 0, 5, 0, 6, 0, 1, 0, 2, 0, 3, 0, 4},
        {0, 5, 0, 6, 0, 1, 0, 2, 0, 3, 0, 4, 0},
    },
    // Level 5 – space invader silhouette
    {
        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0},
        {0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0},
        {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},
        {0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0},
        {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
        {1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1},
        {1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1},
        {0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0},
    },
    // Level 6 – the gauntlet
    {
        {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
        {6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6},
        {6, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 6},
        {6, 5, 4, 3, 3, 3, 3, 3, 3, 3, 4, 5, 6},
        {6, 5, 4, 3, 9, 9, 9, 9, 9, 3, 4, 5, 6},
        {6, 5, 4, 3, 9, 2, 2, 2, 9, 3, 4, 5, 6},
        {6, 5, 4, 3, 9, 2, 1, 2, 9, 3, 4, 5, 6},
        {6, 5, 4, 3, 9, 2, 2, 2, 9, 3, 4, 5, 6},
        {6, 5, 4, 3, 9, 9, 9, 9, 9, 3, 4, 5, 6},
        {6, 5, 4, 3, 3, 3, 3, 3, 3, 3, 4, 5, 6},
        {6, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 6},
        {6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6},
        {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
    },
};

// ── helpers ───────────────────────────────────────────────────────────────────
bool overlap(Rect a, Rect b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}
std::string zpad(int v, int width) {
    std::string s = std::to_string(v);
    if ((int)s.size() < width) s = std::string(width - s.size(), '0') + s;
    return s;
}
int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

// random.choices(POWERUP_KINDS, POWERUP_WEIGHTS)[0]
std::string weighted_powerup() {
    int total = 0;
    for (int w : POWERUP_WEIGHTS) total += w;
    int r = rnd_int(1, total);  // 1..total
    int acc = 0;
    for (size_t i = 0; i < POWERUP_KINDS.size(); ++i) {
        acc += POWERUP_WEIGHTS[i];
        if (r <= acc) return POWERUP_KINDS[i];
    }
    return POWERUP_KINDS.back();
}

// glow_rect (entities.py): concentric expanded rounded rects, falling alpha.
void glow_rect(Surface& s, Rect rect, Color color, int radius, int alpha) {
    for (int r = radius; r > 0; --r) {
        int a = int(alpha * std::pow(float(r) / radius, 2.5f));
        draw::rect(s, Color(color.r, color.g, color.b, a),
                   {rect.x - r, rect.y - r, rect.w + r * 2, rect.h + r * 2},
                   0, std::max(2, r));
    }
}

// ── Laser ─────────────────────────────────────────────────────────────────────
struct Laser {
    static constexpr int W = 3, H = 16;
    static constexpr float SPEED = 500;
    float x, y;
    bool alive = true;
    Laser(int px, int py) : x(float(px)), y(float(py)) {}
    Rect rect() const { return {int(x - W / 2), int(y - H), W, H}; }
    void update(float dt) {
        y -= SPEED * dt;
        if (y < -20) alive = false;
    }
    void draw(Surface& s) const {
        if (!alive) return;
        draw_glow_bolt(s, Color(255, 80, 255), int(x), int(y - H), int(y), std::max(2, W / 2));
    }
};

// ── PowerUp ───────────────────────────────────────────────────────────────────
struct PowerUp {
    static constexpr int W = 40, H = 16;
    static constexpr float SPEED = 100;
    float x, y, t = 0;
    std::string kind, label;
    Color color;
    bool alive = true;
    PowerUp(float cx, float cy, const std::string& k)
        : x(cx - W / 2.0f), y(cy), kind(k), label(powerup_label(k)),
          color(powerup_color(k)) {}
    Rect rect() const { return {int(x), int(y), W, H}; }
    void update(float dt, int screen_h) {
        y += SPEED * dt;
        t += dt;
        if (y > screen_h) alive = false;
    }
    void draw(Surface& s, Font& font) const {
        if (!alive) return;
        Rect r = rect();
        float pulse = 0.7f + 0.3f * std::sin(t * 6);
        Color c(int(color.r * pulse), int(color.g * pulse), int(color.b * pulse));
        glow_rect(s, r, c, 6, 90);
        draw::rect(s, c, r, 0, 3);
        auto txt = font.render(label, Color(0, 0, 0));
        s.blit(*txt, r.x + r.w / 2 - txt->width() / 2,
               r.y + r.h / 2 - txt->height() / 2);
    }
};

// ── Brick ─────────────────────────────────────────────────────────────────────
struct Brick {
    static constexpr int W = 56, H = 20;
    int col, row, x, y, hp, max_hp;
    bool alive = true;
    // moving bricks: drift horizontally and bounce at the play-field edges. fx is
    // a high-precision x mirror (int x is rounded from it each frame).
    bool moving = false;
    float fx = 0.0f, mvx = 0.0f;
    Brick(int c, int r, int px, int py, int h)
        : col(c), row(r), x(px), y(py), hp(h), max_hp(h), fx(float(px)) {}
    void make_moving(float speed) { moving = true; mvx = speed; fx = float(x); }
    // drift; bounce off the left/right walls (with a small margin so the glow fits).
    void move(float dt, int screen_w) {
        if (!moving || !alive) return;
        fx += mvx * dt;
        if (fx < 4) { fx = 4; mvx = std::fabs(mvx); }
        if (fx + W > screen_w - 4) { fx = float(screen_w - 4 - W); mvx = -std::fabs(mvx); }
        x = int(fx);
    }
    Rect rect() const { return {x, y, W, H}; }
    bool indestructible() const { return hp == 9; }
    Color color() const { return brick_color(hp); }
    bool hit() {
        if (indestructible()) return false;
        hp -= 1;
        if (hp <= 0) { alive = false; return true; }
        return false;
    }
    void draw(Surface& s) const {
        if (!alive) return;
        Rect r = rect();
        Color color = this->color();
        float dmg = max_hp > 1 ? 1.0f - float(hp) / max_hp : 0.0f;
        if (dmg > 0) {
            color = Color(std::max(0, int(color.r * (1 - dmg * 0.5f))),
                          std::max(0, int(color.g * (1 - dmg * 0.5f))),
                          std::max(0, int(color.b * (1 - dmg * 0.5f))));
        }
        glow_rect(s, r, color, 3, 35);
        draw::rect(s, color, r, 0, 2);
        draw::rect(s, Color(std::min(255, color.r + 60), std::min(255, color.g + 60),
                            std::min(255, color.b + 60)),
                   {r.x + 2, r.y + 2, r.w - 4, 3}, 0, 1);
        draw::rect(s, Color(color.r / 2, color.g / 2, color.b / 2), r, 1, 2);
        if (indestructible())
            draw::line(s, Color(220, 220, 255), r.x + 4, r.y + 4,
                       r.x + r.w - 4, r.y + r.h - 4, 1);
        if (moving)  // little chevrons hint the brick is on the move
            draw::line(s, Color(255, 255, 255, 90),
                       r.x + r.w / 2 - 4, r.y + r.h / 2,
                       r.x + r.w / 2 + 4, r.y + r.h / 2, 1);
    }
};

// ── Well ──────────────────────────────────────────────────────────────────────
// A gravity / wind well: a field source that gently steers passing balls. An
// attractor curves the ball inward; a repulsor pushes it away. The pull is
// clamped so it only BENDS the path — it can never trap the ball or stall it.
struct Well {
    float x, y, radius, strength;  // strength>0 attract, <0 repel
    float t = 0;
    bool attract = true;
    Well(float px, float py, float rad, float str, bool att)
        : x(px), y(py), radius(rad), strength(str), attract(att) {}
    void update(float dt) { t += dt; }
    // Apply a steering acceleration to a ball's velocity. Falls off with distance
    // and is capped so the ball stays player-controllable.
    void steer(float bx, float by, float& vx, float& vy, float dt) const {
        float dx = x - bx, dy = y - by;
        float d = std::hypot(dx, dy);
        if (d > radius || d < 1.0f) return;
        float falloff = 1.0f - d / radius;          // 1 at core → 0 at rim
        float a = strength * falloff * falloff;     // gentle, smooth
        float ux = dx / d, uy = dy / d;
        if (!attract) { ux = -ux; uy = -uy; }
        vx += ux * a * dt;
        vy += uy * a * dt;
    }
    void draw(Surface& s) const {
        Color base = attract ? Color(120, 90, 255) : Color(255, 160, 60);
        float pulse = 0.5f + 0.5f * std::sin(t * 3.0f);
        // a few concentric pulsing rings + a swirling spoke set
        for (int i = 0; i < 3; ++i) {
            float fr = (i + 1) / 3.0f;
            int rr = int(radius * fr * (0.7f + 0.3f * pulse));
            int a = int(70 * (1 - fr) + 20);
            draw::circle(s, Color(base.r, base.g, base.b, a), int(x), int(y), rr, 2);
        }
        int spokes = 8;
        for (int i = 0; i < spokes; ++i) {
            float ang = t * (attract ? 1.6f : -1.6f) + i * (2 * PI / spokes);
            float r0 = radius * 0.2f, r1 = radius * (0.45f + 0.2f * pulse);
            int x0 = int(x + std::cos(ang) * r0), y0 = int(y + std::sin(ang) * r0);
            int x1 = int(x + std::cos(ang) * r1), y1 = int(y + std::sin(ang) * r1);
            draw::line(s, Color(base.r, base.g, base.b, 90), x0, y0, x1, y1, 2);
        }
        draw::circle(s, Color(base.r, base.g, base.b, int(120 * pulse + 60)),
                     int(x), int(y), 5);
    }
};

// ── Paddle ────────────────────────────────────────────────────────────────────
struct Paddle {
    static constexpr float SPEED = 380;
    int screen_w, screen_h, w, h;
    float x, y;
    Color color = Color(80, 200, 255);
    bool laser_on = false, catch_on = false;
    float laser_cd = 0;
    // ── SMASH ability: hold to charge a meter, release to launch a nearby ball ──
    static constexpr float SMASH_FULL = 1.0f;     // seconds to a full charge
    static constexpr float SMASH_CD = 1.6f;       // cooldown after firing
    float smash_charge = 0;    // 0..SMASH_FULL while charging
    float smash_cd = 0;        // cooldown timer
    bool charging = false;
    float smash_glow = 0;      // brief flash on release (for draw)
    Paddle(int sw, int sh)
        : screen_w(sw), screen_h(sh), w(PADDLE_W_NORMAL), h(PADDLE_H),
          x(float(sw / 2 - PADDLE_W_NORMAL / 2)), y(float(sh - PADDLE_Y_OFF)) {}
    Rect rect() const { return {int(x), int(y), w, h}; }
    int center_x() const { return int(x + w / 2.0f); }
    float charge_ratio() const { return std::min(1.0f, smash_charge / SMASH_FULL); }
    bool smash_ready() const { return smash_cd <= 0; }
    void update(float dt, float move_dir) {
        x += move_dir * SPEED * dt;
        x = std::max(0.0f, std::min(float(screen_w - w), x));
        if (laser_cd > 0) laser_cd -= dt;
        if (smash_cd > 0) smash_cd -= dt;
        if (smash_glow > 0) smash_glow -= dt * 3.0f;
    }
    void set_width(int nw) {
        float cx = x + w / 2.0f;
        w = nw;
        x = std::max(0.0f, std::min(float(screen_w - w), cx - w / 2.0f));
    }
    std::vector<Laser> shoot_laser() {
        std::vector<Laser> out;
        if (laser_on && laser_cd <= 0) {
            laser_cd = 0.3f;
            out.emplace_back(int(x + 4), int(y));
            out.emplace_back(int(x + w - 4), int(y));
        }
        return out;
    }
    void draw(Surface& s) const {
        Rect r = rect();
        float cr = charge_ratio();
        // charging up: the paddle glows hotter (white→gold) and pulses
        Color body = color;
        int glow_a = 40;
        if (charging && cr > 0) {
            float p = 0.6f + 0.4f * std::sin(smash_charge * 22.0f);
            body = lerp_color(color, Color(255, 230, 120), cr * p);
            glow_a = int(40 + 90 * cr);
        }
        if (smash_glow > 0) {  // a white burst on release
            body = lerp_color(body, WHITE, std::min(1.0f, smash_glow));
            glow_a = int(glow_a + 120 * std::min(1.0f, smash_glow));
        }
        glow_rect(s, r, body, charging ? 5 + int(cr * 4) : 5, glow_a);
        draw::rect(s, body, r, 0, 4);
        draw::rect(s, Color(200, 240, 255), {r.x + 2, r.y + 2, r.w - 4, 3}, 0, 2);
        if (charging && cr > 0) {  // a charge bar riding above the paddle
            int bw = r.w - 8, bx = r.x + 4, by = r.y - 7;
            draw::rect(s, Color(30, 30, 40), {bx, by, bw, 4});
            Color fc = cr >= 1.0f ? Color(255, 90, 40) : Color(255, 220, 80);
            draw::rect(s, fc, {bx, by, int(bw * cr), 4});
        }
        if (laser_on) {
            int lxs[2] = {int(x) + 4, int(x + w) - 4};
            for (int lx : lxs)
                draw::rect(s, Color(255, 80, 255), {lx - 2, int(y) - 4, 4, 4});
        }
    }
};

// ── Ball ──────────────────────────────────────────────────────────────────────
struct Ball {
    static constexpr float BASE_SPEED = 385, MAX_SPEED = 640, SPEED_STEP = 28;
    static constexpr int HITS_PER_TIER = 8;
    float x, y, vx, vy;
    int r = BALL_R;
    bool alive = true;
    Color color = Color(255, 255, 180);
    bool attached = false;
    float attach_offset = 0.0f;
    float speed_mult = 1.0f;
    int brick_hits = 0;
    bool fire = false;   // mega/fire ball: smashes through bricks for a while
    float base_speed = BASE_SPEED;  // floor speed; raised by endless loops

    // angle: NaN sentinel via has_angle flag.
    Ball(float px, float py, bool has_angle = false, float angle = 0.0f)
        : x(px), y(py) {
        if (!has_angle) {
            // wider, slightly randomized launch -> immediate side-to-side variance
            float offset = PI * (0.28f + rnd01() * 0.12f);  // ~50-72° off vertical
            if (rnd01() < 0.5f) offset = -offset;
            angle = -PI / 2 + offset;
        }
        vx = std::cos(angle) * BASE_SPEED;
        vy = std::sin(angle) * BASE_SPEED;
    }
    float speed() const { return std::hypot(vx, vy); }
    float current_target_speed() const {
        int tier = brick_hits / HITS_PER_TIER;
        float base = base_speed + tier * SPEED_STEP;
        return std::min(MAX_SPEED, base * speed_mult);
    }
    void set_speed(float spd) {
        float s = speed();
        if (s > 0) { vx = vx / s * spd; vy = vy / s * spd; }
    }
    void on_brick_hit() { brick_hits += 1; }
    Rect rect() const { return {int(x - r), int(y - r), r * 2, r * 2}; }

    // returns 0=none, 1=wall, 2=lost
    int update(float dt, int screen_w, int screen_h, const Paddle& paddle) {
        if (attached) {
            x = paddle.x + attach_offset;
            y = paddle.y - r - 1;
            return 0;
        }
        set_speed(current_target_speed());
        x += vx * dt;
        y += vy * dt;
        if (x - r <= 0) { x = r; vx = std::abs(vx); return 1; }
        if (x + r >= screen_w) { x = screen_w - r; vx = -std::abs(vx); return 1; }
        if (y - r <= 0) { y = r; vy = std::abs(vy); return 1; }
        if (y > screen_h + 40) { alive = false; return 2; }
        return 0;
    }
    void bounce_paddle(const Paddle& paddle) {
        float rel = (x - (paddle.x + paddle.w / 2.0f)) / (paddle.w / 2.0f);
        rel = std::max(-0.95f, std::min(0.95f, rel));
        float angle = rel * (PI * 0.38f) - PI / 2;
        float spd = speed();
        vx = std::cos(angle) * spd;
        vy = std::sin(angle) * spd;
        y = paddle.y - r - 1;
    }
    void release() {
        if (attached) {
            attached = false;
            vy = vy >= 0 ? -std::abs(vy) : vy;
        }
    }
    void draw(Surface& s) const {
        if (!alive) return;
        Color core = fire ? Color(255, 120, 30) : color;
        int halo = fire ? r + 8 : r + 5;
        for (int rr = halo; rr > r; --rr) {
            float fr = float(rr - r) / (halo - r);
            int a = int((fire ? 130 : 80) * fr * fr * 0.5f);
            Color gc = fire ? Color(255, 80, 20) : color;
            draw::circle(s, Color(gc.r, gc.g, gc.b, a), int(x), int(y), rr);
        }
        draw::circle(s, core, int(x), int(y), r);
        draw::circle(s, Color(255, 255, 255), int(x) - 2, int(y) - 2, std::max(1, r / 3));
    }
};

struct Flash {
    std::string text;
    float x, y, life;
    Color color;
};

Color darker(Color c, float f) {
    return Color(int(c.r * f), int(c.g * f), int(c.b * f), c.a);
}

// ── BossShot ──────────────────────────────────────────────────────────────────
// A chunky brick the boss hurls down at the paddle. Kills a ball it strikes and
// can clip the paddle (costs a life like dropping the ball would).
struct BossShot {
    static constexpr int W = 22, H = 18;
    float x, y, vx, vy;
    Color color;
    bool alive = true;
    bool minion = false;          // drifting brick-minion (googly, weaves) vs plain hurled brick
    float t = 0, weave = 0;       // animation clock + per-minion weave phase
    BossShot(float cx, float cy, float vxx, float vyy, Color c, bool min = false)
        : x(cx - W / 2.0f), y(cy), vx(vxx), vy(vyy), color(c), minion(min) {
        weave = rnd_uniform(0, 6.28f);
    }
    BossShot(float cx, float cy, float speed, Color c)  // back-compat: straight drop
        : BossShot(cx, cy, 0.0f, speed, c, false) {}
    Rect rect() const { return {int(x), int(y), W, H}; }
    void update(float dt, int screen_w, int screen_h) {
        t += dt;
        if (minion) vx = std::sin(t * 3 + weave) * 70.0f;  // weave as it sinks
        x += vx * dt;
        y += vy * dt;
        if (x < -W) x = float(screen_w);                    // wrap minions across walls
        if (x > screen_w) x = -W;
        if (y > screen_h + 30) alive = false;
    }
    void draw(Surface& s) const {
        if (!alive) return;
        Rect r = rect();
        glow_rect(s, r, color, 4, 60);
        draw::rect(s, color, r, 0, 3);
        draw::rect(s, darker(color, 0.5f), r, 1, 3);
        draw::line(s, Color(255, 255, 255, 120), r.x + 3, r.y + 3, r.x + r.w - 3, r.y + 3, 1);
        if (minion) {  // tiny googly eye so the minions read as little bosslings
            int ex = r.x + r.w / 2, ey = r.y + r.h / 2;
            draw::circle(s, WHITE, ex, ey, 4);
            draw::circle(s, BLACK, ex, ey + 1, 2);
        }
    }
};

// ── BossBullet ─────────────────────────────────────────────────────────────────
// A small energy pellet the boss spits down at the paddle. Faster and smaller than
// a hurled brick; the paddle must side-step it. Costs a life on contact.
struct BossBullet {
    static constexpr int W = 7, H = 14;
    float x, y, vx, vy;
    Color color;
    bool alive = true;
    BossBullet(float px, float py, float vxx, float vyy, Color c)
        : x(px), y(py), vx(vxx), vy(vyy), color(c) {}
    Rect rect() const { return {int(x - W / 2), int(y - H / 2), W, H}; }
    void update(float dt, int screen_w, int screen_h) {
        x += vx * dt;
        y += vy * dt;
        if (y > screen_h + 20 || y < -20 || x < -20 || x > screen_w + 20) alive = false;
    }
    void draw(Surface& s) const {
        if (!alive) return;
        draw_glow_bolt(s, color, int(x), int(y - H / 2), int(y + H / 2), 3);
    }
};

// Cross-game cameo: a giant invader from the OTHER games crashes the breakout —
// it sweeps across crushing bricks in its path and lobs the occasional shot at the
// paddle (gently — you're busy with the ball). The ball hurts it; pop it for a
// power-up rain. A surprise spectacle, not a full boss fight.
struct CameoInvader {
    float x, y, vx, bob = 0, fire_t = 1.4f, crush_t = 0, die_t = 0;
    int w = 132, h = 84, hp, max_hp, sprite_idx;
    Color col;
    bool alive = true, dying = false;
    static constexpr float DEATH = 0.9f;
    CameoInvader(int screen_w, int level) {
        sprite_idx = rnd_int(0, 3);
        const Color cs[4] = {Color(120, 255, 140), Color(200, 100, 255),
                             Color(90, 200, 255), Color(255, 90, 90)};
        col = cs[sprite_idx];
        max_hp = hp = 14 + level;
        y = 64; vx = 105 + level * 4;
        if (rnd01() < 0.5f) x = float(-w); else { x = float(screen_w); vx = -vx; }
    }
    Rect rect() const { return {int(x), int(y), w, h}; }
    bool take_hit(int dmg) {
        if (dying) return false;
        hp -= dmg;
        if (hp <= 0) { dying = true; die_t = 0; return true; }
        return false;
    }
    void update(float dt, int screen_w, float paddle_cx, std::vector<Brick>& bricks,
                std::vector<BossBullet>& out, ParticleSystem& parts) {
        bob += dt;
        if (dying) {
            die_t += dt;
            if (rnd01() < 0.6f) parts.explode_brick(x + rnd_uniform(0, w), y + rnd_uniform(0, h), col);
            if (die_t >= DEATH) alive = false;
            return;
        }
        x += vx * dt;
        if (x < 8) { x = 8; vx = std::fabs(vx); }
        if (x + w > screen_w - 8) { x = float(screen_w - 8 - w); vx = -std::fabs(vx); }
        y += 7 * dt;  // creep downward
        crush_t -= dt;
        if (crush_t <= 0) {  // crush bricks underfoot
            crush_t = 0.07f;
            for (auto& b : bricks)
                if (b.alive && !b.indestructible() && overlap(rect(), b.rect())) {
                    b.alive = false;
                    parts.explode_brick(b.x + Brick::W / 2, b.y + Brick::H / 2, b.color());
                }
        }
        fire_t -= dt;
        if (fire_t <= 0) {  // a sparse aimed shot at the paddle
            fire_t = rnd_uniform(1.7f, 2.9f);
            float bx = x + w / 2, by = y + h;
            float dx = paddle_cx - bx, n = std::max(1.0f, std::hypot(dx, 340.0f));
            out.emplace_back(bx, by, dx / n * 230, 340.0f / n * 230, col);
        }
    }
    void draw(Surface& s, float pcx) const {
        if (!alive) return;
        float sc = dying ? std::max(0.0f, 1 - die_t / DEATH) : 1.0f;
        int bw = int(w * sc), bh = int(h * sc);
        int bx = int(x + (w - bw) / 2), by = int(y + (h - bh) / 2 + std::sin(bob * 2) * 3);
        if (bw < 4) return;
        draw::rect(s, Color(col.r, col.g, col.b, 70), {bx - 6, by - 6, bw + 12, bh + 12}, 0, 8);
        draw_boss_alien(s, sprite_idx, int(bob * 4) % 2, bx, by, bw, bh, col);
        int er = std::max(4, bh / 7), eyy = by + int(bh * 0.34f);
        for (int i = 0; i < 2; ++i) {
            int ex = bx + (i ? 2 * bw / 3 : bw / 3);
            draw::circle(s, WHITE, ex, eyy, er);
            float dx = pcx - ex, dd = std::max(1.0f, std::fabs(dx));
            draw::circle(s, BLACK, ex + int(dx / dd * er / 2), eyy + er / 3, std::max(2, er / 2));
        }
        draw::rect(s, Color(30, 30, 30), {int(x), int(y) - 8, w, 4});
        draw::rect(s, col, {int(x), int(y) - 8, int(w * std::max(0.0f, float(hp) / max_hp)), 4});
    }
};

// A request the boss raises (read by the game) to materialise a defensive band of
// real bricks in front of itself — the ball must chew through them to reach it.
struct ShieldRequest { bool pending = false; int rows = 1; int hp = 2; };

// ── Boss ──────────────────────────────────────────────────────────────────────
// A big fat blocky boss made of brick. Adapted from space_invaders' Boss: blocky
// body with a phosphor grid, googly eyes that track the ball, an HP bar, spawn
// invincibility, and a comedic deflate-death that rains power-ups. It lumbers
// side to side and periodically lobs bricks at the paddle. The ball damages it.
struct BossDef { const char* name; Color color; int w, h, hp; float atk_iv; int cell; };
const BossDef BREAKER_BOSSES[3] = {
    {"BRICKZILLA",  Color(255, 120, 60),  240, 150, 24, 1.4f, 22},
    {"THE WALL",    Color(120, 200, 255), 320, 160, 38, 1.2f, 24},
    {"MEGABRICK",   Color(200, 90, 255),  380, 200, 56, 1.0f, 28},
};

struct Boss {
    std::string name;
    Color color;
    int w, h, cell;
    int hp, max_hp;
    int tier = 1, loop = 0;
    float x, y, vx;
    float atk_t = 0, atk_iv, hit_flash = 0, bob = 0;
    bool dying = false, finished = false;
    float die_t = 0, invuln_ = 0;
    float battle_y = 80;
    bool entering = true, just_stomped = false, fake_started = false;
    bool did_fake = false, faking = false;
    float fake_t = 0, enter_t = 0;
    static constexpr float DEATH_DUR = 1.6f;

    // ── attack system: HP phases, telegraphed pick, one-shot + sustained patterns ──
    // attacks: 0 BRICK_LOB  1 BRICK_FAN  2 AIMED_VOLLEY  3 BULLET_SPREAD
    //          4 SLAM  5 SUMMON_MINIONS  6 SHIELD_UP  7 BULLET_RAIN
    int phase = 1, cur_attack = -1, pending_attack = -1, slam_phase = 0;
    float telegraph = 0, sub_t = 0, attack_t = 0;
    // weak-point: a bright brick on the boss face that takes extra damage when lit.
    bool weak_open = false; float weak_t = 0; float weak_x = 0.5f;  // x as frac of width
    // surprise: ONE of {fake-out death, brick avalanche, split into minions}.
    int surprise_kind = 0;          // chosen at spawn; 0 fake, 1 avalanche, 2 split
    bool avalanche_pending = false, split_pending = false;
    bool did_surprise = false;
    ShieldRequest shield_req;

    bool damageable() const { return !dying && !faking && !entering && invuln_ <= 0; }
    // weak-point lit → ball/laser does bonus damage. Read by the game.
    Rect weak_rect() const {
        int wpw = std::max(18, cell), wph = std::max(14, cell);
        int wx = int(x + weak_x * w - wpw / 2.0f);
        int wy = int(y + h * 0.5f - wph / 2.0f + std::sin(bob * 2) * 4);
        return {wx, wy, wpw, wph};
    }

    Boss(int level, int screen_w) {
        // level is the displayed level number; bosses appear every 5 levels.
        tier = level / 5;                     // 1,2,3,...
        int idx = (tier - 1) % 3;
        loop = (tier - 1) / 3;
        const BossDef& d = BREAKER_BOSSES[idx];
        const char* pre = loop == 0 ? "" : loop == 1 ? "MEGA " : loop == 2 ? "OMEGA " : "VOID ";
        name = std::string(pre) + d.name;
        color = d.color; cell = d.cell;
        w = d.w + loop * 24; h = d.h + loop * 12;
        max_hp = hp = int(d.hp * (1.0f + 0.4f * (tier - 1)));
        atk_iv = std::max(0.55f, d.atk_iv - loop * 0.1f);
        x = (screen_w - w) / 2.0f; y = -float(h);
        vx = (60 + tier * 6) * (rnd01() < 0.5f ? 1.0f : -1.0f);
        entering = true; enter_t = 0; battle_y = 80;
        // the gentle first boss always uses the harmless fake-out gag; tougher
        // bosses roll between the three surprises so the player can't predict it.
        surprise_kind = tier <= 1 ? 0 : rnd_int(0, 2);
    }
    Rect rect() const { return {int(x), int(y), w, h}; }
    float hp_ratio() const { return std::max(0.0f, float(hp) / max_hp); }

    void take_damage(int dmg, ParticleSystem& parts) {
        if (!damageable()) return;
        hp -= dmg; hit_flash = 0.16f;
        if (hp <= 0) { dying = true; die_t = 0; hp = 0; return; }
        // The one-time surprise triggers near a quarter health.
        if (!did_surprise && hp <= int(max_hp * 0.27f)) {
            did_surprise = true;
            if (surprise_kind == 0) { faking = true; fake_t = 0; fake_started = true; }
            else if (surprise_kind == 1) { avalanche_pending = true; }
            else { split_pending = true; }
        }
        (void)parts;
    }

    // ── attack pool: grows with HP-phase; nasty attacks gated by TIER so the
    // first boss stays gentle and later bosses get mean. (mirrors space_invaders) ──
    int pick_attack() {
        std::vector<int> pool = {0, 0, 2};            // BRICK_LOB ×2 (calm staple), AIMED_VOLLEY
        if (phase >= 2) {
            pool.insert(pool.end(), {1, 3});          // BRICK_FAN, BULLET_SPREAD
            if (tier >= 2) pool.insert(pool.end(), {6, 5});  // SHIELD_UP, SUMMON_MINIONS
        }
        if (phase >= 3) {                             // enrage
            pool.push_back(2);                        // weight aimed volleys
            if (tier >= 2) { pool.push_back(4); pool.push_back(7); }  // SLAM, BULLET_RAIN
            if (tier >= 3) pool.push_back(5);         // extra minions on the hardest tiers
        }
        return pool[rnd_int(0, (int)pool.size() - 1)];
    }
    void begin_attack(int a) {
        cur_attack = a; attack_t = 0; sub_t = 99; slam_phase = 0;
        if (a == 6) {  // SHIELD_UP is resolved immediately as a request to the game
            shield_req.pending = true;
            shield_req.rows = (phase >= 3) ? 2 : 1;
            shield_req.hp = 2 + tier / 2;
            cur_attack = -1;
        }
    }

    // make a downward energy pellet (paddle must dodge horizontally)
    BossBullet pellet(float bx, float by, float ang, float spd) const {
        return BossBullet(bx, by, std::sin(ang) * spd, std::cos(ang) * spd,
                          lerp_color(color, WHITE, 0.4f));
    }

    void run_attack(float dt, float paddle_cx, int screen_w, int screen_h,
                    std::vector<BossShot>& bricks, std::vector<BossBullet>& bullets) {
        attack_t += dt; sub_t += dt;
        float bx = x + w / 2.0f, by = y + h - 6;
        Color bc = darker(color, 0.8f);
        switch (cur_attack) {
            case 0:  // BRICK_LOB: a few near-straight bricks
                for (float off : {-0.25f, 0.0f, 0.25f})
                    bricks.emplace_back(bx + off * w * 0.5f, by, off * 60.0f, 230.0f, bc, false);
                cur_attack = -1; break;
            case 1: {  // BRICK_FAN: a wall of bricks across the boss with a safe gap
                int n = 7, gap = rnd_int(1, n - 2);
                for (int i = 0; i < n; ++i) {
                    if (std::abs(i - gap) <= 1) continue;          // a 2-wide gap to slip the paddle under
                    float px = x + (i + 0.5f) * (w / float(n));
                    bricks.emplace_back(px, by, 0.0f, 210.0f, bc, false);
                }
                cur_attack = -1; break;
            }
            case 2: {  // AIMED_VOLLEY: bricks lobbed toward the paddle's current x
                float dx = paddle_cx - bx;
                float vx0 = std::max(-160.0f, std::min(160.0f, dx * 0.7f));
                for (int i = -1; i <= 1; ++i)
                    bricks.emplace_back(bx, by, vx0 + i * 40.0f, 235.0f, bc, false);
                cur_attack = -1; break;
            }
            case 3: {  // BULLET_SPREAD: downward pellet fan
                for (int i = -2; i <= 2; ++i)
                    bullets.push_back(pellet(bx, by, i * 0.22f, 300.0f));
                cur_attack = -1; break;
            }
            case 4: {  // SLAM: wind up, dive toward the paddle row, retreat
                // never dive so far the body could reach the paddle zone (it's a
                // threat via the shockwave, not body contact) — leave a safe band.
                float target = std::min(battle_y + 320.0f, float(screen_h) - 150 - h);
                if (slam_phase == 0) { if (attack_t >= 0.4f) slam_phase = 1; }
                else if (slam_phase == 1) { y += 760 * dt; if (y >= target) { y = target; slam_phase = 2; sub_t = 0; } }
                else if (slam_phase == 2) {  // shockwave of bricks left+right, leaving the centre clear
                    for (int i = 0; i < 6; ++i) {
                        float side = (i < 3 ? -1.0f : 1.0f);
                        bricks.emplace_back(bx + side * (40 + (i % 3) * 50), y + h - 6,
                                            side * (90 + (i % 3) * 40), 120.0f, bc, false);
                    }
                    slam_phase = 3;
                } else { y -= 600 * dt; if (y <= battle_y) { y = battle_y; cur_attack = -1; } }
                break;
            }
            case 5: {  // SUMMON_MINIONS: weaving brick-minions drift down
                int n = 2 + (phase >= 3 ? 1 : 0);
                for (int i = 0; i < n; ++i) {
                    float px = x + rnd_uniform(w * 0.2f, w * 0.8f);
                    bricks.emplace_back(px, by, rnd_uniform(-50, 50), 110.0f,
                                        lerp_color(color, WHITE, 0.2f), true);
                }
                cur_attack = -1; break;
            }
            case 7:  // BULLET_RAIN: sustained pellets from random points across the boss
                if (sub_t >= 0.12f) {
                    sub_t = 0;
                    bullets.push_back(pellet(rnd_uniform(x + 10, x + w - 10), by, 0.0f, 270.0f));
                }
                if (attack_t >= 1.4f) cur_attack = -1;
                break;
            default: cur_attack = -1; break;
        }
        (void)screen_w;
    }

    // Steps the boss; appends thrown bricks / pellets. The game reads flags for
    // reveal / surprise banners and resolves shield/avalanche/split requests.
    void update(float dt, int screen_w, int screen_h, float paddle_cx,
                std::vector<BossShot>& bricks, std::vector<BossBullet>& bullets) {
        bob += dt;
        if (entering) {  // lumber down into the arena in heavy steps
            enter_t += dt;
            if (enter_t >= 0.22f) {
                enter_t = 0; y += 26; just_stomped = true;
                if (y >= battle_y) { y = battle_y; entering = false; invuln_ = 1.5f; }
            }
            return;
        }
        if (hit_flash > 0) hit_flash -= dt * 6;
        if (invuln_ > 0) invuln_ -= dt;
        if (dying) {
            die_t += dt;
            if (die_t >= DEATH_DUR) finished = true;
            return;
        }
        if (faking) {  // deflate, sulk, bounce back at half health
            fake_t += dt;
            if (fake_t >= 1.1f) { faking = false; hp = int(max_hp * 0.5f); }
            return;
        }

        // ── phases: faster & nastier as health drops ──
        phase = hp_ratio() > 0.6f ? 1 : hp_ratio() > 0.3f ? 2 : 3;
        float aggr = (tier <= 1) ? 0.9f : 0.6f;   // the first boss stays calm even at low HP
        float iv = atk_iv * (phase == 3 ? aggr : phase == 2 ? 0.85f : 1.0f);

        // weak-point glows open on a slow cycle once aggressive; hitting it does bonus dmg
        if (phase >= 2) {
            weak_t += dt;
            if (!weak_open && weak_t >= 2.2f) { weak_open = true; weak_t = 0; weak_x = rnd_uniform(0.25f, 0.75f); }
            else if (weak_open && weak_t >= 2.0f) { weak_open = false; weak_t = 0; }
        } else { weak_open = false; }

        // lumber side to side (faster when enraged)
        float move = vx * (phase == 3 ? 1.35f : 1.0f);
        x += move * dt;
        if (x < 6) { x = 6; vx = std::fabs(vx); }
        if (x + w > screen_w - 6) { x = screen_w - 6 - w; vx = -std::fabs(vx); }

        if (telegraph > 0) {                 // winding up the next attack (readable warning ring)
            telegraph -= dt;
            if (telegraph <= 0) begin_attack(pending_attack);
        } else if (cur_attack >= 0) {        // a sustained attack is running
            run_attack(dt, paddle_cx, screen_w, screen_h, bricks, bullets);
        } else {                             // idle: pick the next attack after a beat
            atk_t += dt;
            if (atk_t >= iv) {
                atk_t = 0;
                pending_attack = pick_attack();
                telegraph = tier <= 1 ? 0.7f : (phase == 3 ? 0.35f : 0.55f);
            }
        }
        (void)screen_h;
    }

    void draw_eye(Surface& s, int ex, int ey, int er, float tcx, float tcy) const {
        draw::circle(s, WHITE, ex, ey, er);
        float dx = tcx - ex, dy = tcy - ey, d = std::max(1.0f, std::hypot(dx, dy));
        int off = er / 2;
        int px = ex + int(dx / d * off), py = ey + int(dy / d * off);
        if (hit_flash > 0.07f) {  // dazed cross-eyes when freshly smacked
            draw::line(s, BLACK, ex - er / 2, ey - er / 2, ex + er / 2, ey + er / 2, 3);
            draw::line(s, BLACK, ex - er / 2, ey + er / 2, ex + er / 2, ey - er / 2, 3);
        } else {
            draw::circle(s, BLACK, px, py, std::max(2, er / 2));
        }
    }

    void draw(Surface& s, float tcx, float tcy) const {
        if (y + h < 0) return;
        float sc = dying ? std::max(0.0f, 1 - die_t / DEATH_DUR)
                         : faking ? 0.6f : 1.0f;
        int bw = int(w * sc), bh = int(h * sc);
        int bx = int(x + (w - bw) / 2), by = int(y + (h - bh) / 2 + std::sin(bob * 2) * 4);
        if (bw < 4 || bh < 4) return;
        Color body = hit_flash > 0 ? lerp_color(color, WHITE, 0.6f) : color;
        // enrage tints the body redder so the phase reads at a glance
        if (!dying && !faking && phase == 3) body = lerp_color(body, RED, 0.25f);
        glow_rect(s, {bx, by, bw, bh}, body, 8, 60);
        // a GIANT INVADER built from bricks (shared pixel-alien art, brick texture)
        draw_boss_alien(s, (tier - 1) % 4, int(bob * 3) % 2, bx, by, bw, bh, body, true);
        if (invuln_ > 0) {  // spawn shield shimmer: not yet damageable
            float p = 0.5f + 0.5f * std::sin(bob * 18);
            draw::rect(s, Color(120, 200, 255, int(120 * p)), {bx - 8, by - 8, bw + 16, bh + 16}, 3, 10);
        }
        if (telegraph > 0 && !dying && !faking) {  // wind-up warning ring (red, pulsing)
            float p = 0.4f + 0.6f * std::sin(bob * 30);
            draw::rect(s, Color(255, 60, 60, int(150 * p)), {bx - 7, by - 7, bw + 14, bh + 14}, 3, 12);
        }
        // weak-point brick: a bright pulsing panel on the face
        if (weak_open && damageable()) {
            Rect wr = weak_rect();
            float p = 0.5f + 0.5f * std::sin(bob * 14);
            glow_rect(s, wr, YELLOW, 6, int(120 * p));
            draw::rect(s, lerp_color(YELLOW, WHITE, p * 0.5f), wr, 0, 3);
            draw::rect(s, RED, wr, 2, 3);
        }

        int er = std::max(7, bh / 8);
        int eyl = bx + bw / 3, eyr = bx + 2 * bw / 3, eyy = by + int(bh * 0.34f);
        if (dying || faking) {  // o_o shocked face
            draw::circle(s, WHITE, eyl, eyy, er);
            draw::circle(s, WHITE, eyr, eyy, er);
            draw::circle(s, BLACK, eyl, eyy, std::max(2, er / 2));
            draw::circle(s, BLACK, eyr, eyy, std::max(2, er / 2));
            draw::circle(s, BLACK, bx + bw / 2, by + int(bh * 0.7f), bh / 8);
            return;
        }
        draw_eye(s, eyl, eyy, er, tcx, tcy);
        draw_eye(s, eyr, eyy, er, tcx, tcy);
        // mouth reacts to health phase
        int mx = bx + bw / 2, my = by + int(bh * 0.7f), mw = bw / 3;
        if (phase == 1) {  // smug grin
            for (int i = -mw; i <= mw; i += 4)
                draw::circle(s, BLACK, mx + i, my + int(std::sin((i + mw) / float(2 * mw) * PI) * 8), 3);
        } else if (phase == 2) {  // flat / nervous
            draw::rect(s, BLACK, {mx - mw, my - 2, mw * 2, 5}, 0, 2);
        } else {  // angry zigzag (enrage)
            int px = mx - mw, py = my;
            for (int i = 0; i <= 6; ++i) {
                int nx = mx - mw + i * (mw * 2 / 6), ny = my + ((i % 2) ? 7 : -7);
                draw::line(s, BLACK, px, py, nx, ny, 3);
                px = nx; py = ny;
            }
        }
    }
};

// ── Game ──────────────────────────────────────────────────────────────────────
enum class St { Ready, Play, Dead, Win, GameOver, Victory };

class ArkanoidGame : public Scene {
public:
    explicit ArkanoidGame(const Context& ctx)
        : Scene(ctx), pause_(ctx.W, ctx.H, ctx.fonts),
          initials_(ctx.W, ctx.H, ctx.fonts) {
        high_score_ = ctx.scores ? ctx.scores->high_score("arkanoid") : 0;
        new_game();
        if (ctx_.sfx) ctx_.sfx->start_ambient(2);  // calm/warm
    }

    void update(float dt, const Input& in) override;
    void draw(Surface& s) override;
    void on_exit() override { submit_scores(); if (ctx_.sfx) ctx_.sfx->stop_ambient(); }

private:
    static constexpr float AMBIENT_STAR_T = 0.2f;
    PauseMenu pause_;
    InitialsEntry initials_;
    bool entry_started_ = false;
    ParticleSystem particles_;
    int high_score_ = 0;
    float star_t_ = 0;
    std::vector<Flash> flash_;
    bool submitted_ = false;

    int score_, lives_, level_idx_;
    St state_;
    float dead_t_ = 0, win_t_ = 0;

    std::unique_ptr<Paddle> paddle_;
    std::vector<Ball> balls_;
    std::vector<Brick> bricks_;
    std::vector<Well> wells_;              // gravity/wind wells (later levels)
    std::vector<PowerUp> powerups_;
    std::vector<Laser> lasers_;
    // power-up timers: insertion-ordered (kind, seconds remaining).
    std::vector<std::pair<std::string, float>> pw_timers_;

    // ── progression + boss + juice + telemetry ──
    std::unique_ptr<Boss> boss_;
    std::unique_ptr<CameoInvader> cameo_;
    std::vector<BossShot> boss_shots_;
    std::vector<BossBullet> boss_bullets_;
    bool boss_level_ = false, boss_revealed_ = false;
    float base_speed_ = Ball::BASE_SPEED;  // climbs each endless loop
    float game_t_ = 0, level_t0_ = 0;      // accumulating game clock
    int lvl_bricks_ = 0, lvl_balls_lost_ = 0, lvl_powerups_ = 0;  // per-level counters
    float white_flash_ = 0;                // full-screen pop on boss death / clear

    void tele(const J& j) { if (ctx_.tele) ctx_.tele->event(j); }
    // displayed level number (1-based). Boss every 5th level.
    int level_num() const { return level_idx_ + 1; }
    bool is_boss_level(int n) const { return n % 5 == 0; }

    void new_game() {
        score_ = 0;
        lives_ = 3;
        level_idx_ = 0;
        entry_started_ = false;
        particles_.clear();
        flash_.clear();
        submitted_ = false;
        game_t_ = 0;
        int start = 0;
        bot_ = std::getenv("VOID_BOT") != nullptr || ctx_.demo;
        if (const char* e = std::getenv("VOID_LEVEL")) start = std::max(0, atoi(e) - 1);
        level_idx_ = start;
        load_level();
    }
    void load_level() {
        int n = level_num();
        boss_level_ = is_boss_level(n);
        boss_revealed_ = false;
        boss_.reset();
        cameo_.reset();
        boss_shots_.clear();
        boss_bullets_.clear();
        int N = (int)LEVELS.size();
        // Count how many non-boss levels precede this one. The 6 hand-made layouts
        // map onto the first 6 non-boss levels (so the level-5 boss doesn't eat a
        // hand-made layout); afterwards we generate procedurally.
        int non_boss_idx = 0;
        for (int i = 1; i < n; ++i) if (!is_boss_level(i)) non_boss_idx++;
        // ball base speed climbs once the hand-made layouts are exhausted (endless
        // loops): each batch of N non-boss levels past the set adds to the floor.
        int loop = non_boss_idx < N ? 0 : (non_boss_idx - N) / N + 1;
        base_speed_ = Ball::BASE_SPEED + std::min(160.0f, loop * 22.0f);

        paddle_ = std::make_unique<Paddle>(ctx_.W, ctx_.H);
        balls_.clear();
        bricks_.clear();
        wells_.clear();
        powerups_.clear();
        lasers_.clear();
        state_ = St::Ready;
        dead_t_ = 0;
        win_t_ = 0;
        pw_timers_.clear();

        if (boss_level_) {
            boss_ = std::make_unique<Boss>(n, ctx_.W);
            // a light scattering of bricks for cover/extra points during the fight
            build_boss_decor();
        } else if (non_boss_idx < N) {
            build_bricks(LEVELS[non_boss_idx]);  // hand-made layouts, in order
        } else {
            build_procedural(loop);  // endless escalation past the hand-made set
        }
        attach_ball();

        // Cross-game cameo: an invader sometimes crashes a normal level.
        if (!boss_level_ && (std::getenv("VOID_FORCE_CAMEO") || (n >= 3 && rnd01() < 0.22f))) {
            cameo_ = std::make_unique<CameoInvader>(ctx_.W, n);
            add_flash("⚠  INVADER INCURSION  ⚠", ctx_.W / 2, ctx_.H / 2 - 30, RED, 2.0f);
            if (ctx_.sfx) ctx_.sfx->life_lost();
            if (ctx_.fx) ctx_.fx->shake(8);
        }

        // Palette drift: slow per-level hue oscillation; boss levels tint warm.
        if (ctx_.fx)
            ctx_.fx->palette_hue = 0.18f * std::sin(n * 0.5f) + (boss_level_ ? -0.12f : 0.0f);

        level_t0_ = game_t_;
        lvl_bricks_ = lvl_balls_lost_ = lvl_powerups_ = 0;
        tele(J().add("ev", "level_start").add("level", n)
                .add("boss", boss_level_ ? 1 : 0).add("t", (double)game_t_)
                .add("lives", lives_).add("base_speed", (double)base_speed_));
    }
    // A handful of indestructible-ish bricks so the boss arena isn't empty.
    void build_boss_decor() {
        int total_w = BRICK_COLS * (BRICK_W + BRICK_GAP) - BRICK_GAP;
        int start_x = (ctx_.W - total_w) / 2;
        int ri = 6;  // a low band, below the boss, above the paddle
        for (int ci = 1; ci < BRICK_COLS - 1; ci += 3) {
            int x = start_x + ci * (BRICK_W + BRICK_GAP);
            int y = BRICK_TOP + ri * (BRICK_H + BRICK_GAP) + 80;
            bricks_.emplace_back(ci, ri, x, y, 2);
        }
    }
    // SHIELD_UP attack: drop a band of real bricks just below the boss so the ball
    // has to chew through them before it can reach the boss again. Spawned inside
    // the boss's horizontal span; respects the screen brick grid for tidy alignment.
    void spawn_boss_shield(int rows, int hp) {
        if (!boss_) return;
        int total_w = BRICK_COLS * (BRICK_W + BRICK_GAP) - BRICK_GAP;
        int start_x = (ctx_.W - total_w) / 2;
        int band_y = int(boss_->y + boss_->h + 14);
        for (int r = 0; r < rows; ++r) {
            int yy = band_y + r * (BRICK_H + BRICK_GAP);
            for (int ci = 0; ci < BRICK_COLS; ++ci) {
                int bxp = start_x + ci * (BRICK_W + BRICK_GAP);
                // only under the boss body, and leave one open column so the ball
                // can still find a route up (keeps it fair)
                if (bxp + BRICK_W < boss_->x || bxp > boss_->x + boss_->w) continue;
                if (ci % 4 == 2) continue;  // a periodic gap
                bricks_.emplace_back(ci, 20 + r, bxp, yy, hp);
            }
        }
    }
    // Procedurally generated layout for endless play past the hand-made set.
    // Difficulty climbs with the loop count: more rows, more multi-hit and
    // indestructible bricks.
    void build_procedural(int loops) {
        if (loops < 1) loops = 1;
        int total_w = BRICK_COLS * (BRICK_W + BRICK_GAP) - BRICK_GAP;
        int start_x = (ctx_.W - total_w) / 2;
        int rows = std::min(13, 7 + loops);                 // grows, capped to fit
        int max_hp = std::min(6, 3 + loops);                // tougher bricks appear
        float silver_p = std::min(0.18f, 0.04f * loops);    // some indestructibles
        float gap_p = 0.18f;                                // holes for variety
        for (int ri = 0; ri < rows; ++ri) {
            for (int ci = 0; ci < BRICK_COLS; ++ci) {
                if (rnd01() < gap_p) continue;
                int hp;
                if (rnd01() < silver_p) hp = 9;              // indestructible
                else hp = rnd_int(1, max_hp);
                // top rows skew tougher
                if (ri < rows / 3 && hp != 9) hp = std::min(max_hp, hp + 1);
                int x = start_x + ci * (BRICK_W + BRICK_GAP);
                int y = BRICK_TOP + ri * (BRICK_H + BRICK_GAP);
                bricks_.emplace_back(ci, ri, x, y, hp);
            }
        }
        // safety: never produce a layout with no destructible bricks (would clear
        // instantly or stall on indestructibles).
        bool any = false;
        for (auto& b : bricks_) if (!b.indestructible()) { any = true; break; }
        if (!any) {
            int y = BRICK_TOP + (rows / 2) * (BRICK_H + BRICK_GAP);
            for (int ci = 2; ci < BRICK_COLS - 2; ++ci)
                bricks_.emplace_back(ci, rows / 2, start_x + ci * (BRICK_W + BRICK_GAP), y, 1);
        }

        // ── MOVING BRICKS: from the 2nd endless loop, a couple of lower rows drift
        // sideways so the layout breathes. Indestructibles stay put (anchors). ──
        if (loops >= 2) {
            float chance = std::min(0.35f, 0.12f * loops);
            for (auto& b : bricks_) {
                if (b.indestructible()) continue;
                if (b.row < rows / 2) continue;          // only lower half drifts
                if (rnd01() < chance) {
                    float spd = rnd_uniform(40, 70 + loops * 6) * (rnd01() < 0.5f ? 1 : -1);
                    b.make_moving(spd);
                }
            }
        }

        // ── GRAVITY/WIND WELLS: from the 3rd endless loop, scatter one or two field
        // sources in the open mid-field. Subtle steering keeps the ball controllable
        // while bending its path past the bricks. ──
        if (loops >= 3) {
            int n = (loops >= 5) ? 2 : 1;
            for (int i = 0; i < n; ++i) {
                float wx = rnd_uniform(ctx_.W * 0.2f, ctx_.W * 0.8f);
                float wy = rnd_uniform(BRICK_TOP + rows * (BRICK_H + BRICK_GAP) + 40,
                                       ctx_.H * 0.66f);
                bool attract = rnd01() < 0.5f;
                float rad = rnd_uniform(110, 150);
                // strength is an acceleration; kept low so it only bends the path.
                float str = rnd_uniform(360, 520);
                wells_.emplace_back(wx, wy, rad, str, attract);
            }
        }
    }
    void build_bricks(const Layout& layout) {
        int total_w = BRICK_COLS * (BRICK_W + BRICK_GAP) - BRICK_GAP;
        int start_x = (ctx_.W - total_w) / 2;
        for (int ri = 0; ri < (int)layout.size(); ++ri)
            for (int ci = 0; ci < (int)layout[ri].size(); ++ci) {
                int hp = layout[ri][ci];
                if (hp == 0) continue;
                int x = start_x + ci * (BRICK_W + BRICK_GAP);
                int y = BRICK_TOP + ri * (BRICK_H + BRICK_GAP);
                bricks_.emplace_back(ci, ri, x, y, hp);
            }
    }
    void attach_ball() {
        // Launch at ~30° off vertical (random side) so the ball immediately drifts
        // sideways — a straight-up launch just bounces in place with no input.
        float off = PI * (0.15f + rnd01() * 0.06f);   // ~27-38° from vertical
        if (rnd01() < 0.5f) off = -off;
        Ball b(paddle_->center_x(), paddle_->y - 8, true, -PI / 2 + off);
        b.attached = true;
        b.attach_offset = paddle_->w / 2.0f;
        b.base_speed = base_speed_;
        balls_.push_back(b);
    }
    int destructible_count() const {
        int n = 0;
        for (auto& b : bricks_) if (b.alive && !b.indestructible()) ++n;
        return n;
    }
    // ── juggle bonus: count balls actually in play (not attached) ──
    int active_ball_count() const {
        int n = 0;
        for (auto& b : balls_) if (b.alive && !b.attached) ++n;
        return n;
    }
    // Score multiplier for keeping multiple balls alive: +1x per extra ball,
    // capped, so 2 balls→x2, 3→x3, 4+→x4. Returns 1 with a single ball.
    int juggle_mult() const {
        return std::min(JUGGLE_MAX, std::max(1, active_ball_count()));
    }
    static constexpr int JUGGLE_MAX = 4;
    void add_flash(const std::string& t, float x, float y, Color c, float life = 0.8f) {
        flash_.push_back({t, x, y, life, c});
    }
    void submit_scores() {
        if (ctx_.demo) return;  // attract demo never touches the leaderboard
        if (submitted_) return;
        submitted_ = true;
        tele(J().add("ev", "game_over").add("level", level_num()).add("score", score_)
                .add("t", (double)game_t_));
        if (ctx_.scores) {
            bool new_hi = ctx_.scores->submit_arkanoid(score_, level_idx_ + 1);
            if (new_hi)
                add_flash("NEW HI-SCORE!", ctx_.W / 2, ctx_.H / 2 - 40, YELLOW, 2.5f);
            high_score_ = ctx_.scores->high_score("arkanoid");
        }
    }

    float* pw_timer(const std::string& k) {
        for (auto& p : pw_timers_) if (p.first == k) return &p.second;
        return nullptr;
    }
    void set_pw_timer(const std::string& k, float t) {
        if (float* p = pw_timer(k)) { *p = t; return; }
        pw_timers_.emplace_back(k, t);
    }
    void erase_pw_timer(const std::string& k) {
        pw_timers_.erase(std::remove_if(pw_timers_.begin(), pw_timers_.end(),
                         [&](const auto& p) { return p.first == k; }),
                         pw_timers_.end());
    }

    void update_ready(float dt, const Input& in);
    void update_play(float dt, const Input& in);
    float paddle_dir(const Input& in);
    bool bot_ = false;
    void reflect_ball_off_brick(Ball& ball, Rect br);
    void do_smash(float charge);
    void collect_powerup(const PowerUp& pw);
    void deactivate_powerup(const std::string& k);
    void deactivate_all_powerups();
    void lose_life();
    void win_level();

    void draw_hud(Surface& s);
    void draw_powerup_timers(Surface& s);
    void draw_flash(Surface& s);
};

void ArkanoidGame::update(float dt, const Input& in) {
    // pause handling
    for (auto k : in.pressed)
        if (k == SDLK_p && (state_ == St::Play || state_ == St::Ready))
            pause_.toggle();
    if (pause_.active()) {
        if (pause_.update(in) == PauseResult::QuitToMenu) {
            submit_scores();
            quit_to_menu = true;
        }
        return;
    }
    for (auto k : in.pressed)
        if (k == SDLK_ESCAPE) { quit_to_menu = true; return; }

    game_t_ += dt;
    if (white_flash_ > 0) white_flash_ -= dt * 2.0f;
    star_t_ += dt;
    if (star_t_ >= AMBIENT_STAR_T) { star_t_ = 0; particles_.ambient_star(ctx_.W, ctx_.H); }
    particles_.update(dt);
    for (auto& f : flash_) f.life -= dt;
    flash_.erase(std::remove_if(flash_.begin(), flash_.end(),
                 [](const Flash& f) { return f.life <= 0; }), flash_.end());

    // tick power-up timers
    for (size_t i = 0; i < pw_timers_.size();) {
        pw_timers_[i].second -= dt;
        if (pw_timers_[i].second <= 0) {
            std::string k = pw_timers_[i].first;
            deactivate_powerup(k);
            pw_timers_.erase(pw_timers_.begin() + i);
        } else {
            ++i;
        }
    }

    if (state_ == St::Ready) {
        update_ready(dt, in);
    } else if (state_ == St::Play) {
        update_play(dt, in);
    } else if (state_ == St::Dead) {
        dead_t_ -= dt;
        if (dead_t_ <= 0) {
            if (lives_ > 0) { attach_ball(); state_ = St::Ready; }
            else { state_ = St::GameOver; submit_scores(); }
        }
    } else if (state_ == St::Win) {
        win_t_ -= dt;
        if (win_t_ <= 0) {
            level_idx_ += 1;     // endless: keep going past the hand-made levels
            load_level();
        }
    } else if (state_ == St::GameOver || state_ == St::Victory) {
        if (bot_) { new_game(); return; }  // self-play loops for data
        if (ctx_.scores) {
            const std::string gid = "arkanoid";
            if (!entry_started_ && ctx_.scores->qualifies(gid, score_)) {
                entry_started_ = true;
                int rank = 1;
                for (auto& e : ctx_.scores->leaderboard(gid)) if (e.score > score_) rank++;
                initials_.begin("VOID BREAKER", score_, rank);
            }
            if (initials_.active()) {
                if (initials_.update(in)) ctx_.scores->add_score(gid, initials_.name(), score_);
                return;   // block retry while entering initials
            }
        }
        for (auto k : in.pressed)
            if (k == SDLK_RETURN || k == SDLK_SPACE) new_game();
    }
}

float ArkanoidGame::paddle_dir(const Input& in) {
    if (bot_) {
        float target = paddle_->center_x(), lowy = -1e9f;
        for (auto& b : balls_) if (b.alive && b.y > lowy) { lowy = b.y; target = b.x; }
        // hit the ball off-centre (oscillating) so it angles and spreads to clear
        target += std::sin(game_t_ * 2.3f) * 26.0f;
        float cx = paddle_->center_x();
        return target > cx + 4 ? 1.0f : target < cx - 4 ? -1.0f : 0.0f;
    }
    return (in.down(SDLK_RIGHT) || in.down(SDLK_d)) - (in.down(SDLK_LEFT) || in.down(SDLK_a));
}

void ArkanoidGame::update_ready(float dt, const Input& in) {
    paddle_->update(dt, paddle_dir(in));
    for (auto& b : balls_) {
        if (b.attached) {
            b.x = paddle_->x + b.attach_offset;
            b.y = paddle_->y - b.r - 1;
        }
    }
    bool launch = bot_;
    for (auto k : in.pressed)
        if (k == SDLK_SPACE || k == SDLK_UP || k == SDLK_w) launch = true;
    if (launch) { for (auto& b : balls_) b.release(); state_ = St::Play; }
}

void ArkanoidGame::update_play(float dt, const Input& in) {
    paddle_->update(dt, paddle_dir(in));

    // laser shoot and catch release (both keyed on SPACE / LCTRL; bot auto-fires)
    bool fire = bot_;
    for (auto k : in.pressed)
        if (k == SDLK_SPACE || k == SDLK_LCTRL) fire = true;
    if (fire) {
        if (paddle_->laser_on) {
            auto new_lasers = paddle_->shoot_laser();
            if (!new_lasers.empty()) {
                for (auto& l : new_lasers) lasers_.push_back(l);
                if (ctx_.sfx) ctx_.sfx->laser_shoot();
            }
        }
        if (paddle_->catch_on)
            for (auto& b : balls_) if (b.attached) b.release();
    }

    // ── SMASH ability: hold LSHIFT to charge, release to launch a nearby ball ──
    // (bot never charges; it just plays normally.)
    if (!bot_) {
        bool held = in.down(SDLK_LSHIFT) || in.down(SDLK_RSHIFT);
        if (held && paddle_->smash_ready()) {
            if (!paddle_->charging) paddle_->charging = true;
            paddle_->smash_charge = std::min(Paddle::SMASH_FULL,
                                             paddle_->smash_charge + dt);
        } else if (paddle_->charging) {
            // released (or cooldown hit): fire if we built any meaningful charge
            float cr = paddle_->charge_ratio();
            paddle_->charging = false;
            paddle_->smash_charge = 0;
            if (cr >= 0.35f) do_smash(cr);
        }
    }

    // ── moving bricks drift + bounce ──
    for (auto& b : bricks_) b.move(dt, ctx_.W);
    // ── gravity/wind wells animate (steering applied per-ball below) ──
    for (auto& w : wells_) w.update(dt);

    // ── cross-game cameo invader (non-boss levels; reuses boss_bullets_) ──
    if (cameo_) {
        float pcx = paddle_->center_x();
        size_t cbb_before = boss_bullets_.size();
        cameo_->update(dt, ctx_.W, pcx, bricks_, boss_bullets_, particles_);
        if (boss_bullets_.size() > cbb_before && ctx_.sfx) {  // the invader fires
            ctx_.sfx->pan_next((cameo_->x + cameo_->w / 2) / float(ctx_.W));
            ctx_.sfx->enemy_shot();
        }
        for (auto& bb : boss_bullets_) bb.update(dt, ctx_.W, ctx_.H);
        for (auto& bb : boss_bullets_)
            if (bb.alive && overlap(bb.rect(), paddle_->rect())) {
                bb.alive = false;
                particles_.explode_brick(int(bb.x), int(bb.y), bb.color, 12);
                lose_life();
                return;
            }
        boss_bullets_.erase(std::remove_if(boss_bullets_.begin(), boss_bullets_.end(),
                            [](const BossBullet& b) { return !b.alive; }), boss_bullets_.end());
        for (auto& ball : balls_) {
            if (ball.attached || cameo_->dying) continue;
            if (overlap(ball.rect(), cameo_->rect())) {
                bool killed = cameo_->take_hit(ball.fire ? 3 : 1);
                ball.vy = std::fabs(ball.vy);
                ball.y = cameo_->y + cameo_->h + ball.r + 1;
                if (ctx_.sfx) ctx_.sfx->brick_hit();
                if (ctx_.fx) ctx_.fx->shake(3);
                if (killed) {
                    score_ += 800 + level_num() * 40;
                    high_score_ = std::max(high_score_, score_);
                    add_flash("INVADER DOWN!", cameo_->x + cameo_->w / 2, cameo_->y, YELLOW, 1.6f);
                    if (ctx_.sfx) ctx_.sfx->level_clear();
                    if (ctx_.fx) {
                        ctx_.fx->shake(14); ctx_.fx->hitstop(0.05f);
                        ctx_.fx->rumble_explosion();
                        ctx_.fx->shockwave((cameo_->x + cameo_->w / 2) / float(ctx_.W),
                                           (cameo_->y + cameo_->h / 2) / float(ctx_.H));
                    }
                    const char* rain[3] = {"expand", "multi", "life"};
                    for (int i = 0; i < 3; ++i)
                        powerups_.emplace_back(cameo_->x + cameo_->w / 2 + (i - 1) * 56.0f, cameo_->y + 30.0f, std::string(rain[i]));
                }
                break;
            }
        }
        if (!cameo_->alive) cameo_.reset();
    }

    // ── boss update ──
    if (boss_) {
        // The boss aims at the paddle: the player can only dodge horizontally, so
        // the paddle (not the ball) is the threatened target for its attacks.
        float paddle_cx = paddle_->center_x();
        size_t shots_before = boss_shots_.size(), bullets_before = boss_bullets_.size();
        boss_->update(dt, ctx_.W, ctx_.H, paddle_cx, boss_shots_, boss_bullets_);
        // give attacks a voice so the fight is never eerily silent
        if ((boss_shots_.size() > shots_before || boss_bullets_.size() > bullets_before) && ctx_.sfx) {
            ctx_.sfx->pan_next((boss_->x + boss_->w / 2) / float(ctx_.W));
            ctx_.sfx->enemy_shot();
        }
        if (boss_->just_stomped) {
            boss_->just_stomped = false;
            if (!boss_revealed_) {
                boss_revealed_ = true;
                add_flash("!!  " + boss_->name + "  !!", ctx_.W / 2, 150, RED, 1.8f);
                if (ctx_.sfx) ctx_.sfx->life_lost();  // ominous thud on reveal
            }
            for (int i = 0; i < 5; ++i)
                particles_.bunker_hit(boss_->x + rnd_uniform(0, boss_->w), boss_->y + boss_->h);
            if (ctx_.fx) ctx_.fx->shake(8);
        }
        if (boss_->fake_started) {
            boss_->fake_started = false;
            add_flash("...JUST KIDDING!", boss_->x + boss_->w / 2, boss_->y - 10, YELLOW, 1.6f);
        }
        // ── SHIELD_UP: materialise a band of real bricks in front of the boss ──
        if (boss_->shield_req.pending) {
            boss_->shield_req.pending = false;
            spawn_boss_shield(boss_->shield_req.rows, boss_->shield_req.hp);
            add_flash("SHIELDS UP!", boss_->x + boss_->w / 2, boss_->y + boss_->h + 6, CYAN, 1.2f);
            if (ctx_.sfx) ctx_.sfx->brick_hit();
        }
        // ── SURPRISE: brick avalanche — a screen-wide curtain of bricks rains down ──
        if (boss_->avalanche_pending) {
            boss_->avalanche_pending = false;
            int gap = rnd_int(2, BRICK_COLS - 3);  // one safe lane to escape through
            for (int ci = 0; ci < BRICK_COLS; ++ci) {
                if (std::abs(ci - gap) <= 1) continue;
                float px = (ci + 0.5f) * (ctx_.W / float(BRICK_COLS));
                boss_shots_.emplace_back(px, -rnd_uniform(0, 120), 0.0f,
                                         150.0f, darker(boss_->color, 0.7f), false);
            }
            add_flash("AVALANCHE!!", ctx_.W / 2, ctx_.H / 2 - 40, RED, 2.0f);
            if (ctx_.sfx) ctx_.sfx->life_lost();
            if (ctx_.fx) ctx_.fx->shake(14);
        }
        // ── SURPRISE: the boss SPLITS into two weaving brick-minions and keeps fighting ──
        if (boss_->split_pending) {
            boss_->split_pending = false;
            for (int i = 0; i < (boss_->tier >= 3 ? 4 : 3); ++i) {
                float px = boss_->x + rnd_uniform(0, boss_->w);
                boss_shots_.emplace_back(px, boss_->y + boss_->h * 0.5f,
                                         rnd_uniform(-60, 60), 90.0f,
                                         lerp_color(boss_->color, WHITE, 0.2f), true);
            }
            add_flash("IT SPLIT!!", boss_->x + boss_->w / 2, boss_->y - 12, PURPLE, 1.8f);
            if (ctx_.sfx) ctx_.sfx->boss_explosion();
            if (ctx_.fx) ctx_.fx->shake(12);
        }
        for (auto& bs : boss_shots_) bs.update(dt, ctx_.W, ctx_.H);
        for (auto& bb : boss_bullets_) bb.update(dt, ctx_.W, ctx_.H);
    }

    // update balls
    int lost_count = 0;
    for (size_t bi = 0; bi < balls_.size(); ++bi) {
        Ball& ball = balls_[bi];
        if (!ball.alive) continue;
        // gravity/wind wells nudge the velocity (steer), then renormalise so the
        // well bends the path without altering the ball's tier speed.
        if (!ball.attached && !wells_.empty()) {
            float spd = ball.speed();
            for (auto& w : wells_) w.steer(ball.x, ball.y, ball.vx, ball.vy, dt);
            if (spd > 0) ball.set_speed(spd);  // direction changed, speed preserved
        }
        int result = ball.update(dt, ctx_.W, ctx_.H, *paddle_);
        if (result == 1 && ctx_.sfx) ctx_.sfx->ball_wall();
        if (result == 2) { lost_count += 1; continue; }

        particles_.ball_trail(int(ball.x), int(ball.y), ball.color);

        // paddle collision
        if (!ball.attached && ball.vy > 0 && overlap(ball.rect(), paddle_->rect())) {
            if (paddle_->catch_on) {
                ball.attached = true;
                ball.attach_offset = ball.x - paddle_->x;
            } else {
                ball.bounce_paddle(*paddle_);
                particles_.paddle_hit(int(ball.x), int(paddle_->y));
                if (ctx_.sfx) ctx_.sfx->ball_paddle();
            }
        }

        // brick collision (one brick per frame per ball, unless on fire)
        for (auto& brick : bricks_) {
            if (!brick.alive) continue;
            Rect br = brick.rect();
            if (!overlap(ball.rect(), br)) continue;

            // a fire/mega ball smashes destructible bricks outright and keeps going
            bool destroyed;
            if (ball.fire && !brick.indestructible()) {
                brick.hp = 0; brick.alive = false; destroyed = true;
            } else {
                destroyed = brick.hit();
            }
            int cx = br.x + br.w / 2, cy = br.y + br.h / 2;
            ball.on_brick_hit();

            if (destroyed) {
                particles_.explode_brick(cx, cy, brick_color(1));
                lvl_bricks_++;
                int jm = juggle_mult();
                int pts = brick.max_hp * 10 * (ctx_.combo ? ctx_.combo->hit() : 1) * jm;
                score_ += pts;
                high_score_ = std::max(high_score_, score_);
                add_flash("+" + std::to_string(pts), cx, cy, YELLOW);
                if (ctx_.sfx) ctx_.sfx->brick_break();
                if (!brick.indestructible() && rnd01() < POWERUP_CHANCE) {
                    std::string kind = weighted_powerup();
                    powerups_.emplace_back(float(cx), float(cy), kind);
                }
            } else {
                if (ctx_.sfx) ctx_.sfx->brick_hit();
            }

            // fire balls plow through destructible bricks (no bounce, keep scanning);
            // indestructibles still stop them.
            if (ball.fire && destroyed) continue;
            reflect_ball_off_brick(ball, br);
            break;
        }

        // ── ball vs boss ──
        if (boss_ && boss_->damageable() && overlap(ball.rect(), boss_->rect())) {
            int dmg = ball.fire ? 3 : 1;
            // weak-point brick: a lit panel takes a big bonus when struck
            bool weak_hit = boss_->weak_open && overlap(ball.rect(), boss_->weak_rect());
            if (weak_hit) dmg += 4;
            boss_->take_damage(dmg, particles_);
            score_ += (weak_hit ? 40 : 15) * dmg;
            high_score_ = std::max(high_score_, score_);
            particles_.explode_brick(int(ball.x), int(ball.y),
                                     weak_hit ? YELLOW : boss_->color, weak_hit ? 16 : 8);
            if (weak_hit) {
                add_flash("WEAK POINT!", int(ball.x), int(ball.y) - 14, YELLOW, 1.0f);
                boss_->weak_open = false; boss_->weak_t = 0;
            }
            if (ctx_.sfx) ctx_.sfx->brick_hit();
            if (ctx_.fx) ctx_.fx->shake(weak_hit ? 6 : 3);
            if (!ball.fire) {  // bounce off the boss body (treat it as a big brick)
                // ball arrives travelling up into the boss; send it back down and
                // park it just below the boss so it can't tunnel through.
                ball.vy = std::abs(ball.vy);
                ball.y = boss_->y + boss_->h + ball.r + 1;
            }
        }
    }

    // remove dead balls (keep attached)
    balls_.erase(std::remove_if(balls_.begin(), balls_.end(),
                 [](const Ball& b) { return !(b.alive || b.attached); }), balls_.end());

    bool any_left = false;
    for (auto& b : balls_) if (b.alive || b.attached) { any_left = true; break; }
    if (lost_count > 0 && !any_left) {
        lose_life();
        return;
    }

    // ── boss shots vs paddle / balls ──
    if (boss_) {
        for (auto& bs : boss_shots_) {
            if (!bs.alive) continue;
            // a boss brick that reaches the paddle costs a life (like dropping the ball)
            if (overlap(bs.rect(), paddle_->rect())) {
                bs.alive = false;
                particles_.explode_brick(int(bs.x + BossShot::W / 2), int(bs.y), bs.color, 14);
                lose_life();
                return;
            }
            // balls can shatter incoming bricks (and minions) for points
            for (auto& b : balls_) {
                if (!b.alive) continue;
                if (overlap(bs.rect(), b.rect())) {
                    bs.alive = false;
                    particles_.explode_brick(int(bs.x + BossShot::W / 2), int(bs.y), bs.color, 8);
                    if (bs.minion) { score_ += 25; high_score_ = std::max(high_score_, score_); }
                    if (!b.fire) b.vy = -std::abs(b.vy);
                    if (ctx_.sfx) ctx_.sfx->brick_hit();
                    break;
                }
            }
        }
        boss_shots_.erase(std::remove_if(boss_shots_.begin(), boss_shots_.end(),
                          [](const BossShot& s) { return !s.alive; }), boss_shots_.end());

        // ── boss pellets vs paddle / balls (paddle must dodge sideways) ──
        for (auto& bb : boss_bullets_) {
            if (!bb.alive) continue;
            if (overlap(bb.rect(), paddle_->rect())) {
                bb.alive = false;
                particles_.explode_brick(int(bb.x), int(bb.y), bb.color, 12);
                lose_life();
                return;
            }
            // lasers/balls don't shatter pellets — they're meant to be dodged
        }
        boss_bullets_.erase(std::remove_if(boss_bullets_.begin(), boss_bullets_.end(),
                            [](const BossBullet& b) { return !b.alive; }), boss_bullets_.end());

        // ── boss death → comedic deflate, then rain power-ups + clear ──
        if (boss_->finished) {
            float rx = boss_->x + boss_->w / 2, ry = boss_->y + boss_->h / 2;
            for (int i = 0; i < 5; ++i) particles_.ufo_explode(rx + rnd_uniform(-40, 40), ry, 30);
            int bonus = 1000 * (level_num() / 5);
            score_ += bonus;
            high_score_ = std::max(high_score_, score_);
            add_flash(boss_->name + " GOES SPLAT!  +" + std::to_string(bonus),
                      ctx_.W / 2, ctx_.H / 2 - 20, YELLOW, 2.2f);
            // rain a generous spread of power-ups
            const char* rain[] = {"life", "expand", "multi", "fire", "laser", "catch"};
            for (int i = 0; i < 6; ++i)
                powerups_.emplace_back(rx + (i - 2.5f) * 56, ry, std::string(rain[i]));
            white_flash_ = 1.0f;
            if (ctx_.sfx) { ctx_.sfx->big_explosion(); ctx_.sfx->level_clear(); }
            if (ctx_.fx) {
                ctx_.fx->shake(28); ctx_.fx->hitstop(0.10f);
                ctx_.fx->rumble_explosion();
                ctx_.fx->shockwave((boss_->x + boss_->w / 2) / float(ctx_.W),
                                   (boss_->y + boss_->h / 2) / float(ctx_.H));
            }
            tele(J().add("ev", "boss_clear").add("level", level_num())
                    .add("dt", (double)(game_t_ - level_t0_)).add("name", boss_->name)
                    .add("lives", lives_));
            boss_.reset();
            boss_shots_.clear();
            boss_bullets_.clear();
            win_level();
            return;
        }
    }

    // update lasers
    for (auto& laser : lasers_) {
        laser.update(dt);
        for (auto& brick : bricks_) {
            if (!brick.alive) continue;
            if (overlap(laser.rect(), brick.rect())) {
                laser.alive = false;
                bool destroyed = brick.hit();
                for (auto& b : balls_)
                    if (b.alive || b.attached) { b.on_brick_hit(); break; }
                if (destroyed) {
                    Rect r = brick.rect();
                    particles_.explode_brick(r.x + r.w / 2, r.y + r.h / 2, brick_color(1));
                    score_ += brick.max_hp * 10;
                    high_score_ = std::max(high_score_, score_);
                    if (ctx_.sfx) ctx_.sfx->brick_break();
                } else {
                    if (ctx_.sfx) ctx_.sfx->brick_hit();
                }
                break;
            }
        }
    }
    lasers_.erase(std::remove_if(lasers_.begin(), lasers_.end(),
                  [](const Laser& l) { return !l.alive; }), lasers_.end());

    // update power-ups
    for (auto& pw : powerups_) {
        pw.update(dt, ctx_.H);
        if (pw.alive && overlap(pw.rect(), paddle_->rect())) {
            collect_powerup(pw);
            pw.alive = false;
        }
    }
    powerups_.erase(std::remove_if(powerups_.begin(), powerups_.end(),
                    [](const PowerUp& p) { return !p.alive; }), powerups_.end());

    // level clear check (boss levels clear when the boss pops, handled above)
    if (!boss_level_ && destructible_count() == 0) {
        if (ctx_.sfx) ctx_.sfx->level_clear();
        if (ctx_.fx) { ctx_.fx->shake(8); ctx_.fx->hitstop(0.05f); }
        white_flash_ = 0.6f;
        win_level();
    }
}

// Shared: lose a ball/life, with juice + telemetry.
void ArkanoidGame::lose_life() {
    lives_ -= 1;
    lvl_balls_lost_++;
    dead_t_ = 2.0f;
    state_ = St::Dead;
    paddle_->charging = false;
    paddle_->smash_charge = 0;
    deactivate_all_powerups();
    if (ctx_.sfx) ctx_.sfx->life_lost();
    if (ctx_.fx) { ctx_.fx->shake(12); ctx_.fx->hitstop(0.06f); ctx_.fx->rumble_death(); }
    tele(J().add("ev", "life_lost").add("level", level_num()).add("t", (double)game_t_)
            .add("lives", lives_).add("bricks", lvl_bricks_));
}

// Shared: a level was cleared; emit telemetry and enter the Win interlude.
void ArkanoidGame::win_level() {
    state_ = St::Win;
    win_t_ = boss_level_ ? 3.4f : 3.0f;
    tele(J().add("ev", "level_clear").add("level", level_num())
            .add("dt", (double)(game_t_ - level_t0_)).add("boss", boss_level_ ? 1 : 0)
            .add("bricks_broken", lvl_bricks_).add("balls_lost", lvl_balls_lost_)
            .add("powerups", lvl_powerups_).add("score", score_).add("lives", lives_));
}

void ArkanoidGame::reflect_ball_off_brick(Ball& ball, Rect br) {
    int r = ball.r;
    int left = br.x, right = br.x + br.w, top = br.y, bottom = br.y + br.h;
    float mx = left - r, mxx = right + r, my = top - r, myy = bottom + r;
    float px = ball.x - ball.vx * 0.016f;
    float py = ball.y - ball.vy * 0.016f;
    bool was_inside_x = mx <= px && px <= mxx;
    bool was_inside_y = my <= py && py <= myy;

    if (was_inside_y && !was_inside_x) {
        bool push_right = ball.vx < 0;
        ball.vx = -ball.vx;
        ball.x = push_right ? (mxx + 1) : (mx - 1);
    } else if (was_inside_x && !was_inside_y) {
        bool push_down = ball.vy < 0;
        ball.vy = -ball.vy;
        ball.y = push_down ? (myy + 1) : (my - 1);
    } else {
        bool push_right = ball.vx < 0;
        bool push_down = ball.vy < 0;
        ball.vx = -ball.vx;
        ball.vy = -ball.vy;
        ball.x = push_right ? (mxx + 1) : (mx - 1);
        ball.y = push_down ? (myy + 1) : (my - 1);
    }
    ball.x = std::max(float(ball.r), std::min(float(ctx_.W - ball.r), ball.x));
    ball.y = std::max(float(ball.r), std::min(float(ctx_.H - ball.r), ball.y));
}

// SMASH: launch a ball near/on the paddle straight up at high speed with a brief
// pierce (fire) so it rips up through a column. Picks an attached/caught ball
// first, else the ball closest to the paddle and above it. Starts the cooldown.
void ArkanoidGame::do_smash(float charge) {
    paddle_->smash_cd = Paddle::SMASH_CD;
    paddle_->smash_glow = 1.0f;

    // choose target: prefer an attached (caught) ball, else nearest above paddle.
    Ball* target = nullptr;
    float best = 1e18f;
    float pcx = paddle_->center_x(), py = paddle_->y;
    for (auto& b : balls_) {
        if (!(b.alive || b.attached)) continue;
        if (b.attached) { target = &b; break; }
        if (b.y > py + 4) continue;                 // only balls above the paddle
        float d = std::fabs(b.x - pcx) + std::fabs(b.y - py) * 0.4f;
        if (d < best) { best = d; target = &b; }
    }
    if (!target) {  // nothing to launch — still telegraph the whiff softly
        if (ctx_.sfx) ctx_.sfx->ball_wall();
        return;
    }

    // launch it (mostly) straight up; charge biases toward MAX_SPEED. A small
    // sideways nudge follows the paddle's current offset so it isn't dead-vertical.
    target->attached = false;
    float spd = Ball::BASE_SPEED + (Ball::MAX_SPEED - Ball::BASE_SPEED) * (0.55f + 0.45f * charge);
    float rel = (target->x - pcx) / std::max(1.0f, paddle_->w / 2.0f);
    rel = std::max(-0.4f, std::min(0.4f, rel));
    float angle = -PI / 2 + rel * (PI * 0.18f);
    target->vx = std::cos(angle) * spd;
    target->vy = std::sin(angle) * spd;
    target->speed_mult = std::max(target->speed_mult, 1.0f);
    target->fire = true;                            // brief pierce up the column
    set_pw_timer("fire", std::max(charge * 1.4f, pw_timer("fire") ? *pw_timer("fire") : 0.0f));

    particles_.paddle_hit(int(target->x), int(paddle_->y));
    add_flash("SMASH!", int(target->x), int(paddle_->y) - 18, Color(255, 150, 40), 0.8f);
    if (ctx_.sfx) { ctx_.sfx->ball_paddle(); ctx_.sfx->laser_shoot(); }
    if (ctx_.fx) { ctx_.fx->shake(7 + int(6 * charge)); ctx_.fx->hitstop(0.03f); ctx_.fx->rumble(0.5f, 0.6f, 200); }
}

void ArkanoidGame::collect_powerup(const PowerUp& pw) {
    if (ctx_.sfx) ctx_.sfx->powerup_collect();
    if (ctx_.fx) ctx_.fx->rumble_pickup();
    const std::string& k = pw.kind;
    add_flash(pw.label, int(pw.x + PowerUp::W / 2), int(pw.y), pw.color, 1.2f);
    lvl_powerups_++;
    tele(J().add("ev", "powerup").add("level", level_num()).add("t", (double)game_t_)
            .add("kind", k));

    if (k == "expand") {
        paddle_->set_width(PADDLE_W_BIG);
        set_pw_timer("expand", 12.0f);
    } else if (k == "shrink") {
        paddle_->set_width(PADDLE_W_SMALL);
        set_pw_timer("shrink", 8.0f);
    } else if (k == "laser") {
        paddle_->laser_on = true;
        set_pw_timer("laser", 15.0f);
    } else if (k == "multi") {
        std::vector<Ball> new_balls;
        for (auto& b : balls_) {
            if (!b.alive && !b.attached) continue;
            for (float off : {-0.4f, 0.4f}) {
                Ball nb(b.x, b.y);
                float speed = b.speed();
                float angle = std::atan2(b.vy, b.vx) + off;
                nb.vx = std::cos(angle) * speed;
                nb.vy = std::sin(angle) * speed;
                nb.base_speed = b.base_speed;
                nb.speed_mult = b.speed_mult;
                nb.brick_hits = b.brick_hits;
                nb.fire = b.fire;
                new_balls.push_back(nb);
            }
        }
        for (auto& nb : new_balls) balls_.push_back(nb);
    } else if (k == "slow") {
        for (auto& b : balls_) b.speed_mult = 0.6f;
        set_pw_timer("slow", 10.0f);
    } else if (k == "fast") {
        for (auto& b : balls_) b.speed_mult = 1.6f;
        set_pw_timer("fast", 8.0f);
    } else if (k == "life") {
        lives_ += 1;
    } else if (k == "catch") {
        paddle_->catch_on = true;
        set_pw_timer("catch", 12.0f);
    } else if (k == "fire") {
        for (auto& b : balls_) b.fire = true;
        set_pw_timer("fire", 8.0f);
    }
}

void ArkanoidGame::deactivate_powerup(const std::string& k) {
    if (k == "expand" || k == "shrink") {
        paddle_->set_width(PADDLE_W_NORMAL);
    } else if (k == "laser") {
        paddle_->laser_on = false;
        if (!paddle_->catch_on)
            for (auto& b : balls_) if (b.attached) b.release();
    } else if (k == "slow" || k == "fast") {
        for (auto& b : balls_) b.speed_mult = 1.0f;
    } else if (k == "catch") {
        paddle_->catch_on = false;
        for (auto& b : balls_) if (b.attached) b.release();
    } else if (k == "fire") {
        for (auto& b : balls_) b.fire = false;
    }
}

void ArkanoidGame::deactivate_all_powerups() {
    for (auto& p : pw_timers_) deactivate_powerup(p.first);
    pw_timers_.clear();
    paddle_->laser_on = false;
    paddle_->catch_on = false;
    paddle_->set_width(PADDLE_W_NORMAL);
    lasers_.clear();
    powerups_.clear();
}

// ── draw ──────────────────────────────────────────────────────────────────────
void ArkanoidGame::draw(Surface& s) {
    s.fill(BLACK);
    for (int x = 0; x < ctx_.W; x += 40) draw::line(s, Color(5, 10, 20), x, 0, x, ctx_.H, 1);
    for (int y = 0; y < ctx_.H; y += 40) draw::line(s, Color(5, 10, 20), 0, y, ctx_.W, y, 1);

    particles_.draw(s);

    for (auto& w : wells_) w.draw(s);
    for (auto& brick : bricks_) brick.draw(s);
    if (boss_) {
        // googly eyes track the nearest live ball (else the paddle)
        float tcx = paddle_->center_x(), tcy = paddle_->y;
        for (auto& b : balls_) if (b.alive) { tcx = b.x; tcy = b.y; break; }
        boss_->draw(s, tcx, tcy);
        for (auto& bs : boss_shots_) bs.draw(s);
        for (auto& bb : boss_bullets_) bb.draw(s);
    }
    if (cameo_) {
        cameo_->draw(s, paddle_->center_x());
        for (auto& bb : boss_bullets_) bb.draw(s);
    }
    for (auto& pw : powerups_) pw.draw(s, *ctx_.fonts.small);
    for (auto& laser : lasers_) laser.draw(s);
    for (auto& ball : balls_) ball.draw(s);
    paddle_->draw(s);

    draw_hud(s);
    if (white_flash_ > 0)
        draw::rect(s, Color(255, 255, 255, int(180 * std::min(1.0f, white_flash_))),
                   {0, 0, ctx_.W, ctx_.H});
    draw_flash(s);
    draw_powerup_timers(s);

    if (state_ == St::Ready) {
        draw_text_centered(s, *ctx_.fonts.small, "PRESS  SPACE  TO  LAUNCH",
                           ctx_.W / 2, ctx_.H - 30, CYAN);
    } else if (state_ == St::Play && paddle_->catch_on) {
        bool any_attached = false;
        for (auto& b : balls_) if (b.attached) { any_attached = true; break; }
        if (any_attached)
            draw_text_centered(s, *ctx_.fonts.small, "SPACE  TO  RELEASE  BALL",
                               ctx_.W / 2, ctx_.H - 30, Color(40, 255, 200));
    } else if (state_ == St::Win) {
        draw_text_centered(s, *ctx_.fonts.big, "LEVEL  CLEAR!", ctx_.W / 2, ctx_.H / 2 - 40, YELLOW);
        int nxt = level_num() + 1;  // endless: there is always a next level
        Color nc = is_boss_level(nxt) ? RED : GREEN;
        std::string lbl = "LEVEL  " + std::to_string(nxt);
        if (is_boss_level(nxt)) lbl += "   -  BOSS!";
        draw_text_centered(s, *ctx_.fonts.med, lbl, ctx_.W / 2, ctx_.H / 2 + 20, nc);
    } else if (state_ == St::GameOver) {
        draw_text_centered(s, *ctx_.fonts.big, "GAME  OVER", ctx_.W / 2, ctx_.H / 2 - 50, RED);
        draw_text_centered(s, *ctx_.fonts.med, "SCORE  " + zpad(score_, 6), ctx_.W / 2, ctx_.H / 2 + 10, WHITE);
        draw_text_centered(s, *ctx_.fonts.small, "PRESS SPACE TO RETRY", ctx_.W / 2, ctx_.H / 2 + 60, DIM);
        if (initials_.active()) initials_.draw(s);
    } else if (state_ == St::Victory) {
        draw_text_centered(s, *ctx_.fonts.big, "ALL LEVELS CLEARED!", ctx_.W / 2, ctx_.H / 2 - 60, YELLOW);
        draw_text_centered(s, *ctx_.fonts.med, "FINAL SCORE  " + zpad(score_, 6), ctx_.W / 2, ctx_.H / 2 + 10, WHITE);
        draw_text_centered(s, *ctx_.fonts.small, "PRESS SPACE TO PLAY AGAIN", ctx_.W / 2, ctx_.H / 2 + 60, DIM);
        if (initials_.active()) initials_.draw(s);
    }

    pause_.draw(s);
}

void ArkanoidGame::draw_hud(Surface& s) {
    draw_text(s, *ctx_.fonts.med, "SCORE  " + zpad(score_, 6), 16, 8, GREEN);
    draw_text_centered(s, *ctx_.fonts.med, "HI  " + zpad(high_score_, 6), ctx_.W / 2, 8, YELLOW);
    draw_text_right(s, *ctx_.fonts.med, "LV  " + std::to_string(level_idx_ + 1), ctx_.W - 16, 8, CYAN);
    draw_text(s, *ctx_.fonts.small, "LIVES", 16, ctx_.H - 26, Color(40, 60, 80));
    for (int i = 0; i < lives_; ++i)
        draw::circle(s, CYAN, 82 + i * 18, ctx_.H - 16, 6);
    draw_text_right(s, *ctx_.fonts.small, "P PAUSE", ctx_.W - 12, ctx_.H - 26, Color(25, 50, 70));

    // combo / multiplier
    if (ctx_.combo && ctx_.combo->active() && state_ == St::Play) {
        int m = ctx_.combo->mult();
        Color cc = m >= 6 ? RED : m >= 4 ? Color(255, 140, 0) : YELLOW;
        draw_text_centered(s, *ctx_.fonts.med, "x" + std::to_string(m) + " COMBO", ctx_.W / 2, 34, cc);
        int bw = 120, bx = ctx_.W / 2 - bw / 2;
        draw::rect(s, Color(40, 40, 40), {bx, 58, bw, 4});
        draw::rect(s, cc, {bx, 58, int(bw * ctx_.combo->ratio()), 4});
    }

    // ── JUGGLE bonus indicator: shown while 2+ balls are in play ──
    if (state_ == St::Play && active_ball_count() >= 2) {
        int jm = juggle_mult();
        // a rising hue with more balls; pulses to feel alive
        float p = 0.7f + 0.3f * std::sin(game_t_ * 8.0f);
        Color jc = jm >= 4 ? Color(int(255 * p), 80, int(255 * p))
                 : jm == 3 ? Color(int(255 * p), int(140 * p), 0)
                           : Color(80, int(255 * p), 200);
        draw_text_centered(s, *ctx_.fonts.med,
                           "JUGGLE x" + std::to_string(jm), ctx_.W / 2, ctx_.H - 56, jc);
    }

    // ── SMASH ability meter (only relevant when playing manually) ──
    if (state_ == St::Play && !bot_) {
        int bw = 90, bx = ctx_.W - bw - 12, by = ctx_.H - 44;
        bool ready = paddle_->smash_ready();
        float fill = paddle_->charging ? paddle_->charge_ratio()
                   : ready ? 1.0f : 1.0f - std::max(0.0f, paddle_->smash_cd / Paddle::SMASH_CD);
        Color fc = !ready ? Color(90, 90, 110)
                 : paddle_->charging ? (paddle_->charge_ratio() >= 1.0f
                                        ? Color(255, 90, 40) : Color(255, 220, 80))
                                     : Color(255, 150, 40);
        draw_text_right(s, *ctx_.fonts.small, "SMASH", bx + bw, by - 14,
                        ready ? Color(255, 150, 40) : Color(70, 70, 90));
        draw::rect(s, Color(30, 30, 40), {bx, by, bw, 6});
        draw::rect(s, fc, {bx, by, int(bw * fill), 6});
    }

    // boss HP bar
    if (boss_ && !boss_->dying && !boss_->entering) {
        int bw = 420, bx = ctx_.W / 2 - bw / 2, by = 40;
        draw_text_centered(s, *ctx_.fonts.small, boss_->name, ctx_.W / 2, by - 16, boss_->color);
        draw::rect(s, Color(20, 20, 30), {bx, by, bw, 12}, 0, 4);
        int fillw = int(bw * boss_->hp_ratio());
        Color hc = boss_->hp_ratio() > 0.5f ? GREEN : boss_->hp_ratio() > 0.25f ? YELLOW : RED;
        draw::rect(s, hc, {bx, by, fillw, 12}, 0, 4);
        draw::rect(s, boss_->color, {bx, by, bw, 12}, 1, 4);
    }
}

void ArkanoidGame::draw_powerup_timers(Surface& s) {
    int y = 36;
    for (auto& p : pw_timers_) {
        const std::string& k = p.first;
        Color col = WHITE;
        if (k == "expand") col = GREEN;
        else if (k == "shrink") col = RED;
        else if (k == "laser") col = PURPLE;
        else if (k == "slow") col = YELLOW;
        else if (k == "fast") col = Color(255, 100, 40);
        else if (k == "catch") col = Color(40, 255, 200);
        else if (k == "fire") col = Color(255, 110, 30);
        std::string up = k;
        for (auto& c : up) c = char(std::toupper((unsigned char)c));
        // f"{t:.0f}s" — round-half-to-even on .0f; use rounding to nearest.
        int secs = int(std::floor(p.second + 0.5f));
        draw_text_right(s, *ctx_.fonts.small, up + "  " + std::to_string(secs) + "s",
                        ctx_.W - 12, y, col);
        y += 18;
    }
}

void ArkanoidGame::draw_flash(Surface& s) {
    for (auto& f : flash_) {
        auto t = ctx_.fonts.small->render(f.text, f.color);
        t->set_alpha(clampi(int(255 * f.life), 0, 255));
        s.blit(*t, int(f.x) - t->width() / 2, int(f.y));
    }
}

}  // namespace

std::unique_ptr<Scene> make_arkanoid(const Context& ctx) {
    return std::make_unique<ArkanoidGame>(ctx);
}
