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

namespace {

// ── Colors ────────────────────────────────────────────────────────────────────
const Color BLACK(0, 0, 0), WHITE(255, 255, 255), GREEN(80, 255, 80);
const Color CYAN(80, 200, 255), YELLOW(255, 220, 40), RED(255, 60, 60);
const Color DIM(40, 80, 40), ORANGE(255, 140, 40);

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
Color darker(Color c, float f) { return Color(int(c.r * f), int(c.g * f), int(c.b * f)); }

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

Path make_entry_path(float slot_x, float slot_y, int screen_w, int screen_h,
                     const std::string& side) {
    Path pts;
    const int steps = 80;
    std::pair<float, float> p0, p1, p2, p3;
    if (side == "right") {
        p0 = {screen_w + 40.0f, -40.0f};
        p1 = {screen_w - 60.0f, screen_h * 0.5f};
        p2 = {screen_w * 0.3f, screen_h * 0.4f};
        p3 = {slot_x, slot_y};
    } else {
        p0 = {-40.0f, -40.0f};
        p1 = {60.0f, screen_h * 0.5f};
        p2 = {screen_w * 0.7f, screen_h * 0.4f};
        p3 = {slot_x, slot_y};
    }
    for (int i = 0; i <= steps; ++i) pts.push_back(bezier(p0, p1, p2, p3, i / float(steps)));
    return pts;
}

Path make_dive_path(float enemy_x, float enemy_y, int screen_w, int screen_h) {
    Path pts;
    const int steps = 120;
    float tx = rnd_uniform(screen_w * 0.2f, screen_w * 0.8f);
    float ty = screen_h - 80.0f;

    std::pair<float, float> p0 = {enemy_x, enemy_y};
    std::pair<float, float> p1 = {enemy_x + rnd_uniform(-120, 120), screen_h * 0.5f};
    std::pair<float, float> p2 = {tx + rnd_uniform(-80, 80), ty};
    std::pair<float, float> p3 = {tx, ty + 40};
    for (int i = 0; i <= steps / 2; ++i) pts.push_back(bezier(p0, p1, p2, p3, i / float(steps / 2)));

    float exit_x = pts.back().first;
    float wrap_x = screen_w - exit_x;
    float re_x = enemy_x;
    float re_y = enemy_y;

    std::pair<float, float> p0b = {exit_x, ty + 40};
    std::pair<float, float> p1b = {exit_x, screen_h + 60.0f};
    std::pair<float, float> p2b = {wrap_x, -60.0f};
    std::pair<float, float> p3b = {re_x, re_y};
    for (int i = 1; i <= steps / 2; ++i)
        pts.push_back(bezier(p0b, p1b, p2b, p3b, i / float(steps / 2)));
    return pts;
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
    static constexpr int MAX_BULLETS = 6;  // raised so spread/wing stacks fit
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

    void update(float dt, const Input& in, int screen_w) {
        if (!alive || captured) return;
        if (invincible > 0) invincible -= dt;
        if (fire_cd > 0) fire_cd -= dt;
        if (in.down(SDLK_LEFT) || in.down(SDLK_a)) x -= SPEED * dt;
        if (in.down(SDLK_RIGHT) || in.down(SDLK_d)) x += SPEED * dt;
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
    // state: entering, formation, diving, returning
    std::string state = "entering";
    Path enter_path;
    float enter_t = 0;
    Path dive_path;
    float dive_t = 0;
    float dive_speed = 220.0f;
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

    void draw(Surface& s) const {
        if (!alive) return;
        std::shared_ptr<Surface> surf;
        if (kind == Kind::Boss && hp == 1) surf = cache().boss_dmg[frame];
        else if (kind == Kind::Boss) surf = cache().boss[frame];
        else if (kind == Kind::Armor) surf = cache().armor[frame];
        else if (kind == Kind::Butterfly) surf = cache().butterfly[frame];
        else surf = cache().bee[frame];
        s.blit(*surf, int(x) - glow_off, int(y) - glow_off);
        if (hit_flash > 0)  // armored grunts spark white when chipped
            draw::rect(s, Color(255, 255, 255, int(120 * hit_flash)), rect(), 2, 2);
        if (captured) {
            auto cap = cache().captured;
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

    explicit TractorBeam(Enemy* b) : boss(b) {}
    float x() const { return boss->x + boss->w / 2; }
    float top_y() const { return boss->y + boss->h; }

    void update(float dt) {
        t += dt;
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

std::vector<Slot> build_formation_slots(int screen_w, int top_y) {
    std::vector<Slot> slots;
    struct Row { int count; Kind kind; };
    Row rows[] = {
        {4, Kind::Boss}, {6, Kind::Boss}, {8, Kind::Butterfly},
        {10, Kind::Butterfly}, {12, Kind::Bee}, {12, Kind::Bee}};
    int y = top_y;
    for (auto& r : rows) {
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

    int W, H, wave;
    bool challenge = false;   // bonus stage: enemies fly patterns but never fire
    std::vector<Enemy> enemies;
    std::vector<TractorBeam> tractor_beams;
    std::vector<Bullet> p_bullets;
    float anim_t = 0;
    bool anim_ticked = false;
    float drift_x = 0;
    int drift_dir = 1;
    // ── formation morphs ──────────────────────────────────────────────────────
    // Every so often the whole formation animates into a new shape over a few
    // seconds: a breathe (scale pulse), a rotate (swirl), or a wave ripple. The
    // morph offsets each enemy's resting slot so the swarm never feels static.
    float morph_cd = 6.0f;     // time until the next morph kicks off
    float morph_t = 0;         // elapsed time within the active morph
    float morph_dur = 0;       // 0 = no morph running
    int   morph_kind = 0;      // 0 breathe, 1 rotate, 2 wave
    float form_cx = 0, form_cy = 0;  // formation centroid (resting slots)
    // entry queue
    int entry_idx = 0;
    float entry_delay = 0;
    std::vector<int> active_entering;  // indices into enemies

    Formation(int sw, int sh, int wv, bool chal = false)
        : W(sw), H(sh), wave(wv), challenge(chal) { build(); }

    void build() {
        auto slots = build_formation_slots(W, 55);
        const char* sides_pat[2] = {"right", "left"};
        // Stage scaling: butterflies and bees can be promoted to armored grunts
        // that take multiple hits. Fraction & HP both rise with the stage.
        // Challenge stages never promote — they're a soft, point-rich breather.
        float armor_p = challenge ? 0.0f
                                  : std::min(0.55f, std::max(0.0f, (wave - 2) * 0.07f));
        int armor_hp = 2 + wave / 6;
        for (int idx = 0; idx < (int)slots.size(); ++idx) {
            Kind k = slots[idx].kind;
            int hp = (k == Kind::Boss) ? 2 : 1;
            // promote some non-boss enemies to armored grunts at higher stages
            if (k != Kind::Boss && wave >= 3 && rnd01() < armor_p) {
                k = Kind::Armor;
                hp = armor_hp;
            }
            Enemy e(slots[idx].x, slots[idx].y, k, idx, hp);
            std::string side = sides_pat[idx % 2];
            e.x = (side == "left") ? -80.0f : float(W + 80);
            e.y = -80.0f;
            e.state = "entering";
            e.enter_path = make_entry_path(slots[idx].x, slots[idx].y, W, H, side);
            e.enter_t = 0;
            enemies.push_back(std::move(e));
        }
        entry_idx = 0;
        entry_delay = 0;

        // centroid of the resting slots — the pivot for rotate / breathe morphs.
        double sx = 0, sy = 0;
        for (auto& e : enemies) { sx += e.slot_x; sy += e.slot_y; }
        int n = std::max(1, (int)enemies.size());
        form_cx = float(sx / n); form_cy = float(sy / n);
    }

    int alive_count() const {
        int n = 0; for (auto& e : enemies) if (e.alive) ++n; return n;
    }
    bool all_in_formation() const {
        for (auto& e : enemies) if (e.alive && e.state != "formation") return false;
        return true;
    }
    float anim_interval() const {
        float r = std::max(0.0f, alive_count() / float(enemies.size()));
        return ANIM_INTERVAL_MIN + (ANIM_INTERVAL_BASE - ANIM_INTERVAL_MIN) * r;
    }
    bool in_formation(const Enemy& e) const { return e.state == "formation"; }

    void update(float dt, int screen_h, int screen_w) {
        anim_ticked = false;
        for (auto& e : enemies) if (e.hit_flash > 0) e.hit_flash -= dt * 5;

        // entry queue release (staggered, faster at higher stages)
        entry_delay -= dt;
        if (entry_delay <= 0 && entry_idx < (int)enemies.size()) {
            active_entering.push_back(entry_idx);
            entry_idx += 1;
            entry_delay = 0.5f - std::min(0.35f, wave * 0.04f);
        }

        // move entering enemies along path
        float speed_scale = 1.0f + wave * 0.06f;
        for (size_t ai = 0; ai < active_entering.size();) {
            Enemy& e = enemies[active_entering[ai]];
            if (!e.alive || e.state != "entering") { active_entering.erase(active_entering.begin() + ai); continue; }
            Path& path = e.enter_path;
            int n = (int)path.size();
            int idx = std::min(int(e.enter_t * (n - 1)), n - 2);
            float frac = e.enter_t * (n - 1) - idx;
            auto [x0, y0] = path[idx];
            auto [x1, y1] = path[idx + 1];
            e.x = x0 + (x1 - x0) * frac;
            e.y = y0 + (y1 - y0) * frac;
            e.enter_t += dt * speed_scale * 0.8f;
            if (e.enter_t >= 1.0f) {
                e.x = e.slot_x; e.y = e.slot_y; e.state = "formation";
                active_entering.erase(active_entering.begin() + ai);
                continue;
            }
            ++ai;
        }

        // formation drift + (optional) morph
        bool any_formation = false;
        for (auto& e : enemies) if (e.alive && e.state == "formation") { any_formation = true; break; }
        if (any_formation) {
            drift_x += DRIFT_SPEED * drift_dir * dt;
            if (std::fabs(drift_x) >= DRIFT_RANGE) drift_dir *= -1;

            // morph scheduler: only morph when the whole swarm has settled.
            bool settled = all_in_formation();
            if (morph_dur <= 0) {
                if (settled) {
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

            // envelope: ease in and back out so the shape returns to rest cleanly.
            float amp = 0;
            if (morph_dur > 0) amp = std::sin(std::min(1.0f, morph_t / morph_dur) * PI);

            for (auto& e : enemies) {
                if (!(e.alive && e.state == "formation")) continue;
                float ox = 0, oy = 0;
                if (amp > 0.001f) {
                    float rx = e.slot_x - form_cx, ry = e.slot_y - form_cy;
                    if (morph_kind == 0) {            // BREATHE: scale toward/away from centre
                        float s = amp * 0.22f * std::sin(morph_t * 3.0f);
                        ox = rx * s; oy = ry * s;
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
                e.x = e.slot_x + drift_x + ox;
                e.y = e.slot_y + oy;
            }
        }

        // animation tick
        anim_t += dt;
        if (anim_t >= anim_interval()) {
            anim_t = 0; anim_ticked = true;
            for (auto& e : enemies) if (e.alive) e.toggle_frame();
        }

        // diving enemies
        for (auto& e : enemies) {
            if (!e.alive || e.state != "diving") continue;
            Path& path = e.dive_path;
            int n = (int)path.size();
            int idx = std::min(int(e.dive_t * (n - 1)), n - 2);
            float frac = e.dive_t * (n - 1) - idx;
            auto [x0, y0] = path[idx];
            auto [x1, y1] = path[idx + 1];
            e.x = x0 + (x1 - x0) * frac;
            e.y = y0 + (y1 - y0) * frac;
            e.dive_t += dt * (e.dive_speed / 220.0f);
            if (e.dive_t >= 1.0f) {
                e.x = e.slot_x; e.y = e.slot_y; e.state = "formation"; e.dive_t = 0;
            }
        }

        // enemy shooting — cap scales up with the stage so later stages bite.
        // Challenge (bonus) stages never fire back: pure pattern-flying for points.
        const int MAX_ENEMY_BULLETS = challenge ? 0 : 3 + wave / 3;
        for (auto& e : enemies) {
            if (!e.alive || (e.state != "formation" && e.state != "diving")) continue;
            if ((int)p_bullets.size() >= MAX_ENEMY_BULLETS) break;
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

    void trigger_dive(int count) {
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
            e.dive_t = 0;
            e.dive_speed = 220 + wave * 12;
            e.dive_path = make_dive_path(e.x, e.y, W, H);
        }
    }

    void trigger_tractor() {
        if (!tractor_beams.empty()) return;
        std::vector<int> bosses;
        for (int i = 0; i < (int)enemies.size(); ++i) {
            Enemy& e = enemies[i];
            if (e.alive && e.kind == Kind::Boss && e.state == "diving") bosses.push_back(i);
        }
        if (bosses.empty()) return;
        int b = bosses[rnd_int(0, (int)bosses.size() - 1)];
        tractor_beams.push_back(TractorBeam(&enemies[b]));
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
    float enter_delay = 0, stomp_t = 0, battle_y = 64, invuln_ = 0;

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
        x = (screen_w - w) / 2.0f; y = -float(h);
        vx = (70 + tier * 8) * (rnd01() < 0.5f ? 1 : -1);
        entering = true; enter_delay = 2.6f; battle_y = 64;
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
        cur_attack = a; attack_t = 0; sub_t = 99; dive_phase = 0; shock_done = false;
        if (a == 1 || a == 7) mouth_open = 0.7f;
        if (a == 9) { beam_active = true; beam_len = 0; }
        if (a == 10) { mouth_open = 0.5f; dive_target_x = x; }
    }
    void run_attack(float dt, float pcx, float pcy, int screen_w,
                    std::vector<Bullet>& out, ParticleSystem& parts) {
        (void)parts;
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
            case 5:  // SPIRAL: rotating twin spray
                if (sub_t >= 0.07f) { sub_t = 0; spin += 0.5f;
                    out.push_back(bb(bx, by, spin, 200));
                    out.push_back(bb(bx, by, spin + PI, 200)); }
                if (attack_t >= 1.7f) cur_attack = -1; break;
            case 6:  // RAIN: bullets from random points across the boss
                if (sub_t >= 0.09f) { sub_t = 0; out.push_back(bb(rnd_uniform(x, x + w), by, 0, 235)); }
                if (attack_t >= 1.5f) cur_attack = -1; break;
            case 7:  // LASER: a sweeping beam of fast bullets
                laser_angle = -0.7f + (attack_t / 1.7f) * 1.4f;
                if (sub_t >= 0.025f) { sub_t = 0; out.push_back(bb(bx, by, laser_angle, 380)); }
                if (attack_t >= 1.7f) cur_attack = -1; break;
            case 9:  // GIANT TRACTOR BEAM: extend, hold (capture window), retract
                if (attack_t < 1.1f) beam_len = std::min(float(beam_max), beam_len + 400 * dt);
                else if (attack_t < 2.9f) beam_len = beam_max;            // hold
                else { beam_len = std::max(0.0f, beam_len - 460 * dt);
                       if (beam_len <= 0) { beam_active = false; cur_attack = -1; } }
                break;
            case 10: {  // DIVE-BOMB: wind up, plunge at the player, shockwave, return
                float target = battle_y + 320;
                if (dive_phase == 0) {            // telegraph wind-up: lock onto the player's column
                    dive_target_x = pcx - w / 2;
                    if (attack_t >= 0.4f) dive_phase = 1;
                } else if (dive_phase == 1) {     // plunge down, sliding toward the player's x
                    y += 880 * dt;
                    x += (dive_target_x - x) * std::min(1.0f, 6 * dt);
                    if (y >= target) { y = target; dive_phase = 2; }
                } else if (dive_phase == 2) {     // impact shockwave fan
                    if (!shock_done) { shock_done = true;
                        for (int i = 0; i < 14; ++i)
                            out.push_back(bb(x + w / 2, y + h - 8, -1.5f + i * (3.0f / 13), 240)); }
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
            if (rnd01() < 0.6f) {  // confetti / chained blasts
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
            run_attack(dt, player_cx, player_cy, screen_w, out, parts);
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
                         : faking ? 0.6f : 1.0f;
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

    PauseMenu pause_;
    InitialsEntry initials_;
    bool entry_started_ = false;
    ParticleSystem particles_;
    int high_score_ = 0;
    float star_t_ = 0;
    float time_acc_ = 0;  // mirrors pygame.time.get_ticks()/1000

    int score_, wave_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Formation> formation_;
    std::unique_ptr<MegaBoss> boss_;
    std::vector<Bullet> p_bullets_, boss_bullets_;
    std::vector<PowerUp> powerups_;
    St state_;
    float dead_t_ = 0, wave_t_ = 0, dive_cd_ = 0, tractor_cd_ = 0, capture_t_ = 0;
    bool beam_pending_active_ = false;
    float beam_pending_t_ = 0;
    bool rescue_pending_ = false;
    bool boss_wave_ = false, boss_revealed_ = false;
    // ── bonus "challenge" stage ───────────────────────────────────────────────
    bool challenge_ = false;
    float challenge_t_ = 0;            // countdown until the bonus stage ends
    int challenge_total_ = 0;         // enemies that entered the bonus stage
    int challenge_kills_ = 0;         // how many the player shot down
    bool challenge_done_ = false;     // payout already resolved
    std::vector<Flash> flash_;
    bool submitted_ = false;

    // weapon progression (persists across the run)
    int w_rapid_ = 0, w_spread_ = 0, w_side_ = 0, w_pierce_ = 0, nukes_ = 0;
    int kills_since_drop_ = 0;
    float shield_ = 0, nuke_flash_ = 0;
    // Rescue reward: a brief twin-shot spread so capture-then-rescue pays off.
    float twin_boost_ = 0;

    // bot + telemetry
    bool bot_ = false;
    float game_t_ = 0, wave_t0_ = 0;
    int wave_kills_ = 0, wave_shots_ = 0, wave_pu_ = 0, wave_deaths_ = 0, wave_nukes_ = 0;
    int wave_captures_ = 0;

    std::string loadout() const {
        return "r" + std::to_string(w_rapid_) + "s" + std::to_string(w_spread_) +
               "w" + std::to_string(w_side_) + "p" + std::to_string(w_pierce_);
    }
    void tele(const J& j) { if (ctx_.tele) ctx_.tele->event(j); }
    int weapon_tier() const { return w_rapid_ + w_spread_ + w_side_ + w_pierce_; }

    void new_game() {
        score_ = 0; wave_ = 0;
        entry_started_ = false; submitted_ = false;
        w_rapid_ = w_spread_ = w_side_ = w_pierce_ = nukes_ = 0;
        kills_since_drop_ = 0;
        shield_ = nuke_flash_ = 0;
        twin_boost_ = 0;
        player_ = std::make_unique<Player>(ctx_.W / 2 - 14, ctx_.H - 70);
        particles_.clear();
        flash_.clear();
        bot_ = std::getenv("VOID_BOT") != nullptr || ctx_.demo;
        int start = 1;
        if (const char* e = std::getenv("VOID_WAVE")) start = std::max(1, atoi(e));
        if (const char* e = std::getenv("VOID_WEAPON")) {
            int t = atoi(e);
            w_rapid_ = std::min(3, t); w_spread_ = std::min(2, t);
            w_side_ = std::min(2, t); w_pierce_ = std::min(2, t); nukes_ = t;
        }
        start_wave(start);
    }
    void start_wave(int w) {
        wave_ = w;
        boss_wave_ = (w % 5 == 0);
        // Bonus "challenge" stage every 4th stage (never on a boss stage): a
        // no-fire-back breather flying fancy patterns purely for points.
        challenge_ = !boss_wave_ && (w % 4 == 0);
        p_bullets_.clear(); boss_bullets_.clear(); powerups_.clear();
        formation_ = std::make_unique<Formation>(ctx_.W, ctx_.H, wave_, challenge_);
        boss_revealed_ = false;
        if (boss_wave_) {
            // a small honor-guard formation enters... then the giant stomps in
            boss_ = std::make_unique<MegaBoss>(wave_, ctx_.W);
        } else {
            boss_.reset();
        }
        challenge_t_ = 16.0f;
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
        dead_t_ = wave_t_ = 0;
        dive_cd_ = std::max(DIVE_INTERVAL_MIN, DIVE_INTERVAL_BASE - wave_ * 0.15f);
        tractor_cd_ = TRACTOR_INTERVAL;
        capture_t_ = 0;
        beam_pending_active_ = false;
        rescue_pending_ = false;
        wave_t0_ = game_t_;
        wave_kills_ = wave_shots_ = wave_pu_ = wave_deaths_ = wave_nukes_ = wave_captures_ = 0;
        if (challenge_) add_flash("CHALLENGE STAGE!", ctx_.W / 2, ctx_.H / 2 - 60, ORANGE, 2.2f, true);
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
            bool hi = ctx_.scores->submit_galaga(score_, wave_);
            if (hi) add_flash("NEW HI-SCORE!", ctx_.W / 2, ctx_.H / 2 - 40, YELLOW, 2.5f);
            high_score_ = ctx_.scores->high_score("galaga");
        }
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
        powerups_.push_back({x, y, 0, k});
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
        const char* msg = "";
        switch (k) {
            case PU::Rapid:  w_rapid_ = std::min(3, w_rapid_ + 1); msg = "RAPID FIRE!"; break;
            case PU::Spread: w_spread_ = std::min(2, w_spread_ + 1); msg = "SPREAD SHOT!"; break;
            case PU::Side:   w_side_ = std::min(2, w_side_ + 1); msg = "WING CANNONS!"; break;
            case PU::Pierce: w_pierce_ = std::min(2, w_pierce_ + 1); msg = "PIERCING ROUNDS!"; break;
            case PU::Shield: shield_ = 7.0f; msg = "SHIELD UP!"; break;
            case PU::Life:   player_->lives++; msg = "EXTRA LIFE!"; break;
            case PU::Nuke:   nukes_++; msg = "NUKE ACQUIRED!"; break;
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
    void detonate_nuke() {
        if (nukes_ <= 0 || state_ != St::Play) return;
        nukes_--;
        nuke_flash_ = 1.0f;
        boss_bullets_.clear();
        if (formation_) formation_->p_bullets.clear();
        wave_nukes_++;
        tele(J().add("ev", "nuke").add("stage", wave_).add("t", (double)game_t_));
        if (formation_) {
            // Vaporises weak enemies; armored grunts are heavily chipped but
            // survive — a nuke softens an armored stage, it doesn't skip it.
            for (auto& e : formation_->enemies) {
                if (!e.alive) continue;
                e.hp -= 3;
                auto [cx, cy] = e.center();
                if (e.hp <= 0) {
                    e.alive = false; wave_kills_++; score_ += e.score_value(true);
                    particles_.explode_invader(cx, cy, e.color());
                } else {
                    e.hit_flash = 1.0f;
                    particles_.explode_invader(cx, cy, e.color(), 8);
                }
            }
        }
        if (boss_) {
            int dmg = std::max(25, boss_->max_hp / 5 + 20);
            boss_->take_damage(dmg, particles_);
            add_flash("DIRECT HIT!", boss_->x + boss_->w / 2, boss_->y + boss_->h / 2, YELLOW, 1.2f, true);
        }
        add_flash("* NUKE *", ctx_.W / 2, ctx_.H / 2 - 30, RED, 1.4f, true);
        if (ctx_.sfx) ctx_.sfx->big_explosion();
        if (ctx_.fx) {
            ctx_.fx->shake(26); ctx_.fx->hitstop(0.07f);
            ctx_.fx->rumble_explosion();
            ctx_.fx->shockwave(0.5f, 0.5f);
        }
    }

    void handle_keys(float dt, const Input& in, bool& want_nuke);
    void fire();
    void update_player_bullets(float dt);
    void check_player_bullet_hits();
    void check_enemy_bullet_hits();
    void check_tractor_beam();
    void update_boss(float dt);
    void update_powerups(float dt);
    void update_entering(float dt, const Input& in);
    void update_play(float dt, const Input& in);
    void update_captured(float dt);
    void hit_player();
    void bot_decide(float& move_dir, bool& want_fire, bool& want_nuke);
    void draw_hud(Surface& s);
    void draw_flash(Surface& s);
};

// ── firing (weapon-stacking) ──────────────────────────────────────────────────
void GalagaGame::fire() {
    float delay = std::max(0.10f, 0.20f * (1.0f - 0.09f * w_rapid_));
    if (player_->fire_cd > 0 || !player_->alive || player_->captured) return;
    if ((int)p_bullets_.size() >= Player::MAX_BULLETS) return;
    player_->fire_cd = delay;
    float px = player_->x + player_->cur_w() / 2.0f, py = player_->y;
    Color col = tier_color();
    int dmg = 1 + weapon_tier() / 4;
    auto add = [&](float x, float vx) {
        Bullet b; b.x = x; b.y = py; b.vx = vx; b.vy = -520;
        b.color = col; b.pierce = w_pierce_; b.damage = dmg;
        b.w = 3 + std::min(3, weapon_tier() / 2);
        p_bullets_.push_back(b);
    };
    add(px, 0);
    // dual fighter keeps its twin barrels
    if (player_->dual_fighter) {
        add(px - player_->w / 2 - 4, 0);
        add(px + player_->w / 2 + 4, 0);
    }
    for (int i = 1; i <= w_side_; ++i) { add(px - i * 11, 0); add(px + i * 11, 0); }
    for (int i = 1; i <= w_spread_; ++i) { add(px, -62.0f * i); add(px, 62.0f * i); }
    // Rescue twin-spread: two extra angled bolts fanning outward (if room).
    if (twin_boost_ > 0 && (int)p_bullets_.size() + 2 <= Player::MAX_BULLETS) {
        add(px, -150.0f); add(px, 150.0f);
        wave_shots_ += 2;
    }
    wave_shots_ += 1 + 2 * w_side_ + 2 * w_spread_;
    if (ctx_.sfx) ctx_.sfx->shoot();
}

void GalagaGame::handle_keys(float dt, const Input& in, bool& want_nuke) {
    float move_dir = 0; bool want_fire = false;
    if (bot_) {
        bot_decide(move_dir, want_fire, want_nuke);
        // emulate held-movement for the bot
        if (player_->alive && !player_->captured) {
            player_->x += move_dir * Player::SPEED * dt;
            player_->x = std::max(0.0f, std::min(float(ctx_.W - player_->cur_w()), player_->x));
            if (player_->invincible > 0) player_->invincible -= dt;
            if (player_->fire_cd > 0) player_->fire_cd -= dt;
        }
        if (want_fire) fire();
        return;
    }
    player_->update(dt, in, ctx_.W);
    for (auto k : in.pressed) {
        if (k == SDLK_b) want_nuke = true;
    }
    if (in.down(SDLK_SPACE) || in.down(SDLK_UP) || in.down(SDLK_w)) fire();
}

void GalagaGame::update_player_bullets(float dt) {
    for (auto& b : p_bullets_) {
        b.update(dt, ctx_.W, ctx_.H);
        if (rnd01() < 0.5f) { auto [tx, ty] = b.tip(); particles_.bullet_trail(tx, ty, b.color, true); }
    }
    p_bullets_.erase(std::remove_if(p_bullets_.begin(), p_bullets_.end(),
                     [](const Bullet& b) { return !b.alive; }), p_bullets_.end());
}

void GalagaGame::check_player_bullet_hits() {
    for (auto& b : p_bullets_) {
        if (!b.alive) continue;
        for (auto& e : formation_->enemies) {
            if (!e.alive || !overlap(b.rect(), e.rect())) continue;
            e.hp -= b.damage; e.hit_flash = 1.0f;
            bool destroyed = e.hp <= 0;
            auto [cx, cy] = e.center();
            if (destroyed) {
                e.alive = false;
                wave_kills_++;
                int pts = e.score_value(formation_->in_formation(e));
                if (e.captured) { pts *= 2; rescue_pending_ = true; }
                // COMBO: chained skill kills multiply points (capture bonus included).
                int mult = ctx_.combo ? ctx_.combo->hit() : 1;
                pts *= mult;
                score_ += pts;
                high_score_ = std::max(high_score_, score_);
                particles_.explode_invader(cx, cy, e.color(), 22);
                Color fc = mult >= 4 ? ORANGE : YELLOW;
                add_flash("+" + std::to_string(pts), cx, cy - 20, fc);
                roll_enemy_drop(cx, cy);
                if (ctx_.sfx) {
                    if (e.kind == Kind::Boss) ctx_.sfx->boss_explosion();
                    else ctx_.sfx->invader_hit();
                }
                if (ctx_.fx) {
                    if (e.kind == Kind::Boss) { ctx_.fx->shake(11); ctx_.fx->hitstop(0.06f); ctx_.fx->rumble(0.6f, 0.7f, 220); }
                    else { ctx_.fx->shake(4); ctx_.fx->rumble(0.25f, 0.4f, 90); }
                }
                if (rescue_pending_) {
                    player_->dual_fighter = true;
                    rescue_pending_ = false;
                    twin_boost_ = 8.0f;   // reward: temporary twin-shot spread
                    add_flash("FIGHTER RESCUED!", ctx_.W / 2, ctx_.H / 2 - 30, CYAN, 2.0f);
                    add_flash("TWIN SPREAD!", ctx_.W / 2, ctx_.H / 2, ORANGE, 1.6f, true);
                    if (ctx_.sfx) ctx_.sfx->rescue();
                }
            } else {
                particles_.explode_invader(cx, cy, e.color(), 8);
                if (ctx_.sfx) ctx_.sfx->invader_hit();
            }
            // Pierce only carries through enemies it KILLS outright — armored
            // grunts that merely get chipped stop the bolt cold.
            if (destroyed && b.pierce > 0) b.pierce--; else b.alive = false;
            break;
        }
        // bolts can hit the mega-boss (no pierce-through)
        if (b.alive && boss_ && boss_->damageable() && overlap(b.rect(), boss_->rect())) {
            boss_->take_damage(b.damage, particles_);
            score_ += 5 * b.damage;
            auto [tx, ty] = b.tip();
            particles_.explode_invader(tx, ty, boss_->color, 6);
            if (ctx_.sfx) ctx_.sfx->invader_hit();
            if (ctx_.fx) ctx_.fx->shake(2);
            b.alive = false;
        }
    }
}

void GalagaGame::check_enemy_bullet_hits() {
    if (!player_->alive || player_->captured || player_->invincible > 0 || shield_ > 0) return;
    auto process = [&](std::vector<Bullet>& bs) -> bool {
        for (auto& b : bs) {
            if (!b.alive) continue;
            if (overlap(b.rect(), player_->rect())) {
                b.alive = false; hit_player(); return true;
            }
        }
        return false;
    };
    if (process(formation_->p_bullets)) return;
    process(boss_bullets_);
}

void GalagaGame::check_tractor_beam() {
    if (player_->captured || !player_->alive) return;
    // small-boss formation tractor beams (classic capture)
    for (auto& tb : formation_->tractor_beams) {
        if (tb.phase == "hold" && overlap(tb.capture_rect(), player_->rect())) {
            player_->captured = true;
            state_ = St::Captured;
            capture_t_ = 0;
            wave_captures_++;
            if (tb.boss->alive) tb.boss->captured = true;
            if (ctx_.sfx) { ctx_.sfx->stop_tractor(); ctx_.sfx->capture(); }
            if (ctx_.fx) { ctx_.fx->shake(8); ctx_.fx->rumble(0.5f, 0.5f, 500); }
            return;
        }
    }
    // mega-boss giant tractor beam (galaga-flavored boss attack)
    if (boss_ && boss_->beam_holding() && overlap(boss_->beam_capture_rect(), player_->rect())) {
        player_->captured = true;
        state_ = St::Captured;
        capture_t_ = 0;
        wave_captures_++;
        if (ctx_.sfx) { ctx_.sfx->stop_tractor(); ctx_.sfx->capture(); }
        if (ctx_.fx) { ctx_.fx->shake(10); ctx_.fx->rumble(0.6f, 0.6f, 500); }
    }
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
            add_flash("!! " + boss_->name + " !!", ctx_.W / 2, 132, RED, 1.8f, true);
            if (ctx_.sfx) ctx_.sfx->player_death();
        }
        if (formation_)  // crush any honor-guard unlucky enough to be underfoot
            for (auto& e : formation_->enemies)
                if (e.alive && overlap(e.rect(), boss_->rect())) {
                    e.alive = false;
                    auto [cx, cy] = e.center();
                    particles_.explode_invader(cx, cy, e.color(), 12);
                    maybe_drop(cx, cy, 0.30f);
                }
        for (int i = 0; i < 6; ++i)
            particles_.bunker_hit(boss_->x + rnd_uniform(0, boss_->w), boss_->y + boss_->h);
        if (ctx_.fx) ctx_.fx->shake(15);
        if (ctx_.sfx) ctx_.sfx->bunker_hit();
    }
    if (boss_->fake_started) {
        boss_->fake_started = false;
        add_flash("...JUST KIDDING!", boss_->x + boss_->w / 2, boss_->y - 10, YELLOW, 1.6f, true);
    }
    if (boss_->split_started) {  // the SHED-THE-SWARM surprise barrage
        boss_->split_started = false;
        add_flash("IT SHEDS THE SWARM!", ctx_.W / 2, ctx_.H / 2 - 30, RED, 1.8f, true);
        if (ctx_.sfx) { ctx_.sfx->boss_explosion(); ctx_.sfx->dive_warble(); }
        if (ctx_.fx) { ctx_.fx->shake(18); ctx_.fx->hitstop(0.05f); ctx_.fx->rumble(0.8f, 0.8f, 260); }
    }
    if (boss_->spawn_minions) {  // boss summons a wave of dive-minions
        boss_->spawn_minions = false;
        if (formation_) formation_->trigger_dive(2 + wave_ / 5);
        if (ctx_.sfx) ctx_.sfx->dive_warble();
    }
    for (auto& b : boss_bullets_) b.update(dt, ctx_.W, ctx_.H);
    boss_bullets_.erase(std::remove_if(boss_bullets_.begin(), boss_bullets_.end(),
                        [](const Bullet& b) { return !b.alive; }), boss_bullets_.end());

    // boss tractor sfx
    if (ctx_.sfx) {
        if (boss_->beam_active && boss_->beam_len > 8) ctx_.sfx->start_tractor();
        else if (formation_ && formation_->tractor_beams.empty()) ctx_.sfx->stop_tractor();
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
    particles_.explode_player(cx, cy);
    // Death costs you your arsenal — start the weapon climb over (nukes survive).
    bool had_weapons = w_rapid_ || w_spread_ || w_side_ || w_pierce_;
    w_rapid_ = w_spread_ = w_side_ = w_pierce_ = 0;
    shield_ = 0;
    twin_boost_ = 0;
    if (had_weapons) add_flash("WEAPONS LOST!", cx, cy - 44, RED, 1.8f, true);
    player_->kill();
    state_ = St::Dead;
    dead_t_ = 2.0f;
    wave_deaths_++;
    tele(J().add("ev", "death").add("stage", wave_).add("t", (double)game_t_)
            .add("load", loadout()).add("lives_left", player_->lives));
    if (ctx_.sfx) { ctx_.sfx->stop_tractor(); ctx_.sfx->player_death(); }
    if (ctx_.fx) { ctx_.fx->shake(16); ctx_.fx->hitstop(0.09f); ctx_.fx->rumble_death(); }
}

void GalagaGame::update_entering(float dt, const Input& in) {
    bool want_nuke = false;
    handle_keys(dt, in, want_nuke);
    if (shield_ > 0) shield_ -= dt;
    if (twin_boost_ > 0) twin_boost_ -= dt;
    formation_->update(dt, ctx_.H, ctx_.W);
    if (ctx_.sfx && formation_->anim_ticked) ctx_.sfx->march_tick();
    if (boss_) update_boss(dt);
    update_player_bullets(dt);
    check_player_bullet_hits();
    check_enemy_bullet_hits();
    update_powerups(dt);
    // a boss stage stays in "entering" until the honor guard is in; the boss
    // reveals itself once the formation settles.
    if (formation_->all_in_formation()) state_ = St::Play;
}

void GalagaGame::update_play(float dt, const Input& in) {
    bool want_nuke = false;
    handle_keys(dt, in, want_nuke);
    if (want_nuke) detonate_nuke();
    if (shield_ > 0) shield_ -= dt;
    if (twin_boost_ > 0) twin_boost_ -= dt;

    if (!player_->alive) { return; }

    formation_->update(dt, ctx_.H, ctx_.W);
    if (ctx_.sfx && formation_->anim_ticked) ctx_.sfx->march_tick();
    if (boss_) update_boss(dt);
    update_player_bullets(dt);

    // dive timer — more divers, more often, each stage. On a challenge stage the
    // whole swarm peels off in rapid fancy dives, since it's all about the show.
    dive_cd_ -= dt;
    if (dive_cd_ <= 0) {
        int count = challenge_ ? 4 + (wave_ / 4) : 1 + (wave_ / 3);
        formation_->trigger_dive(count);
        dive_cd_ = challenge_ ? 0.8f
                              : std::max(DIVE_INTERVAL_MIN, DIVE_INTERVAL_BASE - wave_ * 0.12f);
        if (ctx_.sfx) ctx_.sfx->dive_warble();
    }

    // tractor beam timer (non-boss, non-challenge stages — classic capture intact)
    if (!boss_ && !challenge_) {
        tractor_cd_ -= dt;
        if (tractor_cd_ <= 0 && !player_->captured && !player_->dual_fighter) {
            formation_->trigger_dive(1);
            tractor_cd_ = TRACTOR_INTERVAL + rnd_uniform(-3, 3);
            beam_pending_active_ = true;
            beam_pending_t_ = 1.8f;
        }
        if (beam_pending_active_) {
            beam_pending_t_ -= dt;
            if (beam_pending_t_ <= 0) {
                formation_->trigger_tractor();
                beam_pending_active_ = false;
            }
        }
    }

    check_player_bullet_hits();
    check_enemy_bullet_hits();
    check_tractor_beam();
    // DIVE-BOMB body contact: the plunging boss flattens an unshielded fighter.
    if (boss_ && boss_->cur_attack == 10 && boss_->dive_phase >= 1 &&
        player_->alive && !player_->captured && player_->invincible <= 0 && shield_ <= 0 &&
        overlap(player_->rect(), boss_->rect())) {
        hit_player();
        return;
    }
    update_powerups(dt);

    if (ctx_.sfx && !boss_) {
        if (!formation_->tractor_beams.empty()) ctx_.sfx->start_tractor();
        else ctx_.sfx->stop_tractor();
    }

    // ── boss finished its death throes → win (rains power-ups) ──
    if (boss_ && boss_->finished) {
        score_ += 500 * (wave_ / 5);
        auto rainx = boss_->x + boss_->w / 2;
        powerups_.push_back({rainx, boss_->y + 40, 0, PU::Nuke});
        powerups_.push_back({rainx - 50, boss_->y + 40, 0, PU::Side});
        powerups_.push_back({rainx + 50, boss_->y + 40, 0, PU::Spread});
        add_flash(boss_->name + " GOES SPLAT!", ctx_.W / 2, ctx_.H / 2 - 20, YELLOW, 2.2f, true);
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
        if (ctx_.sfx) ctx_.sfx->stop_tractor();
    }

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
                      ctx_.W / 2, ctx_.H / 2 + 10, YELLOW, 2.2f, true);
            if (perfect) {
                int perfect_bonus = 5000;
                score_ += perfect_bonus;
                add_flash("PERFECT!  +" + std::to_string(perfect_bonus),
                          ctx_.W / 2, ctx_.H / 2 - 40, ORANGE, 2.6f, true);
                if (ctx_.fx) { ctx_.fx->shake(14); ctx_.fx->rumble(0.7f, 0.7f, 300); }
            }
            tele(J().add("ev", "challenge_clear").add("stage", wave_)
                    .add("hits", challenge_kills_).add("total", challenge_total_)
                    .add("perfect", perfect ? 1 : 0).add("load", loadout()));
            state_ = St::WaveClear;
            wave_t_ = 3.0f;
            if (ctx_.sfx) { ctx_.sfx->stop_tractor(); ctx_.sfx->level_clear(); }
        }
        return;   // challenge stages resolve here, never via the normal clear path
    }

    // ── stage clear: all formation enemies gone AND boss (if any) gone ──
    bool formation_clear = formation_->alive_count() == 0;
    if (formation_clear && !boss_) {
        tele(J().add("ev", "stage_clear").add("stage", wave_).add("dt", (double)(game_t_ - wave_t0_))
                .add("kills", wave_kills_).add("shots", wave_shots_).add("pu", wave_pu_)
                .add("deaths", wave_deaths_).add("nukes_used", wave_nukes_)
                .add("captures", wave_captures_).add("load", loadout()).add("lives", player_->lives));
        state_ = St::WaveClear;
        wave_t_ = 3.0f;
        if (ctx_.sfx) { ctx_.sfx->stop_tractor(); ctx_.sfx->level_clear(); }
    }
}

void GalagaGame::update_captured(float dt) {
    capture_t_ += dt;
    formation_->update(dt, ctx_.H, ctx_.W);
    if (boss_) update_boss(dt);
    if (capture_t_ > 2.5f) {
        player_->captured = false;
        player_->kill();
        // capture costs the arsenal too (mirror death reset)
        w_rapid_ = w_spread_ = w_side_ = w_pierce_ = 0;
        shield_ = 0;
        if (player_->lives > 0) {
            player_->respawn(ctx_.W / 2 - 14);
            state_ = St::Play;
            if (ctx_.sfx) ctx_.sfx->spawn_chime();
        } else {
            state_ = St::GameOver;
            submit_scores();
        }
    }
}

void GalagaGame::update(float dt, const Input& in) {
    time_acc_ += dt;
    game_t_ += dt;

    for (auto k : in.pressed)
        if (k == SDLK_p && (state_ == St::Play || state_ == St::Entering)) {
            pause_.toggle();
            if (ctx_.sfx && pause_.active()) ctx_.sfx->stop_tractor();
        }
    if (pause_.active()) {
        if (pause_.update(in) == PauseResult::QuitToMenu) { submit_scores(); quit_to_menu = true; }
        return;
    }
    for (auto k : in.pressed)
        if (k == SDLK_ESCAPE) { quit_to_menu = true; return; }

    star_t_ += dt;
    if (star_t_ >= AMBIENT_STAR_T) { star_t_ = 0; particles_.ambient_star(ctx_.W, ctx_.H); }
    particles_.update(dt);
    if (nuke_flash_ > 0) nuke_flash_ -= dt * 1.6f;
    for (auto& f : flash_) f.life -= dt;
    flash_.erase(std::remove_if(flash_.begin(), flash_.end(),
                 [](const Flash& f) { return f.life <= 0; }), flash_.end());

    if (state_ == St::Entering) {
        update_entering(dt, in);
    } else if (state_ == St::Play) {
        update_play(dt, in);
    } else if (state_ == St::Dead) {
        dead_t_ -= dt;
        if (dead_t_ <= 0) {
            if (player_->lives > 0) {
                player_->respawn(ctx_.W / 2 - 14);
                state_ = St::Play;
                if (ctx_.sfx) ctx_.sfx->spawn_chime();
            } else { state_ = St::GameOver; submit_scores(); }
        }
    } else if (state_ == St::Captured) {
        update_captured(dt);
    } else if (state_ == St::WaveClear) {
        wave_t_ -= dt;
        if (wave_t_ <= 0) start_wave(wave_ + 1);
    } else if (state_ == St::GameOver) {
        if (bot_) { new_game(); return; }  // self-play loops for more data
        if (ctx_.scores) {
            const std::string gid = "galaga";
            if (!entry_started_ && ctx_.scores->qualifies(gid, score_)) {
                entry_started_ = true;
                int rank = 1;
                for (auto& e : ctx_.scores->leaderboard(gid)) if (e.score > score_) rank++;
                initials_.begin("VOID SQUADRONS", score_, rank);
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
                if (!e.alive) continue;
                float ex = e.x + e.w / 2.0f, d = std::fabs(ex - pcx);
                if (d < eb) { eb = d; target = ex; }
            }
        }
        if (boss_ && boss_->damageable()) target = boss_->x + boss_->w / 2.0f;
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
        for (auto& b : boss_bullets_) b.draw(s);
    }
    for (auto& p : powerups_) p.draw(s, *ctx_.fonts.small);
    player_->draw(s);
    for (auto& b : p_bullets_) b.draw(s);

    // shield bubble
    if (shield_ > 0 && player_->alive && !player_->captured) {
        auto [cx, cy] = player_->center();
        float pulse = 0.6f + 0.4f * std::sin(star_t_ * 20 + shield_ * 4);
        int rr = player_->cur_w();
        draw::circle(s, Color(80, 220, 255, int(90 * pulse)), cx, cy, rr, 2);
        draw::circle(s, Color(80, 220, 255, int(30 * pulse)), cx, cy, rr - 2, 1);
    }

    draw_hud(s);
    if (nuke_flash_ > 0)
        draw::rect(s, Color(255, 255, 255, int(200 * std::min(1.0f, nuke_flash_))), {0, 0, ctx_.W, ctx_.H});
    draw_flash(s);

    if (state_ == St::Entering) {
        draw_text_centered(s, *ctx_.fonts.small, "STAGE  " + zpad(wave_, 2),
                           ctx_.W / 2, ctx_.H / 2, CYAN);
    } else if (state_ == St::Captured) {
        int g = int(180 + 75 * std::sin(time_acc_ * 6));
        Color col(g, 80, 255);
        draw_text_centered(s, *ctx_.fonts.small, "FIGHTER CAPTURED!", ctx_.W / 2, ctx_.H - 50, col);
    } else if (state_ == St::WaveClear) {
        draw_text_centered(s, *ctx_.fonts.big, "STAGE  CLEAR", ctx_.W / 2, ctx_.H / 2 - 30, YELLOW);
    } else if (state_ == St::GameOver) {
        draw_text_centered(s, *ctx_.fonts.big, "GAME  OVER", ctx_.W / 2, ctx_.H / 2 - 50, RED);
        draw_text_centered(s, *ctx_.fonts.med, "SCORE  " + zpad(score_, 6), ctx_.W / 2, ctx_.H / 2 + 10, WHITE);
        draw_text_centered(s, *ctx_.fonts.small, "PRESS SPACE TO RETRY", ctx_.W / 2, ctx_.H / 2 + 60, DIM);
        if (initials_.active()) initials_.draw(s);
    }

    pause_.draw(s);
}

void GalagaGame::draw_hud(Surface& s) {
    draw_text(s, *ctx_.fonts.med, "SCORE  " + zpad(score_, 6), 16, 8, GREEN);
    draw_text_centered(s, *ctx_.fonts.med, "HI  " + zpad(high_score_, 6), ctx_.W / 2, 8, YELLOW);
    draw_text_right(s, *ctx_.fonts.med, "STAGE  " + zpad(wave_, 2), ctx_.W - 16, 8, CYAN);

    // COMBO indicator (rising multiplier for chained skill kills). When a boss
    // HP bar is on screen the combo readout shifts left so the two don't collide.
    if (ctx_.combo && ctx_.combo->active() &&
        (state_ == St::Play || state_ == St::Entering)) {
        int m = ctx_.combo->mult();
        Color cc = m >= 6 ? RED : m >= 4 ? ORANGE : YELLOW;
        bool boss_bar = boss_ && !boss_->dying && !boss_->entering;
        bool busy_centre = boss_bar || (challenge_ && !challenge_done_);
        int tx = busy_centre ? 150 : ctx_.W / 2;
        int ty = busy_centre ? 60 : 34;          // tuck under any centred banner
        draw_text_centered(s, *ctx_.fonts.med, "x" + std::to_string(m) + " COMBO", tx, ty, cc);
        int bw = 120, bx = tx - bw / 2;
        draw::rect(s, Color(40, 40, 40), {bx, ty + 24, bw, 4});
        draw::rect(s, cc, {bx, ty + 24, int(bw * ctx_.combo->ratio()), 4});
    }

    // challenge (bonus) stage banner + countdown
    if (challenge_ && !challenge_done_ &&
        (state_ == St::Play || state_ == St::Entering)) {
        float p = 0.6f + 0.4f * std::sin(time_acc_ * 6);
        Color bc(255, int(150 * p), 40);
        draw_text_centered(s, *ctx_.fonts.small, "* BONUS STAGE *  NO ENEMY FIRE", ctx_.W / 2, 40, bc);
        draw_text_centered(s, *ctx_.fonts.small,
                           "HITS " + std::to_string(wave_kills_) + " / " + std::to_string(challenge_total_),
                           ctx_.W / 2, 58, YELLOW);
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

    // weapon loadout (bottom-left)
    int hx = 16, hy = ctx_.H - 46;
    draw_text(s, *ctx_.fonts.small, "PWR", hx, hy, DIM);
    auto pip = [&](int n, Color c, int slot) {
        for (int i = 0; i < 5; ++i)
            draw::rect(s, i < n ? c : Color(30, 40, 30), {hx + 44 + slot * 16, hy + 11 - i * 3, 12, 2});
    };
    pip(w_rapid_, YELLOW, 0); pip(w_spread_, ORANGE, 1); pip(w_side_, CYAN, 2); pip(w_pierce_, Color(200, 100, 255), 3);

    // lives
    draw_text(s, *ctx_.fonts.small, "LIVES", 16, ctx_.H - 26, DIM);
    for (int i = 0; i < player_->lives; ++i)
        draw::polygon(s, CYAN, {{80 + i * 22, ctx_.H - 16},
                                {80 + i * 22 + 7, ctx_.H - 26},
                                {80 + i * 22 + 14, ctx_.H - 16}});

    // nukes + shield (bottom-right)
    if (nukes_ > 0)
        draw_text_right(s, *ctx_.fonts.small, "NUKE x" + std::to_string(nukes_) + "  [B]", ctx_.W - 12, ctx_.H - 44, RED);
    if (twin_boost_ > 0)
        draw_text_right(s, *ctx_.fonts.small, "TWIN " + std::to_string(int(twin_boost_ + 0.99f)) + "s", ctx_.W - 12, ctx_.H - 62, ORANGE);
    if (shield_ > 0)
        draw_text_right(s, *ctx_.fonts.small, "SHIELD " + std::to_string(int(shield_ + 0.99f)) + "s", ctx_.W - 12, ctx_.H - 26, Color(80, 220, 255));
    else
        draw_text_right(s, *ctx_.fonts.small, "P PAUSE", ctx_.W - 12, ctx_.H - 26, Color(25, 50, 70));
}

void GalagaGame::draw_flash(Surface& s) {
    for (auto& f : flash_) {
        Color c = f.color;
        float a = std::max(0.0f, std::min(1.0f, f.life / std::max(0.1f, f.max_life)));
        c.a = uint8_t(255 * a);
        Font& font = f.big ? *ctx_.fonts.med : *ctx_.fonts.small;
        draw_text_centered(s, font, f.text, int(f.x), int(f.y), c);
    }
}

}  // namespace

std::unique_ptr<Scene> make_galaga(const Context& ctx) {
    return std::make_unique<GalagaGame>(ctx);
}
