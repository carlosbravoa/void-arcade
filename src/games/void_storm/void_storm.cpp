// void_storm.cpp — Void Storm.
// An R-Type / Gradius inspired horizontal side-scroller. The world scrolls
// right-to-left through a storm-lit asteroid corridor: chunky orange/red strata
// terrain on the ceiling and floor, with navigable gaps and turrets bolted to
// the rock. The cyan player ship flies free and fires RIGHT. Its signature is a
// CHARGE BEAM — tap SPACE for rapid pea-shots, HOLD to spin up a fat piercing
// lance that punches through a whole column of enemies.
//
// Enemies pour in from the right in waves: straight DARTERS, sine WEAVERS,
// homing DIVERS, tanky CARRIERS that drop power-ups, and terrain TURRETS that
// take their whole platform with them when killed (kill them — it's always the
// right play: the slab they sit on stops blocking the corridor). Power-ups: SPREAD, SPEED, SHIELD, OPTION pod (a trailing drone that
// echoes your fire) and an extra life. Energy/shield model: you have a shield
// bar that soaks hits + lives behind it.
//
// Every 5th stage a giant VOID boss fills the right side with multi-phase
// bullet patterns (walls-with-gaps, spreads, sweeping laser, a charge/ram) and
// a pulsing weak-point CORE; popping it ends the fight with a big shockwave and
// a rain of power-ups. "Bosses from the void."
//
// TERRAIN/TURRET DISCIPLINE: turrets reference their platform by a stable
// integer `id` (Terrain::find_obstacle / destroy_obstacle) and NEVER store a
// raw pointer across frames (the obstacles vector reallocates).
#include "games/void_storm/void_storm.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/boss_art.h"
#include "engine/combo.h"
#include "engine/enemy_art.h"
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

// ── colours ───────────────────────────────────────────────────────────────────
const Color BLACK(0, 0, 0), WHITE(255, 255, 255);
const Color CYAN(80, 210, 255), YELLOW(255, 220, 40), GOLD(255, 200, 60);
const Color RED(255, 60, 60), ORANGE(255, 140, 40), DIM(40, 50, 70);
const Color PURPLE(200, 100, 255), GREEN(80, 255, 130), STEEL(170, 185, 205);

const Color PLAYER_COLOR(80, 210, 255);
const Color SHOT_COLOR(120, 230, 255);
const Color BEAM_COLOR(180, 240, 255);
const Color ENEMY_SHOT_COLOR(255, 90, 40);

const Color C_DARTER(255, 110, 50);    // straight flyer
const Color C_WEAVER(200, 90, 255);    // sine weaver
const Color C_DIVER(255, 70, 110);     // homing diver
const Color C_CARRIER(255, 200, 60);   // power-up carrier (gold)
const Color C_TURRET(90, 255, 140);    // terrain turret

constexpr float PI = 3.14159265358979323846f;
constexpr float TAU = 6.28318530717958647692f;

// Per-stage names; index clamps into this table for flavour.
constexpr int N_STAGE_NAMES = 10;
const char* STAGE_NAMES[N_STAGE_NAMES] = {
    "OUTER RIFT", "ION FIELD", "DEBRIS BELT", "STORM WALL", "VOID THROAT",
    "ASH CANYON", "STATIC SEA", "IRON VEIN", "THUNDERHEAD", "THE MAW"};
// Stage 1..10 use the table; past that it cycles with a lap number.
std::string stage_name(int stage) {
    int i = std::max(0, stage - 1);
    std::string n = STAGE_NAMES[i % N_STAGE_NAMES];
    int lap = i / N_STAGE_NAMES;
    if (lap > 0) n += " " + std::to_string(lap + 1);
    return n;
}
constexpr int BOSS_EVERY = 5;          // a boss every 5th stage

// World scroll (right -> left). Climbs with stage; player has no throttle here.
constexpr float SCROLL_BASE = 130.0f;

// Deep-stage enemy aggression: <1.0 shortens enemy fire cooldowns (so they fire
// more often). Set per stage in advance_stage(); keeps firing escalating past the
// 5 base spawn tables.
float g_aggro = 1.0f;

// SL, EnemyArt, make_enemy_art and blit_enemy now come from engine/enemy_art.h.

Color darker(Color c, float f) { return Color(int(c.r * f), int(c.g * f), int(c.b * f)); }

bool overlap(Rect a, Rect b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

std::string zpad(int v, int width) {
    std::string s = std::to_string(v < 0 ? 0 : v);
    if ((int)s.size() < width) s = std::string(width - (int)s.size(), '0') + s;
    return s;
}

// Soft circular glow halo. The halo for a given (colour, radius, alpha) is
// rendered once and cached — this is called for every bullet and engine every
// frame, and used to allocate four temporary surfaces per call.
const Surface& glow_sprite(Color color, int r, int alpha) {
    static std::map<uint32_t, std::unique_ptr<Surface>> cache;
    r = std::clamp(r, 1, 120);
    alpha = std::clamp(alpha, 0, 255) & ~7;
    uint32_t key = (uint32_t(r) << 20) | (uint32_t(alpha >> 3) << 15) |
                   (uint32_t(color.r >> 3) << 10) | (uint32_t(color.g >> 3) << 5) |
                   uint32_t(color.b >> 3);
    auto it = cache.find(key);
    if (it != cache.end()) return *it->second;
    if (cache.size() > 600) cache.clear();   // bounded; never hit in practice
    int R = r + 8, d = R * 2;
    auto sp = std::make_unique<Surface>(d, d, true);
    for (int y = 0; y < d; ++y)
        for (int x = 0; x < d; ++x) {
            int dx = x - R, dy = y - R, d2 = dx * dx + dy * dy;
            float keep = 1.0f;   // four stacked translucent discs, composited
            for (int gr = r + 8; gr > r; gr -= 2) {
                if (d2 > gr * gr) continue;
                float f = (gr - r) / 8.0f;
                keep *= 1.0f - (alpha * f * f * 0.5f) / 255.0f;
            }
            int a = int((1.0f - keep) * 255.0f);
            if (a > 0) sp->set_at(x, y, Color(color.r, color.g, color.b, a));
        }
    return *(cache[key] = std::move(sp));
}
void glow_circle(Surface& surface, Color color, int cx, int cy, int r, int alpha) {
    const Surface& g = glow_sprite(color, r, alpha);
    surface.blit(g, cx - g.width() / 2, cy - g.height() / 2);
}

// A horizontal glow bolt travelling rightward: two translucent rounded bands
// and a white core. (It used to stamp a filled disc at every pixel of the
// line for every glow layer — millions of blends per frame for a full beam.)
void glow_bolt_h(Surface& s, Color c, int y, int x0, int x1, int half_h) {
    int w = std::max(2, x1 - x0);
    int oh = half_h + 3;
    draw::rect(s, Color(c.r, c.g, c.b, 46), {x0 - 4, y - oh, w + 8, oh * 2}, 0, oh);
    draw::rect(s, Color(c.r, c.g, c.b, 150), {x0 - 1, y - half_h, w + 2, half_h * 2}, 0, half_h);
    int core = std::max(1, half_h * 3 / 5);
    draw::rect(s, WHITE, {x0 + 1, y - core, w - 2, std::max(2, core * 2)}, 0, core);
}

// ── Parallax background ───────────────────────────────────────────────────────
// Two layers of dim specks drifting leftward (with the scroll) + occasional
// lightning flickers from the "storm". Recycle off the left edge.
struct Parallax {
    struct Speck { float x, y; int size; Color color; bool streak; };
    int W, H;
    std::vector<Speck> far_, near_;
    float bolt_t = 0, bolt_life = 0;
    int bolt_x = 0;
    std::vector<std::pair<int,int>> bolt_pts;
    Parallax(int sw, int sh) : W(sw), H(sh) {
        for (int i = 0; i < 90; ++i) {
            int g = rnd_int(18, 42);
            far_.push_back({rnd_uniform(0, W), rnd_uniform(0, H), 1,
                            Color(g, g / 2, g / 3), false});
        }
        for (int i = 0; i < 42; ++i) {
            bool streak = rnd01() < 0.30f;
            int b = rnd_int(40, 80);
            near_.push_back({rnd_uniform(0, W), rnd_uniform(0, H),
                             streak ? rnd_int(5, 11) : 2,
                             Color(b, int(b * 0.55f), int(b * 0.3f)), streak});
        }
    }
    void recycle(std::vector<Speck>& layer) {
        for (auto& s : layer)
            if (s.x < -14) { s.x += W + 28; s.y = rnd_uniform(0, H); }
    }
    void make_bolt() {
        bolt_x = rnd_int(W / 4, W);
        bolt_pts.clear();
        int x = bolt_x, y = 0;
        while (y < H) {
            bolt_pts.emplace_back(x, y);
            y += rnd_int(20, 50);
            x += rnd_int(-40, 24);
        }
        bolt_life = 0.18f;
    }
    void update(float dt, float scroll_speed) {
        float vf = scroll_speed * 0.18f, vn = scroll_speed * 0.42f;
        for (auto& s : far_) s.x -= vf * dt;
        for (auto& s : near_) s.x -= vn * dt;
        recycle(far_);
        recycle(near_);
        bolt_t -= dt;
        if (bolt_life > 0) bolt_life -= dt;
        if (bolt_t <= 0) { bolt_t = rnd_uniform(2.2f, 6.0f); make_bolt(); }
    }
    void draw(Surface& surface) const {
        for (auto& s : far_)
            draw::rect(surface, s.color, {int(s.x), int(s.y), s.size, s.size});
        for (auto& s : near_) {
            if (s.streak)
                draw::line(surface, s.color, int(s.x), int(s.y),
                           int(s.x) + s.size, int(s.y), 1);
            else
                draw::rect(surface, s.color, {int(s.x), int(s.y), s.size, s.size});
        }
        if (bolt_life > 0 && bolt_pts.size() > 1) {
            int a = int(160 * std::min(1.0f, bolt_life / 0.18f));
            for (size_t i = 1; i < bolt_pts.size(); ++i)
                draw::line(surface, Color(200, 220, 255, a),
                           bolt_pts[i - 1].first, bolt_pts[i - 1].second,
                           bolt_pts[i].first, bolt_pts[i].second, 2);
        }
    }
};

// ── Player ship sprite (faces RIGHT, 14 wide × 9 tall, scale 2) ───────────────
const SL PLAYER_SHAPE = {
    "..#...........",
    "..##..........",
    ".####....##...",
    "##########....",
    "###############",
    "##########....",
    ".####....##...",
    "..##..........",
    "..#...........",
};
constexpr int PLAYER_W = 14 * 2;  // 28
constexpr int PLAYER_H = 9 * 2;   // 18

// ── Enemy pixel-art sprites ────────────────────────────────────────────────────
// All enemies FACE LEFT (the player flies on the left), so the "nose"/cockpit is
// on the left edge of each grid and the engines/exhaust are on the right. Each
// type has two frames for a little life (thruster flicker / wing flap / eye).
// Surfaces are built once and cached — never rebuilt per frame.
//
// DARTER — a lean orange interceptor: pointed nose left, swept wings, twin
// engines that flicker frame-to-frame.
const SL DARTER_F0 = {
    "............##",
    ".......#####..",
    "...##########.",
    "###@@@########",
    "##@@@@########",
    "###@@@########",
    "...##########.",
    ".......#####..",
    "............##",
};
const SL DARTER_F1 = {
    "...........###",
    ".......#####..",
    "...##########.",
    "##@@@@########",
    "###@@@########",
    "##@@@@########",
    "...##########.",
    ".......#####..",
    "...........###",
};
// WEAVER — a purple manta/biomech flyer with broad flapping wings and a single
// glaring eye on its leading edge.
const SL WEAVER_F0 = {
    ".....##....##.",
    "...####..####.",
    ".#########.##.",
    "##@@########..",
    "#@@@@#########",
    "##@@########..",
    ".#########.##.",
    "...####..####.",
    ".....##....##.",
};
const SL WEAVER_F1 = {
    "...........##.",
    ".....##...###.",
    ".#########.##.",
    "##@@########..",
    "#@@@@#########",
    "##@@########..",
    ".#########.##.",
    ".....##...###.",
    "...........##.",
};
// DIVER — a small red dart/raptor: aggressive arrow nose, slim body, hot tail.
const SL DIVER_F0 = {
    "....#....",
    "..###....",
    "@@######.",
    "@@@######",
    "@@######.",
    "..###....",
    "....#....",
};
const SL DIVER_F1 = {
    "....#....",
    "..###...#",
    "@@######.",
    "@@@#####.",
    "@@######.",
    "..###...#",
    "....#....",
};
// CARRIER — a fat gold dropship: blunt nose, big hull, cargo bay, two engine
// nacelles. (HP pips drawn over it in code.)
const SL CARRIER_F0 = {
    ".....########..",
    "...##########..",
    "..@@@#########.",
    "..@@##########.",
    ".@@###########.",
    "..@@##########.",
    "..@@@#########.",
    "...##########..",
    ".....########..",
};
const SL CARRIER_F1 = {
    ".....########..",
    "...##########.#",
    "..@@@#########.",
    "..@@##########.",
    ".@@###########.",
    "..@@##########.",
    "..@@@#########.",
    "...##########.#",
    ".....########..",
};
// TURRET — a mounted gun-pod: armoured dome base. The swivelling barrel is drawn
// procedurally (it tracks the player), so the sprite is just the base.
const SL TURRET_BASE = {
    "..######..",
    ".########.",
    "##@@@@@@##",
    "##@@@@@@##",
    "##########",
    "##########",
};

// Lazily-built, function-local caches (one set, shared by all instances).
const EnemyArt& darter_art() {
    static EnemyArt a = make_enemy_art(DARTER_F0, DARTER_F1, C_DARTER, 2);
    return a;
}
const EnemyArt& weaver_art() {
    static EnemyArt a = make_enemy_art(WEAVER_F0, WEAVER_F1, C_WEAVER, 2);
    return a;
}
const EnemyArt& diver_art() {
    static EnemyArt a = make_enemy_art(DIVER_F0, DIVER_F1, C_DIVER, 2, 7, 0.9f);
    return a;
}
const EnemyArt& carrier_art() {
    static EnemyArt a = make_enemy_art(CARRIER_F0, CARRIER_F1, C_CARRIER, 3, 7);
    return a;
}
const EnemyArt& turret_art() {
    static EnemyArt a = make_enemy_art(TURRET_BASE, TURRET_BASE, C_TURRET, 3);
    return a;
}
// Ceiling-mounted turrets hang dome-down: same art, rows reversed.
const EnemyArt& turret_art_ceiling() {
    static const SL flipped(TURRET_BASE.rbegin(), TURRET_BASE.rend());
    static EnemyArt a = make_enemy_art(flipped, flipped, C_TURRET, 3);
    return a;
}

// ── Shots ─────────────────────────────────────────────────────────────────────
// Player shots travel RIGHT (+x). Charge beam is a fat, fast, piercing lance.
struct PlayerShot {
    float x, y, vx, vy = 0;
    int w, h;
    bool alive = true;
    bool beam = false;
    bool full = false;   // released at FULL charge — also vaporises enemy fire in its path
    int pierce = 0;
    int damage = 1;
    Color color = SHOT_COLOR;
    // A beam passes through everything, so it remembers what it already hit:
    // each target takes its damage exactly once, whatever the frame rate.
    std::vector<int> hit_uids;
    bool boss_hit = false;
    bool has_hit(int uid) const {
        return std::find(hit_uids.begin(), hit_uids.end(), uid) != hit_uids.end();
    }
    PlayerShot(float px, float py, float vx_, int w_, int h_)
        : x(px), y(py), vx(vx_), w(w_), h(h_) {}
    Rect rect() const { return {int(x), int(y - h / 2), w, h}; }
    void update(float dt, int sw, int sh) {
        x += vx * dt;
        y += vy * dt;
        if (x > sw + 30 || y < -30 || y > sh + 30) alive = false;
    }
    void draw(Surface& s) const {
        if (!alive) return;
        glow_bolt_h(s, color, int(y), int(x), int(x + w), std::max(2, h / 2));
    }
};

struct EnemyShot {
    float x, y, vx, vy, t_ = 0;
    bool alive = true;
    Color color = ENEMY_SHOT_COLOR;
    int r = 5;
    // Optional fan-out: drift vertically for `settle_t` seconds, then fly
    // straight (the boss WALL bursts out of its muzzle and forms up).
    float settle_t = 0, settle_vy = 0;
    EnemyShot(float px, float py, float angle, float spd)
        : x(px), y(py), vx(std::cos(angle) * spd), vy(std::sin(angle) * spd) {}
    Rect rect() const { return {int(x - r), int(y - r), r * 2, r * 2}; }
    void update(float dt, int sw, int sh) {
        t_ += dt;
        x += vx * dt;
        y += vy * dt;
        if (settle_t > 0) { y += settle_vy * std::min(dt, settle_t); settle_t -= dt; }
        if (x < -30 || x > sw + 30 || y < -30 || y > sh + 30) alive = false;
    }
    void draw(Surface& s) const {
        if (!alive) return;
        int cx = int(x), cy = int(y);
        float pulse = 0.8f + 0.2f * std::sin(t_ * 18);
        int rr = std::max(2, int(r * pulse));
        Color col(color.r, int(color.g * pulse), int(color.b * pulse));
        glow_circle(s, color, cx, cy, r + 2, 64);   // cached halo (fixed size/colour)
        draw::circle(s, col, cx, cy, rr);
        draw::circle(s, Color(255, 230, 210), cx, cy, std::max(1, rr / 2));
    }
};

// ── Power-ups ────────────────────────────────────────────────────────────────
enum class PU { Spread, Speed, Shield, Option, Life, Mystery };
struct PUInfo { const char* label; Color color; };
PUInfo pu_info(PU k) {
    switch (k) {
        case PU::Spread: return {"SPR", ORANGE};
        case PU::Speed:  return {"SPD", CYAN};
        case PU::Shield: return {"SHD", GREEN};
        case PU::Option: return {"POD", GOLD};
        case PU::Life:   return {"1UP", WHITE};
        default:         return {"?", WHITE};
    }
}
// Power-ups drift left with the scroll; the ship flies into them.
struct PowerUp {
    float x, y, t = 0;
    PU kind;
    bool alive = true;
    Rect rect() const { return {int(x - 20), int(y - 13), 40, 26}; }
    void update(float dt, float scroll_speed) {
        t += dt;
        x -= (scroll_speed * 0.6f + 30) * dt;
        y += std::sin(t * 2.5f) * 18 * dt;
        if (x < -30) alive = false;
    }
    void draw(Surface& s, Font& f) const {
        if (!alive) return;
        PUInfo in = pu_info(kind);
        Color c = in.color;
        if (kind == PU::Mystery) {
            float h = std::fmod(t * 2.0f, 1.0f);
            c = Color(int(128 + 127 * std::sin(h * 6.28f)),
                      int(128 + 127 * std::sin(h * 6.28f + 2.1f)),
                      int(128 + 127 * std::sin(h * 6.28f + 4.2f)));
        }
        float pulse = 0.7f + 0.3f * std::sin(t * 8);
        draw::rect(s, Color(c.r, c.g, c.b, int(70 * pulse)), {int(x) - 24, int(y) - 17, 48, 34}, 0, 8);
        draw::rect(s, Color(8, 14, 20), {int(x) - 20, int(y) - 13, 40, 26}, 0, 6);
        draw::rect(s, c, {int(x) - 20, int(y) - 13, 40, 26}, 2, 6);
        draw_text_centered(s, f, in.label, int(x), int(y) - 9, c);
    }
};

// ── TerrainObstacle ───────────────────────────────────────────────────────────
// A steel gun platform jutting from the ceiling (top) or floor (bottom). It is
// solid until every turret mounted on it is destroyed — then it blows apart
// and the lane is open. Turrets reference it by stable `id`.
struct TerrainObstacle {
    Rect rect;
    bool ceiling;        // true: hangs from top; false: rises from bottom
    float x;             // float x for smooth scroll
    bool registered = false;
    int id = 0;

    TerrainObstacle(Rect r, bool ceil, int id_)
        : rect(r), ceiling(ceil), x(float(r.x)), id(id_) {}

    void scroll(float dx) { x -= dx; rect.x = int(x); }
    bool blocks(Rect pr) const { return overlap(rect, pr); }
    bool off_screen() const { return rect.x + rect.w < -40; }
};

// ── Terrain ───────────────────────────────────────────────────────────────────
// Ceiling and floor are columns of chunky strata that scroll left. Heights ease
// toward random targets. Every so often (stage 2+) the walls close into a CHOKE:
// a short narrow throat at a random height. Because heights ease 10% per
// column, the walls slope in visibly for several seconds before the throat
// arrives — telegraphed, never a surprise.
constexpr int COL_W = 12;                 // strata column width
const Color TERR_COLORS[3] = {Color(120, 60, 25), Color(150, 80, 30), Color(90, 45, 18)};
const Color TERR_GLOW(255, 130, 50);
constexpr int PLAT_BURY = 8;              // how far a platform is sunk into the rock

struct Column {
    int top_h, bot_h;     // ceiling height, floor height (px)
    float x;
    int tex = 0;
};

struct Terrain {
    int W, H;
    std::vector<Column> cols;
    std::vector<TerrainObstacle> obstacles;
    int stage = 0;
    float target_top = 60, target_bot = 60;
    float top_h = 60, bot_h = 60;
    int choke_cd = 120, plat_cd = 24;
    int choke_left = 0;       // columns of the current choke still to generate
    int hold_cols = 0;        // columns over which the walls stay level (platform span)
    int col_seq = 0, obs_seq = 1;
    bool blocked = false;     // boss in the corridor: stop new features

    Terrain(int sw, int sh) : W(sw), H(sh) {
        // The opening screen is plain corridor: no platforms (their turrets
        // would pop in on-screen) and no chokes.
        blocked = true;
        int n = W / COL_W + 4;
        for (int i = 0; i < n; ++i) append_col(float(i * COL_W));
        blocked = false;
    }

    TerrainObstacle* find_obstacle(int id) {
        for (auto& o : obstacles) if (o.id == id) return &o;
        return nullptr;
    }
    // Remove a platform; returns its last rect so the caller can blow it up.
    bool destroy_obstacle(int id, Rect& out) {
        for (size_t i = 0; i < obstacles.size(); ++i)
            if (obstacles[i].id == id) {
                out = obstacles[i].rect;
                obstacles.erase(obstacles.begin() + long(i));
                return true;
            }
        return false;
    }

    void update(float dt, float scroll_speed, int st) {
        stage = st;
        float dx = scroll_speed * dt;
        for (auto& c : cols) c.x -= dx;
        for (auto& o : obstacles) o.scroll(dx);

        cols.erase(std::remove_if(cols.begin(), cols.end(),
                   [](const Column& c) { return c.x + COL_W < -COL_W; }), cols.end());
        obstacles.erase(std::remove_if(obstacles.begin(), obstacles.end(),
                        [](const TerrainObstacle& o) { return o.off_screen(); }),
                        obstacles.end());

        float right = -1e9f;
        for (auto& c : cols) right = std::max(right, c.x);
        while (right < W + COL_W) {
            right += COL_W;
            append_col(right);
        }
    }

    void append_col(float x) {
        // ease current toward targets so chokes are telegraphed; while a
        // platform's columns are being laid the walls stay level so the slab
        // stays attached to the rock along its whole length.
        if (hold_cols > 0) --hold_cols;
        else {
            top_h += (target_top - top_h) * 0.10f;
            bot_h += (target_bot - bot_h) * 0.10f;
        }

        if (!blocked) {
            if (choke_left > 0) {
                if (--choke_left == 0) {          // throat done: open back up
                    target_top = rnd_uniform(40, H * 0.26f);
                    target_bot = rnd_uniform(40, H * 0.26f);
                }
            } else {
                if (rnd01() < 0.05f) target_top = rnd_uniform(40, H * 0.34f);
                if (rnd01() < 0.05f) target_bot = rnd_uniform(40, H * 0.34f);
                if (stage >= 2 && hold_cols == 0 && --choke_cd <= 0) {
                    choke_cd = rnd_int(150, 250);
                    choke_left = rnd_int(24, 34);
                    float gap = std::max(124.0f, 176.0f - stage * 4.0f);
                    float mid = rnd_uniform(H * 0.32f, H * 0.68f);
                    target_top = mid - gap / 2;
                    target_bot = H - (mid + gap / 2);
                    plat_cd = std::max(plat_cd, choke_left + 36);   // no slab in a throat
                }
            }
        }

        Column c{int(top_h), int(bot_h), x, col_seq++};
        cols.push_back(c);

        if (blocked) return;

        // Gun platforms jut from ceiling or floor occasionally.
        plat_cd -= 1;
        if (plat_cd <= 0 && choke_left == 0) {
            plat_cd = rnd_int(55, 110);
            bool ceil = rnd01() < 0.5f;
            int pw = rnd_int(70, 130);
            int jut = rnd_int(24, 44);
            if (H - top_h - bot_h - jut >= 190) {     // always leave a wide lane
                Rect r = ceil ? Rect{int(x), int(top_h) - PLAT_BURY, pw, jut + PLAT_BURY}
                              : Rect{int(x), H - int(bot_h) - jut, pw, jut + PLAT_BURY};
                obstacles.emplace_back(r, ceil, obs_seq++);
                hold_cols = pw / COL_W + 2;
            }
        }
    }

    // Solid wall rects near a probe rect (for player + shot collisions).
    bool wall_blocks(Rect pr) const {
        for (auto& c : cols) {
            if (c.x + COL_W < pr.x || c.x > pr.x + pr.w) continue;
            Rect topR{int(c.x), 0, COL_W + 1, c.top_h};
            Rect botR{int(c.x), H - c.bot_h, COL_W + 1, c.bot_h};
            if (overlap(topR, pr) || overlap(botR, pr)) return true;
        }
        for (auto& o : obstacles)
            if (o.blocks(pr)) return true;
        return false;
    }

    // Open span (rock edge to rock edge) of the column under screen x.
    void corridor_raw(float x, int& y_top, int& y_bot) const {
        int th = int(top_h), bh = int(bot_h);
        for (auto& c : cols) {
            if (x >= c.x && x < c.x + COL_W) { th = c.top_h; bh = c.bot_h; break; }
        }
        y_top = th;
        y_bot = H - bh;
    }
    // Tightest open span over [x0, x1] (several columns).
    void corridor_span(float x0, float x1, int& y_top, int& y_bot) const {
        y_top = 0; y_bot = H;
        bool any = false;
        for (auto& c : cols) {
            if (c.x + COL_W < x0 || c.x > x1) continue;
            y_top = std::max(y_top, c.top_h);
            y_bot = std::min(y_bot, H - c.bot_h);
            any = true;
        }
        if (!any) corridor_raw(x0, y_top, y_bot);
    }
    // Corridor clearance at a given screen x, with a safety margin.
    void corridor_at(float x, int& y_top, int& y_bot) const {
        corridor_raw(x, y_top, y_bot);
        y_top += 18;
        y_bot -= 18;
    }

    void draw(Surface& surface) const {
        for (auto& c : cols) {
            int cx = int(c.x);
            Color col = TERR_COLORS[(c.tex / 3) % 3];
            if (c.top_h > 0) {
                draw::rect(surface, col, {cx, 0, COL_W + 1, c.top_h});
                draw::rect(surface, TERR_GLOW, {cx, c.top_h - 3, COL_W + 1, 3});
            }
            if (c.bot_h > 0) {
                int by = surface.height() - c.bot_h;
                draw::rect(surface, col, {cx, by, COL_W + 1, c.bot_h});
                draw::rect(surface, TERR_GLOW, {cx, by, COL_W + 1, 3});
            }
        }
        const Color plate(96, 108, 126), plate_dk(62, 72, 88);
        for (auto& o : obstacles) {
            Rect r = o.rect;
            draw::rect(surface, plate_dk, r);
            draw::rect(surface, plate, {r.x + 2, r.y + 2, r.w - 4, r.h - 4});
            // bright lip on the face that points into the corridor
            int lip = o.ceiling ? r.y + r.h - 3 : r.y;
            draw::rect(surface, TERR_GLOW, {r.x, lip, r.w, 3});
            for (int px = r.x + 8; px < r.x + r.w - 6; px += 18)   // rivets
                draw::rect(surface, plate_dk, {px, r.y + r.h / 2 - 1, 3, 3});
        }
    }
};

// ── Player ────────────────────────────────────────────────────────────────────
struct OptionPod {
    float x = 0, y = 0;          // current pos
    OptionPod() = default;
    OptionPod(float px, float py) : x(px), y(py) {}
};

// A little tech-drone sprite for the option pods (cached body + glow halo).
const std::vector<std::string> OPTION_POD_ART = {
    "..####..",
    ".######.",
    "##.##.##",
    "########",
    "##.##.##",
    ".######.",
    "..####..",
};
struct PodSprite { std::shared_ptr<Surface> body, glow; };
inline const PodSprite& pod_sprite() {
    static PodSprite p;
    if (!p.body) {
        p.body = std::make_shared<Surface>(make_sprite_surface(OPTION_POD_ART, GOLD, 2));
        p.glow = glow_surface(*p.body, GOLD, 6, 0.7f);
    }
    return p;
}

struct Player {
    static constexpr float SPEED = 280.0f;
    static constexpr int GLOW = 4;               // halo padding baked into `surf`
    static constexpr float TRAIL_STEP = 4.0f;    // px of travel per trail sample
    static constexpr int POD_GAP = 8;            // trail samples between pods (32 px)
    float x, y;
    bool alive = true;
    float invincible = 0, fire_cd = 0;
    int w = PLAYER_W, h = PLAYER_H;
    float exhaust_t = 0, exhaust_acc = 0;
    std::shared_ptr<Surface> surf;               // ship + soft halo, built once

    // progression (reset on death except options carry one)
    int spread_ = 0;
    int speed_ = 0;
    float shield_timer_ = 0;     // active reflective shield bubble
    int shield_hp_ = 0;          // shield bar (soaks hits before a life is lost)
    std::vector<OptionPod> options_;   // trailing drones echoing fire
    // Path history for the pods, sampled every TRAIL_STEP px of travel (not
    // every frame), so pod spacing is the same at any frame rate and the pods
    // hold formation behind the ship when it stops instead of piling onto it.
    std::vector<std::pair<float,float>> trail_;

    // charge beam
    float charge_ = 0;           // 0..1 charge level
    bool charging_ = false;

    float bdx = 0, bdy = 0; bool bfire = false; bool bcharge = false;

    Player(int px, int py) : x(float(px)), y(float(py)) {
        Surface ship = make_sprite_surface(PLAYER_SHAPE, PLAYER_COLOR, 2);
        surf = glow_surface(ship, PLAYER_COLOR, GLOW, 0.55f);
        seed_trail();
    }
    Rect rect() const { return {int(x), int(y), w, h}; }
    // Slightly forgiving box for things that kill on touch.
    Rect hit_rect() const { return {int(x) + 3, int(y) + 3, w - 6, h - 6}; }
    std::pair<int, int> center() const { return {int(x + w / 2.0f), int(y + h / 2.0f)}; }
    std::pair<int, int> nose() const { return {int(x + w), int(y + h / 2.0f)}; }

    float move_speed() const { return SPEED * (1.0f + 0.16f * speed_); }
    float fire_rate() const { return 0.16f; }
    bool safe() const { return invincible > 0 || shield_timer_ > 0; }

    // Lay the history out in a line behind the ship (new game / respawn / first pod).
    void seed_trail() {
        trail_.clear();
        for (int i = 0; i < 3 * POD_GAP + 2; ++i) trail_.emplace_back(x - i * TRAIL_STEP, y);
        place_pods();
    }
    void place_pods() {
        for (size_t i = 0; i < options_.size(); ++i) {
            size_t idx = std::min(trail_.size() - 1, (i + 1) * size_t(POD_GAP));
            options_[i].x = trail_[idx].first; options_[i].y = trail_[idx].second;
        }
    }

    void reset_on_death() {
        spread_ = 0; speed_ = 0; shield_timer_ = 0; shield_hp_ = 0;
        charge_ = 0; charging_ = false;
        if (!options_.empty()) options_.resize(1);   // keep one option as mercy
    }

    void update(float dt, int sw, int sh, ParticleSystem& fx) {
        if (!alive) return;
        if (invincible > 0) invincible -= dt;
        if (fire_cd > 0) fire_cd -= dt;
        if (shield_timer_ > 0) shield_timer_ -= dt;
        exhaust_t += dt;

        float spd = move_speed();
        x += bdx * spd * dt;
        y += bdy * spd * dt;
        x = std::max(0.0f, std::min(float(sw - w), x));
        y = std::max(0.0f, std::min(float(sh - h), y));

        // engine exhaust: emitted here on a fixed time step (it used to be
        // emitted from draw(), i.e. per rendered frame and even while paused)
        exhaust_acc = std::min(exhaust_acc + dt, 0.05f);
        while (exhaust_acc >= 1.0f / 60.0f) {
            exhaust_acc -= 1.0f / 60.0f;
            fx.ball_trail(x, y + h / 2.0f, Color(80, 180, 255));
        }

        // record the path for the pods
        float ddx = x - trail_.front().first, ddy = y - trail_.front().second;
        float d = std::hypot(ddx, ddy);
        int guard = 0;
        while (d >= TRAIL_STEP && guard++ < 64) {
            float k = TRAIL_STEP / d;
            trail_.insert(trail_.begin(), {trail_.front().first + ddx * k,
                                           trail_.front().second + ddy * k});
            ddx = x - trail_.front().first; ddy = y - trail_.front().second;
            d = std::hypot(ddx, ddy);
        }
        if (trail_.size() > size_t(3 * POD_GAP + 2)) trail_.resize(size_t(3 * POD_GAP + 2));
        place_pods();
    }

    // Tap fire: rapid pea shots from nose + each option.
    int tap_fire(std::vector<PlayerShot>& out) {
        if (fire_cd > 0 || !alive) return 0;
        fire_cd = fire_rate();
        auto [gx, gy] = nose();
        int n = 0;
        auto add = [&](float ox, float oy, float vy) {
            PlayerShot s{ox, oy, 720.0f, 16, 5};
            s.vy = vy; s.color = SHOT_COLOR; s.damage = 1;
            out.push_back(s); ++n;
        };
        add(float(gx), float(gy), 0);
        for (int i = 1; i <= spread_; ++i) { add(float(gx), float(gy), -110.0f * i); add(float(gx), float(gy), 110.0f * i); }
        for (auto& o : options_) add(o.x + w, o.y + h / 2.0f, 0);
        return n;
    }

    // Release the charge beam: a fat, fast, piercing lance. It hits each
    // target ONCE (see PlayerShot::hit_uids), so `dmg` is the whole payload.
    int release_beam(std::vector<PlayerShot>& out) {
        float c = charge_;
        charge_ = 0; charging_ = false;
        if (c < 0.12f) return tap_fire(out);   // not enough charge -> normal shot
        bool full = c >= 1.0f;                 // only a max charge clears enemy fire
        auto [gx, gy] = nose();
        // Everything scales with how long it was held, so a slight charge is a
        // small/short/dim lance and a full charge is a fat/long/white-hot one.
        int beam_h = int(6 + 48 * c);          // thickness  (~12 slight … 54 full)
        int beam_w = int(34 + 86 * c);         // length     (~44 slight … 120 full)
        int dmg = int(3 + 13 * c);             // 4 slight … 16 full (a pea shot is 1)
        Color bc = lerp_color(BEAM_COLOR, WHITE, 0.45f * c);  // hotter when fuller
        PlayerShot s{float(gx), float(gy), 1100.0f, beam_w, beam_h};
        s.beam = true; s.full = full; s.color = bc; s.damage = dmg; s.pierce = 999;
        out.push_back(s);
        for (auto& o : options_) {
            // Option pods fire at ~1/3 the main beam's power, so stacking pods on
            // a charged beam stays strong but not abusive.
            PlayerShot os{o.x + w, o.y + h / 2.0f, 1100.0f, std::max(30, beam_w - 24),
                          std::max(7, beam_h - 8)};
            os.beam = true; os.full = full; os.color = bc; os.damage = std::max(1, dmg / 3);
            os.pierce = 999; out.push_back(os);
        }
        fire_cd = 0.18f;
        return 1;
    }

    // Returns true if the hit consumed a shield charge (no life lost).
    bool take_hit() {
        if (shield_hp_ > 0) { shield_hp_--; invincible = 1.0f; return true; }
        return false;
    }
    void kill() { alive = false; invincible = 0.0f; }
    void respawn(int nx, int ny) {
        x = float(nx); y = float(ny);
        alive = true; invincible = 2.5f; fire_cd = 0; charge_ = 0; charging_ = false;
        seed_trail();
    }

    void draw(Surface& surface) const {
        if (!alive) return;
        // option pods: little gold drones with a pulsing core (drawn behind ship)
        const PodSprite& ps = pod_sprite();
        int phw = ps.body->width() / 2, phh = ps.body->height() / 2;
        for (auto& o : options_) {
            int ocx = int(o.x + w / 2), ocy = int(o.y + h / 2);
            float p = 0.55f + 0.45f * std::sin(exhaust_t * 8 + o.x * 0.05f);
            surface.blit(*ps.glow, ocx - phw - 6, ocy - phh - 6);
            surface.blit(*ps.body, ocx - phw, ocy - phh);
            draw::circle(surface, Color(255, 255, 255, int(230 * p)), ocx, ocy, 2);
        }
        if (invincible > 0 && int(invincible * 8) % 2 == 0) return;
        surface.blit(*surf, int(x) - GLOW, int(y) - GLOW);
        // charge glow on the nose
        if (charging_ && charge_ > 0.05f) {
            auto [gx, gy] = nose();
            float p = 0.5f + 0.5f * std::sin(exhaust_t * 30);
            float cq = std::round(charge_ * 8.0f) / 8.0f;   // quantised: cached halos
            int rad = int(4 + 14 * cq);
            Color cc = lerp_color(CYAN, WHITE, cq);
            glow_circle(surface, cc, gx, gy, rad, int(120 * (0.5f + 0.5f * cq)));
            draw::circle(surface, cc, gx, gy, std::max(2, int(rad * 0.5f * (0.7f + 0.3f * p))));
            if (charge_ >= 1.0f) {  // fully charged: pulsing spark ring
                draw::circle(surface, WHITE, gx, gy, rad + 2 + int(3 * p), 2);
                draw::circle(surface, Color(255, 255, 255, 120), gx, gy, rad + 8 + int(4 * p), 1);
            }
        }
        // shield bubble
        if (shield_timer_ > 0 || shield_hp_ > 0) {
            auto [cx, cy] = center();
            float pulse = 0.6f + 0.4f * std::sin(exhaust_t * 20);
            // the timed bubble blinks during its last 1.5 s so its end is no surprise
            if (shield_timer_ > 0 && shield_timer_ < 1.5f && int(shield_timer_ * 10) % 2 == 0)
                pulse *= 0.25f;
            int rr = w;
            Color sc = shield_timer_ > 0 ? Color(80, 255, 160) : Color(60, 180, 140);
            draw::circle(surface, Color(sc.r, sc.g, sc.b, int(90 * pulse)), cx, cy, rr, 2);
            draw::circle(surface, Color(sc.r, sc.g, sc.b, int(30 * pulse)), cx, cy, rr - 2, 1);
        }
    }
};

// ── Enemies ───────────────────────────────────────────────────────────────────
struct Enemy {
    float x, y;
    bool alive = true;
    int hp = 1, max_hp = 1;
    Color color;
    int pts = 0;
    float hit_flash = 0;
    bool carrier = false;       // drops a guaranteed power-up
    bool hardened = false;      // deep-stage armour: extra HP + a steel ring
    int uid = next_uid();       // identity for "this beam already hit me"
    static int next_uid() { static int n = 0; return ++n; }
    virtual ~Enemy() = default;
    // Shove the enemy vertically (keeps flyers inside the corridor).
    virtual void nudge_y(float dy) { y += dy; }
    virtual Rect rect() const = 0;
    virtual std::pair<int, int> center() const = 0;
    virtual void update(float dt, float scroll_speed, Player& player,
                        std::vector<EnemyShot>& shots) = 0;
    virtual void draw(Surface& s) const = 0;
    virtual int station_id() const { return -1; }
    virtual bool is_heavy() const { return max_hp > 2; }
    bool hit(int dmg) {
        hp -= dmg; hit_flash = 1.0f;
        if (hp <= 0) { alive = false; return true; }
        return false;
    }
    void tick_flash(float dt) { if (hit_flash > 0) hit_flash -= dt * 5; }
};

// Straight flyer, sweeps right->left, fires forward.
struct Darter : Enemy {
    static constexpr int W = 26, H = 18;
    static constexpr float SPEED = 150.0f;
    float fire_cd, t_ = 0;
    Darter(float px, float py) {
        x = px; y = py; hp = 1; max_hp = 1; pts = 100; color = C_DARTER;
        fire_cd = rnd_uniform(0.6f, 2.0f);
    }
    Rect rect() const override { return {int(x), int(y), W, H}; }
    std::pair<int, int> center() const override { return {int(x + W / 2.0f), int(y + H / 2.0f)}; }
    void update(float dt, float scroll_speed, Player& player,
                std::vector<EnemyShot>& shots) override;
    void draw(Surface& s) const override {
        auto [cx, cy] = center();
        int frame = int(t_ * 14) & 1;   // fast engine flicker
        blit_enemy(s, darter_art(), frame, cx, cy, hit_flash);
        // engine spark trailing off the right (rear)
        float p = 0.5f + 0.5f * std::sin(t_ * 26);
        glow_circle(s, Color(255, 220, 120), int(x + W), cy, int(3 + 2 * p), 70);
    }
};

// Sine weaver: bobs vertically as it crosses.
struct Weaver : Enemy {
    static constexpr int W = 24, H = 20;
    static constexpr float SPEED = 130.0f;
    float t_ = 0, base_y, amp, fire_cd;
    Weaver(float px, float py) {
        x = px; y = py; base_y = py; hp = 1; max_hp = 1; pts = 150; color = C_WEAVER;
        amp = rnd_uniform(50, 110);
        fire_cd = rnd_uniform(0.5f, 1.8f);
    }
    Rect rect() const override { return {int(x), int(y), W, H}; }
    std::pair<int, int> center() const override { return {int(x + W / 2.0f), int(y + H / 2.0f)}; }
    void nudge_y(float dy) override { y += dy; base_y += dy; }
    void update(float dt, float scroll_speed, Player& player,
                std::vector<EnemyShot>& shots) override;
    void draw(Surface& s) const override {
        auto [cx, cy] = center();
        int frame = std::sin(t_ * 9) > 0 ? 0 : 1;   // slow wing flap
        blit_enemy(s, weaver_art(), frame, cx, cy, hit_flash);
    }
};

// Diver: drifts in, then homes toward the player once.
struct Diver : Enemy {
    static constexpr int W = 22, H = 16;
    float vx = -120, vy = 0, t_ = 0; bool locked = false;
    Diver(float px, float py) {
        x = px; y = py; hp = 2; max_hp = 2; pts = 200; color = C_DIVER;
    }
    Rect rect() const override { return {int(x), int(y), W, H}; }
    std::pair<int, int> center() const override { return {int(x + W / 2.0f), int(y + H / 2.0f)}; }
    void update(float dt, float scroll_speed, Player& player,
                std::vector<EnemyShot>& shots) override;
    void draw(Surface& s) const override {
        auto [cx, cy] = center();
        int frame = int(t_ * 18) & 1;   // hot rapid tail flicker
        blit_enemy(s, diver_art(), frame, cx, cy, hit_flash);
        // angry eye glow on the nose (left)
        float p = 0.5f + 0.5f * std::sin(t_ * 20);
        glow_circle(s, Color(255, 120, 120), int(x + 2), cy, int(2 + 2 * p), 80);
    }
};

// Carrier: tanky gold ship that drifts across; drops a guaranteed power-up.
struct Carrier : Enemy {
    static constexpr int W = 48, H = 28;
    static constexpr float SPEED = 60.0f;
    float fire_cd, t_ = 0;
    Carrier(float px, float py) {
        x = px; y = py; hp = 5; max_hp = 5; pts = 400; color = C_CARRIER; carrier = true;
        fire_cd = rnd_uniform(1.2f, 2.6f);
    }
    Rect rect() const override { return {int(x), int(y), W, H}; }
    std::pair<int, int> center() const override { return {int(x + W / 2.0f), int(y + H / 2.0f)}; }
    void update(float dt, float scroll_speed, Player& player,
                std::vector<EnemyShot>& shots) override;
    void draw(Surface& s) const override {
        Rect r = rect();
        auto [cx, cy] = center();
        int frame = int(t_ * 8) & 1;   // slow engine pulse
        blit_enemy(s, carrier_art(), frame, cx, cy, hit_flash);
        // twin engine glow at the rear (right)
        float p = 0.5f + 0.5f * std::sin(t_ * 12);
        glow_circle(s, Color(255, 230, 150), r.x + r.w - 2, cy - 7, int(3 + 2 * p), 70);
        glow_circle(s, Color(255, 230, 150), r.x + r.w - 2, cy + 7, int(3 + 2 * p), 70);
        // damage pips along the hull
        int pips = std::min(hp, 5);
        for (int i = 0; i < pips; ++i)
            draw::circle(s, WHITE, r.x + 16 + i * 7, cy, 2);
    }
};

// Turret: bolted to a terrain platform; aims at the player and fires. On death
// it carves a gap in its platform (kill it to open a route).
struct Turret : Enemy {
    static constexpr int R = 12;
    float angle = PI, fire_cd;
    int station_id_ = -1;
    bool ceiling = false;
    Turret(float px, float py, int sid, bool ceil) {
        x = px; y = py; hp = 3; max_hp = 3; pts = 250; color = C_TURRET;
        fire_cd = rnd_uniform(1.0f, 2.4f); station_id_ = sid; ceiling = ceil;
    }
    int station_id() const override { return station_id_; }
    Rect rect() const override {
        constexpr int HIT = 19;
        return {int(x - HIT), int(y - HIT), HIT * 2, HIT * 2};
    }
    std::pair<int, int> center() const override { return {int(x), int(y)}; }
    bool is_heavy() const override { return true; }
    void update(float dt, float scroll_speed, Player& player,
                std::vector<EnemyShot>& shots) override;
    void draw(Surface& s) const override {
        int cx = int(x), cy = int(y);
        Color col = hit_flash > 0 ? lerp_color(color, WHITE, 0.6f) : color;
        // swivelling barrel (drawn first so the dome sits over its root)
        int bx = cx + int(std::cos(angle) * (R + 10));
        int by = cy + int(std::sin(angle) * (R + 10));
        draw::line(s, darker(STEEL, 0.7f), cx, cy, bx, by, 6);
        draw::line(s, STEEL, cx, cy, bx, by, 3);
        glow_circle(s, RED, bx, by, 3, 70);   // hot muzzle
        draw::circle(s, Color(255, 200, 120), bx, by, 2);
        // armoured gun-pod dome (sprite), mounted on its platform
        blit_enemy(s, ceiling ? turret_art_ceiling() : turret_art(), 0, cx, cy, hit_flash);
        // central optic / core
        float p = 0.5f + 0.5f * std::sin(angle * 2);
        glow_circle(s, col, cx, cy, int(4 + 2 * p), 70);
        draw::circle(s, WHITE, cx, cy, 2);
        // HP pips on the open side (below a ceiling turret, above a floor one)
        for (int i = 0; i < hp; ++i)
            draw::circle(s, YELLOW, cx - (hp - 1) * 3 + i * 6,
                         cy + (ceiling ? R + 8 : -(R + 8)), 2);
    }
};

// ── Enemy update impls (need full Player) ──────────────────────────────────────
void Darter::update(float dt, float scroll_speed, Player& player,
                    std::vector<EnemyShot>& shots) {
    tick_flash(dt); t_ += dt;
    x -= (SPEED + scroll_speed * 0.4f) * dt;
    if (player.alive) fire_cd -= dt;   // no volley stored up while you are dead
    if (fire_cd <= 0 && player.alive && x > 60) {
        fire_cd = rnd_uniform(1.4f, 3.0f) * g_aggro;
        auto [cx, cy] = center();
        shots.emplace_back(float(cx - 8), float(cy), PI, 230.0f);
    }
}
void Weaver::update(float dt, float scroll_speed, Player& player,
                    std::vector<EnemyShot>& shots) {
    tick_flash(dt); t_ += dt;
    x -= (SPEED + scroll_speed * 0.4f) * dt;
    y = base_y + std::sin(t_ * 3.0f) * amp;
    if (player.alive) fire_cd -= dt;
    if (fire_cd <= 0 && player.alive && x > 60) {
        fire_cd = rnd_uniform(1.0f, 2.4f) * g_aggro;
        auto [cx, cy] = center();
        auto [px, py] = player.center();
        float a = std::atan2(float(py - cy), float(px - cx));
        shots.emplace_back(float(cx), float(cy), a, 220.0f);
    }
}
void Diver::update(float dt, float scroll_speed, Player& player,
                   std::vector<EnemyShot>& shots) {
    tick_flash(dt); t_ += dt;
    (void)shots;
    if (!locked && x < 720 && player.alive) {
        locked = true;
        auto [cx, cy] = center();
        auto [px, py] = player.center();
        float a = std::atan2(float(py - cy), float(px - cx));
        float spd = 230 + scroll_speed * 0.3f;
        vx = std::cos(a) * spd; vy = std::sin(a) * spd;
    }
    x += vx * dt - scroll_speed * 0.25f * dt;
    y += vy * dt;
}
void Carrier::update(float dt, float scroll_speed, Player& player,
                     std::vector<EnemyShot>& shots) {
    tick_flash(dt); t_ += dt;
    x -= (SPEED + scroll_speed * 0.5f) * dt;
    y += std::sin(t_ * 1.4f) * 16 * dt;
    if (player.alive) fire_cd -= dt;
    if (fire_cd <= 0 && player.alive && x > 80) {
        fire_cd = rnd_uniform(1.6f, 3.0f) * g_aggro;
        auto [cx, cy] = center();
        for (float a : {PI - 0.28f, PI, PI + 0.28f})
            shots.emplace_back(float(cx - 8), float(cy), a, 210.0f);
    }
}
void Turret::update(float dt, float scroll_speed, Player& player,
                    std::vector<EnemyShot>& shots) {
    tick_flash(dt);
    x -= scroll_speed * dt;
    if (player.alive) {
        auto [px, py] = player.center();
        float target = std::atan2(float(py) - y, float(px) - x);
        float diff = target - angle;
        diff = std::fmod(diff + PI, TAU); if (diff < 0) diff += TAU; diff -= PI;
        angle += diff * 3.0f * dt;
    }
    if (player.alive) fire_cd -= dt;
    if (fire_cd <= 0 && player.alive && x > 40 && x < 940) {
        fire_cd = rnd_uniform(1.4f, 2.8f) * g_aggro;
        shots.emplace_back(x, y, angle, 240.0f);
    }
}

// ── Boss ───────────────────────────────────────────────────────────────────────
// A giant void-alien that slides in from the RIGHT and looms over that side of
// the screen. Multi-phase: tempo, face and attack pool change as HP drops. A
// pulsing CORE weak-point (phase 2+) takes triple damage. Signature attacks:
// bullet WALL with a gap, FAN spread, sweeping LASER, and a CHARGE/RAM that
// lunges left at the player. On death: big shockwave + power-up rain.
struct BossDef { const char* name; Color color; int w, h, hp; float atk_iv; int face; int cell; };
const BossDef BOSSES[4] = {
    {"STORMSPAWN",  Color(255, 140, 50),  200, 240, 60,  1.4f, 0, 18},
    {"THE GALE",    Color(200, 90, 255),  230, 300, 95,  1.2f, 1, 20},
    {"RIFT EYE",    Color(90, 170, 255),  250, 340, 140, 1.05f, 2, 22},
    {"VOID TITAN",  Color(255, 70, 70),   280, 420, 210, 0.9f, 3, 26},
};

struct Boss {
    std::string name;
    Color color;
    int w, h, cell, face;
    int hp, max_hp;
    float x, y, vy, bob = 0;
    float atk_t = 0, atk_iv, hit_flash = 0;
    bool dying = false, finished = false;
    float die_t = 0;
    static constexpr float DEATH_DUR = 1.7f;
    bool entering = false;
    float battle_x, invuln_ = 0;

    int tier = 1, phase = 1, cur_attack = -1, pending_attack = -1;
    float attack_t = 0, telegraph = 0, sub_t = 0, spin = 0, laser_angle = 0;
    int wall_gap = 0, ram_phase = 0; bool shock_done = false;
    float weak_t = 0; bool weak_open_ = false;
    float home_x, spark_acc = 0;

    bool damageable() const { return !dying && !entering && invuln_ <= 0; }
    bool active() const { return !finished; }

    Boss(int tier_, int sw, int sh) {
        tier = tier_;
        int idx = (tier - 1) % 4, loop = (tier - 1) / 4;
        const BossDef& d = BOSSES[idx];
        const char* pre = loop == 0 ? "" : loop == 1 ? "MEGA " : loop == 2 ? "OMEGA " : "VOID ";
        name = std::string(pre) + d.name;
        color = d.color; face = d.face; cell = d.cell;
        w = d.w + loop * 24; h = d.h + loop * 16;
        max_hp = hp = int(d.hp * (1.0f + 0.45f * (tier - 1)));
        atk_iv = std::max(0.55f, d.atk_iv - loop * 0.1f);
        battle_x = sw - w - 30; home_x = battle_x;
        x = float(sw + 20); y = (sh - h) / 2.0f;
        vy = (40 + tier * 8) * (rnd01() < 0.5f ? 1 : -1);
        entering = true;
    }
    Rect rect() const { return {int(x), int(y), w, h}; }
    float hp_ratio() const { return std::max(0.0f, float(hp) / max_hp); }

    // CORE weak-point: centred on the LEFT face (the side facing the player).
    Rect weak_rect() const {
        int ww = std::max(22, w / 5), wh = std::max(22, h / 5);
        return {int(x + w * 0.20f - ww / 2), int(y + h / 2 - wh / 2), ww, wh};
    }
    bool weak_vulnerable() const { return weak_open_ && damageable(); }
    // Does a shot flying in from the left strike the core? The core sits on
    // the left face, so anything arriving on its rows hits it first. (Testing
    // rect overlap never fired for ordinary shots: they die on the hull's
    // bounding box a frame before they could reach the core's rect.)
    bool strikes_core(Rect sr) const {
        Rect wr = weak_rect();
        return weak_vulnerable() && sr.y < wr.y + wr.h && sr.y + sr.h > wr.y &&
               sr.x < wr.x + wr.w;
    }

    static EnemyShot bb(float bx, float by, float a, float spd) {
        EnemyShot s(bx, by, a, spd);
        s.color = ORANGE; s.r = 6;
        return s;
    }

    void take_damage(int dmg, ParticleSystem& parts, bool weak = false) {
        (void)parts;
        if (!damageable()) return;
        if (weak) dmg *= 3;
        hp -= dmg; hit_flash = 0.16f;
        if (hp <= 0) { dying = true; die_t = 0; hp = 0; }
    }

    // 0 LOB 1 FAN 2 RADIAL 3 AIMED 4 WALL | 5 SPIRAL 6 RAIN 7 LASER 8 RAM
    int pick_attack() {
        std::vector<int> pool = {0, 2, 3};
        if (phase >= 2) {
            pool.insert(pool.end(), {1, 4});
            if (tier >= 2) pool.insert(pool.end(), {5, 8});
        }
        if (phase >= 3) {
            pool.push_back(3); pool.push_back(4);
            if (tier >= 2) { pool.push_back(6); pool.push_back(8); }
            if (tier >= 3) pool.push_back(7);
        }
        return pool[rnd_int(0, (int)pool.size() - 1)];
    }
    void begin_attack(int a) {
        cur_attack = a; attack_t = 0; sub_t = 1.0f; ram_phase = 0; shock_done = false;
        if (a == 4) wall_gap = rnd_int(1, 9);
    }
    void run_attack(float dt, float pcx, float pcy, int sw, int sh,
                    std::vector<EnemyShot>& out) {
        (void)sw;
        attack_t += dt; sub_t += dt;
        float bx = x + w * 0.16f, by = y + h / 2;   // muzzle on the left face
        switch (cur_attack) {
            case 0:  // LOB — three-shot left arc
                for (float a : {-0.30f, 0.0f, 0.30f}) out.push_back(bb(bx, by, PI + a, 215));
                cur_attack = -1; break;
            case 1:  // FAN
                for (int i = -2; i <= 2; ++i) out.push_back(bb(bx, by, PI + i * 0.17f, 250));
                cur_attack = -1; break;
            case 2:  // RADIAL — wide vertical sweep, all leftward
                for (int i = 0; i < 11; ++i)
                    out.push_back(bb(bx, by, PI - 1.2f + i * (2.4f / 10), 210));
                cur_attack = -1; break;
            case 3: {  // AIMED volley at the ship
                float aim = std::atan2(pcy - by, pcx - bx);
                for (int i = -1; i <= 1; ++i) out.push_back(bb(bx, by, aim + i * 0.12f, 300));
                cur_attack = -1; break;
            }
            case 4: {  // WALL — bursts from the muzzle and fans into a curtain with ONE gap
                const float form = 0.55f;
                for (int i = 0; i < 11; ++i) {
                    if (std::abs(i - wall_gap) <= 1) continue;
                    float py = (i + 0.5f) * (sh / 11.0f);
                    EnemyShot s(bx, by, PI, 200);
                    s.color = ORANGE; s.r = 6;
                    s.settle_t = form; s.settle_vy = (py - by) / form;
                    out.push_back(s);
                }
                cur_attack = -1; break;
            }
            // Streams: sub_t carries the remainder over, so the bullet rate is
            // the same at any frame rate (at most one per frame).
            case 5:  // SPIRAL
                if (sub_t >= 0.07f) {
                    sub_t = std::min(sub_t - 0.07f, 0.07f); spin += 0.5f;
                    out.push_back(bb(bx, by, spin, 200));
                    out.push_back(bb(bx, by, spin + PI, 200));
                }
                if (attack_t >= 1.7f) cur_attack = -1;
                break;
            case 6:  // RAIN — bullets from random points along the body, leftward
                if (sub_t >= 0.09f) {
                    sub_t = std::min(sub_t - 0.09f, 0.09f);
                    out.push_back(bb(x + rnd_uniform(0, w * 0.4f), y + rnd_uniform(0, h), PI, 235));
                }
                if (attack_t >= 1.5f) cur_attack = -1;
                break;
            case 7:  // LASER — a sweeping stream of fast bolts
                laser_angle = PI - 0.7f + (attack_t / 1.7f) * 1.4f;
                if (sub_t >= 0.03f) {
                    sub_t = std::min(sub_t - 0.03f, 0.03f);
                    out.push_back(bb(bx, by, laser_angle, 380));
                }
                if (attack_t >= 1.7f) cur_attack = -1;
                break;
            case 8: {  // RAM — wind up, lunge left, spray on impact, retreat
                float target = home_x - 240;
                if (ram_phase == 0) { if (attack_t >= 0.45f) ram_phase = 1; }
                else if (ram_phase == 1) { x -= 780 * dt; if (x <= target) { x = target; ram_phase = 2; } }
                else if (ram_phase == 2) {
                    if (!shock_done) {
                        shock_done = true;
                        for (int i = 0; i < 14; ++i)
                            out.push_back(bb(x, y + h / 2, PI - 1.5f + i * (3.0f / 13), 240));
                    }
                    ram_phase = 3;
                } else { x += 560 * dt; if (x >= home_x) { x = home_x; cur_attack = -1; } }
                break;
            }
            default: cur_attack = -1; break;
        }
    }

    // `hold_fire`: the player is down — keep moving (and finish a ram) but
    // start nothing new, so the fight doesn't freeze or pile up attacks.
    void update(float dt, float pcx, float pcy, int sw, int sh,
                std::vector<EnemyShot>& out, ParticleSystem& parts, bool hold_fire = false) {
        bob += dt; weak_t += dt;
        if (entering) {
            x -= 130 * dt;
            if (x <= battle_x) { x = battle_x; entering = false; invuln_ = 1.4f; }
            return;
        }
        if (hit_flash > 0) hit_flash -= dt;
        if (invuln_ > 0) invuln_ -= dt;
        if (dying) {
            die_t += dt;
            spark_acc = std::min(spark_acc + dt, 0.1f);   // ~36 bursts/s at any fps
            while (spark_acc >= 1.0f / 36.0f) {
                spark_acc -= 1.0f / 36.0f;
                float rx = x + rnd_uniform(0, w), ry = y + rnd_uniform(0, h);
                parts.ufo_explode(rx, ry, 10);
            }
            if (die_t >= DEATH_DUR) finished = true;
            return;
        }
        phase = hp_ratio() > 0.6f ? 1 : hp_ratio() > 0.3f ? 2 : 3;
        weak_open_ = (phase >= 2) && (std::sin(weak_t * 2.0f) > 0.2f);

        // bob vertically (ram overrides x)
        if (cur_attack != 8) {
            y += vy * dt;
            if (y < 6) { y = 6; vy = std::fabs(vy); }
            if (y + h > sh - 6) { y = sh - 6 - h; vy = -std::fabs(vy); }
            // settle x back home if nudged
            x += (home_x - x) * std::min(1.0f, 3 * dt);
        }
        float aggr = (tier <= 1) ? 0.85f : 0.55f;
        float iv = atk_iv * (phase == 3 ? aggr : phase == 2 ? 0.85f : 1.0f);

        if (hold_fire) {
            telegraph = 0; atk_t = 0;
            if (cur_attack == 8) run_attack(dt, pcx, pcy, sw, sh, out);
            else cur_attack = -1;
        } else if (telegraph > 0) {
            telegraph -= dt;
            if (telegraph <= 0) begin_attack(pending_attack);
        } else if (cur_attack >= 0) {
            run_attack(dt, pcx, pcy, sw, sh, out);
        } else {
            atk_t += dt;
            if (atk_t >= iv) {
                atk_t = 0;
                pending_attack = pick_attack();
                telegraph = tier <= 1 ? 0.6f : (phase == 3 ? 0.3f : 0.5f);
            }
        }
    }

    void draw_eye(Surface& s, int ex, int ey, int er, float pcx, float pcy) const {
        // Blocky pixel eye — square sclera + square pupil, snapped to a chunky
        // grid so it matches the rest of the blocky boss art (no smooth circle).
        int cell = std::max(2, er / 2);
        auto snap = [cell](int v) { return (v / cell) * cell; };
        draw::rect(s, WHITE, {snap(ex - er), snap(ey - er), er * 2, er * 2});
        float dx = pcx - ex, dy = pcy - ey, d = std::max(1.0f, std::hypot(dx, dy));
        int off = er / 2;
        int px = ex + int(dx / d * off), py = ey + int(dy / d * off);
        if (hit_flash > 0.06f) {
            draw::line(s, BLACK, ex - er, ey - er, ex + er, ey + er, 4);
            draw::line(s, BLACK, ex - er, ey + er, ex + er, ey - er, 4);
        } else {
            int pr = std::max(2, er / 2);
            draw::rect(s, BLACK, {snap(px - pr), snap(py - pr), pr * 2, pr * 2});
        }
    }

    void draw(Surface& s, float pcx, float pcy) const {
        if (x > s.width()) return;
        float sc = dying ? std::max(0.0f, 1 - die_t / DEATH_DUR) : 1.0f;
        int bw = int(w * sc), bh = int(h * sc);
        int bx = int(x + (w - bw) / 2 + std::sin(bob * 2) * 3), by = int(y + (h - bh) / 2);
        if (bw < 4 || bh < 4) return;
        Color body = hit_flash > 0 ? lerp_color(color, WHITE, 0.6f) : color;
        // soft outer glow — a few translucent rounded rects drawn straight onto s.
        // (glow_rect() allocated `radius` full-size surfaces PER FRAME, which made
        // big-boss fights lag badly at high stages.)
        for (int gi = 4; gi >= 1; --gi) {
            int pad = gi * 3;
            draw::rect(s, Color(body.r, body.g, body.b, 22),
                       {bx - pad, by - pad, bw + 2 * pad, bh + 2 * pad}, 0, cell + pad);
        }
        draw_boss_alien(s, face, int(bob * 3) % 2, bx, by, bw, bh, body);
        if (invuln_ > 0) {
            float p = 0.5f + 0.5f * std::sin(bob * 18);
            draw::rect(s, Color(120, 200, 255, int(130 * p)), {bx - 8, by - 8, bw + 16, bh + 16}, 3, cell + 6);
        }
        if (telegraph > 0) {
            float p = 0.4f + 0.6f * std::sin(bob * 30);
            draw::rect(s, Color(255, 60, 60, int(150 * p)), {bx - 7, by - 7, bw + 14, bh + 14}, 3, cell + 5);
        }
        // LASER: only a short muzzle flare along the firing angle (the bolts
        // are the danger — a full-length solid beam here was a lie), and a thin
        // dashed aim line while it is being telegraphed.
        if (cur_attack == 7 || (telegraph > 0 && pending_attack == 7)) {
            float a = cur_attack == 7 ? laser_angle : PI - 0.7f;
            int mx = bx + int(bw * 0.16f), my = by + bh / 2;
            if (cur_attack == 7) {
                int ex = mx + int(std::cos(a) * 46), ey = my + int(std::sin(a) * 46);
                draw::line(s, Color(255, 120, 60, 150), mx, my, ex, ey, 7);
                draw::line(s, Color(255, 220, 160), mx, my, ex, ey, 3);
            } else {
                for (int d = 30; d < 620; d += 26) {
                    int x0 = mx + int(std::cos(a) * d), y0 = my + int(std::sin(a) * d);
                    int x1 = mx + int(std::cos(a) * (d + 12)), y1 = my + int(std::sin(a) * (d + 12));
                    draw::line(s, Color(255, 90, 60, 150), x0, y0, x1, y1, 1);
                }
            }
        }
        if (dying) {
            int ey = by + int(bh * 0.4f);
            draw::circle(s, WHITE, bx + bw / 3, ey, bh / 12);
            draw::circle(s, WHITE, bx + 2 * bw / 3, ey, bh / 12);
            draw::circle(s, BLACK, bx + bw / 3, ey, bh / 22);
            draw::circle(s, BLACK, bx + 2 * bw / 3, ey, bh / 22);
            return;
        }
        int er = std::max(6, bh / 9);
        int eyl = bx + bw / 3, eyr = bx + 2 * bw / 3, eyy = by + int(bh * 0.30f);
        if (face == 2) draw_eye(s, bx + bw / 2, eyy, er * 2, pcx, pcy);
        else { draw_eye(s, eyl, eyy, er, pcx, pcy); draw_eye(s, eyr, eyy, er, pcx, pcy); }

        // CORE weak-point on the left face (phase 2+)
        if (phase >= 2 && sc > 0.5f) {
            Rect wr = weak_rect();
            int wcx = wr.x + wr.w / 2, wcy = wr.y + wr.h / 2, wrad = wr.w / 2;
            bool open = weak_open_;
            Color core = open ? Color(255, 240, 80) : Color(120, 60, 60);
            float pulse = 0.6f + 0.4f * std::sin(weak_t * (open ? 9.0f : 3.0f));
            if (open) glow_circle(s, core, wcx, wcy, wrad + 4, 110);
            draw::circle(s, BLACK, wcx, wcy, wrad + 3);
            draw::circle(s, Color(core.r, int(core.g * pulse), int(core.b * pulse)), wcx, wcy, wrad);
            draw::circle(s, WHITE, wcx, wcy, std::max(2, wrad / 2));
            if (open) {
                draw::circle(s, Color(255, 240, 80, 160), wcx, wcy, wrad + 6, 2);
                draw::line(s, WHITE, wcx, wcy - wrad - 6, wcx, wcy - wrad, 2);
                draw::line(s, WHITE, wcx, wcy + wrad, wcx, wcy + wrad + 6, 2);
            }
        }
    }
};

// ── Text popups / banners ─────────────────────────────────────────────────────
// Small popups ("+100", "SHIELD!") float up from where they happened and fade.
// Big banners (stage / boss / stage clear) each take their own row at the top
// of the playfield, so two that fire together never overprint each other.
struct Flash {
    std::string text;
    float x, y, life, max_life;
    Color color;
    bool big;
    int row;
};

// ── Spawn table ────────────────────────────────────────────────────────────────
struct SpawnKind { const char* name; int weight; };
struct SpawnTable { float lo, hi; std::vector<SpawnKind> enemies; };
const SpawnTable SPAWN_TABLES[5] = {
    {1.0f, 1.8f, {{"darter", 10}, {"weaver", 2}}},
    {1.0f, 1.9f, {{"darter", 8}, {"weaver", 5}}},
    {1.0f, 1.8f, {{"darter", 6}, {"weaver", 5}, {"diver", 3}}},
    {0.8f, 1.5f, {{"darter", 5}, {"weaver", 5}, {"diver", 4}, {"carrier", 2}}},
    {0.7f, 1.3f, {{"darter", 4}, {"weaver", 5}, {"diver", 5}, {"carrier", 2}}},
};
std::string weighted_choice(const std::vector<SpawnKind>& kinds) {
    int total = 0; for (auto& k : kinds) total += k.weight;
    int r = rnd_int(0, total - 1);
    for (auto& k : kinds) { if (r < k.weight) return k.name; r -= k.weight; }
    return kinds.back().name;
}

struct PendingTurret { int x, y, obs_id; bool ceiling; };

// ── Game ──────────────────────────────────────────────────────────────────────
enum class St { Intro, Play, Dead, GameOver };

class VoidStormGame : public Scene {
public:
    explicit VoidStormGame(const Context& ctx)
        : Scene(ctx), pause_(ctx.W, ctx.H, ctx.fonts),
          initials_(ctx.W, ctx.H, ctx.fonts) {
        high_score_ = ctx.scores ? ctx.scores->high_score("void_storm") : 0;
        new_game();
        if (ctx_.sfx) ctx_.sfx->start_ambient(2);  // calm storm bed
    }
    void update(float dt, const Input& in) override;
    void draw(Surface& s) override;
    void on_exit() override {
        submit_scores();
        charge_loop_stop();
        if (ctx_.sfx) ctx_.sfx->stop_ambient();
    }

private:
    PauseMenu pause_;
    InitialsEntry initials_;
    bool entry_started_ = false;
    ParticleSystem particles_;   // gameplay effects: drawn ABOVE enemies and boss
    ParticleSystem stars_;       // ambient specks: drawn behind everything
    int high_score_ = 0;
    float star_t_ = 0;

    int score_ = 0, lives_ = 3, stage_ = 1;
    float scroll_speed_ = SCROLL_BASE;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Terrain> terrain_;
    std::unique_ptr<Parallax> parallax_;
    std::unique_ptr<Boss> boss_;
    std::vector<std::unique_ptr<Enemy>> enemies_;
    std::vector<EnemyShot> enemy_shots_;
    std::vector<PlayerShot> player_shots_;
    std::vector<PowerUp> powerups_;
    std::vector<Flash> flash_;
    std::vector<PendingTurret> turret_pend_;
    St state_ = St::Intro;
    float intro_t_ = 0, dead_t_ = 0, enemy_cd_ = 1.5f;
    float clear_t_ = 0;             // >0: stage-clear interlude (no spawns, scroll surge)
    float game_over_t_ = 0;         // seconds since game over (panel fade + input lockout)
    bool submitted_ = false, new_hi_ = false, boss_active_ = false;
    int kills_since_drop_ = 0, kills_this_stage_ = 0, kills_needed_ = 12;

    bool bot_ = false;
    bool pad_ = false;              // a gamepad is connected -> pad prompts
    // fire button: a press fires at once, holding past CHARGE_DELAY charges
    bool fire_held_ = false;
    bool fire_block_ = false;       // ignore the button until it has been released once
    float fire_hold_t_ = 0;         // how long fire has been held
    float fire_buffer_t_ = 0;       // a tap made during the cooldown fires when it ends
    static constexpr float CHARGE_DELAY = 0.18f;
    bool max_cued_ = false;         // "fully charged" cue already given for this charge
    bool max_explained_ = false;    // the one-time "MAX erases bullets" banner was shown
    float game_t_ = 0;
    bool charge_loop_on_ = false;   // is the looping charge hum currently playing?
    float chirp_cd_ = 0;            // throttle for enemy spawn vocalisations

    void tele(const J& j) { if (ctx_.tele) ctx_.tele->event(j); }

    // The charge hum is a dedicated looping channel — it MUST always be stopped
    // (release, cancel, death, pause, level-end, game-over). Route every start/
    // stop through these two helpers so the loop can never stick on.
    void charge_loop_start() {
        if (charge_loop_on_) return;
        charge_loop_on_ = true;
        if (ctx_.sfx) ctx_.sfx->start_charge();
    }
    void charge_loop_stop() {
        if (!charge_loop_on_) return;
        charge_loop_on_ = false;
        if (ctx_.sfx) ctx_.sfx->stop_charge();
    }
    // Forget the fire button's history; the next shot needs a fresh press.
    void block_fire() {
        fire_block_ = true; fire_held_ = false; fire_hold_t_ = 0; fire_buffer_t_ = 0;
        if (player_) { player_->charging_ = false; player_->charge_ = 0; }
        max_cued_ = false;
    }

    std::string loadout() const {
        return "w" + std::to_string(player_->spread_) + "s" + std::to_string(player_->speed_) +
               "o" + std::to_string((int)player_->options_.size()) +
               "sh" + std::to_string(player_->shield_hp_);
    }
    void set_hue() {
        if (ctx_.fx) ctx_.fx->palette_hue = stage_ <= 1 ? 0.0f : 0.16f * std::sin(stage_ * 0.7f);
    }
    // World scroll right now: slow drift in the intro and while dead, a surge
    // during the stage-clear interlude. Terrain, turrets and pickups all use
    // this one value, so they always stay locked together.
    float scroll_now() const {
        if (state_ == St::Intro) return scroll_speed_ * 0.4f;
        if (state_ == St::Dead) return scroll_speed_ * 0.35f;
        return clear_t_ > 0 ? scroll_speed_ * 1.5f : scroll_speed_;
    }
    int spawn_y() const {   // a safe place for the ship: middle of the corridor
        int yt, yb;
        terrain_->corridor_span(120.0f, 200.0f, yt, yb);
        return (yt + yb) / 2 - PLAYER_H / 2;
    }

    void new_game() {
        score_ = 0; stage_ = 1; lives_ = 3;
        scroll_speed_ = SCROLL_BASE;
        entry_started_ = false; submitted_ = false; new_hi_ = false;
        terrain_ = std::make_unique<Terrain>(ctx_.W, ctx_.H);
        parallax_ = std::make_unique<Parallax>(ctx_.W, ctx_.H);
        player_ = std::make_unique<Player>(140, ctx_.H / 2 - PLAYER_H / 2);
        boss_.reset();
        enemies_.clear(); enemy_shots_.clear(); player_shots_.clear();
        powerups_.clear(); particles_.clear(); flash_.clear(); turret_pend_.clear();
        kills_since_drop_ = 0; kills_this_stage_ = 0;
        boss_active_ = false;
        charge_loop_stop(); chirp_cd_ = 0;
        intro_t_ = 2.2f; dead_t_ = 0; enemy_cd_ = 1.5f; clear_t_ = 0; game_over_t_ = 0;
        state_ = St::Intro;
        game_t_ = 0;
        kills_needed_ = 12;
        max_explained_ = false;
        block_fire();
        if (ctx_.combo) ctx_.combo->reset();

        bot_ = std::getenv("VOID_BOT") != nullptr || ctx_.demo;
        if (const char* e = std::getenv("VOID_WEAPON")) {
            int t = atoi(e);
            player_->spread_ = std::clamp(t, 0, 3);
            player_->speed_ = std::clamp(t, 0, 3);
            if (t >= 2) { player_->options_.push_back(OptionPod(player_->x, player_->y)); player_->place_pods(); }
        }
        if (const char* e = std::getenv("VOID_STAGE")) stage_ = std::max(1, atoi(e));
        if (stage_ > 1) {
            kills_needed_ = 14 + stage_ * 2;
            scroll_speed_ = SCROLL_BASE + std::min(120.0f, stage_ * 8.0f);
        }
        g_aggro = std::max(0.5f, 1.0f - (stage_ - 1) * 0.04f);
        set_hue();
        if (ctx_.sfx && !ctx_.demo) ctx_.sfx->start_music(6);
        if (std::getenv("VOID_BOSS")) { stage_ = BOSS_EVERY; state_ = St::Play; intro_t_ = 0; start_boss(); }
        tele(J().add("ev", "stage_start").add("stage", stage_).add("load", loadout()).add("lives", lives_));
    }

    // Small popup at a world position. If another popup was just made at the
    // same spot (two pickups at once), this one starts a line higher.
    void add_flash(const std::string& t, float x, float y, Color c, float life = 0.9f) {
        for (auto& f : flash_)
            if (!f.big && std::fabs(f.x - x) < 90 && std::fabs(f.y - y) < 16 &&
                f.max_life - f.life < 0.25f)
                y -= 18;
        x = std::clamp(x, 70.0f, ctx_.W - 70.0f);
        y = std::clamp(y, 70.0f, ctx_.H - 70.0f);
        flash_.push_back({t, x, y, life, life, c, false, 0});
    }
    // Big banner: takes the first free row under the top HUD.
    void add_banner(const std::string& t, Color c, float life = 2.2f) {
        int row = 0;
        for (bool clash = true; clash && row < 6;) {
            clash = false;
            for (auto& f : flash_) if (f.big && f.row == row) { clash = true; ++row; break; }
        }
        flash_.push_back({t, ctx_.W / 2.0f, 0.0f, life, life, c, true, row});
    }

    void submit_scores() {
        if (ctx_.demo) return;
        if (ctx_.scores && !submitted_) {
            submitted_ = true;
            tele(J().add("ev", "game_over").add("stage", stage_).add("score", score_)
                    .add("t", (double)game_t_).add("load", loadout()));
            new_hi_ = ctx_.scores->submit_void_storm(score_, stage_);
            high_score_ = ctx_.scores->high_score("void_storm");
        }
    }

    // ── power-up economy ──
    int weapon_tier() const { return player_->spread_ + player_->speed_ + (int)player_->options_.size(); }
    float drop_penalty() const { return std::clamp(1.0f - weapon_tier() * 0.14f, 0.25f, 1.0f); }
    void spawn_powerup(float x, float y, bool carrier_drop = false) {
        // Carrier drops bias toward the juicy OPTION/SPREAD; normal drops spread out.
        PU k;
        int r = rnd_int(0, 99);
        if (carrier_drop) {
            if (r < 38) k = PU::Option; else if (r < 64) k = PU::Spread;
            else if (r < 82) k = PU::Shield; else if (r < 94) k = PU::Speed;  // slightly fewer extra lives
            else k = PU::Life;
        } else {
            if (r < 30) k = PU::Spread; else if (r < 52) k = PU::Speed;
            else if (r < 72) k = PU::Shield; else if (r < 90) k = PU::Option;
            else if (r < 93) k = PU::Life; else k = PU::Mystery;   // Life 6% -> 3%
        }
        powerups_.push_back({x, y, 0, k});
    }
    void roll_drop(float x, float y, bool heavy) {
        ++kills_since_drop_;
        float chance = (heavy ? 0.32f : 0.11f) * drop_penalty();
        bool drop = (kills_since_drop_ >= 12 && weapon_tier() < 2) || rnd01() < chance;
        if (drop) { kills_since_drop_ = 0; spawn_powerup(x, y); }
    }
    void apply_powerup(PU k) {
        if (k == PU::Mystery)
            k = rnd_choice<PU>({PU::Spread, PU::Speed, PU::Shield, PU::Option, PU::Life});
        const char* msg = "";
        bool life = false;
        switch (k) {
            case PU::Spread: player_->spread_ = std::min(3, player_->spread_ + 1); msg = "SPREAD SHOT!"; break;
            case PU::Speed:  player_->speed_ = std::min(3, player_->speed_ + 1); msg = "SPEED UP!"; break;
            case PU::Shield: player_->shield_hp_ = std::min(3, player_->shield_hp_ + 1);
                             player_->shield_timer_ = 6.0f; msg = "SHIELD UP!"; break;
            case PU::Option:
                if ((int)player_->options_.size() < 3) {
                    player_->options_.push_back(OptionPod(player_->x, player_->y));
                    player_->place_pods();
                    msg = "OPTION POD!";
                } else { score_ += 500; msg = "PODS MAXED  +500"; }
                break;
            case PU::Life:   lives_++; msg = "EXTRA SHIP!"; life = true; break;
            default: break;
        }
        add_flash(msg, player_->x + player_->w / 2, player_->y - 26, life ? WHITE : YELLOW, 1.3f);
        if (ctx_.sfx) { if (life) ctx_.sfx->one_up(); else ctx_.sfx->powerup_collect(); }
        if (ctx_.fx) ctx_.fx->rumble_pickup();
        tele(J().add("ev", "powerup").add("stage", stage_).add("load", loadout()));
    }

    int kill_mult() { return ctx_.combo ? ctx_.combo->hit() : 1; }

    // Turn every enemy bullet into a harmless puff.
    void dissolve_enemy_shots() {
        for (auto& es : enemy_shots_)
            if (es.alive) particles_.explode_invader(es.x, es.y, es.color, 3);
        enemy_shots_.clear();
    }
    // Blow a gun platform apart (its last turret died, or a boss arrived).
    void shatter_platform(int obs_id, bool announce) {
        Rect r;
        if (!terrain_->destroy_obstacle(obs_id, r)) return;
        for (int px = r.x + 10; px < r.x + r.w; px += 22) {
            particles_.explode_invader(float(px), r.y + r.h / 2.0f, STEEL, 9);
            particles_.bunker_hit(float(px), r.y + r.h / 2.0f, 6);
        }
        if (announce) add_flash("LANE OPEN", r.x + r.w / 2.0f, r.y + (r.y < ctx_.H / 2 ? r.h + 22.0f : -26.0f), STEEL, 1.1f);
        if (ctx_.fx) ctx_.fx->shake(7);
    }

    void start_boss() {
        boss_ = std::make_unique<Boss>(stage_ / BOSS_EVERY, ctx_.W, ctx_.H);
        boss_active_ = true;
        terrain_->blocked = true;
        // The boss's arrival tears the corridor clean: everything in it blows
        // up (no points) instead of silently vanishing.
        for (auto& e : enemies_) {
            auto [cx, cy] = e->center();
            if (cx > -40 && cx < ctx_.W + 40) particles_.explode_invader(float(cx), float(cy), e->color, 12);
        }
        enemies_.clear();
        turret_pend_.clear();
        while (!terrain_->obstacles.empty()) shatter_platform(terrain_->obstacles.back().id, false);
        dissolve_enemy_shots();
        add_banner("WARNING: " + boss_->name + " RISES FROM THE VOID!", RED, 2.6f);
        if (ctx_.sfx) ctx_.sfx->boss_alarm();
        if (ctx_.fx) ctx_.fx->shake(14);
        tele(J().add("ev", "boss_start").add("stage", stage_).add("name", boss_->name));
    }
    void end_boss() {
        float rx = boss_->x + boss_->w / 2, ry = boss_->y + boss_->h / 2;
        int bonus = 800 * (stage_ / BOSS_EVERY);
        score_ += bonus;
        // four drops, fanned out so each one is a separate, readable pickup
        powerups_.push_back({rx - 10, ry - 60, 0, PU::Option});
        powerups_.push_back({rx - 60, ry - 20, 0, PU::Spread});
        powerups_.push_back({rx - 60, ry + 20, 0, PU::Shield});
        powerups_.push_back({rx - 10, ry + 60, 0, PU::Life});
        add_banner(boss_->name + " SHATTERS!   +" + std::to_string(bonus), YELLOW, 2.6f);
        if (ctx_.fx) {
            ctx_.fx->shake(30); ctx_.fx->hitstop(0.10f); ctx_.fx->rumble_explosion();
            ctx_.fx->shockwave(rx / float(ctx_.W), ry / float(ctx_.H));
        }
        if (ctx_.sfx) {
            charge_loop_stop();   // ensure no charge hum survives the level break
            ctx_.sfx->pan_next(std::clamp(rx / float(ctx_.W), 0.0f, 1.0f));
            ctx_.sfx->big_explosion();   // the boss shatters — bigger than boss_explosion
            ctx_.sfx->level_clear();
        }
        tele(J().add("ev", "boss_clear").add("stage", stage_).add("name", boss_->name));
        boss_.reset();
        boss_active_ = false;
        terrain_->blocked = false;
        dissolve_enemy_shots();
        advance_stage();
    }

    // The stage's kill quota is met: a short breather before the next stage —
    // fanfare, bonus, enemy fire dissolves, spawns stop and the scroll surges.
    void begin_stage_clear() {
        clear_t_ = 2.4f;
        int bonus = 200 * stage_;
        score_ += bonus;
        add_banner("STAGE " + std::to_string(stage_) + " CLEAR   +" + std::to_string(bonus), GREEN, 2.2f);
        dissolve_enemy_shots();
        if (ctx_.sfx) ctx_.sfx->level_clear();
        if (ctx_.fx) ctx_.fx->shake(5);
    }
    void advance_stage() {
        stage_++;
        clear_t_ = 0;
        kills_this_stage_ = 0;
        kills_needed_ = 14 + stage_ * 2;
        enemy_cd_ = 1.0f;
        scroll_speed_ = SCROLL_BASE + std::min(120.0f, stage_ * 8.0f);
        g_aggro = std::max(0.5f, 1.0f - (stage_ - 1) * 0.04f);  // enemies fire faster deeper in
        add_banner("STAGE " + std::to_string(stage_) + "   " + stage_name(stage_), GOLD, 2.4f);
        set_hue();
        tele(J().add("ev", "stage_start").add("stage", stage_).add("load", loadout()).add("lives", lives_));
        if (stage_ % BOSS_EVERY == 0) start_boss();
    }

    void update_play(float dt, const Input& in);
    void update_dead(float dt);
    void bot_decide();
    void spawn_enemies(float dt);
    void service_turrets();
    void confine_to_corridor(float dt);
    void cull_enemies();
    void on_enemy_killed(Enemy& e, bool by_beam, bool& big_played);
    void check_player_shots();
    void check_enemy_shots();
    void check_enemy_contact();
    void shield_feedback(const char* text);
    void push_into_corridor(float dt);
    void player_die(const char* cause);
    void draw_hud(Surface& s);
    void draw_flash(Surface& s);
};

void VoidStormGame::update(float dt, const Input& in) {
    pad_ = in.pad_connected;
    // Pause works everywhere except on the game-over screen (which has its own
    // menu). ESC opens it too, instead of dropping the run on the spot — the
    // pause menu is where "quit to menu" lives.
    if (!pause_.active() && state_ != St::GameOver &&
        (in.just(SDLK_p) || in.just(SDLK_ESCAPE)))
        pause_.toggle();
    if (pause_.active()) {
        charge_loop_stop();   // never let the hum run under the pause overlay
        PauseResult r = pause_.update(in);
        if (r == PauseResult::QuitToMenu) quit_to_menu = true;
        if (r != PauseResult::None) block_fire();   // the confirm press must not shoot
        return;
    }
    // Outside active play the charge hum has no business running.
    if (state_ != St::Play) charge_loop_stop();

    star_t_ += dt;
    if (star_t_ >= 0.15f) { star_t_ = 0; stars_.ambient_star(ctx_.W, ctx_.H); }
    stars_.update(dt);
    particles_.update(dt);
    if (parallax_) parallax_->update(dt, scroll_now());
    for (auto& f : flash_) {
        f.life -= dt;
        if (!f.big) f.y -= 26.0f * dt;   // popups drift upward as they fade
    }
    flash_.erase(std::remove_if(flash_.begin(), flash_.end(),
                 [](const Flash& f) { return f.life <= 0; }), flash_.end());

    if (state_ == St::Intro) {
        intro_t_ -= dt;
        float scroll = scroll_now();
        terrain_->update(dt, scroll, stage_);
        service_turrets();
        for (auto& e : enemies_) e->update(dt, scroll, *player_, enemy_shots_);
        if (intro_t_ <= 0) {
            state_ = St::Play;
            block_fire();
            if (ctx_.sfx) ctx_.sfx->spawn_chime();
        }
    } else if (state_ == St::Play) {
        game_t_ += dt;
        update_play(dt, in);
    } else if (state_ == St::Dead) {
        update_dead(dt);
    } else if (state_ == St::GameOver) {
        game_over_t_ += dt;
        if (bot_) { new_game(); return; }
        if (ctx_.scores) {
            const std::string gid = "void_storm";
            if (!entry_started_ && ctx_.scores->qualifies(gid, score_)) {
                entry_started_ = true;
                int rank = 1;
                for (auto& e : ctx_.scores->leaderboard(gid)) if (e.score > score_) rank++;
                initials_.begin("VOID STORM", score_, rank);
            }
            if (initials_.active()) {
                if (initials_.update(in)) {
                    ctx_.scores->add_score(gid, initials_.name(), score_);
                    // the confirming press must not also retry
                    game_over_t_ = std::min(game_over_t_, ui::GAME_OVER_LOCK - 0.35f);
                }
                return;
            }
        }
        if (ui::game_over_retry(in, game_over_t_)) new_game();
        else if (ui::game_over_menu(in, game_over_t_)) quit_to_menu = true;
    }
}

// The 2 s after a death: the world keeps drifting (slowly) — shots fly on,
// pickups float by, the boss keeps bobbing — instead of half of it freezing.
void VoidStormGame::update_dead(float dt) {
    dead_t_ -= dt;
    float scroll = scroll_now();
    terrain_->update(dt, scroll, stage_);
    service_turrets();
    for (auto& e : enemies_) e->update(dt, scroll, *player_, enemy_shots_);
    confine_to_corridor(dt);
    cull_enemies();
    if (boss_) {
        boss_->update(dt, player_->x, player_->y, ctx_.W, ctx_.H, enemy_shots_, particles_, true);
        if (boss_->finished) end_boss();
    }
    for (auto& s : player_shots_) s.update(dt, ctx_.W, ctx_.H);
    for (auto& s : enemy_shots_) s.update(dt, ctx_.W, ctx_.H);
    check_player_shots();
    for (auto& p : powerups_) p.update(dt, scroll);
    powerups_.erase(std::remove_if(powerups_.begin(), powerups_.end(),
                    [](const PowerUp& p) { return !p.alive; }), powerups_.end());
    player_shots_.erase(std::remove_if(player_shots_.begin(), player_shots_.end(),
                        [](const PlayerShot& s) { return !s.alive; }), player_shots_.end());
    enemy_shots_.erase(std::remove_if(enemy_shots_.begin(), enemy_shots_.end(),
                       [](const EnemyShot& s) { return !s.alive; }), enemy_shots_.end());

    if (dead_t_ > 0) return;
    if (lives_ > 0) {
        player_->respawn(140, spawn_y());
        dissolve_enemy_shots();
        block_fire();
        state_ = St::Play;
        if (ctx_.sfx) ctx_.sfx->spawn_chime();
    } else {
        state_ = St::GameOver;
        game_over_t_ = 0;
        submit_scores();
        if (ctx_.sfx && !bot_) ctx_.sfx->game_over();
    }
}

void VoidStormGame::bot_decide() {
    Player& p = *player_;
    if (!p.alive) { p.bdx = p.bdy = 0; return; }
    float pcx = p.x + p.w / 2.0f, pcy = p.y + p.h / 2.0f;

    // default: hold a comfy left-third lane, vertically centred in the corridor
    int y_top, y_bot;
    terrain_->corridor_at(pcx, y_top, y_bot);
    float tx = ctx_.W * 0.28f, ty = (y_top + y_bot) / 2.0f;

    // seek nearest power-up
    float best = 1e9f;
    for (auto& pu : powerups_) {
        if (!pu.alive) continue;
        float d = std::hypot(pu.x - pcx, pu.y - pcy);
        if (d < best && d < 320) { best = d; tx = pu.x; ty = pu.y; }
    }
    // dodge incoming enemy bullets (vertical evade)
    for (auto& s : enemy_shots_) {
        if (!s.alive) continue;
        float dx = s.x - pcx, dy = s.y - pcy;
        if (dx > -40 && dx < 230 && std::fabs(dy) < 50)
            ty = pcy + (dy > 0 ? -150 : 150);
    }
    // dodge divers / enemies bearing down
    for (auto& e : enemies_) {
        if (!e->alive) continue;
        auto [ex, ey] = e->center();
        if (ex - pcx > -30 && ex - pcx < 200 && std::fabs(ey - pcy) < 60)
            ty = pcy + (ey > pcy ? -140 : 140);
    }
    // avoid terrain: stay inside the tightest part of the corridor from here
    // to a good way ahead, so a choke is lined up for before it arrives
    int yt2, yb2;
    terrain_->corridor_span(pcx - 24, pcx + 200, yt2, yb2);
    yt2 += 22; yb2 -= 22;
    if (yb2 < yt2) yt2 = yb2 = (yt2 + yb2) / 2;
    ty = std::max(float(yt2), std::min(float(yb2), ty));
    // …and never under/over a gun platform that is about to arrive
    for (auto& o : terrain_->obstacles) {
        if (o.rect.x > pcx + 200 || o.rect.x + o.rect.w < pcx - 30) continue;
        if (o.ceiling) ty = std::max(ty, float(o.rect.y + o.rect.h + 26));
        else ty = std::min(ty, float(o.rect.y - 26));
    }

    p.bdx = tx > pcx + 8 ? 1.f : tx < pcx - 8 ? -1.f : 0.f;
    p.bdy = ty > pcy + 8 ? 1.f : ty < pcy - 8 ? -1.f : 0.f;

    // fire: charge occasionally when many enemies line up, else tap
    int ahead = 0;
    for (auto& e : enemies_) if (e->alive && e->center().first > pcx) ahead++;
    bool want_charge = (boss_ && boss_->active()) || ahead >= 3;
    static const bool force_charge = std::getenv("VOID_CHARGE") != nullptr;  // debug
    if (force_charge) want_charge = true;
    if (want_charge && p.charge_ < 1.0f) { p.charging_ = true; p.bcharge = true; p.bfire = false; }
    else { p.bcharge = false; p.bfire = true; }
}

// Register new platforms and bolt their turrets on. Runs in every state that
// scrolls the terrain, and platforms are only ever created off the right edge,
// so a turret can never pop into view.
void VoidStormGame::service_turrets() {
    for (auto& obs : terrain_->obstacles)
        if (!obs.registered) {
            obs.registered = true;
            Rect r = obs.rect;
            // turret sits on the inward face of the platform
            int ty = obs.ceiling ? r.y + r.h + 6 : r.y - 6;
            if (r.w > 110) {
                for (int tx : {r.x + r.w / 4, r.x + 3 * r.w / 4})
                    turret_pend_.push_back({tx, ty, obs.id, obs.ceiling});
            } else {
                turret_pend_.push_back({r.x + r.w / 2, ty, obs.id, obs.ceiling});
            }
        }
    if (boss_active_) { turret_pend_.clear(); return; }
    for (auto& pt : turret_pend_)
        enemies_.push_back(std::make_unique<Turret>(pt.x, pt.y, pt.obs_id, pt.ceiling));
    turret_pend_.clear();
}

void VoidStormGame::spawn_enemies(float dt) {
    if (boss_active_ || clear_t_ > 0) return;
    if (chirp_cd_ > 0) chirp_cd_ -= dt;
    int zi = std::min(stage_ - 1, 4);
    const SpawnTable& table = SPAWN_TABLES[zi];
    // Deep-stage escalation (the 5 base tables cap at stage 5, so without this it
    // plateaus): spawns speed up, a growing share of enemies are HARDENED (extra
    // HP + steel ring), and squadrons file in more often.
    float rate = std::max(0.45f, 1.0f - (stage_ - 1) * 0.045f);   // shorter gaps deeper
    float armor_p = std::clamp((stage_ - 3) * 0.07f, 0.0f, 0.55f);
    int armor_bonus = 1 + stage_ / 6;
    enemy_cd_ -= dt;
    if (enemy_cd_ > 0) return;
    enemy_cd_ = rnd_uniform(table.lo, table.hi) * rate;

    // Where the corridor is open at the right edge, where they enter.
    int y_top, y_bot;
    terrain_->corridor_span(ctx_.W - 30.0f, ctx_.W + 70.0f, y_top, y_bot);
    y_top += 12; y_bot -= 12;
    int room = y_bot - y_top;
    if (room < 70) { enemy_cd_ = 0.35f; return; }   // gate is nearly shut: wait
    bool tight = room < 190;                        // a choke is coming through

    // A squadron is ONE kind flying one path in single file, so it reads as a
    // formation (and a charged beam down the line is the obvious answer).
    std::string kind = weighted_choice(table.enemies);
    if (tight && (kind == "weaver" || kind == "carrier")) kind = "darter";
    float squad_p = stage_ >= 4 ? std::min(0.34f, 0.05f * stage_) : 0.16f + 0.04f * stage_;
    int squad = (!tight && kind != "carrier" && rnd01() < squad_p) ? rnd_int(3, stage_ >= 4 ? 5 : 4) : 1;
    int eh = kind == "darter" ? Darter::H : kind == "weaver" ? Weaver::H
           : kind == "diver" ? Diver::H : Carrier::H;
    float sy = rnd_uniform(float(y_top), float(std::max(y_top + 1, y_bot - eh)));
    float amp = 0.0f;
    if (kind == "weaver") {   // the whole sine must fit inside the corridor
        amp = std::min(rnd_uniform(50, 110), (room - eh) / 2.0f - 4.0f);
        amp = std::max(amp, 12.0f);
        sy = std::clamp(sy, y_top + amp, std::max(y_top + amp, y_bot - eh - amp));
    }
    bool harden = kind != "carrier" && rnd01() < armor_p;   // a squadron shares its armour
    for (int b = 0; b < squad; ++b) {
        float sx = ctx_.W + 30 + b * 46;   // single file
        Enemy* made = nullptr;
        if (kind == "darter") { auto e = std::make_unique<Darter>(sx, sy); made = e.get(); enemies_.push_back(std::move(e)); }
        else if (kind == "weaver") {
            auto e = std::make_unique<Weaver>(sx, sy);
            e->amp = amp; e->t_ = -b * 0.24f;   // followers trace the leader's wave
            made = e.get(); enemies_.push_back(std::move(e));
        }
        else if (kind == "diver") { auto e = std::make_unique<Diver>(sx, sy); made = e.get(); enemies_.push_back(std::move(e)); }
        else if (kind == "carrier") { auto e = std::make_unique<Carrier>(sx, sy); made = e.get(); enemies_.push_back(std::move(e)); }
        if (made && harden) {
            made->hp += armor_bonus; made->max_hp += armor_bonus;
            made->hardened = true; made->pts = int(made->pts * 1.5f);
        }
    }
    // spawn vocalisation — panned hard right (they enter from the right),
    // throttled so a busy stage doesn't turn into a chorus.
    if (chirp_cd_ <= 0 && ctx_.sfx) {
        chirp_cd_ = rnd_uniform(1.1f, 2.2f);
        ctx_.sfx->pan_next(1.0f);
        ctx_.sfx->alien_chirp(rnd_int(0, 2));
    }
}

// Flyers and pickups are eased back inside the rock walls, so nothing ends up
// buried where shots can't reach it or the ship can't follow.
void VoidStormGame::confine_to_corridor(float dt) {
    const float push = 240.0f * dt;
    for (auto& e : enemies_) {
        if (!e->alive || e->station_id() >= 0) continue;
        Rect r = e->rect();
        int yt, yb;
        terrain_->corridor_raw(r.x + r.w / 2.0f, yt, yb);
        yt += 4; yb -= 4;
        if (r.y < yt) e->nudge_y(std::min(float(yt - r.y), push));
        else if (r.y + r.h > yb) e->nudge_y(-std::min(float(r.y + r.h - yb), push));
    }
    for (auto& p : powerups_) {
        int yt, yb;
        terrain_->corridor_raw(p.x, yt, yb);
        yt += 22; yb -= 22;
        if (p.y < yt) p.y += std::min(yt - p.y, push);
        else if (p.y > yb) p.y -= std::min(p.y - yb, push);
    }
}

void VoidStormGame::cull_enemies() {
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
                   [&](const std::unique_ptr<Enemy>& e) {
                       if (!e->alive) return true;
                       auto [cx, cy] = e->center();
                       return cx < -60 || cy < -80 || cy > ctx_.H + 80;
                   }), enemies_.end());
}

void VoidStormGame::update_play(float dt, const Input& in) {
    const float scroll = scroll_now();
    // ── input / bot ──
    float mx = 0, my = 0;
    bool fire_down = false, beam_release = false;
    if (bot_) {
        bot_decide();
        mx = player_->bdx; my = player_->bdy;
        // bot: when not charging, request a tap each frame (fire_cd gates it)
        if (player_->bcharge) player_->charging_ = true;
        else {
            if (player_->charging_) { beam_release = true; player_->charging_ = false; }
            fire_down = true;
        }
    } else {
        mx = (in.down(SDLK_RIGHT) || in.down(SDLK_d)) - (in.down(SDLK_LEFT) || in.down(SDLK_a));
        my = (in.down(SDLK_DOWN) || in.down(SDLK_s)) - (in.down(SDLK_UP) || in.down(SDLK_w));
        bool held = in.down(SDLK_SPACE) || in.down(SDLK_LCTRL);
        if (fire_block_) { if (!held) fire_block_ = false; held = false; }
        // PRESS fires straight away (no waiting for the release); if the gun is
        // still cooling the shot is buffered for a moment rather than lost.
        if (held && !fire_held_) { fire_hold_t_ = 0; fire_buffer_t_ = 0.22f; }
        if (held) {
            // …and a deliberate HOLD spins up the beam.
            fire_hold_t_ += dt;
            if (!player_->charging_ && fire_hold_t_ >= CHARGE_DELAY) {
                player_->charging_ = true;
                player_->charge_ = std::max(player_->charge_, 0.12f);
                max_cued_ = false;
                fire_buffer_t_ = 0;
            }
        } else if (fire_held_) {  // released this frame
            if (player_->charging_) { beam_release = true; player_->charging_ = false; }
            fire_hold_t_ = 0;
        }
        fire_held_ = held;
        if (fire_buffer_t_ > 0) { fire_buffer_t_ -= dt; fire_down = true; }
    }
    player_->bdx = mx; player_->bdy = my;

    // build charge while charging
    if (player_->charging_) {
        player_->charge_ = std::min(1.0f, player_->charge_ + dt * 0.9f);
        if (player_->charge_ < 1.0f) {
            if (ctx_.fx) ctx_.fx->rumble_charge(player_->charge_);
        } else if (!max_cued_) {
            // fully charged: a clear cue you can hear, see and feel
            max_cued_ = true;
            if (ctx_.sfx) ctx_.sfx->lock_on();
            if (ctx_.fx) { ctx_.fx->rumble_tap(); ctx_.fx->shake(2); }
            auto [nx, ny] = player_->nose();
            particles_.explode_invader(float(nx), float(ny), WHITE, 6);
            add_flash("MAX", float(nx) + 10, float(ny) - 34, WHITE, 0.7f);
            if (!max_explained_ && !bot_) {
                max_explained_ = true;
                add_banner("MAX CHARGE: THE BEAM ALSO ERASES BULLETS", CYAN, 2.6f);
            }
        }
        // bot releases beam when fully charged (VOID_CHARGE=0..1 caps it, for
        // verifying/tuning the progressive beam at a fixed charge level).
        static const float bot_cap = [] {
            const char* e = std::getenv("VOID_CHARGE");
            return e ? std::clamp(float(atof(e)), 0.12f, 1.0f) : 1.0f;
        }();
        if (bot_ && player_->charge_ >= bot_cap) { beam_release = true; player_->charging_ = false; }
    }

    player_->update(dt, ctx_.W, ctx_.H, particles_);

    // ── charge hum loop: on while actively spinning up, off the instant we stop ──
    if (player_->charging_ && !beam_release) charge_loop_start();
    else charge_loop_stop();

    // ── firing ──
    if (beam_release) {
        float charge_at_release = player_->charge_;   // release_beam zeroes it
        charge_loop_stop();
        int n = player_->release_beam(player_shots_);
        if (n > 0 && ctx_.sfx) {
            auto [nx, ny] = player_->nose();
            (void)ny;
            ctx_.sfx->pan_next(nx / float(ctx_.W));
            if (charge_at_release >= 0.12f) ctx_.sfx->charged_blast(charge_at_release);  // scales with charge
            else ctx_.sfx->shoot();
        }
        if (ctx_.fx) ctx_.fx->shake(charge_at_release >= 0.12f ? int(3 + 7 * charge_at_release) : 4);
        max_cued_ = false;
    } else if (fire_down && !player_->charging_) {
        int n = player_->tap_fire(player_shots_);
        if (n > 0) {
            fire_buffer_t_ = 0;
            if (ctx_.sfx) ctx_.sfx->shoot();
        }
    }

    // ── terrain ──
    terrain_->update(dt, scroll, stage_);
    service_turrets();

    // ── player vs terrain ──
    if (player_->alive && terrain_->wall_blocks(player_->hit_rect())) {
        if (player_->safe()) push_into_corridor(dt);        // shielded: bounce off, don't die later
        else if (player_->take_hit()) { shield_feedback("SHIELD!"); push_into_corridor(dt); }
        else player_die("wall");
    }
    if (!player_->alive) return;

    // ── spawn / enemies ──
    spawn_enemies(dt);
    size_t shots_before = enemy_shots_.size();
    for (auto& e : enemies_) e->update(dt, scroll, *player_, enemy_shots_);
    confine_to_corridor(dt);
    // a single panned enemy_shot() per frame in which any enemy fired (so a
    // wall of fire reads as one gritty volley, not a machine-gun of clicks).
    if (enemy_shots_.size() > shots_before && ctx_.sfx) {
        const EnemyShot& ns = enemy_shots_[shots_before];
        ctx_.sfx->pan_next(std::clamp(ns.x / float(ctx_.W), 0.0f, 1.0f));
        ctx_.sfx->enemy_shot();
    }
    check_enemy_contact();
    if (!player_->alive) return;
    cull_enemies();

    // ── boss ──
    if (boss_) {
        auto [pcx, pcy] = player_->center();
        size_t before = enemy_shots_.size();
        boss_->update(dt, float(pcx), float(pcy), ctx_.W, ctx_.H, enemy_shots_, particles_);
        if (enemy_shots_.size() > before && ctx_.sfx) {
            ctx_.sfx->pan_next(0.85f); ctx_.sfx->enemy_shot();
        }
        if (boss_->finished) { end_boss(); return; }
        // boss body contact
        if (player_->alive && !boss_->dying && !boss_->entering &&
            overlap(player_->hit_rect(), boss_->rect())) {
            if (player_->safe()) player_->x = std::max(0.0f, player_->x - 320.0f * dt);
            else if (player_->take_hit()) shield_feedback("SHIELD!");
            else { player_die("boss"); return; }
        }
    }

    // ── shots ──
    for (auto& s : player_shots_) s.update(dt, ctx_.W, ctx_.H);
    for (auto& s : enemy_shots_) s.update(dt, ctx_.W, ctx_.H);

    // A FULLY-charged beam annihilates incoming enemy fire in its path — it reads
    // as an unstoppable lance. Partial charges punch enemies but not bullets.
    for (auto& ps : player_shots_) {
        if (!ps.alive || !ps.beam || !ps.full) continue;
        for (auto& es : enemy_shots_)
            if (es.alive && overlap(ps.rect(), es.rect())) {
                es.alive = false;
                particles_.explode_invader(es.x, es.y, es.color, 5);
            }
    }

    check_player_shots();
    if (!player_->alive) return;
    check_enemy_shots();
    if (!player_->alive) return;

    // ── power-ups ──
    for (auto& p : powerups_) {
        p.update(dt, scroll);
        if (p.alive && player_->alive && overlap(p.rect(), player_->rect())) {
            p.alive = false; apply_powerup(p.kind);
        }
    }
    powerups_.erase(std::remove_if(powerups_.begin(), powerups_.end(),
                    [](const PowerUp& p) { return !p.alive; }), powerups_.end());

    player_shots_.erase(std::remove_if(player_shots_.begin(), player_shots_.end(),
                        [](const PlayerShot& s) { return !s.alive; }), player_shots_.end());
    enemy_shots_.erase(std::remove_if(enemy_shots_.begin(), enemy_shots_.end(),
                       [](const EnemyShot& s) { return !s.alive; }), enemy_shots_.end());

    // ── stage progression: clear a quota of kills, take a breath, advance ──
    if (clear_t_ > 0) {
        clear_t_ -= dt;
        if (clear_t_ <= 0) advance_stage();
    } else if (!boss_active_ && kills_this_stage_ >= kills_needed_) {
        begin_stage_clear();
    }
}

// Score, effects, drops and (for a turret) its platform, for one destroyed enemy.
void VoidStormGame::on_enemy_killed(Enemy& e, bool by_beam, bool& big_played) {
    auto [cx, cy] = e.center();
    int pts = e.pts * kill_mult();
    score_ += pts;
    kills_this_stage_++;
    particles_.explode_invader(cx, cy, e.color, e.is_heavy() ? 22 : 14);
    add_flash("+" + std::to_string(pts), cx, cy - 16, YELLOW);
    if (ctx_.sfx) {
        ctx_.sfx->pan_next(cx / float(ctx_.W));
        if (by_beam) {
            // charged beam destruction = spectacular (once per pass)
            if (!big_played) { ctx_.sfx->big_explosion(); big_played = true; }
            else ctx_.sfx->boss_explosion();
        } else if (e.is_heavy()) {
            ctx_.sfx->boss_explosion();
        } else {
            ctx_.sfx->invader_hit();
        }
    }
    if (ctx_.fx) ctx_.fx->shake(by_beam ? 3 : e.is_heavy() ? 6 : 2);
    if (e.carrier) spawn_powerup(float(cx), float(cy), true);
    else roll_drop(float(cx), float(cy), e.is_heavy());
    // A turret's platform goes with its last gun: the lane it blocked opens.
    int sid = e.station_id();
    if (sid >= 0) {
        bool others = false;
        for (auto& o : enemies_)
            if (o.get() != &e && o->alive && o->station_id() == sid) others = true;
        if (!others) shatter_platform(sid, true);
    }
}

void VoidStormGame::check_player_shots() {
    bool big_played = false;   // throttle big_explosion to once per resolution pass
    for (auto& s : player_shots_) {
        if (!s.alive) continue;
        Rect sr = s.rect();
        // enemies
        for (auto& e : enemies_) {
            if (!e->alive || !overlap(sr, e->rect())) continue;
            if (s.beam) {                       // a beam hits each target once
                if (s.has_hit(e->uid)) continue;
                s.hit_uids.push_back(e->uid);
            }
            if (e->hit(s.damage)) {
                on_enemy_killed(*e, s.beam, big_played);
            } else {
                auto [cx, cy] = e->center();
                particles_.explode_invader(cx, cy, e->color, 6);
                if (ctx_.sfx) {
                    ctx_.sfx->pan_next(cx / float(ctx_.W));
                    if (s.beam) ctx_.sfx->zap(); else ctx_.sfx->bunker_hit();
                }
            }
            if (!s.beam) { s.alive = false; break; }   // beams pass through
        }
        if (!s.alive) continue;
        // boss (a beam damages it once, then passes on through)
        if (boss_ && boss_->damageable() && !s.boss_hit && overlap(sr, boss_->rect())) {
            bool weak = boss_->strikes_core(sr);
            s.boss_hit = true;
            boss_->take_damage(s.damage, particles_, weak);
            score_ += 4 * s.damage * (weak ? 3 : 1);
            float hx = std::max(float(boss_->rect().x), s.x + s.w), hy = s.y;
            if (weak) {
                Rect wr = boss_->weak_rect();
                hx = wr.x + wr.w / 2.0f; hy = wr.y + wr.h / 2.0f;
                add_flash("CORE x3", hx - 30, hy - 30, YELLOW, 0.5f);
            }
            particles_.explode_invader(hx, hy, weak ? YELLOW : boss_->color, weak ? 12 : 6);
            if (ctx_.sfx) {
                ctx_.sfx->pan_next(0.85f);
                if (weak) ctx_.sfx->zap(); else ctx_.sfx->armor_tink();
            }
            if (ctx_.fx) ctx_.fx->shake(weak ? 5 : 2);
            if (boss_->dying) dissolve_enemy_shots();   // the kill ends the danger
            if (!s.beam) s.alive = false;
        }
        if (!s.alive) continue;
        // terrain stops non-beam shots; beams burn through
        if (!s.beam && terrain_->wall_blocks(sr)) {
            particles_.bunker_hit(s.x, s.y, 4);
            s.alive = false;
        }
    }
}

void VoidStormGame::shield_feedback(const char* text) {
    if (ctx_.sfx) {
        ctx_.sfx->pan_next(std::clamp((player_->x + player_->w / 2) / float(ctx_.W), 0.0f, 1.0f));
        ctx_.sfx->shield_hit();
    }
    if (ctx_.fx) ctx_.fx->rumble_hit();
    add_flash(text, player_->x + player_->w / 2, player_->y - 24, GREEN, 0.7f);
}

// Ease the ship back toward the open part of the corridor (used while it is
// protected and touching rock, so protection never ends with it inside a wall).
void VoidStormGame::push_into_corridor(float dt) {
    int yt, yb;
    terrain_->corridor_span(player_->x, player_->x + player_->w, yt, yb);
    float mid = (yt + yb) / 2.0f, pc = player_->y + player_->h / 2.0f;
    for (auto& o : terrain_->obstacles)          // off a gun platform: away from its wall
        if (o.blocks(player_->hit_rect())) mid = o.ceiling ? pc + 200.0f : pc - 200.0f;
    float step = 300.0f * dt;
    if (pc < mid) player_->y += std::min(step, mid - pc);
    else player_->y -= std::min(step, pc - mid);
}

void VoidStormGame::check_enemy_shots() {
    bool safe = player_->safe();
    for (auto& s : enemy_shots_) {
        if (!s.alive) continue;
        if (player_->alive && !safe && overlap(s.rect(), player_->hit_rect())) {
            s.alive = false;
            if (player_->take_hit()) shield_feedback("SHIELD!");   // shield soaked it
            else player_die("shot");
            return;
        }
        // enemy shots are stopped by terrain
        if (terrain_->wall_blocks(s.rect())) { s.alive = false; }
    }
}

// Flying into an enemy hurts: the shield soaks it (and wrecks the rammer),
// otherwise the ship is lost.
void VoidStormGame::check_enemy_contact() {
    if (!player_->alive || player_->safe()) return;
    Rect pr = player_->hit_rect();
    for (auto& e : enemies_) {
        if (!e->alive || !overlap(pr, e->rect())) continue;
        if (player_->take_hit()) {
            shield_feedback("SHIELD!");
            if (e->station_id() < 0 && e->hit(3)) {
                bool big = true;   // no big explosion sound for a ram kill
                on_enemy_killed(*e, false, big);
            }
        } else {
            player_die("ram");
        }
        return;
    }
}

void VoidStormGame::player_die(const char* cause) {
    charge_loop_stop();   // dying mid-charge must silence the hum immediately
    auto [cx, cy] = player_->center();
    particles_.explode_player(cx, cy);
    bool had = player_->spread_ || player_->speed_ || player_->shield_hp_ ||
               player_->options_.size() > 1;
    if (had) add_flash("WEAPONS LOST!", cx, cy - 40, RED, 1.6f);
    player_->reset_on_death();
    player_->kill();
    lives_--;
    state_ = St::Dead; dead_t_ = 2.0f;
    if (ctx_.combo) ctx_.combo->reset();
    dissolve_enemy_shots();
    block_fire();
    tele(J().add("ev", "death").add("stage", stage_).add("t", (double)game_t_)
            .add("cause", cause).add("load", loadout()).add("lives", lives_));
    if (ctx_.sfx) ctx_.sfx->player_death();
    if (ctx_.fx) { ctx_.fx->shake(16); ctx_.fx->hitstop(0.09f); ctx_.fx->rumble_death(); }
}

// ── drawing ──────────────────────────────────────────────────────────────────
void VoidStormGame::draw(Surface& s) {
    s.fill(Color(8, 5, 12));
    if (parallax_) parallax_->draw(s);
    stars_.draw(s);
    terrain_->draw(s);
    for (auto& e : enemies_) {
        e->draw(s);
        if (e->hardened) {  // steel armour ring so the tougher ones read at a glance
            Rect r = e->rect();
            float p = 0.6f + 0.4f * std::sin(game_t_ * 6);
            draw::rect(s, Color(210, 222, 240, int(190 * p)), {r.x - 3, r.y - 3, r.w + 6, r.h + 6}, 2, 6);
        }
    }
    if (boss_) {
        auto [pcx, pcy] = player_->center();
        boss_->draw(s, float(pcx), float(pcy));
    }
    for (auto& p : powerups_) p.draw(s, *ctx_.fonts.small);
    // effects go OVER enemies, boss and rock, so hit sparks and explosions show
    particles_.draw(s);
    for (auto& sh : enemy_shots_) sh.draw(s);   // bullets stay readable on top
    player_->draw(s);
    for (auto& sh : player_shots_) sh.draw(s);

    draw_hud(s);
    draw_flash(s);

    if (state_ == St::Intro) {
        float a = std::clamp(intro_t_ * 1.5f, 0.0f, 1.0f);
        int al = int(255 * a);
        draw::rect(s, Color(0, 0, 0, int(120 * a)), {0, ctx_.H / 2 - 64, ctx_.W, 190});
        draw_text_centered(s, *ctx_.fonts.big,
                           "STAGE " + std::to_string(stage_), ctx_.W / 2, ctx_.H / 2 - 52,
                           Color(150, 210, 255, al));
        draw_text_centered(s, *ctx_.fonts.med, stage_name(stage_),
                           ctx_.W / 2, ctx_.H / 2 + 4, Color(ORANGE.r, ORANGE.g, ORANGE.b, al));
        Color hint(ui::HINT.r, ui::HINT.g, ui::HINT.b, al);
        draw_text_centered(s, *ctx_.fonts.small,
                           std::string("TAP ") + ui::fire_key(pad_) + " TO SHOOT      HOLD TO CHARGE THE BEAM",
                           ctx_.W / 2, ctx_.H / 2 + 50, hint);
        draw_text_centered(s, *ctx_.fonts.small,
                           "DESTROY TURRETS TO BLOW THEIR PLATFORMS OPEN",
                           ctx_.W / 2, ctx_.H / 2 + 76, hint);
    }

    if (state_ == St::GameOver) {
        ui::game_over(s, ctx_.fonts, ctx_.W, ctx_.H, game_over_t_, score_, new_hi_, pad_,
                      "STAGE " + std::to_string(stage_) + "   " + stage_name(stage_));
        if (initials_.active()) initials_.draw(s);
    }
    pause_.draw(s);
}

// HUD rows (nothing shares a row with anything it could collide with):
//   y   6        SCORE | HI | STAGE              on a dark strip
//   y  40..70    boss name + bar                 (boss fights only)
//   y  40 / 82   combo                           (drops below the boss bar)
//   y 118+       banners, one row each           (draw_flash)
//   y H-48, H-26 weapons / ships | charge | shield / pause hint, on a dark plate
void VoidStormGame::draw_hud(Surface& s) {
    Font& med = *ctx_.fonts.med;
    Font& small = *ctx_.fonts.small;
    const int W = ctx_.W, H = ctx_.H;
    draw::rect(s, Color(0, 0, 0, 150), {0, 0, W, 36});
    draw_text(s, med, "SCORE  " + zpad(score_, 6), 16, 6, GREEN);
    draw_text_centered(s, med, "HI  " + zpad(std::max(high_score_, score_), 6), W / 2, 6, YELLOW);
    draw_text_right(s, med, "STAGE  " + std::to_string(stage_), W - 16, 6, CYAN);

    bool boss_bar = boss_ && !boss_->dying && !boss_->entering;
    if (boss_bar) {
        int bw = 420, bx = W / 2 - bw / 2, by = 60;
        draw::rect(s, Color(0, 0, 0, 150), {bx - 10, 38, bw + 20, 40}, 0, 6);
        draw_text_centered(s, small, boss_->name, W / 2, 40, boss_->color);
        draw::rect(s, Color(20, 20, 30), {bx, by, bw, 12}, 0, 4);
        int fillw = int(bw * boss_->hp_ratio());
        Color hc = boss_->hp_ratio() > 0.5f ? GREEN : boss_->hp_ratio() > 0.25f ? YELLOW : RED;
        draw::rect(s, hc, {bx, by, fillw, 12}, 0, 4);
        draw::rect(s, boss_->color, {bx, by, bw, 12}, 1, 4);
    }

    if (ctx_.combo && ctx_.combo->active() && state_ == St::Play) {
        int m = ctx_.combo->mult();
        Color cc = m >= 6 ? RED : m >= 4 ? ORANGE : YELLOW;
        int cy = boss_bar ? 82 : 40;
        draw_text_centered(s, med, "x" + std::to_string(m) + " COMBO", W / 2, cy, cc);
        int bw = 120, bx = W / 2 - bw / 2;
        draw::rect(s, Color(40, 40, 40), {bx, cy + 26, bw, 4});
        draw::rect(s, cc, {bx, cy + 26, int(bw * ctx_.combo->ratio()), 4});
    }

    // ── bottom plate ──
    draw::rect(s, Color(0, 0, 0, 165), {0, H - 54, W, 54});
    const int row1 = H - 48, row2 = H - 26;

    // weapons: three labelled 3-step meters
    auto meter = [&](int x, const char* label, int n, Color c) {
        draw_text(s, small, label, x, row1, n > 0 ? c : ui::LABEL);
        for (int i = 0; i < 3; ++i)
            draw::rect(s, i < n ? c : Color(52, 62, 70), {x + 34 + i * 9, row1 + 4, 7, 11});
    };
    meter(16, "SPR", player_->spread_, ORANGE);
    meter(92, "SPD", player_->speed_, CYAN);
    meter(168, "POD", (int)player_->options_.size(), GOLD);

    // ships
    draw_text(s, small, "SHIPS", 16, row2, ui::LABEL);
    int shown = std::min(lives_, 7);
    for (int i = 0; i < shown; ++i)
        draw::polygon(s, CYAN, {{76 + i * 20, row2 + 3}, {76 + i * 20 + 14, row2 + 9},
                                {76 + i * 20, row2 + 15}});
    if (lives_ > shown)
        draw_text(s, small, "+" + std::to_string(lives_ - shown), 76 + shown * 20, row2, CYAN);

    // charge meter (always present, so the bar is never a surprise)
    {
        int bw = 180, bx = W / 2 - bw / 2, by = row2 + 4;
        float c = state_ == St::Play ? player_->charge_ : 0.0f;
        bool full = c >= 1.0f;
        bool blink = full && std::fmod(game_t_, 0.3f) < 0.15f;
        draw_text_centered(s, small, full ? "CHARGE  MAX" : "CHARGE", W / 2, row1,
                           full ? WHITE : ui::LABEL);
        draw::rect(s, Color(30, 42, 54), {bx, by, bw, 10}, 0, 4);
        if (c > 0.01f) {
            Color cc = full ? (blink ? WHITE : GOLD) : lerp_color(CYAN, GOLD, c);
            draw::rect(s, cc, {bx, by, int(bw * c), 10}, 0, 4);
        }
        draw::rect(s, Color(90, 110, 130), {bx, by, bw, 10}, 1, 4);
    }

    // shield + pause hint (right)
    if (player_->shield_hp_ > 0)
        draw_text_right(s, small, "SHIELD x" + std::to_string(player_->shield_hp_),
                        W - 14, row1, GREEN);
    draw_text_right(s, small, std::string(ui::pause_key(pad_)) + "  PAUSE", W - 14, row2, ui::LABEL);
}

void VoidStormGame::draw_flash(Surface& s) {
    for (auto& f : flash_) {
        Color c = f.color;
        // hold, then fade over the last 40% of the popup's life
        float a = std::clamp(f.life / std::max(0.1f, f.max_life * 0.4f), 0.0f, 1.0f);
        c.a = uint8_t(255 * a);
        if (f.big) {
            int y = 118 + f.row * 34;
            Font& font = *ctx_.fonts.med;
            int tw = font.text_width(f.text);
            draw::rect(s, Color(0, 0, 0, int(140 * a)), {ctx_.W / 2 - tw / 2 - 14, y - 3, tw + 28, 32}, 0, 6);
            draw_text_centered(s, font, f.text, ctx_.W / 2, y, c);
        } else {
            draw_text_centered(s, *ctx_.fonts.small, f.text, int(f.x), int(f.y), c);
        }
    }
}

}  // namespace

std::unique_ptr<Scene> make_void_storm(const Context& ctx) {
    return std::make_unique<VoidStormGame>(ctx);
}
