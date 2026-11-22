// galaga.cpp — Void Squadrons.
// A progression shooter built on the classic Galaga loop: formation entry,
// dive attacks, tractor-beam capture, and the dual-fighter rescue all stay
// intact. Layered on top: stacking power-ups that build toward "massive
// firepower", armored enemies that get meaner each stage, and a big, fat,
// blocky, googly-eyed boss every 5th stage that does something silly.
// NUKE (B) is the great equaliser.
#include "galaga.h"

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
#include "engine/ui.h"

namespace {

// ── Colors ────────────────────────────────────────────────────────────────────
const Color BLACK(0, 0, 0), WHITE(255, 255, 255), GREEN(80, 255, 80);
const Color CYAN(80, 200, 255), YELLOW(255, 220, 40), RED(255, 60, 60);
const Color ORANGE(255, 140, 40);

const Color BEE_COLOR(100, 220, 255);
const Color BUTTERFLY_COLOR(255, 100, 200);
const Color ARMOR_COLOR(170, 185, 205);            // armored grunt: steel
const Color BOSS_COLOR(255, 200, 40);
const Color BOSS_COLOR2(255, 80, 40);
const Color PLAYER_COLOR(80, 200, 255);
const Color PLAYER_CAPTURED_COLOR(255, 80, 80);
const Color TRACTOR_COLOR(120, 80, 255);

constexpr float PI = 3.14159265358979323846f;
constexpr int SCALE = 3;

using SL = std::vector<std::string>;

// ── Sprite tables (mirror sprites.py exactly) ─────────────────────────────────
const SL BEE[2] = {
    {"...#.#...", "..#####..", ".#######.", "##.###.##", "#########",
     "..#####..", "..#...#..", ".#.....#."},
    {"...#.#...", "..#####..", ".#######.", "##.###.##", "#########",
     "..#####..", ".##...##.", "#.......#"}};

const SL BUTTERFLY[2] = {
    {".#.....#.", "##.###.##", "#########", ".##.#.##.", "..#####..",
     ".#.###.#.", "#.#...#.#", ".#.....#."},
    {"#.......#", "##.###.##", "#########", ".##.#.##.", "..#####..",
     ".#.###.#.", ".##...##.", "..#...#.."}};

// chunky armored grunt — visually distinct (solid, riveted body)
const SL ARMOR[2] = {
    {"#########", "#.#####.#", "#########", "##.###.##", "#########",
     "#.#...#.#", "##.###.##", "#.#...#.#"},
    {"#########", "#.#####.#", "#########", "##.###.##", "#########",
     "#.#...#.#", "#.#...#.#", "##.#.#.##"}};

const SL BOSS[2] = {
    {"....###....", "...#####...", "..#######..", ".###.#.###.", "###########",
     "#.#######.#", "#.##...##.#", ".##.###.##.", "..#.....#..", "...##.##...",
     "....###...."},
    {"....###....", "...#####...", "..#######..", ".###.#.###.", "###########",
     "#.#######.#", "##.#...#.##", ".###.#.###.", "..##.##.#..", "...##.##...",
     "....###...."}};

const SL PLAYER_SP = {"....#....", "...###...", "...###...", ".#######.",
                      "#########", "#########", "#.#.#.#.#", "....#...."};

// ── helpers ───────────────────────────────────────────────────────────────────
bool overlap(Rect a, Rect b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}
std::string zpad(int v, int width) {
    std::string s = std::to_string(v);
    if ((int)s.size() < width) s = std::string(width - s.size(), '0') + s;
    return s;
}

std::shared_ptr<Surface> make_glow(const SL& lines, Color color, int radius) {
    Surface base = make_sprite_surface(lines, color, SCALE);
    return glow_surface(base, color, radius);
}

// Cubic Bezier point.
std::pair<float, float> bezier(std::pair<float, float> p0, std::pair<float, float> p1,
                               std::pair<float, float> p2, std::pair<float, float> p3,
                               float t) {
    float u = 1 - t;
    return {u * u * u * p0.first + 3 * u * u * t * p1.first +
                3 * u * t * t * p2.first + t * t * t * p3.first,
            u * u * u * p0.second + 3 * u * u * t * p1.second +
                3 * u * t * t * p2.second + t * t * t * p3.second};
}

using Path = std::vector<std::pair<float, float>>;
using Pt = std::pair<float, float>;

// Every flight path is resampled to evenly spaced points, so an enemy advances
// along it by DISTANCE (px/s) rather than by bezier parameter — speeds are real
// pixel speeds, identical along the whole curve and at any frame rate.
constexpr float PATH_STEP = 6.0f;

Path resample(const Path& raw) {
    Path out;
    if (raw.empty()) return out;
    out.push_back(raw[0]);
    float next = PATH_STEP;  // distance into the current segment of the next sample
    for (size_t i = 1; i < raw.size(); ++i) {
        float ax = raw[i - 1].first, ay = raw[i - 1].second;
        float bx = raw[i].first, by = raw[i].second;
        float seg = std::hypot(bx - ax, by - ay);
        if (seg <= 0) continue;
        while (next <= seg) {
            out.push_back({ax + (bx - ax) * next / seg, ay + (by - ay) * next / seg});
            next += PATH_STEP;
        }
        next -= seg;
    }
    out.push_back(raw.back());
    return out;
}
Path curve(Pt p0, Pt p1, Pt p2, Pt p3) {
    Path raw;
    const int steps = 64;
    for (int i = 0; i <= steps; ++i) raw.push_back(bezier(p0, p1, p2, p3, i / float(steps)));
    return resample(raw);
}

// Six entry swoops; each squad of four shares one so they arrive as a flight.
Path make_entry_path(float sx, float sy, int W, int H, int variant) {
    float w = float(W), h = float(H);
    switch (variant % 6) {
        case 0:  return curve({-40, -40}, {60, h * 0.5f}, {w * 0.7f, h * 0.4f}, {sx, sy});
        case 1:  return curve({w + 40, -40}, {w - 60, h * 0.5f}, {w * 0.3f, h * 0.4f}, {sx, sy});
        case 2:  return curve({-40, h * 0.58f}, {w * 0.45f, h * 0.66f}, {w * 0.58f, h * 0.12f}, {sx, sy});
        case 3:  return curve({w + 40, h * 0.58f}, {w * 0.55f, h * 0.66f}, {w * 0.42f, h * 0.12f}, {sx, sy});
        case 4:  return curve({w * 0.5f - 30, -40}, {w * 0.08f, h * 0.30f}, {w * 0.28f, h * 0.60f}, {sx, sy});
        default: return curve({w * 0.5f + 30, -40}, {w * 0.92f, h * 0.30f}, {w * 0.72f, h * 0.60f}, {sx, sy});
    }
}

// Attack run: swoop down past the player's row and off the bottom of the screen.
// The enemy then re-enters from the top (see make_return_path).
Path make_dive_path(float ex, float ey, float aim_x, int W, int H) {
    float tx = std::clamp(aim_x + rnd_uniform(-130, 130), W * 0.08f, W * 0.92f);
    return curve({ex, ey}, {ex + rnd_uniform(-140, 140), H * 0.5f},
                 {tx + rnd_uniform(-90, 90), H - 150.0f}, {tx, H + 70.0f});
}
Path make_return_path(float from_x, float from_y, float sx, float sy) {
    if (from_y < 0)  // wrapped: drop back in from above
        return curve({from_x, from_y}, {from_x, 30.0f}, {sx, sy - 70.0f}, {sx, sy});
    return curve({from_x, from_y}, {from_x, from_y - 150.0f}, {sx, sy + 150.0f}, {sx, sy});
}
// A boss peeling off to a hover point above the player for a tractor run.
Path make_hover_path(float ex, float ey, float hx, float hy) {
    return curve({ex, ey}, {ex, ey + 130.0f}, {hx, hy - 150.0f}, {hx, hy});
}

// ── Bullet (supports angled travel + piercing + damage) ───────────────────────
struct Bullet {
    static constexpr int BW = 3, BH = 14;
    float x, y, vx = 0, vy;
    Color color;
    bool alive = true;
    int w = BW, h = BH;
    int pierce = 0;   // enemies it can pass through (player bullets only)
    int damage = 1;
    int volley = 0;   // which trigger-pull it came from (player bullets only)

    Rect rect() const {
        return {int(x - w / 2 - 1), int(vy < 0 ? y - h : y), w + 2, h};
    }
    std::pair<int, int> tip() const { return {int(x), int(vy < 0 ? y - h : y + h)}; }
    void update(float dt, int screen_w, int screen_h) {
        x += vx * dt;
        y += vy * dt;
        if (y < -20 || y > screen_h + 20 || x < -20 || x > screen_w + 20) alive = false;
    }
    void draw(Surface& s) const {
        if (!alive) return;
        int y_tip  = vy < 0 ? int(y - h) : int(y + h);
        int y_tail = int(y);
        draw_glow_bolt(s, color, int(x), y_tip, y_tail, std::max(2, w / 2));
    }
};

// ── Power-ups ─────────────────────────────────────────────────────────────────
enum class PU { Rapid, Spread, Side, Pierce, Shield, Life, Nuke, Mystery };
struct PUInfo { const char* label; Color color; };
PUInfo pu_info(PU k) {
    switch (k) {
        case PU::Rapid:  return {"R", YELLOW};
        case PU::Spread: return {"S", ORANGE};
        case PU::Side:   return {"W", CYAN};
        case PU::Pierce: return {"P", Color(200, 100, 255)};
        case PU::Shield: return {"O", GREEN};
        case PU::Life:   return {"+1", WHITE};
        case PU::Nuke:   return {"N", RED};
        default:         return {"?", WHITE};
    }
}
struct PowerUp {
    float x, y, t = 0;
    PU kind;
    bool alive = true;
    static constexpr float VY = 95;
    Rect rect() const { return {int(x - 14), int(y - 12), 28, 24}; }
    void update(float dt, int sh) { y += VY * dt; t += dt; if (y > sh + 20) alive = false; }
    void draw(Surface& s, Font& f) const {
        if (!alive) return;
        PUInfo in = pu_info(kind);
        Color c = in.color;
        if (kind == PU::Mystery) {  // mystery shimmers through the rainbow
            float h = std::fmod(t * 2.0f, 1.0f);
            c = Color(int(128 + 127 * std::sin(h * 6.28f)),
                      int(128 + 127 * std::sin(h * 6.28f + 2.1f)),
                      int(128 + 127 * std::sin(h * 6.28f + 4.2f)));
        }
        float pulse = 0.7f + 0.3f * std::sin(t * 8);
        draw::rect(s, Color(c.r, c.g, c.b, int(70 * pulse)), {int(x) - 18, int(y) - 16, 36, 32}, 0, 8);
        draw::rect(s, Color(8, 12, 18), {int(x) - 14, int(y) - 12, 28, 24}, 0, 6);
        draw::rect(s, c, {int(x) - 14, int(y) - 12, 28, 24}, 2, 6);
        draw_text_centered(s, f, in.label, int(x), int(y) - 9, c);
    }
};

// ── Shared per-kind sprite caches ─────────────────────────────────────────────
struct SpriteCache {
    std::shared_ptr<Surface> bee[2];
    std::shared_ptr<Surface> butterfly[2];
    std::shared_ptr<Surface> armor[2];
    std::shared_ptr<Surface> boss[2];
    std::shared_ptr<Surface> boss_dmg[2];
    // white silhouettes for a shape-correct hit flicker
    std::shared_ptr<Surface> bee_fl[2], butterfly_fl[2], armor_fl[2], boss_fl[2];
    std::shared_ptr<Surface> player;       // single (glow radius 10)
    std::shared_ptr<Surface> dual;         // dual fighter (glow radius 10)
    std::shared_ptr<Surface> captured;     // captured ship (red, glow 6)
    int bee_w, bee_h, but_w, but_h, arm_w, arm_h, boss_w, boss_h, player_w, player_h, dual_w;

    SpriteCache() {
        for (int f = 0; f < 2; ++f) {
            bee[f] = make_glow(BEE[f], BEE_COLOR, 6);
            butterfly[f] = make_glow(BUTTERFLY[f], BUTTERFLY_COLOR, 6);
            armor[f] = make_glow(ARMOR[f], ARMOR_COLOR, 6);
            boss[f] = make_glow(BOSS[f], BOSS_COLOR, 6);
            boss_dmg[f] = make_glow(BOSS[f], BOSS_COLOR2, 6);
            bee_fl[f] = make_glow(BEE[f], WHITE, 6);
            butterfly_fl[f] = make_glow(BUTTERFLY[f], WHITE, 6);
            armor_fl[f] = make_glow(ARMOR[f], WHITE, 6);
            boss_fl[f] = make_glow(BOSS[f], WHITE, 6);
        }
        auto bs = sprite_size(BEE[0], SCALE); bee_w = bs.first; bee_h = bs.second;
        auto us = sprite_size(BUTTERFLY[0], SCALE); but_w = us.first; but_h = us.second;
        auto as_ = sprite_size(ARMOR[0], SCALE); arm_w = as_.first; arm_h = as_.second;
        auto os = sprite_size(BOSS[0], SCALE); boss_w = os.first; boss_h = os.second;

        Surface pbase = make_sprite_surface(PLAYER_SP, PLAYER_COLOR, SCALE);
        player = glow_surface(pbase, PLAYER_COLOR, 10);
        auto ps = sprite_size(PLAYER_SP, SCALE); player_w = ps.first; player_h = ps.second;

        // dual fighter: two ships side by side, then glow over the pair.
        dual_w = player_w * 2 + 8;
        Surface dbase(dual_w, player_h, true);
        dbase.blit(pbase, 0, 0);
        dbase.blit(pbase, player_w + 8, 0);
        dual = glow_surface(dbase, PLAYER_COLOR, 10);

        captured = make_glow(PLAYER_SP, PLAYER_CAPTURED_COLOR, 6);
    }
};

SpriteCache& cache() {
    static SpriteCache c;
    return c;
}

// ── Player (weapon progression layered on top) ────────────────────────────────
struct Player {
    static constexpr float SPEED = 300, RESPAWN_INV = 2.5f;
    // Fire is limited by trigger-pulls in flight, not by individual bolts, so a
    // fat spread/wing volley never chokes the gun.
    static constexpr int MAX_VOLLEYS = 7;
    float x, y;
    bool alive = true;
    float fire_cd = 0, invincible = 0;
    int lives = 3;
    bool captured = false, dual_fighter = false;
    int glow_off = 10, w, h, dual_w;

    Player(int px, int py) : x(px), y(py) {
        w = cache().player_w; h = cache().player_h; dual_w = cache().dual_w;
    }
    int cur_w() const { return dual_fighter ? dual_w : w; }
    Rect rect() const { return {int(x), int(y), cur_w(), h}; }
    std::pair<int, int> center() const {
        return {int(x + cur_w() / 2.0f), int(y + h / 2.0f)};
    }

    void update(float dt, float move_dir, int screen_w) {
        if (!alive || captured) return;
        if (invincible > 0) invincible -= dt;
        if (fire_cd > 0) fire_cd -= dt;
        x += move_dir * SPEED * dt;
        float max_x = screen_w - cur_w();
        x = std::max(0.0f, std::min(max_x, x));
    }
    void kill() {
        alive = false; dual_fighter = false; lives -= 1; invincible = 0;
    }
    void respawn(int nx) {
        alive = true; x = nx; captured = false; invincible = RESPAWN_INV; fire_cd = 0;
    }
    void draw(Surface& s) const {
        if (!alive || captured) return;
        if (invincible > 0 && int(invincible * 8) % 2 == 0) return;
        if (dual_fighter)
            s.blit(*cache().dual, int(x) - glow_off, int(y) - glow_off);
        else
            s.blit(*cache().player, int(x) - glow_off, int(y) - glow_off);
    }
};

// ── Enemy ─────────────────────────────────────────────────────────────────────
enum class Kind { Bee, Butterfly, Armor, Boss };

struct Enemy {
    float slot_x, slot_y, x, y;
    Kind kind;
    int idx;
    int hp, max_hp;
    bool alive = true;
    int frame = 0;
    float hit_flash = 0;
    // state: entering → formation ⇄ diving → returning; a boss may also go
    // "tractor" (fly to a hover point, beam, return); "leaving" flies off-screen.
    std::string state = "entering";
    Path path;              // the path for the current state (evenly spaced points)
    float path_d = 0;       // px travelled along it
    float enter_at = 0;     // formation clock time at which it is released
    bool released = false;  // has left the wings and is actually on screen
    float dive_speed = 400.0f;
    int tractor_phase = 0;  // 0 flying to the hover point, 1 holding / beaming
    bool captured = false;
    int glow_off = 6, w, h;

    Enemy(float sx, float sy, Kind k, int i, int hp_)
        : slot_x(sx), slot_y(sy), x(sx), y(sy), kind(k), idx(i) {
        hp = max_hp = hp_;
        if (k == Kind::Bee) { w = cache().bee_w; h = cache().bee_h; }
        else if (k == Kind::Butterfly) { w = cache().but_w; h = cache().but_h; }
        else if (k == Kind::Armor) { w = cache().arm_w; h = cache().arm_h; }
        else { w = cache().boss_w; h = cache().boss_h; }
    }
    Color color() const {
        if (kind == Kind::Boss && hp == 1) return BOSS_COLOR2;
        if (kind == Kind::Boss) return BOSS_COLOR;
        if (kind == Kind::Armor) return ARMOR_COLOR;
        if (kind == Kind::Butterfly) return BUTTERFLY_COLOR;
        return BEE_COLOR;
    }
    Rect rect() const { return {int(x), int(y), w, h}; }
    std::pair<int, int> center() const { return {int(x + w / 2.0f), int(y + h / 2.0f)}; }
    int score_value(bool in_formation) const {
        // PTS_TABLE: boss (150,400), butterfly (80,160), bee (50,80), armor (120,240)
        if (kind == Kind::Boss) return in_formation ? 150 : 400;
        if (kind == Kind::Armor) return in_formation ? 120 : 240;
        if (kind == Kind::Butterfly) return in_formation ? 80 : 160;
        return in_formation ? 50 : 80;
    }
    float shoot_prob() const {
        if (kind == Kind::Boss) return 0.008f;
        if (kind == Kind::Armor) return 0.006f;
        if (kind == Kind::Butterfly) return 0.005f;
        return 0.003f;
    }
    void toggle_frame() { frame ^= 1; }

    void set_path(Path p) { path = std::move(p); path_d = 0; }
    float path_len() const { return path.size() < 2 ? 0.0f : (path.size() - 1) * PATH_STEP; }
    bool path_done() const { return path_d >= path_len(); }
    float progress() const { float l = path_len(); return l > 0 ? std::min(1.0f, path_d / l) : 1.0f; }
    Pt path_pos() const {
        if (path.empty()) return {x, y};
        if (path.size() < 2) return path[0];
        int n = (int)path.size();
        float f = std::max(0.0f, path_d / PATH_STEP);
        int i = std::min(int(f), n - 2);
        float frac = std::min(1.0f, f - i);
        return {path[i].first + (path[i + 1].first - path[i].first) * frac,
                path[i].second + (path[i + 1].second - path[i].second) * frac};
    }

    void draw(Surface& s) const {
        if (!alive || !released) return;
        const SpriteCache& c = cache();
        const Surface* surf;
        Surface* fl;
        if (kind == Kind::Boss) { surf = (hp == 1 ? c.boss_dmg[frame] : c.boss[frame]).get(); fl = c.boss_fl[frame].get(); }
        else if (kind == Kind::Armor) { surf = c.armor[frame].get(); fl = c.armor_fl[frame].get(); }
        else if (kind == Kind::Butterfly) { surf = c.butterfly[frame].get(); fl = c.butterfly_fl[frame].get(); }
        else { surf = c.bee[frame].get(); fl = c.bee_fl[frame].get(); }
        s.blit(*surf, int(x) - glow_off, int(y) - glow_off);
        if (hit_flash > 0) {  // white silhouette flicker, shaped like the sprite
            fl->set_alpha(int(200 * std::min(1.0f, hit_flash)));
            s.blit(*fl, int(x) - glow_off, int(y) - glow_off);
            fl->set_alpha(-1);
        }
        if (kind == Kind::Armor && hp < max_hp) {  // armour left, once chipped
            int bw = w - 4, fw = std::max(1, bw * hp / max_hp);
            draw::rect(s, Color(30, 34, 40), {int(x) + 2, int(y) + h + 3, bw, 2});
            draw::rect(s, hp * 2 <= max_hp ? ORANGE : WHITE, {int(x) + 2, int(y) + h + 3, fw, 2});
        }
        if (captured) {
            auto cap = c.captured;
            s.blit(*cap, int(x) + w / 2 - cap->width() / 2, int(y) + h + 4);
        }
    }
};

// ── TractorBeam ───────────────────────────────────────────────────────────────
struct TractorBeam {
    Enemy* boss;
    bool active = true;
    float t = 0;
    int width = 40;
    float length = 0;
    int max_len = 260;
    std::string phase = "extend";  // extend → hold → retract
    float hold_t = 1.8f;

    TractorBeam(Enemy* b, int reach) : boss(b), max_len(reach) {}
    float x() const { return boss->x + boss->w / 2; }
    float top_y() const { return boss->y + boss->h; }

    void update(float dt) {
        t += dt;
        if (!boss->alive) { active = false; return; }  // no boss, no beam
        if (phase == "extend") {
            length = std::min(float(max_len), length + 350 * dt);
            if (length >= max_len) phase = "hold";
        } else if (phase == "hold") {
            hold_t -= dt;
            if (hold_t <= 0) phase = "retract";
        } else if (phase == "retract") {
            length = std::max(0.0f, length - 350 * dt);
            if (length <= 0) active = false;
        }
    }
    Rect capture_rect() const {
        int bw = int(width + length * 0.5f);
        return {int(x() - bw / 2), int(top_y() + length - 20), bw, 24};
    }
    void draw(Surface& s) const {
        if (!active || length < 4) return;
        float cx = x();
        float ty = top_y();
        float by = ty + length;
        int half_w = width / 2 + int(length * 0.25f);

        for (int layer = 4; layer > 0; --layer) {
            float scale = 1.0f + layer * 0.15f;
            int alpha = std::max(0, std::min(255,
                int(60 * (layer / 4.0f) * (0.6f + 0.4f * std::sin(t * 8)))));
            int lw = int(half_w * scale);
            draw::polygon(s, Color(TRACTOR_COLOR.r, TRACTOR_COLOR.g, TRACTOR_COLOR.b, alpha),
                          {{int(cx), int(ty)}, {int(cx - lw), int(by)}, {int(cx + lw), int(by)}});
        }
        // bright core
        int core_a = std::max(0, std::min(255, int(80 * (0.7f + 0.3f * std::sin(t * 12)))));
        draw::polygon(s, Color(TRACTOR_COLOR.r, TRACTOR_COLOR.g, TRACTOR_COLOR.b, core_a),
                      {{int(cx), int(ty)}, {int(cx - half_w), int(by)}, {int(cx + half_w), int(by)}});

        // horizontal scan lines
        int steps = int(length / 8);
        for (int i = 0; i < steps; ++i) {
            float fy = ty + i * 8;
            float frac = i / float(std::max(1, steps));
            int fw = int(half_w * frac);
            int la = std::max(0, std::min(255,
                int(40 * (std::sin(t * 10 - i * 0.3f) * 0.5f + 0.5f))));
            draw::rect(s, Color(TRACTOR_COLOR.r, TRACTOR_COLOR.g, TRACTOR_COLOR.b, la),
                       {int(cx - fw), int(fy), std::max(1, fw * 2 + 1), 2});
        }
    }
};

// ── formation slots ───────────────────────────────────────────────────────────
struct Slot { float x, y; Kind kind; };

// `guard` = the short two-row escort that flies under a mega-boss.
std::vector<Slot> build_formation_slots(int screen_w, int top_y, bool guard) {
    std::vector<Slot> slots;
    struct Row { int count; Kind kind; };
    static const Row FULL[] = {{4, Kind::Boss}, {6, Kind::Boss}, {8, Kind::Butterfly},
                               {10, Kind::Butterfly}, {12, Kind::Bee}, {12, Kind::Bee}};
    static const Row ESCORT[] = {{8, Kind::Butterfly}, {10, Kind::Bee}};
    const Row* rows = guard ? ESCORT : FULL;
    const int n_rows = guard ? 2 : 6;
    int y = top_y;
    for (int ri = 0; ri < n_rows; ++ri) {
        const Row& r = rows[ri];
        int total_w = r.count * 52;
        int start_x = (screen_w - total_w) / 2;
        for (int i = 0; i < r.count; ++i)
            slots.push_back({float(start_x + i * 52), float(y), r.kind});
        y += 48;
    }
    return slots;
}

// ── Formation (scales with stage) ─────────────────────────────────────────────
struct Formation {
    static constexpr float ANIM_INTERVAL_BASE = 0.5f, ANIM_INTERVAL_MIN = 0.12f;
    static constexpr float DRIFT_SPEED = 38.0f, DRIFT_RANGE = 90.0f;
    static constexpr int SQUAD = 4;

    int W, H, wave;
    bool challenge = false;   // bonus stage: enemies fly patterns but never fire
    bool guard = false;       // boss stage: a short escort instead of the full swarm
    int top_y = 80;
    float player_y = 650;     // the player's row, for tractor reach
    std::vector<Enemy> enemies;   // never resized after build(): Enemy* stay valid
    std::vector<TractorBeam> tractor_beams;
    std::vector<Bullet> p_bullets;
    float anim_t = 0;
    bool anim_ticked = false;
    float drift_x = 0;
    int drift_dir = 1;
    float clock = 0;          // seconds since the stage began (entry schedule)
    // ── formation morphs ──────────────────────────────────────────────────────
    // Every so often the whole formation animates into a new shape over a few
    // seconds: a breathe (scale pulse), a rotate (swirl), or a wave ripple. The
    // morph offsets each enemy's resting slot so the swarm never feels static.
    float morph_cd = 6.0f;     // time until the next morph kicks off
    float morph_t = 0;         // elapsed time within the active morph
    float morph_dur = 0;       // 0 = no morph running
    int   morph_kind = 0;      // 0 breathe, 1 rotate, 2 wave
    float form_cx = 0, form_cy = 0;  // formation centroid (resting slots)

    Formation(int sw, int sh, int wv, bool chal, bool grd, int top)
        : W(sw), H(sh), wave(wv), challenge(chal), guard(grd), top_y(top) { build(); }

    void build() {
        auto slots = build_formation_slots(W, top_y, guard);
        // Stage scaling: butterflies and bees can be promoted to armored grunts
        // that take multiple hits. Fraction & HP both rise with the stage.
        // Challenge stages never promote — they're a soft, point-rich breather.
        float armor_p = challenge ? 0.0f
                                  : std::min(0.55f, std::max(0.0f, (wave - 2) * 0.07f));
        int armor_hp = 2 + wave / 6;
        // Entry: squads of four share a swoop and follow each other in; a new
        // squad launches every ~half second, so stage 1 is full in about 8 s.
        float squad_iv = std::max(0.28f, 0.50f - wave * 0.02f);
        for (int idx = 0; idx < (int)slots.size(); ++idx) {
            Kind k = slots[idx].kind;
            int hp = (k == Kind::Boss) ? 2 : 1;
            // promote some non-boss enemies to armored grunts at higher stages
            if (k != Kind::Boss && wave >= 3 && rnd01() < armor_p) {
                k = Kind::Armor;
                hp = armor_hp;
            }
            Enemy e(slots[idx].x, slots[idx].y, k, idx, hp);
            int squad = idx / SQUAD, member = idx % SQUAD;
            e.state = "entering";
            e.enter_at = squad * squad_iv + member * 0.10f;
            e.set_path(make_entry_path(slots[idx].x, slots[idx].y, W, H, squad + wave));
            e.x = e.path[0].first; e.y = e.path[0].second;
            enemies.push_back(std::move(e));
        }

        // centroid of the resting slots — the pivot for rotate / breathe morphs.
        double sx = 0, sy = 0;
        for (auto& e : enemies) { sx += e.slot_x; sy += e.slot_y; }
        int n = std::max(1, (int)enemies.size());
        form_cx = float(sx / n); form_cy = float(sy / n);
    }

    int alive_count() const {
        int n = 0; for (auto& e : enemies) if (e.alive) ++n; return n;
    }
    // Enemies the player still has to deal with (ignores ones flying away).
    int hostile_count() const {
        int n = 0; for (auto& e : enemies) if (e.alive && e.state != "leaving") ++n; return n;
    }
    bool all_in_formation() const {
        for (auto& e : enemies) if (e.alive && e.state != "formation") return false;
        return true;
    }
    bool entry_done() const {
        for (auto& e : enemies) if (e.alive && e.state == "entering") return false;
        return true;
    }
    float anim_interval() const {
        float r = std::max(0.0f, alive_count() / float(std::max<size_t>(1, enemies.size())));
        return ANIM_INTERVAL_MIN + (ANIM_INTERVAL_BASE - ANIM_INTERVAL_MIN) * r;
    }
    bool in_formation(const Enemy& e) const { return e.state == "formation"; }
    float dive_speed() const { return std::min(760.0f, 430.0f + wave * 16.0f); }

    // `hostile` = enemies may open fire. Movement, beams and bullets always run.
    void update(float dt, int screen_h, int screen_w, bool hostile) {
        anim_ticked = false;
        clock += dt;
        for (auto& e : enemies) if (e.hit_flash > 0) e.hit_flash -= dt * 5;

        // formation drift + (optional) morph
        bool any_formation = false;
        for (auto& e : enemies) if (e.alive && e.state == "formation") { any_formation = true; break; }
        if (any_formation) {
            // Clamp at the rail and turn there — never "still past the edge, flip again".
            drift_x += DRIFT_SPEED * drift_dir * dt;
            if (drift_x >= DRIFT_RANGE)       { drift_x = DRIFT_RANGE;  drift_dir = -1; }
            else if (drift_x <= -DRIFT_RANGE) { drift_x = -DRIFT_RANGE; drift_dir = 1; }

            // morph scheduler: only morph when the whole swarm has settled.
            if (morph_dur <= 0) {
                if (all_in_formation()) {
                    morph_cd -= dt;
                    if (morph_cd <= 0) {
                        morph_kind = rnd_int(0, 2);
                        morph_dur = 3.4f;
                        morph_t = 0;
                    }
                }
            } else {
                morph_t += dt;
                if (morph_t >= morph_dur) {
                    morph_dur = 0;
                    morph_cd = rnd_uniform(6.0f, 11.0f);
                }
            }
        }
        // envelope: ease in and back out so the shape returns to rest cleanly.
        float amp = 0;
        if (morph_dur > 0) amp = std::sin(std::min(1.0f, morph_t / morph_dur) * PI);

        // Where an enemy's slot currently is, relative to its resting slot.
        auto offset = [&](const Enemy& e) -> Pt {
            float ox = 0, oy = 0;
            if (amp > 0.001f) {
                float rx = e.slot_x - form_cx, ry = e.slot_y - form_cy;
                if (morph_kind == 0) {            // BREATHE: scale toward/away from centre
                    float sc = amp * 0.22f * std::sin(morph_t * 3.0f);
                    ox = rx * sc; oy = ry * sc;
                } else if (morph_kind == 1) {     // ROTATE: swirl around the centroid
                    float a = amp * 0.5f * std::sin(morph_t * 1.6f);
                    float ca = std::cos(a), sa = std::sin(a);
                    ox = (rx * ca - ry * sa) - rx;
                    oy = (rx * sa + ry * ca) - ry;
                } else {                          // WAVE: vertical ripple across columns
                    oy = amp * 26.0f * std::sin(morph_t * 4.0f + rx * 0.035f);
                    ox = amp * 14.0f * std::sin(morph_t * 3.0f + ry * 0.05f);
                }
            }
            return {ox + drift_x, oy};
        };
        // Follow the path, steering onto the LIVE slot over the final stretch so
        // the arrival is seamless however far the formation has drifted.
        auto place_blended = [&](Enemy& e) {
            auto [px, py] = e.path_pos();
            auto [ox, oy] = offset(e);
            float f = std::clamp((e.progress() - 0.55f) / 0.45f, 0.0f, 1.0f);
            f = f * f * (3 - 2 * f);
            e.x = px + ox * f; e.y = py + oy * f;
        };
        auto begin_return = [&](Enemy& e, float fx, float fy) {
            e.state = "returning";
            e.set_path(make_return_path(fx, fy, e.slot_x, e.slot_y));
        };

        const float entry_speed = std::min(900.0f, 650.0f + wave * 15.0f);
        for (auto& e : enemies) {
            if (!e.alive) continue;
            if (e.state == "formation") {
                auto [ox, oy] = offset(e);
                e.x = e.slot_x + ox; e.y = e.slot_y + oy;
            } else if (e.state == "entering") {
                if (!e.released) {
                    if (clock < e.enter_at) continue;
                    e.released = true;
                }
                e.path_d += entry_speed * dt;
                place_blended(e);
                if (e.path_done()) e.state = "formation";
            } else if (e.state == "diving") {
                e.path_d += e.dive_speed * dt;
                auto [px, py] = e.path_pos();
                e.x = px; e.y = py;
                if (e.path_done())  // off the bottom: wrap around and drop back in from the top
                    begin_return(e, std::clamp(e.x, 40.0f, float(W - 40 - e.w)), -50.0f);
            } else if (e.state == "returning") {
                e.path_d += std::max(420.0f, e.dive_speed) * dt;
                place_blended(e);
                if (e.path_done()) e.state = "formation";
            } else if (e.state == "tractor") {
                if (e.tractor_phase == 0) {
                    e.path_d += e.dive_speed * dt;
                    auto [px, py] = e.path_pos();
                    e.x = px; e.y = py;
                    if (e.path_done()) {  // parked over the player's column: open the beam
                        e.tractor_phase = 1;
                        int reach = int(player_y + 14 - (e.y + e.h));
                        tractor_beams.push_back(TractorBeam(&e, std::max(120, reach)));
                    }
                } else if (tractor_beams.empty()) {
                    begin_return(e, e.x, e.y);
                }
            } else if (e.state == "leaving") {
                e.y -= 560 * dt;
                if (e.y < -70) e.alive = false;
            }
        }

        // animation tick
        anim_t += dt;
        if (anim_t >= anim_interval()) {
            anim_t = 0; anim_ticked = true;
            for (auto& e : enemies) if (e.alive) e.toggle_frame();
        }

        // enemy shooting — cap scales up with the stage so later stages bite.
        // Challenge (bonus) stages never fire back: pure pattern-flying for points.
        const int MAX_ENEMY_BULLETS = (challenge || !hostile) ? 0 : 3 + wave / 3;
        for (auto& e : enemies) {
            if (!e.alive || (e.state != "formation" && e.state != "diving")) continue;
            if ((int)p_bullets.size() >= MAX_ENEMY_BULLETS) break;
            if (e.y > player_y - 120) continue;   // no point-blank shots from a diver
            float prob = e.shoot_prob() * (1 + wave * 0.05f);
            if (rnd01() < prob * dt * 60) {
                auto [cx, cy] = e.center();
                Bullet b;
                b.x = float(cx); b.y = float(cy + e.h / 2);
                b.vy = 260.0f + wave * 15;
                b.color = Color(255, 80, 40);
                p_bullets.push_back(b);
            }
        }

        for (auto& b : p_bullets) b.update(dt, screen_w, screen_h);
        p_bullets.erase(std::remove_if(p_bullets.begin(), p_bullets.end(),
                        [](const Bullet& b) { return !b.alive; }), p_bullets.end());

        for (auto& tb : tractor_beams) tb.update(dt);
        tractor_beams.erase(std::remove_if(tractor_beams.begin(), tractor_beams.end(),
                            [](const TractorBeam& tb) { return !tb.active; }), tractor_beams.end());
    }

    void trigger_dive(int count, float aim_x) {
        std::vector<int> candidates;
        for (int i = 0; i < (int)enemies.size(); ++i) {
            Enemy& e = enemies[i];
            if (e.alive && e.state == "formation" && !e.captured) candidates.push_back(i);
        }
        if (candidates.empty()) return;
        int take = std::min(count, (int)candidates.size());
        // random.sample without replacement.
        for (int k = 0; k < take; ++k) {
            int j = rnd_int(k, (int)candidates.size() - 1);
            std::swap(candidates[k], candidates[j]);
            Enemy& e = enemies[candidates[k]];
            e.state = "diving";
            e.dive_speed = dive_speed();
            e.set_path(make_dive_path(e.x, e.y, aim_x, W, H));
        }
    }

    // Classic capture run: one boss leaves the formation, flies to a hover
    // point above the player's current column and HOLDS there while its beam
    // extends (the telegraph), sweeps the row, and retracts. Returns false if no
    // boss was free to do it.
    bool trigger_tractor(float player_cx) {
        if (!tractor_beams.empty()) return false;
        std::vector<int> bosses;
        for (int i = 0; i < (int)enemies.size(); ++i) {
            Enemy& e = enemies[i];
            if (e.alive && e.state == "tractor") return false;
            if (e.alive && e.kind == Kind::Boss && e.state == "formation" && !e.captured)
                bosses.push_back(i);
        }
        if (bosses.empty()) return false;
        Enemy& e = enemies[bosses[rnd_int(0, (int)bosses.size() - 1)]];
        float hx = std::clamp(player_cx - e.w / 2.0f, 40.0f, float(W - 40 - e.w));
        float hy = player_y - 300.0f;
        e.state = "tractor";
        e.tractor_phase = 0;
        e.dive_speed = dive_speed();
        e.set_path(make_hover_path(e.x, e.y, hx, hy));
        return true;
    }

    // Everything still alive breaks off and climbs away (bonus stage timed out).
    void retreat() {
        tractor_beams.clear();
        for (auto& e : enemies) if (e.alive) { e.state = "leaving"; e.released = true; }
    }

    void draw(Surface& s) const {
        for (auto& tb : tractor_beams) tb.draw(s);
        for (auto& e : enemies) e.draw(s);
        for (auto& b : p_bullets) b.draw(s);
    }
};

// ── Boss (big, fat, blocky, googly-eyed — every 5th stage) ────────────────────
struct BossDef { const char* name; Color color; int w, h, hp; float atk_iv; int attack; int face; int cell; };
const BossDef BOSSES[4] = {
    {"GORGON",     Color(90, 230, 120), 210, 150, 30, 1.5f,  0, 0, 18},
    {"THE SWARM",  Color(200, 90, 255), 300, 150, 52, 1.2f,  1, 1, 20},
    {"BEAM LORD",  Color(90, 170, 255), 340, 180, 84, 1.05f, 2, 2, 22},
    {"VOID QUEEN", Color(255, 80, 80),  430, 240, 150, 0.9f, 3, 3, 26},
};

struct MegaBoss {
    std::string name;
    Color color;
    int w, h, cell, attack, face;
    int hp, max_hp;
    float x, y, vx;
    float atk_t = 0, atk_iv, hit_flash = 0, mouth_open = 0, bob = 0;
    bool dying = false, finished = false, faking = false, did_fake = false;
    bool fake_started = false;  // one-shot flag read by the game for the gag banner
    float die_t = 0, fake_t = 0;
    static constexpr float DEATH_DUR = 1.5f;
    // grand entrance: it descends from above in heavy steps, scattering minions
    bool entering = false, just_stomped = false;
    float enter_delay = 0, stomp_t = 0, battle_y = 84, invuln_ = 0;

    // ── attack system: HP phases, telegraphed pick, one-shot + sustained patterns ──
    int tier = 1, phase = 1, cur_attack = -1, pending_attack = -1, dive_phase = 0;
    float attack_t = 0, telegraph = 0, sub_t = 0, spin = 0, laser_angle = 0;
    float dive_target_x = 0;
    bool shock_done = false;
    // giant tractor beam attack (galaga signature) — driven by the attack pool
    bool beam_active = false;
    float beam_len = 0;
    int beam_max = 360;
    bool spawn_minions = false;  // one-shot request: game spawns dive-minions
    // one-time SHED-THE-SWARM surprise: a sudden screen-wide barrage + reposition
    bool did_split = false, split_started = false;

    bool damageable() const { return !dying && !faking && !entering && invuln_ <= 0; }

    MegaBoss(int wave, int screen_w) {
        tier = wave / 5;                      // 1,2,3,...
        int idx = (tier - 1) % 4;
        int loop = (tier - 1) / 4;
        const BossDef& d = BOSSES[idx];
        const char* pre = loop == 0 ? "" : loop == 1 ? "MEGA " : loop == 2 ? "OMEGA " : "VOID ";
        name = std::string(pre) + d.name;
        color = d.color; attack = d.attack; face = d.face; cell = d.cell;
        w = d.w + loop * 24; h = d.h + loop * 12;
        max_hp = hp = int(d.hp * (1.0f + 0.5f * (tier - 1)));
        atk_iv = std::max(0.6f, d.atk_iv - loop * 0.1f);
        x = (screen_w - w) / 2.0f; y = -float(h) - 24.0f;  // glow + bob stay off-screen too
        vx = (70 + tier * 8) * (rnd01() < 0.5f ? 1 : -1);
        entering = true; enter_delay = 2.6f; battle_y = 84;
    }
    Rect rect() const { return {int(x), int(y), w, h}; }
    float hp_ratio() const { return std::max(0.0f, float(hp) / max_hp); }

    static Bullet bb(float bx, float by, float a, float spd) {
        Bullet b; b.x = bx; b.y = by;
        b.vy = spd * std::cos(a); b.vx = spd * std::sin(a);
        b.color = ORANGE; b.w = 5; b.h = 12; b.damage = 1;
        return b;
    }

    void take_damage(int dmg, ParticleSystem&) {
        if (!damageable()) return;
        hp -= dmg; hit_flash = 0.14f;
        if (hp <= 0) { dying = true; die_t = 0; hp = 0; return; }
        // a one-time fake-out death gag around a quarter health
        if (!did_fake && hp <= max_hp * 0.27f && rnd01() < 0.45f) {
            did_fake = true; faking = true; fake_t = 0; fake_started = true;
        }
    }

    // giant tractor capture rect (only while the beam is held out, attack 9)
    Rect beam_capture_rect() const {
        float cx = x + w / 2, ty = y + h;
        int bw = int(60 + beam_len * 0.5f);
        return {int(cx - bw / 2), int(ty + beam_len - 24), bw, 28};
    }
    bool beam_holding() const { return beam_active && beam_len >= beam_max - 6; }

    // ── attack pool ──────────────────────────────────────────────────────────────
    // ONE-SHOT (resolve in a single tick, then idle):
    //   0 LOB   1 FAN   2 RADIAL   3 AIMED   4 WALL(one gap)
    // SUSTAINED (run over time):
    //   5 SPIRAL   6 RAIN   7 LASER
    // GALAGA SIGNATURE (sustained):
    //   9 GIANT TRACTOR BEAM   10 DIVE-BOMB   11 MINION DIVE WAVE
    int pick_attack() {
        // The pool grows as health drops; the scary sustained attacks are gated by
        // TIER so the stage-5 first boss stays gentle and later bosses terrify.
        std::vector<int> pool = {attack, 0, 2, 11};   // signature, LOB, RADIAL, MINIONS (simple, dodgeable)
        if (phase >= 2) {
            pool.insert(pool.end(), {1, 3, 10});      // FAN, AIMED, DIVE-BOMB
            if (tier >= 2) pool.insert(pool.end(), {4, 5, 9});  // WALL, SPIRAL, TRACTOR
        }
        if (phase >= 3) {
            pool.push_back(3);                        // weight aimed up
            pool.push_back(10);                       // and the dive-bomb
            if (tier >= 2) { pool.push_back(6); pool.push_back(9); }  // RAIN, TRACTOR
            if (tier >= 3) pool.push_back(7);         // LASER
        }
        return pool[rnd_int(0, (int)pool.size() - 1)];
    }
    void begin_attack(int a) {
        cur_attack = a; attack_t = 0; sub_t = 0; dive_phase = 0; shock_done = false;
        if (a == 1 || a == 7) mouth_open = 0.7f;
        if (a == 9) { beam_active = true; beam_len = 0; }
        if (a == 10) { mouth_open = 0.5f; dive_target_x = x; }
    }
    void run_attack(float dt, float pcx, float pcy, std::vector<Bullet>& out) {
        attack_t += dt; sub_t += dt;
        float bx = x + w / 2, by = y + h - 8;
        switch (cur_attack) {
            case 0:  // LOB: three lazy arcs
                for (float t : {-0.32f, 0.0f, 0.32f}) out.push_back(bb(bx, by, t, 215));
                cur_attack = -1; break;
            case 1:  // FAN
                for (int i = -2; i <= 2; ++i) out.push_back(bb(bx, by, i * 0.17f, 250));
                cur_attack = -1; break;
            case 2:  // RADIAL
                for (int i = 0; i < 11; ++i) out.push_back(bb(bx, by, -1.3f + i * (2.6f / 10), 210));
                cur_attack = -1; break;
            case 3: {  // AIMED volley at the player
                float aim = std::atan2(pcx - bx, std::max(20.0f, pcy - by));
                for (int i = -1; i <= 1; ++i) out.push_back(bb(bx, by, aim + i * 0.12f, 300));
                cur_attack = -1; break;
            }
            case 4: {  // WALL of bullets with ONE safe gap to thread
                int gap = rnd_int(1, 9);
                for (int i = 0; i < 11; ++i) {
                    if (std::abs(i - gap) <= 1) continue;
                    float px = x + (i + 0.5f) * (w / 11.0f);
                    Bullet b; b.x = px; b.y = by; b.vx = 0; b.vy = 195;
                    b.color = ORANGE; b.w = 5; b.h = 12; out.push_back(b);
                }
                cur_attack = -1; break;
            }
            // Sustained patterns emit on a fixed cadence (`sub_t -= interval`) so the
            // bullet density is the same at 60, 144 or 240 Hz.
            case 5:  // SPIRAL: rotating twin spray
                while (sub_t >= 0.07f) {
                    sub_t -= 0.07f; spin += 0.5f;
                    out.push_back(bb(bx, by, spin, 200));
                    out.push_back(bb(bx, by, spin + PI, 200));
                }
                if (attack_t >= 1.7f) cur_attack = -1;
                break;
            case 6:  // RAIN: bullets from random points across the boss
                while (sub_t >= 0.09f) {
                    sub_t -= 0.09f;
                    out.push_back(bb(rnd_uniform(x, x + w), by, 0, 235));
                }
                if (attack_t >= 1.5f) cur_attack = -1;
                break;
            case 7:  // LASER: a sweeping beam of fast bullets
                laser_angle = -0.7f + (attack_t / 1.7f) * 1.4f;
                while (sub_t >= 0.025f) {
                    sub_t -= 0.025f;
                    out.push_back(bb(bx, by, laser_angle, 380));
                }
                if (attack_t >= 1.7f) cur_attack = -1;
                break;
            case 9: {  // GIANT TRACTOR BEAM: extend, hold (capture window), retract
                // Reach is measured to the player's row, whatever the boss's size.
                beam_max = std::max(120, int(pcy + 10 - (y + h)));
                float rate = beam_max / 1.0f;
                if (attack_t < 1.1f) beam_len = std::min(float(beam_max), beam_len + rate * dt);
                else if (attack_t < 2.9f) beam_len = float(beam_max);     // hold
                else {
                    beam_len = std::max(0.0f, beam_len - rate * 1.2f * dt);
                    if (beam_len <= 0) { beam_active = false; cur_attack = -1; }
                }
                break;
            }
            case 10: {  // DIVE-BOMB: wind up, plunge at the player, shockwave, return
                // Plunge depth comes from the player's row, so the body lands on it.
                float target = std::max(battle_y + 120.0f, pcy + 6.0f - h);
                if (dive_phase == 0) {            // telegraph wind-up: lock onto the player's column
                    dive_target_x = pcx - w / 2;
                    if (attack_t >= 0.4f) dive_phase = 1;
                } else if (dive_phase == 1) {     // plunge down, sliding toward the player's x
                    y += 880 * dt;
                    x += (dive_target_x - x) * std::min(1.0f, 6 * dt);
                    if (y >= target) { y = target; dive_phase = 2; }
                } else if (dive_phase == 2) {     // impact shockwave fan
                    if (!shock_done) {
                        shock_done = true;
                        for (int i = 0; i < 14; ++i)
                            out.push_back(bb(x + w / 2, y + h - 8, -1.5f + i * (3.0f / 13), 240));
                    }
                    dive_phase = 3;
                } else {                          // climb back to the battle line
                    y -= 680 * dt;
                    if (y <= battle_y) { y = battle_y; cur_attack = -1; }
                }
                break;
            }
            case 11:  // MINION DIVE WAVE: peel formation enemies off into dives
                spawn_minions = true;
                cur_attack = -1; break;
            default: cur_attack = -1; break;
        }
    }

    void update(float dt, float player_cx, float player_cy, int screen_w,
                std::vector<Bullet>& out, ParticleSystem& parts) {
        if (entering) {  // descend in heavy steps through the swarm
            bob += dt;
            if (enter_delay > 0) { enter_delay -= dt; return; }
            stomp_t += dt;
            if (stomp_t >= 0.28f) {
                stomp_t = 0; y += 30; just_stomped = true;
                if (y >= battle_y) { y = battle_y; entering = false; invuln_ = 1.6f; }
            }
            return;
        }
        if (hit_flash > 0) hit_flash -= dt * 6;
        if (invuln_ > 0) invuln_ -= dt;
        bob += dt;
        if (dying) {
            die_t += dt;
            beam_active = false;
            if (!finished && rnd01() < 36.0f * dt) {  // confetti / chained blasts (dt-scaled)
                float rx = x + rnd_uniform(0, w), ry = y + rnd_uniform(0, h);
                parts.ufo_explode(rx, ry, 10);
            }
            if (die_t >= DEATH_DUR) finished = true;
            return;
        }
        if (faking) {  // deflate, sulk, then bounce back
            fake_t += dt;
            beam_active = false;
            if (fake_t >= 1.1f) { faking = false; hp = int(max_hp * 0.5f); }
            return;
        }
        // sway (frozen while it dives so the plunge reads clean)
        if (!(cur_attack == 10 && dive_phase >= 1)) {
            x += vx * dt;
            if (x < 6) { x = 6; vx = std::fabs(vx); }
            if (x + w > screen_w - 6) { x = screen_w - 6 - w; vx = -std::fabs(vx); }
        }
        if (mouth_open > 0) mouth_open -= dt;

        // ── phases: faster & nastier as health drops ──
        phase = hp_ratio() > 0.6f ? 1 : hp_ratio() > 0.3f ? 2 : 3;

        // ── one-time SHED-THE-SWARM surprise (tier >= 2, on entering enrage) ──
        // The boss suddenly hops to a random side and unloads a screen-wide barrage
        // while flinging a dive wave — a "where did THAT come from" moment.
        if (!did_split && tier >= 2 && phase == 3 && telegraph <= 0 && cur_attack < 0) {
            did_split = true; split_started = true;
            beam_active = false;
            x = rnd01() < 0.5f ? 6.0f : float(screen_w - 6 - w);   // teleport-hop to a flank
            vx = (x < screen_w / 2.0f) ? std::fabs(vx) : -std::fabs(vx);
            float bx = x + w / 2, by = y + h - 8;
            for (int i = 0; i < 24; ++i)                            // full radial barrage
                out.push_back(bb(bx, by, -PI / 2 + i * (PI / 23), 215));
            for (int i = -3; i <= 3; ++i)                          // aimed cluster on top
                out.push_back(bb(bx, by, i * 0.16f, 290));
            spawn_minions = true;
            mouth_open = 0.7f;
            invuln_ = 0.4f;                                        // brief flinch frame
            atk_t = 0;
            return;
        }

        float aggr = (tier <= 1) ? 0.85f : 0.55f;  // the first boss stays calm even at low HP
        float iv = atk_iv * (phase == 3 ? aggr : phase == 2 ? 0.85f : 1.0f);

        if (telegraph > 0) {                 // winding up the next attack (readable warning ring)
            telegraph -= dt;
            if (telegraph <= 0) begin_attack(pending_attack);
        } else if (cur_attack >= 0) {        // a sustained / staged attack is running
            run_attack(dt, player_cx, player_cy, out);
        } else {                             // idle: pick the next attack after a beat
            atk_t += dt;
            if (atk_t >= iv) {
                atk_t = 0;
                pending_attack = pick_attack();
                telegraph = tier <= 1 ? 0.65f : (phase == 3 ? 0.3f : 0.5f);
            }
        }
    }

    void draw_eye(Surface& s, int ex, int ey, int er, float pcx, float pcy) const {
        draw::circle(s, WHITE, ex, ey, er);
        float dx = pcx - ex, dy = pcy - ey, d = std::max(1.0f, std::hypot(dx, dy));
        int off = er / 2;
        int px = ex + int(dx / d * off), py = ey + int(dy / d * off);
        if (hit_flash > 0.06f) {  // dazed eyes when freshly smacked
            draw::line(s, BLACK, ex - er / 2, ey - er / 2, ex + er / 2, ey + er / 2, 3);
            draw::line(s, BLACK, ex - er / 2, ey + er / 2, ex + er / 2, ey - er / 2, 3);
        } else {
            draw::circle(s, BLACK, px, py, std::max(2, er / 2));
        }
    }

    void draw_beam(Surface& s) const {
        if (!beam_active || beam_len < 4) return;
        float cx = x + w / 2, ty = y + h, by = ty + beam_len;
        int half_w = 30 + int(beam_len * 0.28f);
        for (int layer = 4; layer > 0; --layer) {
            float sc = 1.0f + layer * 0.15f;
            int alpha = std::max(0, std::min(255, int(60 * (layer / 4.0f) * (0.6f + 0.4f * std::sin(bob * 8)))));
            int lw = int(half_w * sc);
            draw::polygon(s, Color(TRACTOR_COLOR.r, TRACTOR_COLOR.g, TRACTOR_COLOR.b, alpha),
                          {{int(cx), int(ty)}, {int(cx - lw), int(by)}, {int(cx + lw), int(by)}});
        }
        int core_a = std::max(0, std::min(255, int(90 * (0.7f + 0.3f * std::sin(bob * 12)))));
        draw::polygon(s, Color(TRACTOR_COLOR.r, TRACTOR_COLOR.g, TRACTOR_COLOR.b, core_a),
                      {{int(cx), int(ty)}, {int(cx - half_w), int(by)}, {int(cx + half_w), int(by)}});
    }

    void draw(Surface& s, float pcx, float pcy) const {
        if (y + h < 0) return;  // still fully above the screen
        draw_beam(s);
        float sc = dying ? std::max(0.0f, 1 - die_t / DEATH_DUR)
                         : faking ? (1.0f - 0.4f * std::sin(PI * std::min(1.0f, fake_t / 1.1f)))
                                  : 1.0f;
        int bw = int(w * sc), bh = int(h * sc);
        int bx = int(x + (w - bw) / 2), by = int(y + (h - bh) / 2 + std::sin(bob * 2) * 4);
        if (bw < 4 || bh < 4) return;
        Color body = hit_flash > 0 ? lerp_color(color, WHITE, 0.6f) : color;
        // soft outer glow
        draw::rect(s, Color(body.r, body.g, body.b, 70), {bx - 6, by - 6, bw + 12, bh + 12}, 0, cell + 4);
        // a GIANT INVADER body (shared pixel-alien art, legs animate)
        draw_boss_alien(s, face, int(bob * 3) % 2, bx, by, bw, bh, body);
        if (invuln_ > 0) {  // spawn-shield shimmer: can't be hurt yet
            float p = 0.5f + 0.5f * std::sin(bob * 18);
            draw::rect(s, Color(120, 200, 255, int(130 * p)), {bx - 8, by - 8, bw + 16, bh + 16}, 3, cell + 6);
        }
        if (telegraph > 0) {  // wind-up warning ring (red, pulsing) so attacks are readable
            float p = 0.4f + 0.6f * std::sin(bob * 30);
            draw::rect(s, Color(255, 60, 60, int(150 * p)), {bx - 7, by - 7, bw + 14, bh + 14}, 3, cell + 5);
        }
        if (cur_attack == 7) {  // LASER: draw the sweeping beam
            int lmx = bx + bw / 2, lmy = by + bh - 6;
            int ex = lmx + int(std::sin(laser_angle) * 900), ey = lmy + int(std::cos(laser_angle) * 900);
            draw::line(s, Color(255, 80, 40, 90), lmx, lmy, ex, ey, 9);
            draw::line(s, Color(255, 200, 120), lmx, lmy, ex, ey, 3);
        }

        if (dying || faking) {  // mouth agape, eyes spinning
            int ey = by + int(bh * 0.4f);
            draw::circle(s, WHITE, bx + bw / 3, ey, bh / 10);
            draw::circle(s, WHITE, bx + 2 * bw / 3, ey, bh / 10);
            draw::circle(s, BLACK, bx + bw / 3, ey, bh / 18);
            draw::circle(s, BLACK, bx + 2 * bw / 3, ey, bh / 18);
            draw::circle(s, BLACK, bx + bw / 2, by + int(bh * 0.7f), bh / 8);  // o_o shock mouth
            return;
        }

        int er = std::max(6, bh / 8);
        int eyl = bx + bw / 3, eyr = bx + 2 * bw / 3, eyy = by + int(bh * 0.36f);
        if (face == 2) {  // cyclops
            draw_eye(s, bx + bw / 2, eyy, er * 2, pcx, pcy);
        } else {
            draw_eye(s, eyl, eyy, er, pcx, pcy);
            draw_eye(s, eyr, eyy, er, pcx, pcy);
        }
        // mouth changes with health / face
        int mx = bx + bw / 2, my = by + int(bh * 0.7f), mw = bw / 3;
        float hr = hp_ratio();
        if (face == 1 || mouth_open > 0) {  // big toothy maw
            int mh = mouth_open > 0 ? bh / 3 : bh / 6;
            draw::rect(s, BLACK, {mx - mw, my - mh / 2, mw * 2, mh}, 0, 4);
            for (int t = -mw + 6; t < mw; t += 14)
                draw::rect(s, WHITE, {mx + t, my - mh / 2, 6, mh / 3});
        } else if (hr > 0.55f) {  // happy grin
            for (int i = -mw; i <= mw; i += 4)
                draw::circle(s, BLACK, mx + i, my + int(std::sin((i + mw) / float(2 * mw) * PI) * 8), 3);
        } else if (hr > 0.28f) {  // flat / nervous
            draw::rect(s, BLACK, {mx - mw, my - 2, mw * 2, 5}, 0, 2);
        } else {  // angry zigzag
            int px = mx - mw, py = my;
            for (int i = 0; i <= 6; ++i) {
                int nx = mx - mw + i * (mw * 2 / 6), ny = my + ((i % 2) ? 7 : -7);
                draw::line(s, BLACK, px, py, nx, ny, 3);
                px = nx; py = ny;
            }
        }
        if (face == 3) {  // a tiny crown, because of course
            int cw = bw / 4, cx = bx + bw / 2 - cw / 2, cy = by - 18;
            std::vector<std::pair<int, int>> crown = {
                {cx, cy + 18}, {cx, cy}, {cx + cw / 4, cy + 10}, {cx + cw / 2, cy - 4},
                {cx + 3 * cw / 4, cy + 10}, {cx + cw, cy}, {cx + cw, cy + 18}};
            draw::polygon(s, YELLOW, crown);
        }
    }
};

struct Flash { std::string text; float x, y, life, max_life; Color color; bool big; };

// ── Game ──────────────────────────────────────────────────────────────────────
// The world keeps simulating in every state except GameOver. Entering / Dead /
// Captured / WaveClear only gate what the player and the enemies may do, so
// nothing freezes in mid-air and drops stay collectable between stages.
enum class St { Entering, Play, Dead, Captured, WaveClear, GameOver };

class GalagaGame : public Scene {
public:
    explicit GalagaGame(const Context& ctx)
        : Scene(ctx), pause_(ctx.W, ctx.H, ctx.fonts),
          initials_(ctx.W, ctx.H, ctx.fonts) {
        high_score_ = ctx.scores ? ctx.scores->high_score("galaga") : 0;
        cache();  // build sprite cache up-front
        new_game();
        if (ctx_.sfx) ctx_.sfx->start_ambient(0);  // deep space
    }

    void update(float dt, const Input& in) override;
    void draw(Surface& s) override;
    void on_exit() override {
        submit_scores();
        if (ctx_.sfx) { ctx_.sfx->stop_tractor(); ctx_.sfx->stop_ambient(); }
    }

private:
    static constexpr float AMBIENT_STAR_T = 0.12f;
    static constexpr float DIVE_INTERVAL_BASE = 4.0f, DIVE_INTERVAL_MIN = 1.2f;
    static constexpr float TRACTOR_INTERVAL = 18.0f;
    static constexpr float CHALLENGE_TIME = 20.0f;
    static constexpr float CAPTURE_LIFT = 1.3f, CAPTURE_TOTAL = 2.3f;
    static constexpr int FORMATION_TOP = 80;
    static constexpr int CAP_RAPID = 3, CAP_SPREAD = 2, CAP_SIDE = 2;

    PauseMenu pause_;
    InitialsEntry initials_;
    bool entry_started_ = false;
    ParticleSystem particles_;
    int high_score_ = 0;
    float star_t_ = 0;
    float time_acc_ = 0;  // mirrors pygame.time.get_ticks()/1000
    bool pad_ = false;    // a gamepad is connected → pad-flavoured prompts

    int score_, wave_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Formation> formation_;
    std::unique_ptr<MegaBoss> boss_;
    std::vector<Bullet> p_bullets_, boss_bullets_;
    std::vector<PowerUp> powerups_;
    St state_;
    float dead_t_ = 0, wave_t_ = 0, dive_cd_ = 0, tractor_cd_ = 0, capture_t_ = 0;
    float stage_t_ = 0, game_over_t_ = 0;
    bool boss_wave_ = false, boss_revealed_ = false, nuke_hint_ = false, new_hi_ = false;
    // capture in progress: who is hauling the ship up (null = the mega-boss),
    // where the ship started, and whether it has docked under its captor yet
    Enemy* captor_ = nullptr;
    float cap_x0_ = 0, cap_y0_ = 0;
    bool cap_docked_ = false;
    bool tractor_snd_ = false;
    // ── bonus "challenge" stage ───────────────────────────────────────────────
    bool challenge_ = false;
    float challenge_t_ = 0;            // countdown until the bonus stage ends
    int challenge_total_ = 0;         // enemies that entered the bonus stage
    int challenge_kills_ = 0;         // how many the player shot down
    bool challenge_done_ = false;     // payout already resolved
    std::vector<Flash> flash_;
    bool submitted_ = false;

    // weapon progression (persists across the run)
    int w_rapid_ = 0, w_spread_ = 0, w_side_ = 0, nukes_ = 0;
    int kills_since_drop_ = 0, volley_id_ = 0;
    float shield_ = 0, nuke_flash_ = 0;
    float pierce_t_ = 0;   // PIERCE is a timed buff
    // Rescue reward: a brief twin-shot spread so capture-then-rescue pays off.
    float twin_boost_ = 0;
    float deflect_fx_ = 0;      // shield / spawn-guard just ate a shot
    float tink_cd_ = 0;         // rate-limits the "deflected" sound

    // bot + telemetry
    bool bot_ = false;
    float game_t_ = 0, wave_t0_ = 0;
    int wave_kills_ = 0, wave_shots_ = 0, wave_pu_ = 0, wave_deaths_ = 0, wave_nukes_ = 0;
    int wave_captures_ = 0, clear_kills_ = 0, clear_acc_ = 0;

    std::string loadout() const {
        return "r" + std::to_string(w_rapid_) + "s" + std::to_string(w_spread_) +
               "w" + std::to_string(w_side_) + "p" + std::to_string(pierce_t_ > 0 ? 1 : 0);
    }
    void tele(const J& j) { if (ctx_.tele) ctx_.tele->event(j); }
    int weapon_tier() const { return w_rapid_ + w_spread_ + w_side_; }

    void new_game() {
        score_ = 0; wave_ = 0;
        entry_started_ = false; submitted_ = false; new_hi_ = false;
        w_rapid_ = w_spread_ = w_side_ = nukes_ = 0;
        kills_since_drop_ = 0; volley_id_ = 0;
        nuke_hint_ = false;
        shield_ = nuke_flash_ = pierce_t_ = deflect_fx_ = tink_cd_ = 0;
        twin_boost_ = 0;
        game_over_t_ = 0;
        player_ = std::make_unique<Player>(ctx_.W / 2 - 14, ctx_.H - 70);
        particles_.clear();
        flash_.clear();
        bot_ = std::getenv("VOID_BOT") != nullptr || ctx_.demo;
        int start = 1;
        if (const char* e = std::getenv("VOID_WAVE")) start = std::max(1, atoi(e));
        if (const char* e = std::getenv("VOID_WEAPON")) {
            int t = atoi(e);
            w_rapid_ = std::min(CAP_RAPID, t); w_spread_ = std::min(CAP_SPREAD, t);
            w_side_ = std::min(CAP_SIDE, t); nukes_ = t;
        }
        if (const char* e = std::getenv("VOID_LIVES")) player_->lives = std::max(1, atoi(e));
        if (ctx_.sfx && !ctx_.demo) ctx_.sfx->start_music(1);
        start_wave(start);
    }
    void start_wave(int w) {
        wave_ = w;
        boss_wave_ = (w % 5 == 0);
        // Bonus "challenge" stage every 4th stage (never on a boss stage): a
        // no-fire-back breather flying fancy patterns purely for points.
        challenge_ = !boss_wave_ && (w % 4 == 0);
        p_bullets_.clear(); boss_bullets_.clear(); powerups_.clear();
        captor_ = nullptr; cap_docked_ = false;
        boss_revealed_ = false;
        if (boss_wave_) {
            // the giant stomps in up top; a short two-row honor guard flies
            // beneath it instead of the full swarm hiding behind its body
            boss_ = std::make_unique<MegaBoss>(wave_, ctx_.W);
            int guard_top = int(boss_->battle_y) + boss_->h + 28;
            formation_ = std::make_unique<Formation>(ctx_.W, ctx_.H, wave_, false, true, guard_top);
        } else {
            boss_.reset();
            formation_ = std::make_unique<Formation>(ctx_.W, ctx_.H, wave_, challenge_, false, FORMATION_TOP);
        }
        formation_->player_y = player_->y;
        challenge_t_ = CHALLENGE_TIME;
        challenge_total_ = challenge_ ? (int)formation_->enemies.size() : 0;
        challenge_kills_ = 0;
        challenge_done_ = false;
        // Palette drift: a slow per-stage hue oscillation; boss stages tint warm,
        // challenge stages get a cool, dreamier shift.
        if (ctx_.fx)
            ctx_.fx->palette_hue = 0.18f * std::sin(w * 0.5f)
                                 + (boss_wave_ ? -0.12f : 0.0f)
                                 + (challenge_ ? 0.20f : 0.0f);
        state_ = St::Entering;
        dead_t_ = wave_t_ = stage_t_ = 0;
        dive_cd_ = std::max(DIVE_INTERVAL_MIN, DIVE_INTERVAL_BASE - wave_ * 0.15f);
        tractor_cd_ = TRACTOR_INTERVAL * 0.6f;   // first capture run comes a little sooner
        capture_t_ = 0;
        wave_t0_ = game_t_;
        wave_kills_ = wave_shots_ = wave_pu_ = wave_deaths_ = wave_nukes_ = wave_captures_ = 0;
        tele(J().add("ev", "stage_start").add("stage", wave_).add("boss", boss_wave_ ? 1 : 0)
                .add("challenge", challenge_ ? 1 : 0)
                .add("t", (double)game_t_).add("load", loadout()).add("lives", player_->lives));
    }
    void add_flash(const std::string& t, float x, float y, Color c, float life = 0.9f, bool big = false) {
        flash_.push_back({t, x, y, life, life, c, big});
    }
    void submit_scores() {
        if (ctx_.demo) return;  // attract demo never touches the leaderboard
        if (submitted_) return;
        submitted_ = true;
        tele(J().add("ev", "game_over").add("stage", wave_).add("score", score_)
                .add("t", (double)game_t_).add("load", loadout()));
        if (ctx_.scores) {
            new_hi_ = ctx_.scores->submit_galaga(score_, wave_);
            high_score_ = ctx_.scores->high_score("galaga");
        }
    }
    void enter_game_over() {
        state_ = St::GameOver;
        game_over_t_ = 0;
        submit_scores();
        tractor_snd_ = false;
        clear_enemy_fire();
        formation_->retreat();   // the swarm peels away, leaving the panel a clear sky
        if (ctx_.sfx) { ctx_.sfx->stop_tractor(); ctx_.sfx->game_over(); }
    }

    Color tier_color() const {
        int t = weapon_tier();
        if (t <= 1) return CYAN;
        if (t <= 4) return lerp_color(CYAN, YELLOW, (t - 1) / 3.0f);
        return lerp_color(YELLOW, WHITE, std::min(1.0f, (t - 4) / 4.0f));
    }

    // Drops get rarer the stronger you already are — flattens the snowball and
    // lets a freshly reset (post-death) player recover fast.
    float drop_penalty() const { return std::clamp(1.0f - weapon_tier() * 0.16f, 0.2f, 1.0f); }
    void spawn_powerup(float x, float y) {
        int r = rnd_int(0, 99);
        PU k;
        if (r < 26) k = PU::Rapid; else if (r < 50) k = PU::Spread; else if (r < 68) k = PU::Side;
        else if (r < 80) k = PU::Pierce; else if (r < 89) k = PU::Shield;
        else if (r < 95) k = PU::Nuke; else if (r < 98) k = PU::Life; else k = PU::Mystery;
        powerups_.push_back({std::clamp(x, 20.0f, ctx_.W - 20.0f), std::max(40.0f, y), 0, k});
    }
    void maybe_drop(float x, float y, float base) {
        if (rnd01() < base * drop_penalty()) spawn_powerup(x, y);
    }
    // Per-kill drop with a pity timer so the first power-up never drags.
    void roll_enemy_drop(float x, float y) {
        ++kills_since_drop_;
        bool drop = (kills_since_drop_ >= 12 && weapon_tier() < 2) ||
                    rnd01() < 0.13f * drop_penalty();
        if (drop) { kills_since_drop_ = 0; spawn_powerup(x, y); }
    }
    void apply_powerup(PU k) {
        if (k == PU::Mystery) k = rnd_choice<PU>({PU::Rapid, PU::Spread, PU::Side, PU::Pierce, PU::Nuke, PU::Shield});
        std::string msg;
        // A weapon that is already maxed pays out in points instead of nothing.
        auto level_up = [&](int& lvl, int cap, const char* name) {
            if (lvl >= cap) { score_ += 500; msg = std::string(name) + " MAXED  +500"; }
            else { ++lvl; msg = std::string(name) + (lvl >= cap ? " MAX!" : "!"); }
        };
        switch (k) {
            case PU::Rapid:  level_up(w_rapid_, CAP_RAPID, "RAPID FIRE"); break;
            case PU::Spread: level_up(w_spread_, CAP_SPREAD, "SPREAD SHOT"); break;
            case PU::Side:   level_up(w_side_, CAP_SIDE, "WING CANNONS"); break;
            case PU::Pierce: pierce_t_ = std::min(12.0f, pierce_t_ + 8.0f); msg = "PIERCING ROUNDS!"; break;
            case PU::Shield: shield_ = 7.0f; msg = "SHIELD UP!"; break;
            case PU::Life:
                player_->lives++; msg = "EXTRA LIFE!";
                if (ctx_.sfx) ctx_.sfx->one_up();
                break;
            case PU::Nuke:
                nukes_++; msg = "NUKE ACQUIRED!";
                if (!nuke_hint_) {
                    nuke_hint_ = true;
                    add_flash(std::string("PRESS  ") + ui::nuke_key(pad_) + "  TO NUKE!",
                              ctx_.W / 2, ctx_.H / 2 + 50, RED, 3.0f, true);
                }
                break;
            default: break;
        }
        auto [pcx, pcy] = player_->center();
        add_flash(msg, pcx, pcy - 30, YELLOW, 1.2f);
        wave_pu_++;
        tele(J().add("ev", "powerup").add("stage", wave_).add("t", (double)game_t_)
                .add("load", loadout()).add("nukes", nukes_));
        if (ctx_.sfx) ctx_.sfx->powerup_collect();
        if (ctx_.fx) ctx_.fx->rumble_pickup();
    }
    // A boss carrying a captured fighter was destroyed: the fighter comes home.
    void rescue_fighter() {
        player_->dual_fighter = true;
        player_->x = std::min(player_->x, float(ctx_.W - player_->cur_w()));
        twin_boost_ = 8.0f;   // reward: temporary twin-shot spread
        add_flash("FIGHTER RESCUED!", ctx_.W / 2, ctx_.H / 2 + 60, CYAN, 2.0f);
        add_flash("TWIN SPREAD!", ctx_.W / 2, ctx_.H / 2 + 84, ORANGE, 1.6f, true);
        if (ctx_.sfx) ctx_.sfx->rescue();
    }
    void detonate_nuke() {
        if (nukes_ <= 0 || !player_->alive || player_->captured) return;
        if (state_ != St::Play && state_ != St::Entering) return;
        nukes_--;
        nuke_flash_ = 1.0f;
        boss_bullets_.clear();
        if (formation_) formation_->p_bullets.clear();
        wave_nukes_++;
        tele(J().add("ev", "nuke").add("stage", wave_).add("t", (double)game_t_));
        if (formation_) {
            // Vaporises weak enemies; armored grunts are heavily chipped but
            // survive — a nuke softens an armored stage, it doesn't skip it.
            // Only what is actually on screen is caught in the blast.
            for (auto& e : formation_->enemies) {
                if (!e.alive || !e.released || e.state == "leaving") continue;
                e.hp -= 3;
                auto [cx, cy] = e.center();
                if (e.hp <= 0) {
                    e.alive = false; wave_kills_++; score_ += e.score_value(true);
                    particles_.explode_invader(cx, cy, e.color());
                    if (e.captured) rescue_fighter();
                } else {
                    e.hit_flash = 1.0f;
                    particles_.explode_invader(cx, cy, e.color(), 8);
                }
            }
        }
        if (boss_ && !boss_->dying) {
            float fx = boss_->x + boss_->w / 2;
            float fy = std::max(110.0f, boss_->y + boss_->h / 2);
            if (boss_->damageable()) {
                int dmg = std::max(25, boss_->max_hp / 5 + 20);
                boss_->take_damage(dmg, particles_);
                add_flash("DIRECT HIT!", fx, fy, YELLOW, 1.2f, true);
            } else {  // entrance / spawn-shield / fake-out: say so instead of lying
                add_flash("BOSS IMMUNE!", fx, fy, ARMOR_COLOR, 1.2f, true);
                if (ctx_.sfx) ctx_.sfx->armor_tink();
            }
        }
        add_flash("* NUKE *", ctx_.W / 2, ctx_.H / 2 - 30, RED, 1.4f, true);
        if (ctx_.sfx) ctx_.sfx->big_explosion();
        if (ctx_.fx) {
            ctx_.fx->shake(26); ctx_.fx->hitstop(0.07f);
            ctx_.fx->rumble_explosion();
            ctx_.fx->shockwave(0.5f, 0.5f);
        }
    }
    // A shot bounced off the shield / spawn-guard / an immune boss.
    void deflect(float x, float y, bool on_player) {
        particles_.bunker_hit(x, y, 6);
        if (on_player) deflect_fx_ = 0.25f;
        if (tink_cd_ <= 0 && ctx_.sfx) {
            tink_cd_ = 0.08f;
            ctx_.sfx->pan_next(x / float(ctx_.W));
            if (on_player) ctx_.sfx->shield_hit(); else ctx_.sfx->armor_tink();
        }
    }
    void clear_enemy_fire() {  // pop every hostile shot with a small spark
        auto pop = [&](std::vector<Bullet>& bs) {
            for (auto& b : bs) if (b.alive) particles_.bunker_hit(b.x, b.y, 3);
            bs.clear();
        };
        if (formation_) pop(formation_->p_bullets);
        pop(boss_bullets_);
    }
    bool vulnerable() const {
        return player_->alive && !player_->captured && player_->invincible <= 0 && shield_ <= 0;
    }
    void begin_capture(Enemy* captor) {
        player_->captured = true;
        state_ = St::Captured;
        capture_t_ = 0;
        captor_ = captor;
        cap_x0_ = player_->x; cap_y0_ = player_->y;
        cap_docked_ = false;
        wave_captures_++;
        add_flash("FIGHTER CAPTURED!", ctx_.W / 2, ctx_.H - 130, Color(200, 120, 255), 2.2f, true);
        if (ctx_.sfx) ctx_.sfx->capture();
        if (ctx_.fx) { ctx_.fx->shake(8); ctx_.fx->rumble(0.5f, 0.5f, 500); }
    }
    // Where the hauled-up ship is right now (top-left), and how far along (0..1).
    Pt capture_pos(float& u) const {
        u = std::clamp(capture_t_ / CAPTURE_LIFT, 0.0f, 1.0f);
        float e = u * u * (3 - 2 * u);
        float tx = cap_x0_, ty = cap_y0_ - 200;
        if (captor_) {
            tx = captor_->x + captor_->w / 2.0f - player_->w / 2.0f;
            ty = captor_->y + captor_->h + 4;
        } else if (boss_) {
            tx = boss_->x + boss_->w / 2.0f - player_->w / 2.0f;
            ty = boss_->y + boss_->h * 0.7f;
        }
        float wob = std::sin(capture_t_ * 22) * 5 * (1 - e);
        return {cap_x0_ + (tx - cap_x0_) * e + wob, cap_y0_ + (ty - cap_y0_) * e};
    }

    void fire();
    void update_player_bullets(float dt);
    void check_player_bullet_hits();
    bool check_enemy_bullet_hits();
    bool check_tractor_beam();
    bool check_collisions();
    void update_boss(float dt);
    void update_powerups(float dt);
    void step_world(float dt, const Input& in);
    void update_captured(float dt, const Input& in);
    void hit_player();
    void bot_decide(float& move_dir, bool& want_fire, bool& want_nuke);
    void draw_hud(Surface& s);
    void draw_flash(Surface& s);
    void draw_banner(Surface& s);
};

// ── firing (weapon-stacking) ──────────────────────────────────────────────────
void GalagaGame::fire() {
    float delay = std::max(0.10f, 0.20f * (1.0f - 0.09f * w_rapid_));
    if (player_->fire_cd > 0 || !player_->alive || player_->captured) return;
    // Cap by VOLLEYS still in flight (each trigger-pull is one, however many
    // bolts it throws), so extra barrels never slow the gun down.
    int volleys = 0, last = -1;
    for (auto& b : p_bullets_)
        if (b.alive && b.volley != last) { ++volleys; last = b.volley; }
    if (volleys >= Player::MAX_VOLLEYS) return;
    player_->fire_cd = delay;
    ++volley_id_;
    float px = player_->x + player_->cur_w() / 2.0f, py = player_->y;
    Color col = tier_color();
    int dmg = 1 + weapon_tier() / 4;
    int bolts = 0;
    auto add = [&](float x, float vx) {
        Bullet b; b.x = x; b.y = py; b.vx = vx; b.vy = -520;
        b.color = col; b.pierce = pierce_t_ > 0 ? 2 : 0; b.damage = dmg;
        b.w = 3 + std::min(3, weapon_tier() / 2);
        b.volley = volley_id_;
        p_bullets_.push_back(b);
        ++bolts;
    };
    add(px, 0);
    // dual fighter keeps its twin barrels
    if (player_->dual_fighter) {
        add(px - player_->w / 2 - 4, 0);
        add(px + player_->w / 2 + 4, 0);
    }
    for (int i = 1; i <= w_side_; ++i) { add(px - i * 11, 0); add(px + i * 11, 0); }
    for (int i = 1; i <= w_spread_; ++i) { add(px, -62.0f * i); add(px, 62.0f * i); }
    // Rescue twin-spread: two extra angled bolts fanning outward.
    if (twin_boost_ > 0) { add(px, -150.0f); add(px, 150.0f); }
    wave_shots_ += bolts;
    if (ctx_.sfx) ctx_.sfx->shoot();
}

void GalagaGame::update_player_bullets(float dt) {
    const float trail_p = 30.0f * dt;  // ≈ every other frame at 60 Hz, at any refresh rate
    for (auto& b : p_bullets_) {
        b.update(dt, ctx_.W, ctx_.H);
        if (rnd01() < trail_p) { auto [tx, ty] = b.tip(); particles_.bullet_trail(tx, ty, b.color, true); }
    }
    p_bullets_.erase(std::remove_if(p_bullets_.begin(), p_bullets_.end(),
                     [](const Bullet& b) { return !b.alive; }), p_bullets_.end());
}

void GalagaGame::check_player_bullet_hits() {
    for (auto& b : p_bullets_) {
        if (!b.alive) continue;
        for (auto& e : formation_->enemies) {
            if (!e.alive || !e.released || e.state == "leaving" || !overlap(b.rect(), e.rect())) continue;
            e.hp -= b.damage; e.hit_flash = 1.0f;
            bool destroyed = e.hp <= 0;
            auto [cx, cy] = e.center();
            if (destroyed) {
                e.alive = false;
                wave_kills_++;
                int pts = e.score_value(formation_->in_formation(e));
                // A boss shot down while it still holds a fighter frees it. (If the
                // capture is still in progress the player simply breaks free instead.)
                bool rescue = e.captured && state_ != St::Captured;
                if (rescue) pts *= 2;
                // COMBO: chained skill kills multiply points (capture bonus included).
                int mult = ctx_.combo ? ctx_.combo->hit() : 1;
                pts *= mult;
                score_ += pts;
                particles_.explode_invader(cx, cy, e.color(), 22);
                Color fc = mult >= 4 ? ORANGE : YELLOW;
                add_flash("+" + std::to_string(pts), cx, cy - 20, fc);
                roll_enemy_drop(cx, cy);
                if (ctx_.sfx) {
                    ctx_.sfx->pan_next(cx / float(ctx_.W));
                    if (e.kind == Kind::Boss) ctx_.sfx->boss_explosion();
                    else ctx_.sfx->invader_hit();
                }
                if (ctx_.fx) {
                    if (e.kind == Kind::Boss) { ctx_.fx->shake(11); ctx_.fx->hitstop(0.06f); ctx_.fx->rumble(0.6f, 0.7f, 220); }
                    else { ctx_.fx->shake(4); ctx_.fx->rumble(0.25f, 0.4f, 90); }
                }
                if (rescue) rescue_fighter();
            } else {
                particles_.bunker_hit(float(cx), float(cy), 7);
                if (ctx_.sfx) {
                    ctx_.sfx->pan_next(cx / float(ctx_.W));
                    if (e.kind == Kind::Armor) ctx_.sfx->armor_tink(); else ctx_.sfx->invader_hit();
                }
            }
            // Pierce only carries through enemies it KILLS outright — armored
            // grunts that merely get chipped stop the bolt cold.
            if (destroyed && b.pierce > 0) b.pierce--; else b.alive = false;
            break;
        }
        // bolts can hit the mega-boss (no pierce-through)
        if (b.alive && boss_ && !boss_->dying && overlap(b.rect(), boss_->rect())) {
            auto [tx, ty] = b.tip();
            if (boss_->damageable()) {
                boss_->take_damage(b.damage, particles_);
                score_ += 5 * b.damage;
                particles_.explode_invader(tx, ty, boss_->color, 6);
                if (ctx_.sfx) ctx_.sfx->invader_hit();
                if (ctx_.fx) ctx_.fx->shake(2);
            } else {  // entrance / spawn-shield / fake-out: the bolt visibly bounces
                deflect(float(tx), float(ty), false);
            }
            b.alive = false;
        }
    }
}

// Returns true if the player was hit (state changed).
bool GalagaGame::check_enemy_bullet_hits() {
    if (!player_->alive || player_->captured) return false;
    const bool safe = player_->invincible > 0 || shield_ > 0;
    auto process = [&](std::vector<Bullet>& bs) -> bool {
        for (auto& b : bs) {
            if (!b.alive || !overlap(b.rect(), player_->rect())) continue;
            b.alive = false;
            if (safe) { deflect(b.x, b.y, true); continue; }  // shield / spawn-guard eats it
            hit_player();
            return true;
        }
        return false;
    };
    return process(formation_->p_bullets) || process(boss_bullets_);
}

// Returns true if the player was captured (state changed).
bool GalagaGame::check_tractor_beam() {
    if (player_->captured || !player_->alive || player_->invincible > 0) return false;
    // small-boss formation tractor beams (classic capture)
    for (auto& tb : formation_->tractor_beams) {
        if (tb.phase == "hold" && tb.boss->alive && overlap(tb.capture_rect(), player_->rect())) {
            tb.phase = "retract";   // it has what it came for
            begin_capture(tb.boss);
            return true;
        }
    }
    // mega-boss giant tractor beam (galaga-flavored boss attack)
    if (boss_ && boss_->beam_holding() && overlap(boss_->beam_capture_rect(), player_->rect())) {
        begin_capture(nullptr);
        return true;
    }
    return false;
}

// Body contact: the dive-bombing mega-boss and diving enemies. True if hit.
bool GalagaGame::check_collisions() {
    if (!vulnerable()) return false;
    // DIVE-BOMB body contact: the plunging boss flattens an unshielded fighter.
    if (boss_ && boss_->cur_attack == 10 && boss_->dive_phase >= 1 &&
        overlap(player_->rect(), boss_->rect())) {
        hit_player();
        return true;
    }
    // A DIVING enemy that flies INTO you crashes your fighter. Bonus stages are
    // exempt: there the swarm is pure target practice.
    if (challenge_) return false;
    for (auto& e : formation_->enemies) {
        if (!e.alive || e.state != "diving") continue;
        if (!overlap(player_->rect(), e.rect())) continue;
        e.alive = false;
        auto [cx, cy] = e.center();
        particles_.explode_invader(cx, cy, e.color(), 18);
        hit_player();
        return true;
    }
    return false;
}

void GalagaGame::update_boss(float dt) {
    if (!boss_) return;
    auto [pcx, pcy] = player_->center();
    size_t bb_before = boss_bullets_.size();
    boss_->update(dt, pcx, pcy, ctx_.W, boss_bullets_, particles_);
    if (boss_bullets_.size() > bb_before && ctx_.sfx) {  // give boss fire a voice
        ctx_.sfx->pan_next((boss_->x + boss_->w / 2) / float(ctx_.W));
        ctx_.sfx->enemy_shot();
    }

    if (boss_->just_stomped) {
        boss_->just_stomped = false;
        if (!boss_revealed_) {  // the surprise reveal mid-stage
            boss_revealed_ = true;
            add_flash("!! " + boss_->name + " !!", ctx_.W / 2, ctx_.H / 2 + 60, RED, 1.8f, true);
            if (ctx_.sfx) ctx_.sfx->boss_alarm();
        }
        if (formation_)  // crush any honor-guard unlucky enough to be underfoot
            for (auto& e : formation_->enemies)
                if (e.alive && e.released && overlap(e.rect(), boss_->rect())) {
                    e.alive = false;
                    auto [cx, cy] = e.center();
                    particles_.explode_invader(cx, cy, e.color(), 12);
                    maybe_drop(cx, cy, 0.30f);
                }
        for (int i = 0; i < 6; ++i)
            particles_.bunker_hit(boss_->x + rnd_uniform(0, boss_->w), boss_->y + boss_->h);
        if (ctx_.fx) { ctx_.fx->shake(15); ctx_.fx->rumble_boss_step(); }
        if (ctx_.sfx) ctx_.sfx->bunker_hit();
    }
    if (boss_->fake_started) {
        boss_->fake_started = false;
        add_flash("...JUST KIDDING!", boss_->x + boss_->w / 2, boss_->y + boss_->h + 8, YELLOW, 1.6f, true);
    }
    if (boss_->split_started) {  // the SHED-THE-SWARM surprise barrage
        boss_->split_started = false;
        add_flash("IT SHEDS THE SWARM!", ctx_.W / 2, ctx_.H / 2 + 60, RED, 1.8f, true);
        if (ctx_.sfx) { ctx_.sfx->boss_explosion(); ctx_.sfx->dive_warble(); }
        if (ctx_.fx) { ctx_.fx->shake(18); ctx_.fx->hitstop(0.05f); ctx_.fx->rumble(0.8f, 0.8f, 260); }
    }
    if (boss_->spawn_minions) {  // boss summons a wave of dive-minions
        boss_->spawn_minions = false;
        if (formation_) formation_->trigger_dive(2 + wave_ / 5, float(pcx));
        if (ctx_.sfx) ctx_.sfx->dive_warble();
    }
}

void GalagaGame::update_powerups(float dt) {
    for (auto& p : powerups_) {
        p.update(dt, ctx_.H);
        if (p.alive && player_->alive && !player_->captured && overlap(p.rect(), player_->rect())) {
            p.alive = false;
            apply_powerup(p.kind);
        }
    }
    powerups_.erase(std::remove_if(powerups_.begin(), powerups_.end(),
                    [](const PowerUp& p) { return !p.alive; }), powerups_.end());
}

void GalagaGame::hit_player() {
    auto [cx, cy] = player_->center();
    // A dual fighter takes the hit on its wingman: lose the second ship, keep
    // the life and the arsenal, and get a moment of grace.
    if (player_->dual_fighter) {
        particles_.explode_player(player_->x + player_->w + 8 + player_->w / 2.0f, cy, 30);
        player_->dual_fighter = false;
        player_->invincible = 1.5f;
        twin_boost_ = 0;
        add_flash("WINGMAN LOST!", cx, cy - 44, RED, 1.6f, true);
        if (ctx_.sfx) ctx_.sfx->player_death();
        if (ctx_.fx) { ctx_.fx->shake(12); ctx_.fx->hitstop(0.06f); ctx_.fx->rumble_hit(); }
        return;
    }
    particles_.explode_player(cx, cy);
    // Death costs you your arsenal — start the weapon climb over (nukes survive).
    bool had_weapons = w_rapid_ || w_spread_ || w_side_ || pierce_t_ > 0;
    w_rapid_ = w_spread_ = w_side_ = 0; pierce_t_ = 0;
    shield_ = 0;
    twin_boost_ = 0;
    if (had_weapons) add_flash("WEAPONS LOST!", cx, cy - 44, RED, 1.8f, true);
    player_->kill();
    state_ = St::Dead;
    dead_t_ = 2.0f;
    wave_deaths_++;
    tele(J().add("ev", "death").add("stage", wave_).add("t", (double)game_t_)
            .add("load", loadout()).add("lives_left", player_->lives));
    if (ctx_.sfx) ctx_.sfx->player_death();
    if (ctx_.fx) { ctx_.fx->shake(16); ctx_.fx->hitstop(0.09f); ctx_.fx->rumble_death(); }
}

// One step of the whole world, called in every state but GameOver:
//   Entering  — squads fly in; the ship fights normally (nuke included)
//   Play      — everything, plus dive / tractor timers and stage-clear checks
//   Dead      — enemies keep flying but hold fire; shots and drops keep moving
//   Captured  — same, while the ship is hauled up the beam
//   WaveClear — no hostiles; the ship mops up falling drops
void GalagaGame::step_world(float dt, const Input& in) {
    const bool live = state_ == St::Entering || state_ == St::Play;
    const bool can_act = player_->alive && !player_->captured &&
                         (live || state_ == St::WaveClear);
    float move_dir = 0; bool want_fire = false, want_nuke = false;
    if (can_act) {
        if (bot_) {
            bot_decide(move_dir, want_fire, want_nuke);
        } else {
            move_dir = float(in.down(SDLK_RIGHT) || in.down(SDLK_d)) -
                       float(in.down(SDLK_LEFT) || in.down(SDLK_a));
            want_fire = in.down(SDLK_SPACE) || in.down(SDLK_UP) || in.down(SDLK_w);
            want_nuke = in.just(SDLK_b);
        }
    }
    if (want_nuke) detonate_nuke();
    if (live) {  // timed buffs only burn while the fight is actually on
        if (shield_ > 0) shield_ -= dt;
        if (pierce_t_ > 0) pierce_t_ -= dt;
        if (twin_boost_ > 0) twin_boost_ -= dt;
    }
    if (deflect_fx_ > 0) deflect_fx_ -= dt;
    if (tink_cd_ > 0) tink_cd_ -= dt;
    player_->update(dt, move_dir, ctx_.W);
    if (want_fire) fire();

    formation_->player_y = player_->y;
    formation_->update(dt, ctx_.H, ctx_.W, live);
    if (ctx_.sfx && live && formation_->anim_ticked) ctx_.sfx->march_tick();
    update_boss(dt);
    for (auto& b : boss_bullets_) b.update(dt, ctx_.W, ctx_.H);
    update_player_bullets(dt);
    check_player_bullet_hits();

    if (state_ == St::Play) {
        float pcx = player_->x + player_->cur_w() / 2.0f;
        // dive timer — more divers, more often, each stage. On a challenge stage the
        // whole swarm peels off in rapid fancy dives, since it's all about the show.
        dive_cd_ -= dt;
        if (dive_cd_ <= 0) {
            int count = challenge_ ? 4 + (wave_ / 4) : 1 + (wave_ / 3);
            formation_->trigger_dive(count, pcx);
            dive_cd_ = challenge_ ? 0.8f
                                  : std::max(DIVE_INTERVAL_MIN, DIVE_INTERVAL_BASE - wave_ * 0.12f);
            if (ctx_.sfx) ctx_.sfx->dive_warble();
        }
        // tractor run (non-boss, non-challenge stages): a boss flies down to hover
        // over the player's column and opens its beam.
        if (!boss_ && !challenge_ && !player_->dual_fighter) {
            tractor_cd_ -= dt;
            if (tractor_cd_ <= 0) {
                bool carrying = false;  // one captured fighter at a time
                for (auto& e : formation_->enemies) if (e.alive && e.captured) carrying = true;
                if (!carrying && formation_->trigger_tractor(pcx)) {
                    tractor_cd_ = TRACTOR_INTERVAL + rnd_uniform(-3, 3);
                    if (ctx_.sfx) ctx_.sfx->dive_warble();
                } else {
                    tractor_cd_ = 3.0f;  // nobody free right now: try again shortly
                }
            }
        }
    }
    if (state_ == St::Entering && formation_->entry_done()) state_ = St::Play;

    if (live) {
        // each of these may end the player's turn (Dead / Captured)
        if (!check_enemy_bullet_hits() && !check_tractor_beam()) check_collisions();
    }
    boss_bullets_.erase(std::remove_if(boss_bullets_.begin(), boss_bullets_.end(),
                        [](const Bullet& b) { return !b.alive; }), boss_bullets_.end());
    update_powerups(dt);

    // tractor loop follows whatever beam is out. start_tractor() is a no-op
    // while it is already playing, so this also restores it after a pause.
    if (ctx_.sfx) {
        bool beam = !formation_->tractor_beams.empty() ||
                    (boss_ && boss_->beam_active && boss_->beam_len > 8);
        if (beam) ctx_.sfx->start_tractor();
        else if (tractor_snd_) ctx_.sfx->stop_tractor();
        tractor_snd_ = beam;
    }

    // ── boss finished its death throes (rains power-ups, the guard goes with it) ──
    if (boss_ && boss_->finished) {
        score_ += 500 * (wave_ / 5);
        float rainx = std::clamp(boss_->x + boss_->w / 2, 80.0f, ctx_.W - 80.0f);
        float rainy = std::max(60.0f, boss_->y + 40);
        powerups_.push_back({rainx, rainy, 0, PU::Nuke});
        powerups_.push_back({rainx - 50, rainy, 0, PU::Side});
        powerups_.push_back({rainx + 50, rainy, 0, PU::Spread});
        add_flash(boss_->name + " GOES SPLAT!", ctx_.W / 2, ctx_.H / 2 + 60, YELLOW, 2.2f, true);
        if (ctx_.sfx) ctx_.sfx->big_explosion();
        if (ctx_.fx) {
            ctx_.fx->shake(30); ctx_.fx->hitstop(0.10f);
            ctx_.fx->rumble_explosion();
            ctx_.fx->shockwave((boss_->x + boss_->w / 2) / float(ctx_.W),
                               (boss_->y + boss_->h / 2) / float(ctx_.H));
        }
        tele(J().add("ev", "boss_clear").add("stage", wave_).add("dt", (double)(game_t_ - wave_t0_))
                .add("name", boss_->name).add("load", loadout()).add("lives", player_->lives));
        boss_.reset();
        // The honor guard doesn't outlive its boss: every straggler pops with it.
        for (auto& e : formation_->enemies)
            if (e.alive) {
                e.alive = false;
                if (!e.released) continue;
                auto [cx, cy] = e.center();
                particles_.explode_invader(cx, cy, e.color(), 14);
                score_ += e.score_value(true);
            }
        formation_->tractor_beams.clear();
        clear_enemy_fire();
    }

    if (state_ != St::Play) return;

    auto finish_stage = [&]() {
        clear_kills_ = wave_kills_;
        clear_acc_ = wave_shots_ > 0 ? std::min(100, wave_kills_ * 100 / wave_shots_) : 0;
        clear_enemy_fire();
        state_ = St::WaveClear;
        wave_t_ = 2.4f;
        if (ctx_.sfx) ctx_.sfx->level_clear();
    };

    // ── challenge (bonus) stage: ends on a timer or when the swarm is cleared ──
    if (challenge_ && !challenge_done_) {
        challenge_t_ -= dt;
        bool swept = formation_->alive_count() == 0;
        if (swept || challenge_t_ <= 0) {
            challenge_done_ = true;
            challenge_kills_ = wave_kills_;
            int hit_bonus = challenge_kills_ * 100;
            score_ += hit_bonus;
            bool perfect = challenge_total_ > 0 && challenge_kills_ >= challenge_total_;
            add_flash(std::to_string(challenge_kills_) + " HIT  +" + std::to_string(hit_bonus),
                      ctx_.W / 2, ctx_.H / 2 + 110, YELLOW, 2.2f, true);
            if (perfect) {
                int perfect_bonus = 5000;
                score_ += perfect_bonus;
                add_flash("PERFECT!  +" + std::to_string(perfect_bonus),
                          ctx_.W / 2, ctx_.H / 2 + 138, ORANGE, 2.4f, true);
                if (ctx_.fx) { ctx_.fx->shake(14); ctx_.fx->rumble(0.7f, 0.7f, 300); }
            }
            tele(J().add("ev", "challenge_clear").add("stage", wave_)
                    .add("hits", challenge_kills_).add("total", challenge_total_)
                    .add("perfect", perfect ? 1 : 0).add("load", loadout()));
            formation_->retreat();   // survivors peel away instead of freezing
            finish_stage();
        }
        return;   // challenge stages resolve here, never via the normal clear path
    }

    // ── stage clear: all formation enemies gone AND boss (if any) gone ──
    if (formation_->alive_count() == 0 && !boss_) {
        tele(J().add("ev", "stage_clear").add("stage", wave_).add("dt", (double)(game_t_ - wave_t0_))
                .add("kills", wave_kills_).add("shots", wave_shots_).add("pu", wave_pu_)
                .add("deaths", wave_deaths_).add("nukes_used", wave_nukes_)
                .add("captures", wave_captures_).add("load", loadout()).add("lives", player_->lives));
        finish_stage();
    }
}

void GalagaGame::update_captured(float dt, const Input& in) {
    capture_t_ += dt;
    step_world(dt, in);
    if (state_ != St::Captured) return;
    // Shoot the captor down mid-haul (shots already in flight count) and the
    // fighter drops free.
    bool captor_gone = captor_ ? !captor_->alive : (!boss_ || boss_->dying);
    if (captor_gone) {
        player_->captured = false;
        player_->invincible = 1.5f;
        captor_ = nullptr;
        state_ = formation_->entry_done() ? St::Play : St::Entering;
        add_flash("BROKE FREE!", ctx_.W / 2, ctx_.H - 130, CYAN, 1.6f, true);
        if (ctx_.sfx) ctx_.sfx->rescue();
        return;
    }
    if (!cap_docked_ && capture_t_ >= CAPTURE_LIFT) {
        cap_docked_ = true;
        if (captor_) captor_->captured = true;   // now it carries the fighter home
        if (ctx_.fx) ctx_.fx->shake(5);
    }
    if (capture_t_ >= CAPTURE_TOTAL) {
        // capture costs a life and the arsenal (mirror the death reset) — and says so
        bool had_weapons = w_rapid_ || w_spread_ || w_side_ || pierce_t_ > 0;
        w_rapid_ = w_spread_ = w_side_ = 0; pierce_t_ = 0;
        shield_ = 0; twin_boost_ = 0;
        player_->captured = false;
        player_->kill();
        wave_deaths_++;
        captor_ = nullptr;
        add_flash(had_weapons ? "FIGHTER LOST  -  WEAPONS LOST!" : "FIGHTER LOST!",
                  ctx_.W / 2, ctx_.H - 130, RED, 1.8f, true);
        tele(J().add("ev", "death").add("stage", wave_).add("t", (double)game_t_)
                .add("load", loadout()).add("lives_left", player_->lives));
        if (ctx_.fx) ctx_.fx->rumble_hit();
        if (player_->lives > 0) {
            clear_enemy_fire();
            player_->respawn(ctx_.W / 2 - 14);
            state_ = formation_->entry_done() ? St::Play : St::Entering;
            if (ctx_.sfx) ctx_.sfx->spawn_chime();
        } else {
            enter_game_over();
        }
    }
}

void GalagaGame::update(float dt, const Input& in) {
    pad_ = in.pad_connected;
    time_acc_ += dt;

    // Pause works everywhere except the game-over screen (where P / START = menu).
    // ESC opens the pause menu too (quit lives there) instead of dropping the run.
    if ((in.just(SDLK_p) || (in.just(SDLK_ESCAPE) && !pause_.active())) && state_ != St::GameOver) {
        pause_.toggle();
        if (ctx_.sfx && pause_.active()) ctx_.sfx->stop_tractor();
    }
    if (pause_.active()) {
        if (pause_.update(in) == PauseResult::QuitToMenu) { submit_scores(); quit_to_menu = true; }
        return;
    }
    if (in.just(SDLK_ESCAPE) && initials_.active()) {
        quit_to_menu = true;
        return;
    }

    game_t_ += dt;
    stage_t_ += dt;
    star_t_ += dt;
    if (star_t_ >= AMBIENT_STAR_T) { star_t_ -= AMBIENT_STAR_T; particles_.ambient_star(ctx_.W, ctx_.H); }
    particles_.update(dt);
    if (nuke_flash_ > 0) nuke_flash_ -= dt * 1.6f;
    for (auto& f : flash_) f.life -= dt;
    flash_.erase(std::remove_if(flash_.begin(), flash_.end(),
                 [](const Flash& f) { return f.life <= 0; }), flash_.end());

    if (state_ == St::Entering || state_ == St::Play) {
        step_world(dt, in);
    } else if (state_ == St::Dead) {
        dead_t_ -= dt;
        step_world(dt, in);
        if (state_ == St::Dead && dead_t_ <= 0) {
            if (player_->lives > 0) {
                clear_enemy_fire();  // respawn into a clean sky
                player_->respawn(ctx_.W / 2 - 14);
                state_ = formation_->entry_done() ? St::Play : St::Entering;
                if (ctx_.sfx) ctx_.sfx->spawn_chime();
            } else {
                enter_game_over();
            }
        }
    } else if (state_ == St::Captured) {
        update_captured(dt, in);
    } else if (state_ == St::WaveClear) {
        wave_t_ -= dt;
        step_world(dt, in);
        // Hold the next stage until any drops still falling are grabbed or gone.
        if (state_ == St::WaveClear && wave_t_ <= 0 && (powerups_.empty() || wave_t_ < -6.0f))
            start_wave(wave_ + 1);
    } else if (state_ == St::GameOver) {
        game_over_t_ += dt;
        formation_->update(dt, ctx_.H, ctx_.W, false);   // survivors fly off behind the panel
        if (bot_) {  // self-play loops for more data (after a beat on the panel)
            if (game_over_t_ >= 2.5f) new_game();
            return;
        }
        if (ctx_.scores) {
            const std::string gid = "galaga";
            if (!entry_started_ && game_over_t_ >= ui::GAME_OVER_LOCK &&
                ctx_.scores->qualifies(gid, score_)) {
                entry_started_ = true;
                int rank = 1;
                for (auto& e : ctx_.scores->leaderboard(gid)) if (e.score > score_) rank++;
                initials_.begin("VOID SQUADRONS", score_, rank);
                return;  // don't feed the opening frame's key press to the entry
            }
            if (initials_.active()) {
                if (initials_.update(in)) ctx_.scores->add_score(gid, initials_.name(), score_);
                return;   // block retry while entering initials
            }
        }
        if (ui::game_over_retry(in, game_over_t_)) new_game();
        else if (ui::game_over_menu(in, game_over_t_)) quit_to_menu = true;
    }
}

// ── A modest "competent player" policy for balance measurement ────────────────
void GalagaGame::bot_decide(float& move_dir, bool& want_fire, bool& want_nuke) {
    want_fire = true;
    if (!player_->alive || player_->captured) { move_dir = 0; return; }
    float pcx = player_->x + player_->cur_w() / 2.0f, py = player_->y;

    float threat_dir = 0, closest = 1e9f;
    int near = 0;
    auto scan = [&](std::vector<Bullet>& bs) {
        for (auto& b : bs) {
            if (!b.alive) continue;
            float dy = py - b.y;                 // >0: bullet above, coming down
            if (dy < -10 || dy > 320) continue;
            float dx = b.x - pcx;
            if (std::fabs(dx) < 130) ++near;
            if (std::fabs(dx) < 46 && dy < closest) { closest = dy; threat_dir = dx > 0 ? -1.0f : 1.0f; }
        }
    };
    if (formation_) scan(formation_->p_bullets);
    scan(boss_bullets_);
    // divers and tractor beams are threats too: sidestep anything coming down the lane
    if (formation_ && !challenge_) {
        for (auto& e : formation_->enemies) {
            if (!e.alive || e.state != "diving") continue;
            float dy = py - e.y, dx = e.x + e.w / 2.0f - pcx;
            if (dy < -10 || dy > 260) continue;
            if (std::fabs(dx) < 60 && dy < closest) { closest = dy; threat_dir = dx > 0 ? -1.0f : 1.0f; }
        }
        for (auto& tb : formation_->tractor_beams) {
            float dx = tb.x() - pcx;
            if (std::fabs(dx) < 120) { closest = 0; threat_dir = dx > 0 ? -1.0f : 1.0f; }
        }
    }

    if (threat_dir != 0) {
        move_dir = threat_dir;
    } else {
        // prefer a nearby power-up; otherwise slide under the closest enemy and shoot it
        float target = pcx, best = 1e9f;
        for (auto& p : powerups_) {
            if (!p.alive) continue;
            float d = std::fabs(p.x - pcx);
            if (d < best && d < 200) { best = d; target = p.x; }
        }
        if (best > 1e8f && formation_) {
            float eb = 1e9f;
            for (auto& e : formation_->enemies) {
                if (!e.alive || !e.released) continue;
                float ex = e.x + e.w / 2.0f, d = std::fabs(ex - pcx);
                if (d < eb) { eb = d; target = ex; }
            }
            if (boss_ && boss_->damageable() && eb > 1e8f) target = boss_->x + boss_->w / 2.0f;
        }
        move_dir = target > pcx + 6 ? 1.0f : target < pcx - 6 ? -1.0f : 0.0f;
    }
    want_nuke = nukes_ > 0 && near >= 7;
    if (pcx < 40) move_dir = 1; else if (pcx > ctx_.W - 40) move_dir = -1;
}

// ── drawing ───────────────────────────────────────────────────────────────────
void GalagaGame::draw(Surface& s) {
    s.fill(BLACK);
    for (int x = 0; x < ctx_.W; x += 40) draw::line(s, Color(5, 5, 18), x, 0, x, ctx_.H, 1);
    for (int y = 0; y < ctx_.H; y += 40) draw::line(s, Color(5, 5, 18), 0, y, ctx_.W, y, 1);

    particles_.draw(s);
    formation_->draw(s);
    if (boss_) {
        auto [pcx, pcy] = player_->center();
        boss_->draw(s, pcx, pcy);
    }
    for (auto& b : boss_bullets_) b.draw(s);   // always: they can outlive the boss
    for (auto& p : powerups_) p.draw(s, *ctx_.fonts.small);
    player_->draw(s);
    for (auto& b : p_bullets_) b.draw(s);

    // captured fighter being hauled up the beam (until it docks under its captor)
    if (state_ == St::Captured && !cap_docked_) {
        float u = 0;
        auto [hx, hy] = capture_pos(u);
        auto cap = cache().captured;
        bool tumble = int(capture_t_ * 14) % 2 == 0;   // flicker blue/red as it is taken
        if (tumble && u < 0.8f) s.blit(*cache().player, int(hx) - 10, int(hy) - 10);
        else s.blit(*cap, int(hx) - 6, int(hy) - 6);
    }

    // shield bubble: pulses, flares when it eats a shot, blinks before it drops
    if (player_->alive && !player_->captured) {
        auto [cx, cy] = player_->center();
        int rr = player_->cur_w();
        float hit = std::max(0.0f, deflect_fx_ / 0.25f);
        if (shield_ > 0) {
            bool blink_off = shield_ < 1.5f && std::fmod(shield_, 0.25f) < 0.1f;
            if (!blink_off) {
                float pulse = 0.6f + 0.4f * std::sin(time_acc_ * 9 + shield_ * 4);
                draw::circle(s, Color(80, 220, 255, int(90 * pulse + 150 * hit)), cx, cy, rr, 2);
                draw::circle(s, Color(80, 220, 255, int(30 * pulse + 60 * hit)), cx, cy, rr - 2, 1);
            }
        } else if (hit > 0) {  // spawn-guard has no bubble of its own: flash one on impact
            draw::circle(s, Color(255, 255, 255, int(170 * hit)), cx, cy, rr, 2);
        }
    }

    draw_hud(s);
    if (nuke_flash_ > 0)
        draw::rect(s, Color(255, 255, 255, int(200 * std::min(1.0f, nuke_flash_))), {0, 0, ctx_.W, ctx_.H});
    draw_flash(s);
    draw_banner(s);

    if (state_ == St::GameOver) {
        ui::game_over(s, ctx_.fonts, ctx_.W, ctx_.H, game_over_t_, score_, new_hi_, pad_,
                      "REACHED STAGE " + std::to_string(wave_));
        if (initials_.active()) initials_.draw(s);
    }

    pause_.draw(s);
}

// Stage intro and stage-clear banners: both fade in and out, in the clear band
// between the formation and the player's row.
void GalagaGame::draw_banner(Surface& s) {
    const int cx = ctx_.W / 2, cy = ctx_.H / 2 + 60;
    auto faded = [](Color c, float a) { return Color(c.r, c.g, c.b, int(255 * std::clamp(a, 0.0f, 1.0f))); };
    if ((state_ == St::Entering || state_ == St::Play) && stage_t_ < 2.2f) {
        float a = std::min(stage_t_ / 0.25f, (2.2f - stage_t_) / 0.5f);
        draw_text_centered(s, *ctx_.fonts.big, "STAGE " + std::to_string(wave_), cx, cy, faded(CYAN, a));
        if (challenge_)
            draw_text_centered(s, *ctx_.fonts.med, "CHALLENGE STAGE  -  NO ENEMY FIRE", cx, cy + 52, faded(ORANGE, a));
        else
            draw_text_centered(s, *ctx_.fonts.small, "GET READY", cx, cy + 54, faded(ui::HINT, a));
        if (wave_ == 1 && !bot_) {  // the only controls the game needs, shown once
            std::string hint = pad_ ? std::string("STICK  MOVE      ") + ui::fire_key(pad_) + "  FIRE"
                                    : std::string("← →  MOVE      ") + ui::fire_key(pad_) + "  FIRE";
            draw_text_centered(s, *ctx_.fonts.small, hint, cx, cy + 82, faded(ui::HINT, a));
        }
    } else if (state_ == St::WaveClear) {
        float t = 2.4f - wave_t_;   // seconds since the clear
        float a = std::min(t / 0.25f, std::max(0.0f, wave_t_ + 0.6f) / 0.6f);
        draw_text_centered(s, *ctx_.fonts.big, challenge_ ? "BONUS  OVER" : "STAGE  CLEAR", cx, cy, faded(YELLOW, a));
        if (!challenge_)
            draw_text_centered(s, *ctx_.fonts.small,
                               "KILLS " + std::to_string(clear_kills_) + "      ACCURACY " +
                                   std::to_string(clear_acc_) + "%",
                               cx, cy + 54, faded(ui::HINT, a));
    }
}

void GalagaGame::draw_hud(Surface& s) {
    Font& med = *ctx_.fonts.med;
    Font& small = *ctx_.fonts.small;
    // ── top row: score / hi / stage ──
    draw_text(s, med, "SCORE  " + zpad(score_, 6), 16, 8, GREEN);
    draw_text_centered(s, med, "HI  " + zpad(std::max(high_score_, score_), 6), ctx_.W / 2, 8, YELLOW);
    draw_text_right(s, med, "STAGE  " + zpad(wave_, 2), ctx_.W - 16, 8, CYAN);

    // ── second row, left: combo (kept out of the centre, which banners own) ──
    if (ctx_.combo && ctx_.combo->active() && state_ != St::GameOver) {
        int m = ctx_.combo->mult();
        Color cc = m >= 6 ? RED : m >= 4 ? ORANGE : YELLOW;
        draw_text(s, small, "x" + std::to_string(m) + " COMBO", 16, 38, cc);
        draw::rect(s, Color(40, 40, 40), {16, 58, 100, 3});
        draw::rect(s, cc, {16, 58, int(100 * ctx_.combo->ratio()), 3});
    }

    // ── second row, centre: bonus-stage status WITH its countdown ──
    if (challenge_ && !challenge_done_ && (state_ == St::Play || state_ == St::Entering)) {
        float p = 0.6f + 0.4f * std::sin(time_acc_ * 6);
        int secs = std::max(0, int(std::ceil(challenge_t_)));
        Color tc = secs <= 5 ? Color(255, int(90 + 120 * p), 60) : Color(255, int(170 + 60 * p), 60);
        draw_text_centered(s, small,
                           "BONUS STAGE    HITS " + std::to_string(wave_kills_) + " / " +
                               std::to_string(challenge_total_) + "    TIME " + std::to_string(secs),
                           ctx_.W / 2, 38, tc);
        int bw = 300, bx = ctx_.W / 2 - bw / 2;
        draw::rect(s, Color(40, 40, 40), {bx, 60, bw, 4});
        draw::rect(s, tc, {bx, 60, int(bw * std::clamp(challenge_t_ / CHALLENGE_TIME, 0.0f, 1.0f)), 4});
    }

    // ── second row, centre: boss name, then its HP bar on a row of its own ──
    if (boss_ && !boss_->dying && !boss_->entering) {
        int bw = 420, bx = ctx_.W / 2 - bw / 2, by = 58;
        draw_text_centered(s, small, boss_->name, ctx_.W / 2, 38, boss_->color);
        draw::rect(s, Color(20, 20, 30), {bx, by, bw, 10}, 0, 4);
        int fillw = int(bw * boss_->hp_ratio());
        Color hc = boss_->hp_ratio() > 0.5f ? GREEN : boss_->hp_ratio() > 0.25f ? YELLOW : RED;
        draw::rect(s, hc, {bx, by, fillw, 10}, 0, 4);
        draw::rect(s, boss_->color, {bx, by, bw, 10}, 1, 4);
    }

    // ── bottom-left: weapon levels (one pip per real level) and lives ──
    const int row_a = ctx_.H - 39, row_b = ctx_.H - 21;
    draw_text(s, small, "PWR", 16, row_a, ui::LABEL);
    auto pips = [&](int n, int cap, Color c, int slot) {
        for (int i = 0; i < cap; ++i)
            draw::rect(s, i < n ? c : Color(50, 62, 56), {60 + slot * 18, row_a + 12 - i * 4, 13, 3});
    };
    pips(w_rapid_, CAP_RAPID, YELLOW, 0);
    pips(w_spread_, CAP_SPREAD, ORANGE, 1);
    pips(w_side_, CAP_SIDE, CYAN, 2);

    draw_text(s, small, "LIVES", 16, row_b, ui::LABEL);
    int shown = std::min(player_->lives, 6);
    for (int i = 0; i < shown; ++i)
        draw::polygon(s, CYAN, {{80 + i * 22, row_b + 14},
                                {80 + i * 22 + 7, row_b + 4},
                                {80 + i * 22 + 14, row_b + 14}});
    if (player_->lives > shown)
        draw_text(s, small, "+" + std::to_string(player_->lives - shown), 80 + shown * 22, row_b, CYAN);

    // ── bottom-right: timed buffs on one row, nuke + pause hint on the other ──
    auto seg = [&](int& x, int y, const std::string& t, Color c) {
        draw_text_right(s, small, t, x, y, c);
        x -= small.text_width(t) + 18;
    };
    int xa = ctx_.W - 12, xb = ctx_.W - 12;
    seg(xb, row_b, std::string(ui::pause_key(pad_)) + " PAUSE", ui::LABEL);
    if (nukes_ > 0)
        seg(xb, row_b, "NUKE x" + std::to_string(nukes_) + "  [" + ui::nuke_key(pad_) + "]", RED);
    if (shield_ > 0)
        seg(xa, row_a, "SHIELD " + std::to_string(int(shield_ + 0.99f)) + "s", Color(80, 220, 255));
    if (pierce_t_ > 0)
        seg(xa, row_a, "PIERCE " + std::to_string(int(pierce_t_ + 0.99f)) + "s", Color(200, 100, 255));
    if (twin_boost_ > 0)
        seg(xa, row_a, "TWIN " + std::to_string(int(twin_boost_ + 0.99f)) + "s", ORANGE);
}

void GalagaGame::draw_flash(Surface& s) {
    for (auto& f : flash_) {
        Color c = f.color;
        float a = std::max(0.0f, std::min(1.0f, f.life / std::max(0.1f, f.max_life)));
        c.a = uint8_t(255 * a);
        Font& font = f.big ? *ctx_.fonts.med : *ctx_.fonts.small;
        // keep the whole string on screen even when its anchor hugs a wall
        int half = font.text_width(f.text) / 2 + 6;
        int x = half * 2 >= ctx_.W ? ctx_.W / 2 : std::clamp(int(f.x), half, ctx_.W - half);
        draw_text_centered(s, font, f.text, x, int(f.y), c);
    }
}

}  // namespace

std::unique_ptr<Scene> make_galaga(const Context& ctx) {
    return std::make_unique<GalagaGame>(ctx);
}
