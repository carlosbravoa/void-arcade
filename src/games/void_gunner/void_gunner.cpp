// void_gunner.cpp — Void Gunner.
// A first-person, PSEUDO-3D rail shooter (Starblade / Space Harrier feel). It is
// a pure EFFECT: there is no real 3D. Every entity lives in a fixed direction
// (azimuth a, elevation e) plus a depth z (1 = far in the void, 0 = on top of
// you) that decays over time. The player has a view direction (view_a, view_e)
// that the ARROWS swing; the crosshair stays welded to screen centre. You AIM by
// panning a target under the centred reticle, then fire; you DODGE by panning
// away from an incoming shot. Twin laser streams converge from the bottom
// corners into the centre. Enemies grow out of the void as they rush you.
#include "games/void_gunner/void_gunner.h"

#include <algorithm>
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
#include "engine/surface.h"
#include "engine/telemetry.h"
#include "engine/text.h"
#include "engine/ui.h"

namespace {
const Color BLACK(0, 0, 0), WHITE(255, 255, 255);
const Color CYAN(90, 220, 255), RED(255, 70, 70), YELLOW(255, 220, 40);
const Color GREEN(90, 255, 120), ORANGE(255, 150, 50), PURPLE(190, 110, 255);
const Color GOLD(255, 200, 70);
// Energy canister: pale mint, deliberately unlike any enemy (the armoured beetle is gold).
const Color CAN(175, 255, 215), CAN_GLOW(90, 255, 160);
constexpr float PI = 3.14159265358979323846f;

// ── Projection constants (tuned for 900x720) ────────────────────────────────
// sx = W/2 + (a-view_a) * SPREAD / z ; size = BASE / z. A far (z=1) enemy you're
// looking straight at sits at centre, tiny; nearing (z->0) it grows and, if off
// your view axis, slides off-centre and rushes past the edge.
constexpr float SPREAD = 360.0f;   // px of screen travel per radian per unit depth
constexpr float BASE = 34.0f;      // on-screen half-size at z=1
constexpr float CROSS_R = 46.0f;   // crosshair lock/hit radius (px at centre)

std::string zpad(int v, int w) {
    std::string s = std::to_string(v);
    if ((int)s.size() < w) s = std::string(w - s.size(), '0') + s;
    return s;
}
float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

// ── Projection helper ────────────────────────────────────────────────────────
struct Proj { float sx, sy, scale; bool on_screen; };
Proj project(float a, float e, float z, float view_a, float view_e, int W, int H) {
    float zz = std::max(z, 0.05f);
    float ra = a - view_a, re = e - view_e;
    Proj p;
    p.sx = W * 0.5f + ra * SPREAD / zz;
    p.sy = H * 0.5f + re * SPREAD / zz;
    p.scale = 1.0f / std::max(zz, 0.06f);
    p.on_screen = p.sx > -200 && p.sx < W + 200 && p.sy > -200 && p.sy < H + 200;
    return p;
}

// ── Pixel-art aliens ─────────────────────────────────────────────────────────
// Each enemy type is a distinct 2-frame creature that looms out of the void and
// grows as z->0. '#' = filled cell. Rows are scaled to fill the projected box.
// Charger — orange "wasp drone": swept wings + stinger, eyes that read at size.
inline const std::vector<std::string> SPR_CHARGER[2] = {
    {"...#.......#...",
     "..#.#.....#.#..",
     "...##.....##...",
     ".#.#########.#.",
     "##.#########.##",
     "#.###.#.#.###.#",
     "...##.#.#.##...",
     "..#.#######.#..",
     ".#...#####...#.",
     "......###.....",
     ".......#......"},
    {".#.#.......#.#.",
     "..#.#.....#.#..",
     "...##.....##...",
     "#..#########..#",
     "##.#########.##",
     "#.###.#.#.###.#",
     "..###.#.#.###..",
     "...########....",
     "..#..#####..#..",
     "......###.....",
     ".......#......"}};
// Weaver — purple "jellyfish squid": dome head + dangling tentacles.
inline const std::vector<std::string> SPR_WEAVER[2] = {
    {"....######....",
     "..##########..",
     ".############.",
     "############.#",
     "#.##.####.##.#",
     "#.#..####..#.#",
     "############.#",
     ".##.####.##..",
     ".#.#.#.#.#.#.",
     "#..#.#.#.#..#",
     "...#...#...#."},
    {"....######....",
     "..##########..",
     ".############.",
     "#.############",
     "#.##.####.##.#",
     "#.#..####..#.#",
     "#.############",
     "..##.####.##.",
     ".#.#.#.#.#.#.",
     "#..#.#.#.#..#",
     ".#...#...#..."}};
// Shooter — red "gunship insect": broad cannon arms + glaring eye band.
inline const std::vector<std::string> SPR_SHOOTER[2] = {
    {"#....####....#",
     "##..######..##",
     "#.##########.#",
     "..##########..",
     ".#.#.####.#.#.",
     ".############.",
     "#.##.####.##.#",
     "#.#........#.#",
     "#.#.######.#.#",
     "....#....#....",
     "...#......#..."},
    {"#....####....#",
     "#.#.######.#.#",
     "#.##########.#",
     "..##########..",
     ".#.#.####.#.#.",
     ".############.",
     "#.##.####.##.#",
     ".#.#......#.#.",
     "#.#.######.#.#",
     "...#......#...",
     "..#........#.."}};
// Tank enemy — gold "armoured beetle": heavy carapace + horns (HP 3).
inline const std::vector<std::string> SPR_TANKER[2] = {
    {"..#......#..",
     "...#....#...",
     "..########..",
     ".##########.",
     "############",
     "#.##.##.##.#",
     "############",
     "#.########.#",
     "#.#.####.#.#",
     ".#.#....#.#.",
     "..#......#.."},
    {".#........#.",
     "..#......#..",
     "..########..",
     ".##########.",
     "############",
     "#.##.##.##.#",
     "############",
     "#.########.#",
     "#.#.####.#.#",
     "#..#....#..#",
     ".#........#."}};
// Energy canister — gold core in a containment cell (the refill Tank pickup).
inline const std::vector<std::string> SPR_CANISTER[2] = {
    {"..########..",
     ".#........#.",
     "#.##.##.##.#",
     "#....##....#",
     "#.#.####.#.#",
     "#.######.#.#",
     "#.#.####.#.#",
     "#....##....#",
     "#.##.##.##.#",
     ".#........#.",
     "..########.."},
    {"..########..",
     ".#........#.",
     "#.#.####.#.#",
     "#...####...#",
     "#.########.#",
     "#.########.#",
     "#.########.#",
     "#...####...#",
     "#.#.####.#.#",
     ".#........#.",
     "..########.."}};

// Agile boss — a high-res "void raider": an aggressive swept arrowhead warship
// with twin forward prongs, layered hull plating, swept wings, twin engine flares
// and a hollow core socket (where the weak-spot node sits). 28x22 so it reads as a
// detailed, fast, distinct menace rather than a chunky blown-up sprite.
inline const std::vector<std::string> SPR_RAIDER[2] = {
    {"............#..#............",
     "...........##..##...........",
     "..........###..###..........",
     "........#####..#####........",
     ".......######..######.......",
     "......######....######......",
     "....########....########....",
     "...#########....#########...",
     "..##########....##########..",
     "..####....########....####..",
     ".####......######......####.",
     ".###........####........###.",
     ".###........####........###.",
     ".####......######......####.",
     "..####....########....####..",
     "..##########....##########..",
     "...#########....#########...",
     "....########....########....",
     ".....######......######.....",
     "....###.###......###.###....",
     "...##....##......##....##...",
     "..#......#........#......#.."},
    {".............##.............",
     "...........######...........",
     "..........###..###..........",
     "........#####..#####........",
     ".......######..######.......",
     "......######....######......",
     "....########....########....",
     "...#########....#########...",
     "..##########....##########..",
     "..####....########....####..",
     ".####......######......####.",
     ".###........####........###.",
     ".###........####........###.",
     ".####......######......####.",
     "..####....########....####..",
     "..##########....##########..",
     "...#########....#########...",
     "....########....########....",
     ".....######......######.....",
     "...####.####....####.####...",
     "..##......##....##......##..",
     ".#..........#..#..........#."}};

// Render a '#'/'.' pixel grid scaled into box (bx,by,bw,bh) in `color`, modelled
// on draw_boss_alien. Adds a dim oversized glow pass under the bright pass so the
// CRT bloom makes the creature glow. For tiny on-screen sizes the caller should
// fall back to a lit blob instead of calling this.
inline void draw_pixel_sprite(Surface& s, const std::vector<std::string>& spr,
                              int bx, int by, int bw, int bh, Color color) {
    int rows = (int)spr.size(), cols = 0;
    for (auto& r : spr) cols = std::max(cols, (int)r.size());
    if (rows == 0 || cols == 0) return;
    float cw = bw / float(cols), chh = bh / float(rows);
    int cellw = int(cw) + 1, cellh = int(chh) + 1;
    // dim oversized glow pass (slightly bigger cells, low alpha) → bloom feeds it
    Color glow(color.r, color.g, color.b, 70);
    int gw = cellw + std::max(2, cellw / 3), gh = cellh + std::max(2, cellh / 3);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < (int)spr[r].size(); ++c)
            if (spr[r][c] == '#') {
                int px = bx + int(c * cw), py = by + int(r * chh);
                draw::rect(s, glow, {px - gw / 6, py - gh / 6, gw, gh});
            }
    // bright body pass with a lit top-edge per cell for a phosphor sheen
    Color lit(std::min(255, color.r + 60), std::min(255, color.g + 60),
              std::min(255, color.b + 60));
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < (int)spr[r].size(); ++c)
            if (spr[r][c] == '#') {
                int px = bx + int(c * cw), py = by + int(r * chh);
                draw::rect(s, color, {px, py, cellw, cellh});
                if (cellh >= 4)
                    draw::rect(s, lit, {px, py, cellw, std::max(1, cellh / 4)});
            }
}

// ── Starfield ────────────────────────────────────────────────────────────────
struct Star {
    float a, e, z, speed;
    void recycle() {
        a = rnd_uniform(-2.2f, 2.2f);
        e = rnd_uniform(-1.8f, 1.8f);
        z = 1.0f;
        speed = rnd_uniform(0.28f, 0.62f);
    }
    void seed() { recycle(); z = rnd_uniform(0.05f, 1.0f); }
};


// ── Enemy ────────────────────────────────────────────────────────────────────
enum class EType { Charger, Weaver, Shooter, Tank };

inline const std::vector<std::string>& enemy_sprite(EType t, int frame) {
    int f = frame & 1;
    switch (t) {
        case EType::Charger: return SPR_CHARGER[f];
        case EType::Weaver:  return SPR_WEAVER[f];
        case EType::Shooter: return SPR_SHOOTER[f];
        case EType::Tank:    return SPR_TANKER[f];
    }
    return SPR_CHARGER[f];
}

// Entry-pattern motion. Beyond the legacy weave, an enemy can ride a scripted
// path while z closes: Squadron holds a formation slot then peels off; Sequence
// snakes along a shared lane; Orbit sweeps an arc across the field.
enum class Pattern { Single, Squadron, Sequence, Orbit };

struct Enemy {
    EType type;
    float a, e, z;
    float a0, e0;          // home direction (weave oscillates around this)
    float vz;              // approach speed (z per second)
    float weave_amp, weave_freq, phase;
    int hp, max_hp;
    int id = 0;            // stable id so a homing missile can track this enemy
    float fire_t;          // shooter countdown
    float hit_flash = 0;
    bool alive = true;
    int frame = 0;
    float anim_t = 0;      // drives the 2-frame creature animation
    // ── pattern motion ──
    Pattern pat = Pattern::Single;
    float life_t = 0;      // seconds alive (drives scripted paths)
    float peel_t = 1e9f;   // time at which a Squadron enemy peels off
    bool peeled = false;
    float form_a = 0, form_e = 0;   // squadron formation slot (target a/e)
    float orbit_r = 0, orbit_w = 0, orbit_ph = 0, orbit_cy = 0;  // orbit arc params
    // Ramming run: the offset currently pulling it off its pattern and onto your
    // view axis (applied on top of the pattern position, removed before the next
    // pattern step so the scripted paths stay untouched).
    float ram_da = 0, ram_de = 0;

    Color color() const {
        switch (type) {
            case EType::Charger: return ORANGE;
            case EType::Weaver:  return PURPLE;
            case EType::Shooter: return RED;
            case EType::Tank:    return GOLD;
        }
        return ORANGE;
    }
    int points() const {
        switch (type) {
            case EType::Charger: return 100;
            case EType::Weaver:  return 150;
            case EType::Shooter: return 200;
            case EType::Tank:    return 300;
        }
        return 100;
    }
};

// On-screen edge length (px) of an enemy / canister at depth z. Shared by the
// renderer, the laser hit test and the lock indicator so they always agree.
inline int enemy_px(float z) { return std::min(300, std::max(6, int(BASE / std::max(z, 0.07f)))); }
inline int tank_px(float z)  { return std::min(200, std::max(10, int(BASE / std::max(z, 0.07f)))); }

// ── Energy tank (mint canister; shoot to refill) ────────────────────────────
struct Tank {
    float a, e, z, vz, phase;
    bool alive = true;
};

// ── Homing missile (auto-locks the target under/near the crosshair) ──────────
// Flies in SCREEN space from the bottom centre, homing toward its target enemy's
// projected position; detonates on arrival (big damage + splash) or if its target
// dies / it times out.
struct Missile {
    float sx, sy;          // current screen position
    float vx = 0, vy = 0;  // current screen velocity
    int target_id = -1;    // enemy id to chase (-1 = dumb-fire toward the reticle)
    float tx = 0, ty = 0;  // fallback target point (reticle) if the lock is lost
    float t = 0;
    bool alive = true;
};

// ── Enemy shot: STARTS at the firer's direction (sa,se) and travels toward the
// aim direction (ta,te = your view at fire time) as z closes, growing as it
// nears. It visibly leaves the enemy and lands where you were looking — pan away
// to dodge. Whether it connects is decided ON SCREEN (see shot_proj()).
constexpr float SHOT_HIT_Z = 0.07f;    // depth at which a shot "arrives"
// A shot's screen position stops magnifying below this depth. The raw 1/z
// projection would fling anything a few degrees off-axis far off screen in its
// last frames; clamping it keeps the bolt in view right up to arrival, so what
// you see land near the reticle is exactly what hits.
constexpr float SHOT_ZREF = 0.30f;
constexpr float SHOT_HIT_R = 190.0f;   // px from the reticle that still connects (~0.16 rad)
struct EShot {
    float sa, se;          // start direction (firer)
    float ta, te;          // target / aim direction
    float z, z0, vz;       // depth, start depth, approach speed
    Color color;
    bool alive = true;
    float prog() const {
        float span = z0 - SHOT_HIT_Z;
        return span > 0.001f ? clampf((z0 - z) / span, 0.0f, 1.0f) : 1.0f;
    }
    float ca() const { return sa + (ta - sa) * prog(); }  // current azimuth
    float ce() const { return se + (te - se) * prog(); }  // current elevation
};
// Build an enemy shot from a firer direction toward a target direction. The
// approach speed is capped so even a close-range shot takes long enough to dodge.
inline EShot make_eshot(float sa, float se, float ta, float te, float z, float vz, Color c) {
    EShot s; s.sa = sa; s.se = se; s.ta = ta; s.te = te;
    s.z = z; s.z0 = z;
    s.vz = std::min(vz, std::max(0.05f, (z - SHOT_HIT_Z) / 0.50f));
    s.color = c; return s;
}

// ── Player laser streak (cosmetic convergence beam) ──────────────────────────
struct Laser { float side; float t; };  // side: -1 left, +1 right

struct Flash { std::string text; float x, y, life, max_life; Color color; bool big; };

// One queued announcement (wave cleared / next wave / boss warning …). They play
// one at a time, above the action, so they never stack or cover the reticle.
struct Note { std::string title, sub; Color color; float dur; };

// ── Boss ─────────────────────────────────────────────────────────────────────
// A SMALL, AGILE void-raider. Instead of a giant slow target, it is a compact
// menace that darts, strafes and feints around your view at a mid depth. It
// carries one fixed glowing weak-spot node welded to its body — the challenge is
// tracking a fast small target, not pixel-hunting a giant. Multi-phase
// telegraphed attacks remain, but the BODY is mobile and modest in size.
struct Boss {
    int sprite_idx;
    std::string name;
    Color color;
    int hp, max_hp;
    float a, e, z;         // glide position + depth (mid depth; darts around)
    float sway_t = 0;
    float hit_flash = 0;
    // ── agile movement: chase a series of dart targets, occasionally feint ──
    float ta = 0, te = 0;        // current dart target (a/e)
    float move_t = 0;            // time until next dart re-target
    float feint = 0;             // brief reversal/jink timer
    // weak-spot is FIXED to the body's central core socket (a constant local
    // offset). wp_e ~ +0.02 sits it just below dead-centre, inside the hull — not
    // floating above the head (the old -0.16 was tuned to the previous sprite).
    float wp_a = 0.0f, wp_e = 0.02f;
    float wp_t = 0;              // pulse phase only
    // attack system
    int phase = 1;
    float atk_t = 0, atk_iv = 1.6f, telegraph = 0;
    int pending = -1, cur = -1;
    float sub_t = 0;
    bool dying = false, finished = false;
    float die_t = 0;
    float invuln = 1.4f;
    float hurt_t = 0;            // hit-stun: staggers/slows the boss when shot

    float hp_ratio() const { return std::max(0.0f, float(hp) / max_hp); }
    bool damageable() const { return !dying && invuln <= 0; }
    void phase_from_hp() { float r = hp_ratio(); phase = r > 0.6f ? 1 : r > 0.3f ? 2 : 3; }
    // Where the body actually IS: the glide position plus a faint wobble so it
    // never sits perfectly still. The wobble is a pure function of time (an
    // offset, not something accumulated every frame), so it is the same size at
    // any frame rate.
    float pa() const { return a + std::sin(sway_t * 7.0f) * 0.016f; }
    float pe() const { return e + std::cos(sway_t * 8.0f) * 0.012f; }
    float wa() const { return pa() + wp_a; }   // weak-spot direction
    float we() const { return pe() + wp_e; }
};

enum class St { Intro, Play, Dead, GameOver };

class VoidGunnerGame : public Scene {
public:
    explicit VoidGunnerGame(const Context& ctx)
        : Scene(ctx), pause_(ctx.W, ctx.H, ctx.fonts), initials_(ctx.W, ctx.H, ctx.fonts) {
        high_score_ = ctx.scores ? ctx.scores->high_score("void_gunner") : 0;
        for (auto& st : stars_) st.seed();
        new_game();
        if (ctx_.sfx) ctx_.sfx->start_ambient(0);
    }
    void update(float dt, const Input& in) override;
    void draw(Surface& s) override;
    void on_exit() override {
        submit_scores();
        if (ctx_.sfx) ctx_.sfx->stop_ambient();
    }

private:
    static constexpr int N_STARS = 420;  // a dense starfield sells the 3D motion
    static constexpr float MAX_ENERGY = 100.0f;
    static constexpr float VIEW_LIM = 1.25f;       // clamp on view pan
    static constexpr float ARC_LIM = 1.12f;        // enemies never leave the arc you can aim at
    // Slower, weightier turning. The view doesn't snap to the held direction; it
    // tracks a pan *velocity* that accelerates toward the input and decays when
    // released, so panning feels like swinging your head/ship with momentum.
    static constexpr float PAN_SPEED = 0.62f;      // rad/s — deliberate, floaty
    static constexpr float PAN_ACCEL = 6.0f;       // how fast vel chases the input
    static constexpr float PAN_DAMP = 5.5f;        // how fast vel bleeds off when idle

    // ── Laser energy (no more infinite fire) ──
    static constexpr float LASER_MAX = 1.0f;
    static constexpr float LASER_COST = 0.10f;     // ~9 shots before lockout (0.90 usable / 0.10)
    static constexpr float LASER_REGEN = 0.70f;    // /s while not firing → ~1.4s refill
    static constexpr float LASER_PAUSE = 0.18f;    // brief regen lockout after a shot
    static constexpr float LASER_MIN_FIRE = 0.10f; // running it below this overheats the laser
    static constexpr float LASER_RESUME = 0.35f;   // an overheated laser fires again from here

    static constexpr float RAM_Z = 0.24f;          // below this depth an enemy swings in to ram you
    static constexpr float RAM_RUSH = 1.9f;        // approach-speed multiplier during the ramming run
    static constexpr float HURT_IFRAMES = 0.35f;   // no second hit inside this window
    static constexpr int   LIFE_EVERY = 25000;     // first bonus life; each later one costs more
    static constexpr int   LIVES_MAX = 4;

    PauseMenu pause_;
    InitialsEntry initials_;
    ParticleSystem particles_;
    bool entry_started_ = false;
    int high_score_ = 0;

    int score_ = 0, wave_ = 0;
    int lives_ = 3;
    int next_life_at_ = LIFE_EVERY, life_step_ = LIFE_EVERY;
    float energy_ = MAX_ENERGY;
    St state_ = St::Intro;

    float view_a_ = 0, view_e_ = 0;
    float pan_va_ = 0, pan_ve_ = 0;     // pan velocity (inertia/momentum)
    float laser_ = LASER_MAX;           // 0..1 laser energy
    float laser_pause_ = 0;             // regen lockout timer after firing
    float overheat_t_ = 0;              // visual: crosshair reds when denied
    bool overheated_ = false;           // locked out until LASER_RESUME
    // ── weapons: upgradeable laser + auto-lock missiles ──
    int laser_lvl_ = 1;                 // 1..LASER_LVL_MAX; more damage + faster fire
    static constexpr int LASER_LVL_MAX = 4;
    int missiles_ = 3;                  // auto-lock missile ammo
    static constexpr int MISSILES_MAX = 8;
    float missile_cd_ = 0;
    bool msl_btn_prev_ = false;         // shoulder-button edge state for missiles
    int next_eid_ = 1;                  // hands out Enemy::id
    std::vector<Missile> pmissiles_;    // player missiles in flight
    Star stars_[N_STARS];
    std::vector<Enemy> enemies_;
    std::vector<Tank> tanks_;
    std::vector<EShot> eshots_;
    std::vector<Laser> lasers_;
    std::vector<Flash> flash_;
    std::vector<Note> notes_;      // queued announcements; front() is showing
    float note_t_ = 0;             // how long the front note has been up
    std::unique_ptr<Boss> boss_;

    int to_spawn_ = 0;             // enemies left to release this wave
    float spawn_t_ = 0;
    // ── formation release state ──
    int form_kind_ = 0;            // current formation being released (0=single)
    int form_left_ = 0;            // members still to release in the active formation
    int form_idx_ = 0;            // index within the formation (slot/order)
    float form_lane_a_ = 0, form_lane_e_ = 0;  // shared lane for snake/squadron
    float form_orbit_dir_ = 1;     // orbit sweep direction
    float form_step_ = 0.25f;      // gap between members of the active formation
    float tank_t_ = 0;
    float fire_cd_ = 0;
    float lock_glow_ = 0;          // pulse when something's under the reticle
    float hit_flash_ = 0;          // red screen flash on energy hit
    float dead_t_ = 0, intro_t_ = 0, go_t_ = 0;
    float invinc_ = 0;             // post-respawn invincibility
    float hurt_t_ = 0;             // brief i-frames after any hit
    float pip_t_ = 0;              // low-energy beep cadence
    float chirp_t_ = 0;            // throttle: min gap between spawn chirps
    bool lock_active_ = false;     // edge-detect crosshair lock for lock_on()
    bool threat_ = false;          // an incoming shot is on course to hit (reticle warning)
    bool boss_wave_ = false;
    bool submitted_ = false, new_hi_ = false;
    bool bot_ = false;
    bool pad_ = false;             // a gamepad is connected → show pad prompts
    float game_t_ = 0;

    void tele(const J& j) { if (ctx_.tele) ctx_.tele->event(j); }

    void new_game() {
        score_ = 0; wave_ = 0; lives_ = 3; energy_ = MAX_ENERGY;
        next_life_at_ = life_step_ = LIFE_EVERY;
        view_a_ = view_e_ = 0; pan_va_ = pan_ve_ = 0;
        laser_ = LASER_MAX; laser_pause_ = 0; overheat_t_ = 0; overheated_ = false;
        laser_lvl_ = 1; missiles_ = 3; missile_cd_ = 0; next_eid_ = 1;
        enemies_.clear(); tanks_.clear(); eshots_.clear(); lasers_.clear(); flash_.clear();
        pmissiles_.clear();
        notes_.clear(); note_t_ = 0;
        boss_.reset();
        particles_.clear();
        submitted_ = false; new_hi_ = false; entry_started_ = false; go_t_ = 0;
        invinc_ = 0; hurt_t_ = 0; hit_flash_ = 0; fire_cd_ = 0; lock_glow_ = 0; pip_t_ = 0;
        chirp_t_ = 0; lock_active_ = false; threat_ = false;
        if (ctx_.combo) ctx_.combo->reset();
        bot_ = std::getenv("VOID_BOT") != nullptr || ctx_.demo;
        if (ctx_.sfx && !ctx_.demo) ctx_.sfx->start_music(5);
        int start = 1;
        if (const char* e = std::getenv("VOID_WAVE")) start = std::max(1, atoi(e));
        start_wave(start);
    }

    // Queue an announcement; they show one after another near the top.
    void announce(const std::string& title, const std::string& sub, Color c, float dur = 1.3f) {
        notes_.push_back({title, sub, c, dur});
    }

    void start_wave(int w) {
        // Seamless after the first wave: you keep flying/shooting through the
        // transition (no freeze) — the wave is announced with a queued banner.
        bool seamless = (wave_ > 0);
        wave_ = w;
        boss_wave_ = (w % 4 == 0) || std::getenv("VOID_GBOSS");  // debug: force boss
        eshots_.clear();
        if (!seamless) { view_a_ = view_e_ = 0; pan_va_ = pan_ve_ = 0; }  // keep the view continuous mid-run
        form_kind_ = -1; form_left_ = 0;
        if (boss_wave_) {
            spawn_boss(w);
            to_spawn_ = 0;
        } else {
            boss_.reset();
            // more, faster enemies each wave
            to_spawn_ = 6 + w * 2;
        }
        spawn_t_ = seamless ? 0.8f : 0.5f;   // a short beat before they arrive
        tank_t_ = rnd_uniform(5.0f, 9.0f);
        if (ctx_.fx) ctx_.fx->palette_hue = 0.20f * std::sin(w * 0.6f) + (boss_wave_ ? -0.10f : 0.0f);
        if (seamless) {
            state_ = St::Play;   // never stop — announce with a non-blocking banner
            if (boss_wave_) announce("WARNING", "A VOID BOSS APPROACHES", RED, 1.8f);
            else announce("WAVE " + std::to_string(w), "ENEMIES INCOMING", CYAN, 1.3f);
        } else {
            state_ = St::Intro;
            intro_t_ = 1.8f;
        }
        tele(J().add("ev", "wave_start").add("wave", wave_).add("boss", boss_wave_ ? 1 : 0)
                .add("t", (double)game_t_).add("lives", lives_));
    }

    void spawn_boss(int w) {
        boss_ = std::make_unique<Boss>();
        int tier = std::max(1, w / 4);        // 1,2,3...  (>=1 so idx never negative)
        int idx = (tier - 1) % 4;
        boss_->sprite_idx = idx;
        const char* names[4] = {"VOID WARDEN", "THE DEVOURER", "NULL SOVEREIGN", "OMEGA HUSK"};
        const Color cols[4] = {GREEN, PURPLE, CYAN, RED};
        int loop = (tier - 1) / 4;
        const char* pre = loop == 0 ? "" : loop == 1 ? "MEGA " : "OMEGA ";
        boss_->name = std::string(pre) + names[idx];
        boss_->color = cols[idx];
        // smaller, agile boss has less HP than the old giant — it's hard to TRACK,
        // not hard to chip; but still a meaningful multi-phase fight.
        boss_->max_hp = boss_->hp = 38 + tier * 15;
        // Entrance: it starts as a speck deep in the void, dead ahead, and rushes
        // up to its fighting depth while its shield (invuln) is still up.
        boss_->a = view_a_; boss_->e = clampf(view_e_ - 0.05f, -0.4f, 0.4f);
        boss_->z = 1.0f;
        boss_->ta = boss_->a; boss_->te = boss_->e;
        boss_->move_t = 2.0f;
        boss_->atk_iv = std::max(0.8f, 1.7f - tier * 0.12f);
        boss_->invuln = 2.0f;
        if (ctx_.sfx) ctx_.sfx->boss_alarm();
    }

    void add_flash(const std::string& t, float x, float y, Color c, float life = 1.0f, bool big = false) {
        flash_.push_back({t, x, y, life, life, c, big});
    }

    void submit_scores() {
        if (ctx_.demo) return;
        if (submitted_) return;
        submitted_ = true;
        tele(J().add("ev", "game_over").add("wave", wave_).add("score", score_).add("t", (double)game_t_));
        if (ctx_.scores) {
            new_hi_ = ctx_.scores->submit_void_gunner(score_, wave_);
            high_score_ = ctx_.scores->high_score("void_gunner");
        }
    }

    // Per-type stats applied to a freshly-built enemy.
    void apply_type_stats(Enemy& e) {
        float base = 0.10f + wave_ * 0.012f;
        e.vz = base * rnd_uniform(0.85f, 1.25f);
        switch (e.type) {
            case EType::Charger: e.weave_amp = 0;     e.weave_freq = 0;   e.hp = 1; break;
            case EType::Weaver:  e.weave_amp = rnd_uniform(0.3f, 0.6f); e.weave_freq = rnd_uniform(1.0f, 1.8f); e.hp = 1; break;
            case EType::Shooter: e.weave_amp = 0.15f; e.weave_freq = 0.8f; e.hp = 1; e.vz *= 0.85f; break;
            case EType::Tank:    e.weave_amp = 0.2f;  e.weave_freq = 0.6f; e.hp = 3; e.vz *= 0.75f; break;
        }
        e.max_hp = e.hp;
        e.fire_t = rnd_uniform(1.2f, 2.6f);
    }

    // Throttled vocalisation as an enemy surfaces, panned to where it appears.
    void chirp_for(const Enemy& e) {
        if (ctx_.sfx && chirp_t_ <= 0) {
            chirp_t_ = 0.32f;
            Proj p = project(e.a, e.e, e.z, view_a_, view_e_, ctx_.W, ctx_.H);
            ctx_.sfx->pan_next(clampf(p.sx / float(ctx_.W), 0.f, 1.f));
            int variant = e.type == EType::Tank ? 2 : e.type == EType::Shooter ? 1 : 0;
            ctx_.sfx->alien_chirp(variant);
        }
    }

    void spawn_tank() {
        Tank t;
        t.a = rnd_uniform(-1.0f, 1.0f);
        t.e = rnd_uniform(-0.6f, 0.6f);
        t.z = 1.0f;
        t.vz = 0.085f;
        t.phase = rnd_uniform(0, 6.28f);
        tanks_.push_back(t);
    }

    Proj proj(float a, float e, float z) const {
        return project(a, e, z, view_a_, view_e_, ctx_.W, ctx_.H);
    }
    // Is the reticle on a target whose sprite is `size_px` across? ONE test for
    // the laser and the lock indicator: the sprite's on-screen box plus a little
    // forgiveness, so a huge near enemy is hit anywhere on its body.
    bool reticle_on(float a, float e, float z, int size_px) const {
        Proj p = proj(a, e, z);
        // most of the sprite's box (its corners are empty) plus a forgiveness pad
        // that is generous for distant specks and shrinks as the target looms
        float half = size_px * 0.4f + std::max(10.0f, 46.0f - size_px * 0.12f);
        return std::fabs(p.sx - ctx_.W * 0.5f) <= half && std::fabs(p.sy - ctx_.H * 0.5f) <= half;
    }
    bool reticle_on_weak_spot() const {
        Proj wp = proj(boss_->wa(), boss_->we(), boss_->z);
        float dx = wp.sx - ctx_.W * 0.5f, dy = wp.sy - ctx_.H * 0.5f;
        return dx * dx + dy * dy <= CROSS_R * CROSS_R;
    }
    // Where an enemy shot is on screen (position magnification clamped at
    // SHOT_ZREF — see the constant). Used for drawing AND for the hit test.
    Proj shot_proj(const EShot& s) const {
        float zz = std::max(s.z, SHOT_ZREF);
        Proj p;
        p.sx = ctx_.W * 0.5f + (s.ca() - view_a_) * SPREAD / zz;
        p.sy = ctx_.H * 0.5f + (s.ce() - view_e_) * SPREAD / zz;
        p.scale = 1.0f / zz;
        p.on_screen = p.sx > -200 && p.sx < ctx_.W + 200 && p.sy > -200 && p.sy < ctx_.H + 200;
        return p;
    }
    // Would this shot connect if it arrived right now (i.e. is its landing point
    // within SHOT_HIT_R px of the reticle)?
    bool shot_on_course(const EShot& s) const {
        float dx = (s.ta - view_a_) * SPREAD / SHOT_ZREF, dy = (s.te - view_e_) * SPREAD / SHOT_ZREF;
        return dx * dx + dy * dy <= SHOT_HIT_R * SHOT_HIT_R;
    }
    void deny_blip() {
        if (ctx_.sfx) { ctx_.sfx->pan_next(0.5f); ctx_.sfx->low_energy_pip(); }
    }

    // The shot kills the enemy under the reticle; nearest (smallest z) wins. Also
    // hits tanks and the boss weak point.
    void fire();
    void fire_missile();
    void update_missiles(float dt);
    void detonate_missile(const Missile& m);
    bool energy_hit(float amount);
    void update_play(float dt, const Input& in);
    void update_game_over(float dt, const Input& in);
    void update_boss(float dt, bool player_alive);
    void boss_take(int dmg, float stun);
    void bot_decide(float& pan_a, float& pan_e, bool& want_fire);

    // formation spawning
    EType roll_type();
    void begin_formation();        // pick the next entrance pattern for the wave
    void release_one();            // emit one enemy from the active formation

    void draw_starfield(Surface& s);
    void draw_crosshair(Surface& s);
    void draw_hud(Surface& s);
    void draw_flash(Surface& s);
    void draw_notes(Surface& s);
    void draw_radar(Surface& s);
    void draw_eshot(Surface& s, const EShot& sh);
    void draw_enemy(Surface& s, const Enemy& e);
    void draw_tank(Surface& s, const Tank& t);
    void draw_boss(Surface& s);
};

// ── Enemy type roll (weighted, Tank gated by wave) ──────────────────────────
EType VoidGunnerGame::roll_type() {
    int r = rnd_int(0, 99);
    if (wave_ >= 3 && r < 18) return EType::Tank;
    if (r < 38) return EType::Shooter;
    if (r < 66) return EType::Weaver;
    return EType::Charger;
}

// ── Formation entrances ───────────────────────────────────────────────────────
// Pick the next coordinated entrance for this wave. Patterns are telegraphed by
// shared lanes/timing so the player can read where the group will come from.
//   0 Single   — a lone enemy
//   1 Squadron — a V/line entering together holding formation, then peeling off
//   2 Sequence — enemies file in one-after-another along a shared lane (snake)
//   3 Orbit    — enemies sweep in on a circular arc across your field while z closes
void VoidGunnerGame::begin_formation() {
    // weight patterns by wave: early waves favour singles; later waves crowd in.
    int roll = rnd_int(0, 99);
    int kind;
    if (wave_ <= 1)        kind = roll < 55 ? 0 : roll < 80 ? 2 : 1;
    else if (wave_ == 2)   kind = roll < 35 ? 0 : roll < 60 ? 1 : roll < 85 ? 2 : 3;
    else                   kind = roll < 22 ? 0 : roll < 52 ? 1 : roll < 78 ? 2 : 3;

    form_kind_ = kind;
    form_idx_ = 0;
    // shared lane: where the formation originates in (a,e) space.
    form_lane_a_ = rnd_uniform(-0.9f, 0.9f);
    form_lane_e_ = rnd_uniform(-0.5f, 0.5f);
    form_orbit_dir_ = rnd01() < 0.5f ? -1.0f : 1.0f;

    switch (kind) {
        case 0: form_left_ = 1; form_step_ = 0.0f; break;
        case 1: form_left_ = std::min(5, 3 + wave_ / 2); form_step_ = 0.10f; break;  // squadron: near-simultaneous
        case 2: form_left_ = std::min(6, 4 + wave_ / 2); form_step_ = 0.32f; break;  // snake: filed in
        case 3: form_left_ = std::min(5, 3 + wave_ / 2); form_step_ = 0.22f; break;  // orbit
    }
    // don't release more than the wave budget allows
    form_left_ = std::min(form_left_, to_spawn_);
}

// Emit one enemy from the active formation, configuring its scripted path.
void VoidGunnerGame::release_one() {
    Enemy e;
    e.type = roll_type();
    int i = form_idx_++;
    int n = form_left_ + i;  // approx total slots (i increments as we go)

    switch (form_kind_) {
        case 0: {  // single — legacy weave around a random home
            e.pat = Pattern::Single;
            e.a0 = e.a = rnd_uniform(-1.0f, 1.0f);
            e.e0 = e.e = rnd_uniform(-0.7f, 0.7f);
            apply_type_stats(e);
            break;
        }
        case 1: {  // SQUADRON — a V centred on the lane; hold, then peel off
            e.pat = Pattern::Squadron;
            int half = i / 2; float sgn = (i & 1) ? 1.0f : -1.0f;
            if (i == 0) { sgn = 0; half = 0; }
            float spread = 0.22f;
            e.form_a = clampf(form_lane_a_ + sgn * half * spread, -1.05f, 1.05f);
            e.form_e = clampf(form_lane_e_ - half * 0.10f, -0.8f, 0.8f);  // V dips
            e.a0 = e.a = e.form_a;
            e.e0 = e.e = e.form_e;
            apply_type_stats(e);
            e.vz *= 0.7f;                       // slower approach: they hold formation
            e.peel_t = rnd_uniform(2.0f, 3.0f); // peel off after holding a beat
            e.weave_amp = 0;                    // formation overrides weave until peel
            break;
        }
        case 2: {  // SEQUENCE / snake — file in along the lane, staggered phase
            e.pat = Pattern::Sequence;
            e.a0 = e.a = form_lane_a_;
            e.e0 = e.e = form_lane_e_;
            apply_type_stats(e);
            e.weave_amp = 0.45f;                // the lane snakes side-to-side
            e.weave_freq = 1.3f;
            e.phase = -i * 0.7f;                // each trails the one ahead
            break;
        }
        case 3: {  // ORBIT — sweep an arc across the field while closing in
            e.pat = Pattern::Orbit;
            e.orbit_r = rnd_uniform(0.6f, 0.95f);
            e.orbit_w = form_orbit_dir_ * rnd_uniform(0.7f, 1.0f);   // angular speed
            e.orbit_ph = form_lane_a_ * 0.6f + i * (PI / std::max(2, n));
            e.orbit_cy = form_lane_e_ * 0.5f;
            e.a0 = e.a = std::cos(e.orbit_ph) * e.orbit_r;
            e.e0 = e.e = e.orbit_cy + std::sin(e.orbit_ph) * e.orbit_r * 0.45f;
            apply_type_stats(e);
            e.weave_amp = 0;                    // the orbit IS the motion
            break;
        }
    }
    e.z = 1.0f;
    e.phase = e.pat == Pattern::Sequence ? e.phase : rnd_uniform(0, 6.28f);
    // Anchor any weave to the spawn position so it doesn't snap on the first frame.
    if (e.weave_amp > 0) {
        e.a0 = e.a - std::sin(e.phase) * e.weave_amp;
        e.e0 = e.e - std::cos(e.phase * 0.7f) * e.weave_amp * 0.5f;
    }
    e.id = next_eid_++;
    chirp_for(e);
    enemies_.push_back(e);

    if (--form_left_ <= 0) form_kind_ = -1;  // formation exhausted
}

// Damage the boss (laser or missile) and start its death if that finishes it.
void VoidGunnerGame::boss_take(int dmg, float stun) {
    boss_->hp -= dmg; boss_->hit_flash = 0.16f;
    boss_->hurt_t = std::max(boss_->hurt_t, stun);
    if (boss_->hp <= 0 && !boss_->dying) { boss_->dying = true; boss_->die_t = 0; }
}

// ── Fire ─────────────────────────────────────────────────────────────────────
void VoidGunnerGame::fire() {
    if (fire_cd_ > 0) return;
    // ── laser energy gate: an overheated laser stays locked out until it has
    //    recharged to LASER_RESUME (one blip when it trips, not a stutter). ──
    if (overheated_) { overheat_t_ = 0.35f; return; }
    if (laser_ < LASER_MIN_FIRE) {
        overheated_ = true; overheat_t_ = 0.35f;
        deny_blip();
        return;
    }
    laser_ -= LASER_COST;
    laser_pause_ = LASER_PAUSE;     // brief regen lockout after each shot
    if (laser_ < LASER_MIN_FIRE) { overheated_ = true; deny_blip(); }
    fire_cd_ = 0.16f - 0.02f * (laser_lvl_ - 1);   // upgraded lasers fire faster
    lasers_.push_back({-1.0f, 0.0f});
    lasers_.push_back({+1.0f, 0.0f});
    if (ctx_.sfx) ctx_.sfx->beam_fire();   // bright convergent player laser
    if (ctx_.fx) ctx_.fx->rumble_tap();

    int W = ctx_.W;
    // Boss weak point first (it's the priority target on boss waves).
    if (boss_ && !boss_->dying && reticle_on_weak_spot()) {
        Proj wp = proj(boss_->wa(), boss_->we(), boss_->z);
        if (!boss_->damageable()) {
            // still shielded (arriving): the shot visibly glances off
            particles_.explode_invader(int(wp.sx), int(wp.sy), Color(160, 215, 255), 4);
            if (ctx_.sfx) ctx_.sfx->armor_tink();
            return;
        }
        boss_take(3 + laser_lvl_ * 2, 0.22f);
        score_ += 15 * (ctx_.combo ? ctx_.combo->hit() : 1);
        particles_.explode_invader(int(wp.sx), int(wp.sy), boss_->color, 10);
        if (ctx_.sfx) ctx_.sfx->invader_hit();
        if (ctx_.fx) ctx_.fx->shake(3);
        return;
    }

    // Best (nearest) enemy under the reticle.
    int best = -1; float best_z = 1e9f;
    for (int i = 0; i < (int)enemies_.size(); ++i) {
        Enemy& e = enemies_[i];
        if (!e.alive) continue;
        if (reticle_on(e.a, e.e, e.z, enemy_px(e.z)) && e.z < best_z) { best_z = e.z; best = i; }
    }
    // Energy canisters too (refill).
    int best_tank = -1; float bt_z = 1e9f;
    for (int i = 0; i < (int)tanks_.size(); ++i) {
        Tank& t = tanks_[i];
        if (!t.alive) continue;
        if (reticle_on(t.a, t.e, t.z, tank_px(t.z)) && t.z < bt_z) { bt_z = t.z; best_tank = i; }
    }

    // Whichever is nearer (enemy vs tank) gets hit; tanks are valuable so a tie
    // favours the tank.
    if (best_tank >= 0 && (best < 0 || bt_z <= best_z + 0.02f)) {
        Tank& t = tanks_[best_tank];
        t.alive = false;
        energy_ = std::min(MAX_ENERGY, energy_ + 40.0f);
        laser_ = std::min(LASER_MAX, laser_ + 0.6f);  // tanks recharge the laser too
        overheated_ = false;
        bool got_missile = missiles_ < MISSILES_MAX;
        if (got_missile) missiles_++;                 // ...and top up a missile
        overheat_t_ = 0;
        Proj p = proj(t.a, t.e, t.z);
        particles_.ufo_explode(int(p.sx), int(p.sy), 24);
        add_flash(got_missile ? "+ENERGY +LASER +MISSILE" : "+ENERGY +LASER",
                  p.sx, p.sy - 30, CAN, 1.1f, true);
        if (ctx_.sfx) { ctx_.sfx->pan_next(clampf(p.sx / float(W), 0.f, 1.f)); ctx_.sfx->energy_refill(); }
        if (ctx_.fx) { ctx_.fx->rumble_pickup(); ctx_.fx->shake(4); }
        return;
    }
    if (best >= 0) {
        Enemy& e = enemies_[best];
        e.hp -= laser_lvl_; e.hit_flash = 0.18f;
        Proj p = proj(e.a, e.e, e.z);
        if (e.hp <= 0) {
            e.alive = false;
            int pts = e.points();
            int mult = ctx_.combo ? ctx_.combo->hit() : 1;
            score_ += pts * mult;
            particles_.explode_invader(int(p.sx), int(p.sy), e.color(),
                                       int(18 + 22 * p.scale * 0.1f));
            add_flash("+" + std::to_string(pts * mult), p.sx, p.sy - 22, YELLOW);
            if (ctx_.sfx) {
                ctx_.sfx->pan_next(clampf(p.sx / float(W), 0.f, 1.f));
                // the armoured beetle gets a meatier kill; others a sharp zap
                if (e.type == EType::Tank) ctx_.sfx->boss_explosion();
                else { ctx_.sfx->zap(); ctx_.sfx->invader_hit(); }
            }
            if (ctx_.fx) ctx_.fx->shake(3 + std::min(6.0f, p.scale));
        } else {
            // armour chipped, not dead yet
            particles_.explode_invader(int(p.sx), int(p.sy), e.color(), 6);
            if (ctx_.sfx) { ctx_.sfx->pan_next(clampf(p.sx / float(W), 0.f, 1.f)); ctx_.sfx->armor_tink(); }
        }
    }
}

// ── Auto-lock missile ─────────────────────────────────────────────────────────
// Launches from the bottom centre and homes to a target it picks automatically —
// the enemy nearest the reticle (or the boss). Detonates with splash damage.
void VoidGunnerGame::fire_missile() {
    if (missile_cd_ > 0) return;
    if (missiles_ <= 0) { deny_blip(); return; }   // out of ammo
    int W = ctx_.W, H = ctx_.H;
    // Auto-lock: the enemy nearest the reticle wins (depth breaks ties).
    int best = -1; float best_score = 1e18f;
    for (int i = 0; i < (int)enemies_.size(); ++i) {
        Enemy& e = enemies_[i];
        if (!e.alive) continue;
        Proj p = proj(e.a, e.e, e.z);
        float dx = p.sx - W * 0.5f, dy = p.sy - H * 0.5f;
        float sc = dx * dx + dy * dy + e.z * 4000.0f;
        if (sc < best_score) { best_score = sc; best = i; }
    }
    Missile m;
    m.sx = W * 0.5f; m.sy = H + 20.0f;
    m.vx = 0; m.vy = -700.0f;
    if (best >= 0) {
        Enemy& e = enemies_[best];
        m.target_id = e.id;
        Proj p = proj(e.a, e.e, e.z);
        m.tx = p.sx; m.ty = p.sy;
    } else if (boss_ && !boss_->dying) {
        if (!boss_->damageable()) {
            // shield still up: hold the missile instead of wasting it
            if (ctx_.sfx) { ctx_.sfx->pan_next(0.5f); ctx_.sfx->armor_tink(); }
            missile_cd_ = 0.25f;
            return;
        }
        m.target_id = -2;   // -2 = boss weak point
        Proj p = proj(boss_->wa(), boss_->we(), boss_->z);
        m.tx = p.sx; m.ty = p.sy;
    } else {
        m.target_id = -1;   // nothing to lock — fly toward the reticle
        m.tx = W * 0.5f; m.ty = H * 0.42f;
    }
    pmissiles_.push_back(m);
    missiles_--;
    missile_cd_ = 0.30f;
    if (ctx_.sfx) { ctx_.sfx->pan_next(0.5f); ctx_.sfx->beam_fire(); }
    if (ctx_.fx) ctx_.fx->rumble_pickup();
}

void VoidGunnerGame::update_missiles(float dt) {
    for (auto& m : pmissiles_) {
        if (!m.alive) continue;
        m.t += dt;
        // Track the locked target's live position; if it dies, coast to the last point.
        if (m.target_id >= 0) {
            const Enemy* tgt = nullptr;
            for (auto& e : enemies_) if (e.alive && e.id == m.target_id) { tgt = &e; break; }
            if (tgt) { Proj p = proj(tgt->a, tgt->e, tgt->z); m.tx = p.sx; m.ty = p.sy; }
            else m.target_id = -1;
        } else if (m.target_id == -2 && boss_ && boss_->damageable()) {
            Proj p = proj(boss_->wa(), boss_->we(), boss_->z);
            m.tx = p.sx; m.ty = p.sy;
        }
        float dx = m.tx - m.sx, dy = m.ty - m.sy;
        float d = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
        float dvx = dx / d * 780.0f, dvy = dy / d * 780.0f;
        m.vx += (dvx - m.vx) * std::min(1.0f, dt * 8.0f);
        m.vy += (dvy - m.vy) * std::min(1.0f, dt * 8.0f);
        m.sx += m.vx * dt; m.sy += m.vy * dt;
        if (dx * dx + dy * dy < 24 * 24 || m.t > 1.7f || m.sy < -30) {
            detonate_missile(m);
            m.alive = false;
        }
    }
    pmissiles_.erase(std::remove_if(pmissiles_.begin(), pmissiles_.end(),
                     [](const Missile& m) { return !m.alive; }), pmissiles_.end());
}

void VoidGunnerGame::detonate_missile(const Missile& m) {
    int W = ctx_.W;
    particles_.explode_invader(int(m.sx), int(m.sy), Color(255, 180, 90), 26);
    if (ctx_.sfx) { ctx_.sfx->pan_next(clampf(m.sx / float(W), 0.f, 1.f)); ctx_.sfx->big_explosion(); }
    if (ctx_.fx) ctx_.fx->shake(6);
    const float R = 95.0f;   // splash radius, in screen px around the blast
    for (auto& e : enemies_) {
        if (!e.alive) continue;
        Proj p = proj(e.a, e.e, e.z);
        float dx = p.sx - m.sx, dy = p.sy - m.sy;
        float reach = R + enemy_px(e.z) * 0.5f;   // the blast catches the sprite, not just its centre
        if (dx * dx + dy * dy > reach * reach) continue;
        e.hp -= (e.id == m.target_id) ? 4 : 2;   // direct hit hurts more than the splash
        e.hit_flash = 0.18f;
        if (e.hp <= 0) {
            e.alive = false;
            int pts = e.points();
            int mult = ctx_.combo ? ctx_.combo->hit() : 1;
            score_ += pts * mult;
            particles_.explode_invader(int(p.sx), int(p.sy), e.color(), 16);
            add_flash("+" + std::to_string(pts * mult), p.sx, p.sy - 20, YELLOW);
        }
    }
    if (boss_ && boss_->damageable()) {
        Proj p = proj(boss_->wa(), boss_->we(), boss_->z);
        float dx = p.sx - m.sx, dy = p.sy - m.sy;
        if (dx * dx + dy * dy <= (R * 1.2f) * (R * 1.2f)) {
            boss_take(6, 0.35f);
            int pts = 40 * (ctx_.combo ? ctx_.combo->hit() : 1);
            score_ += pts;
            particles_.explode_invader(int(p.sx), int(p.sy), boss_->color, 16);
            add_flash("+" + std::to_string(pts), p.sx, p.sy - 34, YELLOW);
        }
    }
}

// Returns true if the hit actually landed (not shrugged off by i-frames).
bool VoidGunnerGame::energy_hit(float amount) {
    if (state_ != St::Play || invinc_ > 0 || hurt_t_ > 0) return false;
    energy_ -= amount;
    hit_flash_ = 1.0f;
    hurt_t_ = HURT_IFRAMES;   // one volley = one hit; no multi-bolt stacking
    if (ctx_.fx) { ctx_.fx->shake(12); ctx_.fx->rumble_hit(); ctx_.fx->hitstop(0.05f); }
    if (energy_ > 0) {
        if (ctx_.sfx) ctx_.sfx->shield_hit();     // a hit: shields ping…
        return true;
    }
    energy_ = 0;
    lives_--;
    particles_.explode_player(ctx_.W / 2, ctx_.H / 2, 60);
    if (ctx_.sfx) ctx_.sfx->player_death();       // …only a kill gets the death sound
    if (ctx_.fx) { ctx_.fx->shake(22); ctx_.fx->hitstop(0.10f); ctx_.fx->rumble_death(); ctx_.fx->shockwave(0.5f, 0.5f); }
    state_ = St::Dead; dead_t_ = 2.0f;
    tele(J().add("ev", "death").add("wave", wave_).add("t", (double)game_t_).add("lives_left", lives_));
    return true;
}

// ── Update ───────────────────────────────────────────────────────────────────
void VoidGunnerGame::update(float dt, const Input& in) {
    pad_ = in.pad_connected;
    // Pause works everywhere except the game-over screen (where START/P = menu).
    // ESC opens the pause menu too (quit lives there) instead of dropping the run.
    if ((in.just(SDLK_p) || (in.just(SDLK_ESCAPE) && !pause_.active())) && state_ != St::GameOver) pause_.toggle();
    if (pause_.active()) {
        if (pause_.update(in) == PauseResult::QuitToMenu) quit_to_menu = true;
        return;
    }

    game_t_ += dt;
    // (the shared combo timer is ticked by main.cpp — ticking it here too halved the window)
    particles_.update(dt);
    if (hit_flash_ > 0) hit_flash_ -= dt * 2.2f;
    if (lock_glow_ > 0) lock_glow_ -= dt * 3.0f;
    for (auto& f : flash_) f.life -= dt;
    flash_.erase(std::remove_if(flash_.begin(), flash_.end(),
                 [](const Flash& f) { return f.life <= 0; }), flash_.end());
    if (!notes_.empty()) {
        note_t_ += dt;
        if (note_t_ >= notes_.front().dur) { notes_.erase(notes_.begin()); note_t_ = 0; }
    }

    // Starfield always streaming (intro + play) for the "flowing at you" feel.
    for (auto& st : stars_) {
        st.z -= st.speed * dt;
        if (st.z < 0.04f) st.recycle();
    }
    for (auto& l : lasers_) l.t += dt;
    lasers_.erase(std::remove_if(lasers_.begin(), lasers_.end(),
                  [](const Laser& l) { return l.t > 0.12f; }), lasers_.end());

    // Laser energy recharges in EVERY state — including between stages and on the
    // intro — pausing only briefly right after a shot. (Was Play-only before.)
    if (laser_pause_ > 0) laser_pause_ -= dt;
    else if (laser_ < LASER_MAX) laser_ = std::min(LASER_MAX, laser_ + LASER_REGEN * dt);
    if (overheated_ && laser_ >= LASER_RESUME) overheated_ = false;
    if (overheat_t_ > 0) overheat_t_ -= dt;

    if (state_ == St::GameOver) { update_game_over(dt, in); return; }
    if (state_ == St::Intro) {
        intro_t_ -= dt;
        if (intro_t_ <= 0) state_ = St::Play;
        return;
    }
    // Play AND Dead both run the scene: while you are down the void keeps moving
    // (enemies hang back, shots fly past) instead of freezing for two seconds.
    update_play(dt, in);
    if (state_ == St::Dead) {
        dead_t_ -= dt;
        if (dead_t_ <= 0) {
            if (lives_ > 0) {
                energy_ = MAX_ENERGY;
                invinc_ = 2.5f;
                eshots_.clear();
                state_ = St::Play;
                if (ctx_.sfx) ctx_.sfx->spawn_chime();
            } else {
                state_ = St::GameOver; go_t_ = 0;
                notes_.clear(); pmissiles_.clear(); eshots_.clear();
                submit_scores();
                if (ctx_.sfx && !bot_) ctx_.sfx->game_over();
            }
        }
    }
}

void VoidGunnerGame::update_game_over(float dt, const Input& in) {
    go_t_ += dt;
    if (bot_) { new_game(); return; }
    if (ctx_.scores) {
        const std::string gid = "void_gunner";
        if (!entry_started_ && ctx_.scores->qualifies(gid, score_)) {
            entry_started_ = true;
            int rank = 1;
            for (auto& e : ctx_.scores->leaderboard(gid)) if (e.score > score_) rank++;
            initials_.begin("VOID GUNNER", score_, rank);
        }
        if (initials_.active()) {
            if (initials_.update(in)) ctx_.scores->add_score(gid, initials_.name(), score_);
            return;
        }
    }
    if (ui::game_over_retry(in, go_t_)) new_game();
    else if (ui::game_over_menu(in, go_t_)) quit_to_menu = true;
}

void VoidGunnerGame::update_play(float dt, const Input& in) {
    // `live` = the player is in the fight. While dead the scene still animates,
    // but nothing spawns, shoots or closes in.
    const bool live = state_ == St::Play;
    if (fire_cd_ > 0) fire_cd_ -= dt;
    if (live && invinc_ > 0) invinc_ -= dt;
    if (hurt_t_ > 0) hurt_t_ -= dt;
    if (chirp_t_ > 0) chirp_t_ -= dt;
    if (missile_cd_ > 0) missile_cd_ -= dt;

    // ── input / bot: pan the view & fire ──
    float pan_a = 0, pan_e = 0; bool want_fire = false, want_missile = false;
    if (!live) {
        msl_btn_prev_ = in.pad[PadThrUp] || in.pad[PadThrDown];
    } else if (bot_) {
        bot_decide(pan_a, pan_e, want_fire);
        // occasionally lob a missile so the bot demos the weapon
        if (missiles_ > 0 && missile_cd_ <= 0 && !enemies_.empty() && rnd01() < 1.8f * dt) want_missile = true;
    } else {
        pan_a = (in.down(SDLK_RIGHT) || in.down(SDLK_d)) - (in.down(SDLK_LEFT) || in.down(SDLK_a));
        pan_e = (in.down(SDLK_DOWN) || in.down(SDLK_s)) - (in.down(SDLK_UP) || in.down(SDLK_w));
        want_fire = in.down(SDLK_SPACE);
        // missile: keyboard B / LSHIFT (one per press), gamepad Y (→B) OR either
        // shoulder button (edge-detected so a held shoulder fires just one).
        if (in.just(SDLK_b) || in.just(SDLK_LSHIFT)) want_missile = true;
        bool msl_btn = in.pad[PadThrUp] || in.pad[PadThrDown];  // RB / LB
        if (msl_btn && !msl_btn_prev_) want_missile = true;
        msl_btn_prev_ = msl_btn;
    }
    // ── inertial pan: the view velocity chases the held input and decays when
    //    released, so turning has weight/momentum instead of snapping. ──
    float target_va = pan_a * PAN_SPEED, target_ve = pan_e * PAN_SPEED;
    float ka = (pan_a != 0 ? PAN_ACCEL : PAN_DAMP) * dt;
    float ke = (pan_e != 0 ? PAN_ACCEL : PAN_DAMP) * dt;
    pan_va_ += (target_va - pan_va_) * std::min(1.0f, ka);
    pan_ve_ += (target_ve - pan_ve_) * std::min(1.0f, ke);
    view_a_ += pan_va_ * dt;
    view_e_ += pan_ve_ * dt;
    // Gentle spring back toward centre when not actively panning.
    if (pan_a == 0) view_a_ -= clampf(view_a_, -1.0f, 1.0f) * 0.9f * dt;
    if (pan_e == 0) view_e_ -= clampf(view_e_, -1.0f, 1.0f) * 0.9f * dt;
    // At the pan limit, kill any velocity still pushing outward so the view
    // doesn't "stick" there (frame-rate independent, unlike a per-frame decay).
    if (view_a_ <= -VIEW_LIM && pan_va_ < 0) pan_va_ = 0;
    if (view_a_ >= VIEW_LIM && pan_va_ > 0) pan_va_ = 0;
    if (view_e_ <= -VIEW_LIM && pan_ve_ < 0) pan_ve_ = 0;
    if (view_e_ >= VIEW_LIM && pan_ve_ > 0) pan_ve_ = 0;
    view_a_ = clampf(view_a_, -VIEW_LIM, VIEW_LIM);
    view_e_ = clampf(view_e_, -VIEW_LIM, VIEW_LIM);

    if (want_fire) fire();
    if (want_missile) fire_missile();
    update_missiles(dt);

    // ── spawn enemies as coordinated formations / tanks ──
    if (live && !boss_wave_) {
        spawn_t_ -= dt;
        if (to_spawn_ > 0 && spawn_t_ <= 0) {
            // Start a new formation if the previous one finished releasing.
            if (form_kind_ < 0 || form_left_ <= 0) begin_formation();
            release_one();
            to_spawn_--;
            if (form_left_ > 0) {
                // mid-formation cadence: tight for squadrons, paced for snakes
                spawn_t_ = std::max(0.06f, form_step_) * rnd_uniform(0.85f, 1.15f);
            } else {
                // gap before the next formation/single arrives
                spawn_t_ = std::max(0.6f, 1.5f - wave_ * 0.05f) * rnd_uniform(0.8f, 1.25f);
            }
        }
    }
    if (live) {
        tank_t_ -= dt;
        if (tank_t_ <= 0) { spawn_tank(); tank_t_ = rnd_uniform(7.0f, 12.0f); }
    }

    // ── enemies approach ──
    for (auto& e : enemies_) {
        if (!e.alive) continue;
        if (e.hit_flash > 0) e.hit_flash -= dt * 5;
        // closing in while you fight; drifting back a little while you are down
        // (and once committed to the ramming run it lunges — RAM_RUSH× faster)
        if (state_ == St::Play) e.z -= e.vz * (e.z < RAM_Z ? RAM_RUSH : 1.0f) * dt;
        else e.z = std::min(1.0f, e.z + 0.07f * dt);
        e.life_t += dt;
        // animate faster as they near you (legs/wings flap quicker up close)
        e.anim_t += dt * (4.0f + (1.0f - e.z) * 6.0f);
        e.frame = (int(e.anim_t) & 1);
        e.phase += e.weave_freq * dt;
        // back onto the scripted path (drop last frame's ramming offset)
        e.a -= e.ram_da; e.e -= e.ram_de;
        // ── scripted formation/path motion ──
        if (e.pat == Pattern::Squadron && !e.peeled) {
            // ease toward the formation slot; hold until peel time.
            e.a += (e.form_a - e.a) * std::min(1.0f, dt * 3.0f);
            e.e += (e.form_e - e.e) * std::min(1.0f, dt * 3.0f);
            if (e.life_t >= e.peel_t) {
                e.peeled = true;
                // peel: accelerate and lurch outward, then resume a weave
                e.vz *= 1.8f;
                e.weave_amp = rnd_uniform(0.25f, 0.5f);
                e.weave_freq = rnd_uniform(1.2f, 2.0f);
                e.phase = rnd_uniform(0, 6.28f);
                // anchor the weave to the CURRENT position so a/e don't jump on
                // the first weave frame (this was the "teleport").
                e.a0 = e.a - std::sin(e.phase) * e.weave_amp;
                e.e0 = e.e - std::cos(e.phase * 0.7f) * e.weave_amp * 0.5f;
            }
        } else if (e.pat == Pattern::Orbit) {
            // sweep an arc across the field while z closes (radius eases inward)
            float ang = e.orbit_ph + e.orbit_w * e.life_t;
            float r = e.orbit_r * (0.55f + 0.45f * e.z);  // tightens as it nears
            e.a = std::cos(ang) * r;
            e.e = e.orbit_cy + std::sin(ang) * r * 0.45f;
        } else if (e.weave_amp > 0) {
            e.a = e.a0 + std::sin(e.phase) * e.weave_amp;
            e.e = e.e0 + std::cos(e.phase * 0.7f) * e.weave_amp * 0.5f;
        }
        // Never beyond the arc the player can actually aim at: a wide weave
        // flattens against the edge instead of hiding where no shot can reach.
        e.a = clampf(e.a, -ARC_LIM, ARC_LIM);
        e.e = clampf(e.e, -ARC_LIM, ARC_LIM);
        // ── ramming run: in its last stretch it swings off its path and straight
        //    at you, so the hit you take is the enemy you SEE filling the screen
        //    (and it gives you one last clean shot at it). ──
        {
            float t = clampf((RAM_Z - e.z) / (RAM_Z - 0.05f), 0.0f, 1.0f);
            float k = 1.0f - (1.0f - t) * (1.0f - t);
            e.ram_da = (view_a_ - e.a) * k; e.ram_de = (view_e_ - e.e) * k;
            e.a += e.ram_da; e.e += e.ram_de;
        }
        // Shooters fire toward your CURRENT view (so dodging by panning works) —
        // never from point-blank, where no pan could get you clear in time.
        if (state_ == St::Play && (e.type == EType::Shooter || e.type == EType::Tank)) {
            e.fire_t -= dt;
            if (e.fire_t <= 0 && e.z > 0.36f && e.z < 0.85f) {
                e.fire_t = rnd_uniform(1.6f, 2.8f);
                eshots_.push_back(make_eshot(e.a, e.e, view_a_, view_e_, e.z,
                                             std::min(0.95f, 0.55f + wave_ * 0.02f), e.color()));
                if (ctx_.sfx) {
                    Proj ep = proj(e.a, e.e, e.z);
                    ctx_.sfx->pan_next(clampf(ep.sx / float(ctx_.W), 0.f, 1.f));
                    ctx_.sfx->enemy_shot();
                }
            }
        }
        // Reached you -> energy hit (direct collision).
        if (state_ == St::Play && e.z <= 0.05f) {
            e.alive = false;
            if (energy_hit(20.0f))
                add_flash("IMPACT!", ctx_.W / 2, ctx_.H / 2 + 96, RED, 0.9f, true);
        }
    }
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
                   [](const Enemy& e) { return !e.alive; }), enemies_.end());

    // ── tanks drift in; expire harmlessly if missed ──
    for (auto& t : tanks_) {
        if (!t.alive) continue;
        t.z -= t.vz * dt;
        t.phase += dt;
        if (t.z <= 0.06f) t.alive = false;   // missed it; floats past
    }
    tanks_.erase(std::remove_if(tanks_.begin(), tanks_.end(),
                 [](const Tank& t) { return !t.alive; }), tanks_.end());

    // ── enemy shots: fly to the direction they were aimed at; on arrival they
    //    connect only if they land near the reticle ON SCREEN ──
    threat_ = false;
    for (auto& s : eshots_) {
        if (!s.alive) continue;
        s.z -= s.vz * dt;
        if (s.z <= SHOT_HIT_Z) {
            s.z = SHOT_HIT_Z; s.alive = false;
            if (shot_on_course(s)) {
                energy_hit(14.0f);
            } else if (state_ == St::Play && ctx_.sfx) {
                ctx_.sfx->bunker_hit();   // whiffed past you
            }
        } else if (s.prog() > 0.45f && shot_on_course(s)) {
            threat_ = true;               // reticle warning: pan away NOW
        }
    }
    eshots_.erase(std::remove_if(eshots_.begin(), eshots_.end(),
                  [](const EShot& s) { return !s.alive; }), eshots_.end());

    // ── boss ──
    if (boss_) update_boss(dt, state_ == St::Play);

    if (state_ != St::Play) { lock_active_ = false; return; }

    // ── low-energy accelerating warning pips ──
    if (energy_ < 25.0f && energy_ > 0) {
        pip_t_ -= dt;
        if (pip_t_ <= 0) {
            float frac = energy_ / 25.0f;             // 1 at 25%, 0 near death
            pip_t_ = 0.15f + 0.25f * frac;            // 0.4s -> 0.15s as it drops
            if (ctx_.sfx) ctx_.sfx->low_energy_pip();
        }
    }

    // ── lock highlight: is something under the reticle? (same test as fire()) ──
    {
        bool locked = false;
        for (auto& e : enemies_) if (e.alive && reticle_on(e.a, e.e, e.z, enemy_px(e.z))) { locked = true; break; }
        if (!locked) for (auto& t : tanks_) if (t.alive && reticle_on(t.a, t.e, t.z, tank_px(t.z))) { locked = true; break; }
        if (!locked && boss_ && boss_->damageable() && reticle_on_weak_spot()) locked = true;
        if (locked) lock_glow_ = 1.0f;
        // play lock_on() once on the rising edge (don't spam every locked frame)
        if (locked && !lock_active_ && ctx_.sfx) ctx_.sfx->lock_on();
        lock_active_ = locked;
    }

    // ── bonus life every LIFE_EVERY points ──
    while (score_ >= next_life_at_) {
        life_step_ *= 2;                 // 25k, 75k, 175k, … so lives stay precious
        next_life_at_ += life_step_;
        if (lives_ < LIVES_MAX) {
            lives_++;
            add_flash("EXTRA LIFE!", ctx_.W - 90, ctx_.H - 64, CYAN, 1.6f);
            if (ctx_.sfx) ctx_.sfx->one_up();
        }
    }

    // ── wave clear ──
    if (!boss_wave_ && to_spawn_ == 0 && enemies_.empty()) {
        int bonus = 200 * wave_;
        announce("WAVE CLEARED!", "+" + std::to_string(bonus) + " BONUS", YELLOW, 1.1f);
        score_ += bonus;
        if (ctx_.sfx) ctx_.sfx->level_clear();
        tele(J().add("ev", "wave_clear").add("wave", wave_).add("score", score_).add("t", (double)game_t_));
        start_wave(wave_ + 1);   // seamless — keep traveling into the next wave
    }
}

void VoidGunnerGame::update_boss(float dt, bool player_alive) {
    Boss& b = *boss_;
    if (b.hit_flash > 0) b.hit_flash -= dt * 6;
    bool arriving = b.invuln > 0;
    if (b.invuln > 0) b.invuln -= dt;
    b.sway_t += dt;
    b.wp_t += dt;
    // The weak-spot is FIXED to the body (constant local offset) — it only pulses.
    // (b.wp_a / b.wp_e stay at their initialised values.)

    if (b.dying) {
        b.die_t += dt;
        if (rnd01() < 36.0f * dt) {
            Proj p = proj(b.pa() + rnd_uniform(-0.3f, 0.3f), b.pe() + rnd_uniform(-0.2f, 0.2f), b.z);
            particles_.ufo_explode(int(p.sx), int(p.sy), 12);
        }
        if (b.die_t >= 1.6f && player_alive) {
            int bonus = 1000 * std::max(1, wave_ / 4);
            score_ += bonus;
            Proj p = proj(b.pa(), b.pe(), b.z);
            if (ctx_.fx) {
                ctx_.fx->shake(30); ctx_.fx->hitstop(0.10f);
                ctx_.fx->rumble_explosion();
                ctx_.fx->shockwave(clampf(p.sx / float(ctx_.W), 0.f, 1.f),
                                   clampf(p.sy / float(ctx_.H), 0.f, 1.f));
            }
            if (ctx_.sfx) ctx_.sfx->big_explosion();   // bigger than boss_explosion
            // boss reward: LASER UPGRADE + restock missiles
            std::string reward = "+" + std::to_string(bonus) + "   MISSILES RESTOCKED";
            if (laser_lvl_ < LASER_LVL_MAX) {
                laser_lvl_++;
                reward = "LASER UPGRADED  Lv" + std::to_string(laser_lvl_) + "   +3 MISSILES";
            }
            announce(b.name + " DESTROYED!", reward, YELLOW, 2.0f);
            missiles_ = std::min(MISSILES_MAX, missiles_ + 3);
            tele(J().add("ev", "boss_clear").add("wave", wave_).add("name", b.name)
                    .add("laser_lvl", laser_lvl_).add("t", (double)game_t_));
            boss_.reset();
            if (ctx_.sfx) ctx_.sfx->level_clear();
            start_wave(wave_ + 1);   // seamless — keep flying after the boss falls
        }
        return;
    }

    // ── Mobile body: glides between targets, weaving around your view. Trackable —
    // a touch faster each phase, but never a twitchy blur. ──
    b.phase_from_hp();
    if (b.hurt_t > 0) b.hurt_t -= dt;
    // It FEELS the pain: the more worn down it is — and briefly right after each
    // hit — the slower it glides AND attacks, so a laser duel actually grinds it
    // down instead of it enraging out of reach.
    float pain = 0.40f + 0.60f * b.hp_ratio();      // 1.0 fresh -> 0.40 near death
    float stun = b.hurt_t > 0 ? 0.12f : 1.0f;       // near-freeze in the hit-stun window
    float amul = pain * stun;                       // combined action-speed multiplier
    float agility = (b.phase == 3 ? 0.95f : b.phase == 2 ? 0.85f : 0.78f) * amul;
    float adt = dt * amul;                          // attacks/telegraph advance slower when hurt
    b.move_t -= dt;
    if (b.move_t <= 0) {
        // pick a fresh target; an occasional (rarer, gentler) feint.
        bool feint = rnd01() < 0.14f;
        b.feint = feint ? rnd_uniform(0.25f, 0.40f) : 0.0f;
        b.move_t = feint ? b.feint : rnd_uniform(1.0f, 1.8f) / agility;
        float reach = 0.78f;
        b.ta = rnd_uniform(-reach, reach);
        b.te = -0.05f + rnd_uniform(-0.28f, 0.28f);
        // bias a strafe relative to your view so it weaves around you
        if (rnd01() < 0.4f) b.ta = clampf(view_a_ + rnd_uniform(-0.6f, 0.6f), -0.95f, 0.95f);
    }
    // glide toward the target (smooth, not snappy) → readable, trackable motion.
    // (The faint wobble lives in Boss::pa()/pe() as an offset, not added here.)
    float chase = std::min(1.0f, dt * (3.0f * agility));
    b.a += (b.ta - b.a) * chase;
    b.e += (b.te - b.e) * chase;
    // Depth: rushes in from the void on arrival, then holds mid-depth (feinting
    // a little closer on phase 3).
    float zt = b.phase == 3 ? 0.46f : 0.55f;
    b.z += (zt - b.z) * std::min(1.0f, dt * (arriving ? 2.6f : 0.8f));

    if (arriving || !player_alive) return;   // no attacks while arriving / while you are down

    float iv = b.atk_iv * (b.phase == 3 ? 0.6f : b.phase == 2 ? 0.82f : 1.0f);
    // every burst gets exactly ONE shot sound, panned to the boss
    auto shot_sfx = [&]() {
        if (!ctx_.sfx) return;
        Proj p = proj(b.pa(), b.pe(), b.z);
        ctx_.sfx->pan_next(clampf(p.sx / float(ctx_.W), 0.f, 1.f));
        ctx_.sfx->enemy_shot();
    };

    if (b.telegraph > 0) {
        b.telegraph -= adt;
        if (b.telegraph <= 0) { b.cur = b.pending; b.sub_t = 99; b.atk_t = 0;
            if (ctx_.sfx) ctx_.sfx->dive_warble(); }
    } else if (b.cur >= 0) {
        // run sustained attack
        b.atk_t += adt; b.sub_t += adt;
        // 0 RADIAL burst, 1 AIMED stream at your view, 2 SPREAD fan
        if (b.cur == 0) {  // RADIAL: spray outward from the boss in a fan
            for (int i = 0; i < 9; ++i)
                eshots_.push_back(make_eshot(b.pa(), b.pe(), b.pa() + (i - 4) * 0.26f,
                                             b.pe() + rnd_uniform(-0.25f, 0.25f), b.z, 0.5f, b.color));
            shot_sfx();
            b.cur = -1;
        } else if (b.cur == 1) {  // AIMED: stream at your view, from the boss
            if (b.sub_t >= 0.18f) { b.sub_t = 0;
                eshots_.push_back(make_eshot(b.pa(), b.pe(), view_a_, view_e_, b.z, 0.7f, b.color));
                shot_sfx(); }
            if (b.atk_t >= 1.4f) b.cur = -1;
        } else if (b.cur == 2) {  // SPREAD: a fan biased toward you, from the boss
            for (int i = -3; i <= 3; ++i)
                eshots_.push_back(make_eshot(b.pa(), b.pe(), b.pa() + i * 0.14f, b.pe() + 0.1f, b.z, 0.55f, b.color));
            shot_sfx();
            b.cur = -1;
        } else b.cur = -1;
    } else {
        b.atk_t += adt;
        if (b.atk_t >= iv) {
            b.atk_t = 0;
            b.pending = rnd_int(0, 2);
            b.telegraph = b.phase == 3 ? 0.45f : 0.7f;
        }
    }
}

// ── Bot: pan toward the nearest / lowest-z target, fire when aligned. Prioritise
// energy tanks when low; on boss waves track the weak point. ──────────────────
void VoidGunnerGame::bot_decide(float& pan_a, float& pan_e, bool& want_fire) {
    int W = ctx_.W, H = ctx_.H;
    // Choose a target direction (a,e).
    float ta = 0, te = 0; bool have = false; float best = 1e9f;

    bool low = energy_ < 45.0f;
    // tanks when low on energy (high priority)
    if (low) {
        for (auto& t : tanks_) {
            if (!t.alive) continue;
            if (t.z < best) { best = t.z; ta = t.a; te = t.e; have = true; }
        }
    }
    if (!have && boss_ && boss_->damageable()) {
        ta = boss_->wa(); te = boss_->we(); have = true; best = boss_->z;
    }
    if (!have) {
        // nearest enemy (lowest z), bias toward shooters slightly via z weighting
        for (auto& e : enemies_) {
            if (!e.alive) continue;
            float score = e.z;
            if (score < best) { best = score; ta = e.a; te = e.e; have = true; }
        }
    }
    if (!have && !low) {
        for (auto& t : tanks_) {
            if (!t.alive) continue;
            if (t.z < best) { best = t.z; ta = t.a; te = t.e; have = true; }
        }
    }

    if (have) {
        float da = ta - view_a_, de = te - view_e_;
        // With the slower/inertial pan, anticipate: account for current pan
        // velocity so the bot doesn't overshoot a small fast target (the boss).
        float lead_a = da - pan_va_ * 0.18f, lead_e = de - pan_ve_ * 0.18f;
        // smaller deadzone so it tracks the agile boss tightly
        pan_a = std::fabs(lead_a) > 0.02f ? (lead_a > 0 ? 1.0f : -1.0f) : 0.0f;
        pan_e = std::fabs(lead_e) > 0.02f ? (lead_e > 0 ? 1.0f : -1.0f) : 0.0f;
        // Fire when the target projects near centre AND we have laser energy.
        Proj p = project(ta, te, std::max(best, 0.1f), view_a_, view_e_, W, H);
        float dx = p.sx - W * 0.5f, dy = p.sy - H * 0.5f;
        bool on_target = dx * dx + dy * dy <= (CROSS_R * 1.05f) * (CROSS_R * 1.05f);
        // Pace fire to laser energy: don't fire when low so a paced refill kicks
        // in; resume freely once it's recovered. Keep a small reserve for the boss.
        float need = LASER_MIN_FIRE + LASER_COST;
        if (on_target && !overheated_ && laser_ >= need) want_fire = true;
    } else {
        pan_a = pan_e = 0;
    }

    // Dodge: if an incoming shot is aimed at where we are AND about to land, pan
    // away (override aim).
    for (auto& s : eshots_) {
        if (!s.alive) continue;
        if (s.prog() < 0.4f) continue;
        float da = s.ta - view_a_, de = s.te - view_e_;
        if (std::fabs(da) < 0.22f && std::fabs(de) < 0.22f) {
            pan_a = 1.0f; pan_e = (de > 0 ? -1.0f : 1.0f);  // swing aside
            break;
        }
    }
}

// ── Drawing ──────────────────────────────────────────────────────────────────
void VoidGunnerGame::draw_starfield(Surface& s) {
    int W = ctx_.W, H = ctx_.H;
    for (auto& st : stars_) {
        Proj p = project(st.a, st.e, st.z, view_a_, view_e_, W, H);
        if (p.sx < 0 || p.sx >= W || p.sy < 0 || p.sy >= H) continue;
        // brighter & bigger as they near; streak from centre as they rush out
        float b = clampf((1.0f - st.z) * 1.3f, 0.12f, 1.0f);
        int v = int(40 + 180 * b);
        Color c(v, v, std::min(255, v + 30));
        int sz = st.z < 0.25f ? 2 : 1;
        // short motion streak toward where it came from (centre)
        float dx = p.sx - W * 0.5f, dy = p.sy - H * 0.5f;
        float len = clampf((1.0f - st.z) * 14.0f, 0, 14);
        float d = std::max(1.0f, std::hypot(dx, dy));
        int tx = int(p.sx - dx / d * len), ty = int(p.sy - dy / d * len);
        if (st.z < 0.4f) draw::line(s, Color(v / 2, v / 2, v / 2, 160), tx, ty, int(p.sx), int(p.sy), 1);
        draw::rect(s, c, {int(p.sx), int(p.sy), sz, sz});
    }
}

void VoidGunnerGame::draw_crosshair(Surface& s) {
    int cx = ctx_.W / 2, cy = ctx_.H / 2;
    bool locked = lock_glow_ > 0;
    // Overheated (out of laser energy) → crosshair reds & dims to read "can't fire".
    bool overheat = overheat_t_ > 0 || overheated_;
    Color base = overheat ? Color(255, 80, 70) : locked ? YELLOW : CYAN;
    if (overheat && !overheated_) base = lerp_color(CYAN, Color(255, 80, 70), 0.7f);
    if (overheat) { base.r = base.r * 7 / 10; base.g = base.g * 7 / 10; base.b = base.b * 7 / 10; }
    locked = locked && !overheat;
    float glow = 0.4f + 0.4f * std::sin(game_t_ * 6) + (locked ? 0.3f * lock_glow_ : 0);
    // incoming-fire warning: a shot is on course to land here — pan away
    if (threat_ && state_ == St::Play) {
        float pp = 0.5f + 0.5f * std::sin(game_t_ * 26);
        int r = int(CROSS_R) + 16;
        Color wc(255, 70, 70, int(90 + 140 * pp));
        for (int q = 0; q < 4; ++q) {   // four corner brackets
            int sx = (q & 1) ? 1 : -1, sy = (q & 2) ? 1 : -1;
            draw::line(s, wc, cx + sx * r, cy + sy * r, cx + sx * (r - 14), cy + sy * r, 3);
            draw::line(s, wc, cx + sx * r, cy + sy * r, cx + sx * r, cy + sy * (r - 14), 3);
        }
    }
    // outer ring
    draw::circle(s, Color(base.r, base.g, base.b, int(160)), cx, cy, int(CROSS_R), 2);
    if (locked) {
        draw::circle(s, Color(255, 220, 60, int(120 * lock_glow_)), cx, cy, int(CROSS_R) + 5, 2);
        draw::circle(s, Color(255, 220, 60, int(60 * lock_glow_)), cx, cy, int(CROSS_R) + 9, 1);
    }
    // ticks
    int g = 10;
    draw::line(s, base, cx - int(CROSS_R) - 2, cy, cx - g, cy, 2);
    draw::line(s, base, cx + g, cy, cx + int(CROSS_R) + 2, cy, 2);
    draw::line(s, base, cx, cy - int(CROSS_R) - 2, cx, cy - g, 2);
    draw::line(s, base, cx, cy + g, cx, cy + int(CROSS_R) + 2, 2);
    // centre dot
    draw::circle(s, base, cx, cy, 3);
    draw::circle(s, Color(base.r, base.g, base.b, int(70 * (glow + 0.3f))), cx, cy, 6, 1);
}

void VoidGunnerGame::draw_eshot(Surface& s, const EShot& sh) {
    Proj p = shot_proj(sh);
    if (!p.on_screen) return;
    int r = std::max(3, int(7.0f / std::max(sh.z, 0.07f)));
    r = std::min(r, 30);
    // Blocky bolt (retro): a glow plate, a solid core, a hot pixel centre.
    int gx = int(p.sx), gy = int(p.sy);
    draw::rect(s, Color(sh.color.r, sh.color.g, sh.color.b, 80), {gx - r - 3, gy - r - 3, 2 * r + 6, 2 * r + 6});
    draw::rect(s, sh.color, {gx - r, gy - r, 2 * r, 2 * r});
    int ir = std::max(1, r / 2);
    draw::rect(s, WHITE, {gx - ir, gy - ir, 2 * ir, 2 * ir});
}

void VoidGunnerGame::draw_enemy(Surface& s, const Enemy& e) {
    Proj p = proj(e.a, e.e, e.z);
    if (!p.on_screen) return;
    int sz = enemy_px(e.z);
    Color c = e.hit_flash > 0 ? lerp_color(e.color(), WHITE, 0.75f) : e.color();
    int cx = int(p.sx), cy = int(p.sy);
    // Very far/tiny: a cheap lit blob (don't draw a grid of micro-rects).
    if (sz < 16) {
        draw::circle(s, Color(c.r, c.g, c.b, 110), cx, cy, sz / 2 + 2);
        draw::circle(s, c, cx, cy, std::max(2, sz / 2));
        draw::circle(s, lerp_color(c, WHITE, 0.5f), cx, cy, std::max(1, sz / 5));
        return;
    }
    int bx = cx - sz / 2, by = cy - sz / 2;
    draw_pixel_sprite(s, enemy_sprite(e.type, e.frame), bx, by, sz, sz, c);
    // glowing eyes — sit in the head band, scale with size, pulse a little
    if (sz > 22) {
        int er = std::max(2, sz / 14);
        int eo = sz / 5;
        int ey = cy - sz / 6;
        Color eye = e.type == EType::Tank ? RED
                  : e.type == EType::Shooter ? YELLOW
                  : e.type == EType::Weaver ? Color(230, 180, 255)
                  : Color(255, 240, 180);
        draw::circle(s, eye, cx - eo, ey, er);
        draw::circle(s, eye, cx + eo, ey, er);
        draw::circle(s, WHITE, cx - eo, ey, std::max(1, er / 2));
        draw::circle(s, WHITE, cx + eo, ey, std::max(1, er / 2));
    }
    // hp pips for the armoured beetle
    if (e.type == EType::Tank && e.max_hp > 1 && sz > 26) {
        for (int i = 0; i < e.max_hp; ++i)
            draw::rect(s, i < e.hp ? RED : Color(50, 50, 50), {cx - 12 + i * 9, by - 8, 6, 3});
    }
}

// Energy canister: pale mint with a green halo and an "E" — reads as a pickup,
// never as the (gold) armoured beetle.
void VoidGunnerGame::draw_tank(Surface& s, const Tank& t) {
    Proj p = proj(t.a, t.e, t.z);
    if (!p.on_screen) return;
    int sz = tank_px(t.z);
    float pulse = 0.6f + 0.4f * std::sin(t.phase * 5 + game_t_ * 4);
    int cx = int(p.sx), cy = int(p.sy);
    int frame = (int(game_t_ * 5 + t.phase) & 1);
    // pulsing containment glow behind the canister
    draw::circle(s, Color(CAN_GLOW.r, CAN_GLOW.g, CAN_GLOW.b, int(110 * pulse)), cx, cy, sz / 2 + 6);
    if (sz < 16) {
        draw::circle(s, CAN, cx, cy, std::max(2, sz / 2));
        draw::circle(s, WHITE, cx, cy, std::max(1, sz / 4));
        return;
    }
    int bx = cx - sz / 2, by = cy - sz / 2;
    // brighten as it pulses so the core glows in the shader
    Color body = lerp_color(CAN, WHITE, 0.3f * pulse);
    draw_pixel_sprite(s, SPR_CANISTER[frame], bx, by, sz, sz, body);
    // hot energy core in the middle
    draw::circle(s, Color(235, 255, 245, int(200 * pulse)), cx, cy, std::max(2, sz / 6));
    if (sz > 22) draw_text_centered(s, *ctx_.fonts.small, "E", cx, cy - 8, Color(10, 70, 40));
}

void VoidGunnerGame::draw_boss(Surface& s) {
    Boss& b = *boss_;
    int W = ctx_.W, H = ctx_.H;
    float sc = b.dying ? std::max(0.0f, 1.0f - b.die_t / 1.6f) : 1.0f;
    Proj p = project(b.pa(), b.pe(), b.z, view_a_, view_e_, W, H);
    // Compact agile boss: modest on-screen size (was 9.0x → now 3.4x).
    int bw = int(BASE / std::max(b.z, 0.07f) * 3.4f * sc);
    int bh = int(bw * 0.82f);
    bw = std::min(bw, 280); bh = std::min(bh, 240);
    if (bw <= 6 || bh <= 6) return;
    int bx = int(p.sx) - bw / 2, by = int(p.sy) - bh / 2;
    Color body = b.hit_flash > 0 ? lerp_color(b.color, WHITE, 0.6f) : b.color;
    draw::rect(s, Color(body.r, body.g, body.b, 50), {bx - 8, by - 8, bw + 16, bh + 16});
    // compact raider sprite — distinct, fast-animating menace
    int frame = int(b.sway_t * 12) % 2;
    draw_pixel_sprite(s, SPR_RAIDER[frame], bx, by, bw, bh, body);
    if (b.invuln > 0) {
        float pp = 0.5f + 0.5f * std::sin(b.sway_t * 18);
        draw::rect(s, Color(120, 200, 255, int(120 * pp)), {bx - 6, by - 6, bw + 12, bh + 12}, 3);
    }
    if (b.telegraph > 0) {
        float pp = 0.4f + 0.6f * std::sin(b.sway_t * 30);
        draw::rect(s, Color(255, 60, 60, int(150 * pp)), {bx - 5, by - 5, bw + 10, bh + 10}, 3);
    }
    // ── weak-spot: a small blocky "eye" socket inside the hull. Subtle —
    // a pulsing pixel pupil the bloom makes glow, not a big reticle. ──
    if (!b.dying) {
        Proj wp = project(b.wa(), b.we(), b.z, view_a_, view_e_, W, H);
        float pulse = 0.45f + 0.55f * std::sin(game_t_ * 6);
        int wr = std::max(3, bw / 15);
        int wx = int(wp.sx), wy = int(wp.sy);
        Color eye = lerp_color(RED, YELLOW, pulse);
        draw::rect(s, Color(10, 0, 0), {wx - wr - 1, wy - wr - 1, 2 * wr + 2, 2 * wr + 2});  // dark socket
        draw::rect(s, Color(eye.r, eye.g, eye.b, int(120 * pulse)), {wx - wr - 2, wy - wr - 2, 2 * wr + 4, 2 * wr + 4});  // glow
        draw::rect(s, eye, {wx - wr, wy - wr, 2 * wr, 2 * wr});                                // pupil
        draw::rect(s, WHITE, {wx - wr / 2, wy - wr / 2, std::max(1, wr), std::max(1, wr)});    // hot core
    }
}

void VoidGunnerGame::draw(Surface& s) {
    // deep near-black space, faint blue tint
    s.fill(Color(3, 5, 14));
    draw_starfield(s);
    particles_.draw(s);

    int W = ctx_.W, H = ctx_.H;

    // ── the whole scene in ONE far-to-near list, so an incoming bolt is drawn
    //    over the enemy/boss that fired it instead of hiding behind it ──
    {
        struct Item { float z; int kind; int idx; };
        std::vector<Item> order;
        order.reserve(eshots_.size() + enemies_.size() + tanks_.size() + 1);
        for (int i = 0; i < (int)eshots_.size(); ++i)
            if (eshots_[i].alive) order.push_back({eshots_[i].z, 0, i});
        for (int i = 0; i < (int)enemies_.size(); ++i)
            if (enemies_[i].alive) order.push_back({enemies_[i].z, 1, i});
        for (int i = 0; i < (int)tanks_.size(); ++i)
            if (tanks_[i].alive) order.push_back({tanks_[i].z, 2, i});
        if (boss_) order.push_back({boss_->z + 0.001f, 3, 0});   // ties: its own shots go on top
        std::stable_sort(order.begin(), order.end(),
                         [](const Item& x, const Item& y) { return x.z > y.z; });
        for (auto& it : order) {
            switch (it.kind) {
                case 0: draw_eshot(s, eshots_[it.idx]); break;
                case 1: draw_enemy(s, enemies_[it.idx]); break;
                case 2: draw_tank(s, tanks_[it.idx]); break;
                default: draw_boss(s); break;
            }
        }
    }

    // ── twin converging laser streams (Star Fox style) ──
    for (auto& l : lasers_) {
        float fade = clampf(1.0f - l.t / 0.12f, 0, 1);
        int sx = l.side < 0 ? 20 : W - 20;
        int sy = H - 10;
        int cx = W / 2, cy = H / 2;
        Color beam(120, 240, 255, int(220 * fade));
        Color core(220, 255, 255, int(240 * fade));
        draw::line(s, beam, sx, sy, cx, cy, 5);
        draw::line(s, core, sx, sy, cx, cy, 2);
        // muzzle flare at the bottom corner
        draw::circle(s, Color(180, 250, 255, int(200 * fade)), sx, sy, 8);
    }

    // player missiles (screen space): a glowing orange dart with a short tail
    for (auto& m : pmissiles_) {
        if (!m.alive) continue;
        float ang = std::atan2(m.vy, m.vx);
        int tx = int(m.sx - std::cos(ang) * 20), ty = int(m.sy - std::sin(ang) * 20);
        draw::line(s, Color(255, 150, 50, 150), tx, ty, int(m.sx), int(m.sy), 5);
        draw::circle(s, Color(255, 190, 90, 120), int(m.sx), int(m.sy), 8);
        draw::circle(s, Color(255, 235, 170), int(m.sx), int(m.sy), 3);
    }

    if (state_ != St::Dead && state_ != St::GameOver) draw_crosshair(s);
    draw_radar(s);
    draw_hud(s);

    // red flash on energy hit
    if (hit_flash_ > 0)
        draw::rect(s, Color(255, 40, 40, int(110 * std::min(1.0f, hit_flash_))), {0, 0, W, H});

    // intro banner (first wave only — later waves are announced on the fly)
    if (state_ == St::Intro) {
        float a = std::min(1.0f, intro_t_ * 1.5f);
        if (boss_wave_)
            draw_text_centered(s, *ctx_.fonts.big, "WARNING", W / 2, 190,
                               Color(255, int(80 * a), int(80 * a)));
        else
            draw_text_centered(s, *ctx_.fonts.big, "WAVE " + std::to_string(wave_), W / 2, 190,
                               Color(int(90 + 165 * a), 220, 255));
        draw_text_centered(s, *ctx_.fonts.small,
                           boss_wave_ ? "A VOID BOSS APPROACHES" : "ENEMIES INCOMING",
                           W / 2, 246, boss_wave_ ? RED : ui::LABEL);
        if (!ctx_.demo) {
            std::string l1 = pad_
                ? std::string("STICK AIM    A FIRE    Y MISSILE")
                : std::string("ARROWS AIM    SPACE FIRE    B MISSILE");
            draw_text_centered(s, *ctx_.fonts.small, l1, W / 2, 440, ui::HINT);
            draw_text_centered(s, *ctx_.fonts.small, "SHOOT THE PALE CANISTERS TO RECHARGE",
                               W / 2, 462, ui::HINT);
        }
    }

    draw_flash(s);
    if (state_ == St::Dead && lives_ > 0)
        draw_text_centered(s, *ctx_.fonts.med, "SHIELDS DOWN", W / 2, H / 2 - 14, RED);

    if (state_ == St::GameOver) {
        if (initials_.active()) {
            // initials entry owns the screen: just the dim plate behind it, so the
            // game-over text doesn't show through the entry box
            draw::rect(s, Color(0, 0, 0, 165), {0, 0, W, H});
            initials_.draw(s);
        } else {
            ui::game_over(s, ctx_.fonts, W, H, go_t_, score_, new_hi_, pad_,
                          "WAVE " + std::to_string(wave_));
        }
    } else {
        draw_notes(s);
    }
    pause_.draw(s);
}

// HUD rows (top): 8 score/hi/wave · 36 combo text · 62 combo bar · 74 boss name ·
// 94 boss HP bar · 112 boss hint · 146/176 announcements. The reticle (y 314–406)
// is never written over.
void VoidGunnerGame::draw_hud(Surface& s) {
    int W = ctx_.W, H = ctx_.H;
    draw_text(s, *ctx_.fonts.med, "SCORE  " + zpad(score_, 6), 16, 8, GREEN);
    draw_text_centered(s, *ctx_.fonts.med, "HI  " + zpad(high_score_, 6), W / 2, 8, YELLOW);
    draw_text_right(s, *ctx_.fonts.med, "WAVE  " + std::to_string(wave_), W - 16, 8, CYAN);

    // combo
    if (ctx_.combo && ctx_.combo->active() && state_ == St::Play) {
        int m = ctx_.combo->mult();
        Color cc = m >= 6 ? RED : m >= 4 ? ORANGE : YELLOW;
        draw_text_centered(s, *ctx_.fonts.med, "x" + std::to_string(m) + " COMBO", W / 2, 36, cc);
        int bw = 120, bx = W / 2 - bw / 2;
        draw::rect(s, Color(40, 40, 40), {bx, 62, bw, 4});
        draw::rect(s, cc, {bx, 62, int(bw * ctx_.combo->ratio()), 4});
    }

    // ── energy bar (bottom centre) ──
    int ew = 360, ex = W / 2 - ew / 2, ey = H - 30;
    float r = energy_ / MAX_ENERGY;
    Color ec = r > 0.5f ? GREEN : r > 0.25f ? YELLOW : RED;
    if (r < 0.25f) {  // flash when critical
        float fl = 0.5f + 0.5f * std::sin(game_t_ * 12);
        ec = lerp_color(ec, WHITE, 0.3f * fl);
    }
    draw_text(s, *ctx_.fonts.small, "ENERGY", ex, ey - 18, ui::LABEL);
    draw::rect(s, Color(20, 25, 35), {ex, ey, ew, 14}, 0, 5);
    draw::rect(s, ec, {ex, ey, int(ew * std::max(0.0f, r)), 14}, 0, 5);
    draw::rect(s, Color(ec.r, ec.g, ec.b, 120), {ex, ey, ew, 14}, 1, 5);

    // ── laser energy bar (above ENERGY, cyan, clearly distinct) ──
    bool overheat = overheated_;
    int lw = ew, lx = W / 2 - lw / 2, ly = ey - 38;  // same width as the ENERGY bar
    float lr = laser_ / LASER_MAX;
    Color lc = overheat ? RED : laser_ < 0.3f ? ORANGE : CYAN;
    if (overheat) {  // flash "RECHARGE" lockout
        float fl = 0.5f + 0.5f * std::sin(game_t_ * 16);
        lc = lerp_color(RED, WHITE, 0.4f * fl);
    }
    std::string llabel = overheat ? std::string("LASER  RECHARGING")
                                   : "LASER  Lv" + std::to_string(laser_lvl_);
    draw_text(s, *ctx_.fonts.small, llabel, lx, ly - 18, overheat ? lc : ui::LABEL);
    draw::rect(s, Color(14, 24, 32), {lx, ly, lw, 10}, 0, 4);
    draw::rect(s, lc, {lx, ly, int(lw * std::max(0.0f, lr)), 10}, 0, 4);
    draw::rect(s, Color(lc.r, lc.g, lc.b, 130), {lx, ly, lw, 10}, 1, 4);
    // a tick at the level an overheated laser comes back online
    int tickx = lx + int(lw * (LASER_RESUME / LASER_MAX));
    draw::line(s, Color(160, 220, 255, 140), tickx, ly - 2, tickx, ly + 12, 1);

    // ── missiles: to the RIGHT of the scanner (never inside it), with the key ──
    {
        int mx0 = 152, my0 = H - 84;
        draw_text(s, *ctx_.fonts.small, "MISSILES", mx0, my0,
                  missiles_ > 0 ? Color(255, 170, 80) : ui::LABEL);
        for (int i = 0; i < missiles_; ++i) {
            int mx = mx0 + 5 + i * 13, my = my0 + 22;
            draw::polygon(s, Color(255, 190, 90), {{mx, my + 8}, {mx - 4, my}, {mx + 4, my}});
        }
        draw_text(s, *ctx_.fonts.small, "[" + std::string(ui::nuke_key(pad_)) + "] LAUNCH",
                  mx0, my0 + 36, ui::LABEL);
    }

    // ── lives ──
    draw_text_right(s, *ctx_.fonts.small, "LIVES", W - 124, H - 30, ui::LABEL);
    for (int i = 0; i < lives_; ++i)
        draw::polygon(s, CYAN, {{W - 114 + i * 22, H - 14}, {W - 114 + i * 22 + 7, H - 24}, {W - 114 + i * 22 + 14, H - 14}});
    if (state_ != St::GameOver)
        draw_text_right(s, *ctx_.fonts.small, std::string(ui::pause_key(pad_)) + "  PAUSE",
                        W - 16, H - 54, ui::LABEL);

    // ── boss HP (its own rows, below the combo meter) ──
    if (boss_ && !boss_->dying) {
        int bw = 420, bx = W / 2 - bw / 2, by = 94;
        draw_text_centered(s, *ctx_.fonts.small, boss_->name, W / 2, by - 20, boss_->color);
        draw::rect(s, Color(20, 20, 30), {bx, by, bw, 12}, 0, 4);
        int fillw = int(bw * boss_->hp_ratio());
        Color hc = boss_->hp_ratio() > 0.5f ? GREEN : boss_->hp_ratio() > 0.25f ? YELLOW : RED;
        draw::rect(s, hc, {bx, by, fillw, 12}, 0, 4);
        draw::rect(s, boss_->color, {bx, by, bw, 12}, 1, 4);
        draw_text_centered(s, *ctx_.fonts.small,
                           boss_->invuln > 0 ? "SHIELDED — WAIT FOR IT" : "AIM AT THE GLOWING WEAK POINT",
                           W / 2, by + 16, ui::LABEL);
    }

    // invincibility hint
    if (invinc_ > 0 && state_ == St::Play && int(invinc_ * 6) % 2 == 0)
        draw_text_centered(s, *ctx_.fonts.small, "SHIELDS REGENERATING", W / 2, H - 112, CYAN);
}

// ── Scanner (situational awareness) ──────────────────────────────────────────
// A SQUARE forward scope in the bottom-left corner: it shows what's ahead of you,
// not all around. Each contact is plotted by its direction RELATIVE to your view
// — azimuth (a−view_a) → X (right +), elevation (e−view_e) → Y (down +), matching
// the main view's projection — clamped to the frame so off-axis contacts sit on
// the edge that points toward them. Closer contacts draw as bigger blips.
// Enemies are round blips in their own colour; energy canisters are a pale "+".
void VoidGunnerGame::draw_radar(Surface& s) {
    int R = 60;                         // half-size of the square scope
    int cx = 20 + R, cy = ctx_.H - 30 - R;
    int x0 = cx - R, y0 = cy - R, sz2 = R * 2;
    // square scope backing + frame (glow through the CRT)
    draw::rect(s, Color(6, 16, 22, 200), {x0, y0, sz2, sz2}, 0, 4);
    draw::rect(s, Color(40, 120, 150, 150), {x0, y0, sz2, sz2}, 2, 4);
    // inner guide box + cross-hair axes (centre = dead ahead)
    draw::rect(s, Color(30, 90, 120, 80), {cx - R / 2, cy - R / 2, R, R}, 1, 3);
    draw::line(s, Color(30, 80, 110, 90), x0, cy, x0 + sz2, cy, 1);
    draw::line(s, Color(30, 80, 110, 90), cx, y0, cx, y0 + sz2, 1);
    // forward/centre marker
    draw::circle(s, Color(120, 230, 255, 200), cx, cy, 3);

    // Azimuth/elevation that maps to the frame edge. Beyond this, contacts clamp
    // to the edge pointing toward them.
    const float FOV = 1.4f;
    auto spot = [&](float ra, float re) {
        float nx = clampf(ra / FOV, -1.0f, 1.0f);
        float ny = clampf(re / FOV, -1.0f, 1.0f);
        return std::make_pair(cx + int(nx * (R - 6)), cy + int(ny * (R - 6)));
    };
    auto plot = [&](float ra, float re, float z, Color c, int sz, bool pulse) {
        auto [bx, by] = spot(ra, re);
        float prox = clampf(1.0f - z, 0.0f, 1.0f);   // 0=far, 1=on top of you
        int ps = sz + int(prox * 2);
        if (pulse) {
            float pp = 0.5f + 0.5f * std::sin(game_t_ * 7);
            draw::circle(s, Color(c.r, c.g, c.b, int(120 * pp)), bx, by, ps + 4);
        }
        draw::circle(s, Color(c.r, c.g, c.b, 150), bx, by, ps + 1);
        draw::circle(s, c, bx, by, ps);
    };

    for (auto& e : enemies_) {
        if (!e.alive) continue;
        plot(e.a - view_a_, e.e - view_e_, e.z, e.color(), 2, false);
    }
    for (auto& t : tanks_) {
        if (!t.alive) continue;
        auto [bx, by] = spot(t.a - view_a_, t.e - view_e_);
        draw::line(s, CAN, bx - 4, by, bx + 4, by, 2);
        draw::line(s, CAN, bx, by - 4, bx, by + 4, 2);
    }
    if (boss_ && !boss_->dying)
        plot(boss_->pa() - view_a_, boss_->pe() - view_e_, boss_->z, boss_->color, 4, true);

    draw_text_centered(s, *ctx_.fonts.small, "SCANNER", cx, cy + R + 3, ui::LABEL);
}

void VoidGunnerGame::draw_flash(Surface& s) {
    for (auto& f : flash_) {
        Color c = f.color;
        float a = std::max(0.0f, std::min(1.0f, f.life / std::max(0.1f, f.max_life)));
        c.a = uint8_t(255 * a);
        Font& font = f.big ? *ctx_.fonts.med : *ctx_.fonts.small;
        draw_text_centered(s, font, f.text, int(f.x), int(f.y), c);
    }
}

// The announcement at the head of the queue: title + one sub-line, faded in and
// out, on rows of their own above the action.
void VoidGunnerGame::draw_notes(Surface& s) {
    if (notes_.empty()) return;
    const Note& n = notes_.front();
    float a = std::min({1.0f, note_t_ / 0.15f, (n.dur - note_t_) / 0.30f});
    a = clampf(a, 0.0f, 1.0f);
    Color c = n.color; c.a = uint8_t(255 * a);
    draw_text_centered(s, *ctx_.fonts.med, n.title, ctx_.W / 2, 146, c);
    if (!n.sub.empty()) {
        Color sc = ui::HINT; sc.a = uint8_t(255 * a);
        draw_text_centered(s, *ctx_.fonts.small, n.sub, ctx_.W / 2, 176, sc);
    }
}
}  // namespace

std::unique_ptr<Scene> make_void_gunner(const Context& ctx) {
    return std::make_unique<VoidGunnerGame>(ctx);
}
