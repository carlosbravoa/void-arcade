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
// blow a gap in their platform when killed (kill them — it's always the right
// play). Power-ups: SPREAD, SPEED, SHIELD, OPTION pod (a trailing drone that
// echoes your fire) and an extra life. Energy/shield model: you have a shield
// bar that soaks hits + lives behind it.
//
// Every 5th stage a giant VOID boss fills the right side with multi-phase
// bullet patterns (walls-with-gaps, spreads, sweeping laser, a charge/ram) and
// a pulsing weak-point CORE; popping it ends the fight with a big shockwave and
// a rain of power-ups. "Bosses from the void."
//
// TERRAIN/TURRET DISCIPLINE: turrets reference their platform by a stable
// integer `id` via Terrain::find_obstacle(id) and NEVER store a raw pointer
// across frames (the obstacles vector reallocates). This mirrors the fix made
// in void_raid and avoids the classic use-after-free.
#include "games/void_storm/void_storm.h"

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
#include "engine/sprite.h"
#include "engine/surface.h"
#include "engine/telemetry.h"
#include "engine/text.h"

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
const char* STAGE_NAMES[5] = {"OUTER RIFT", "ION FIELD", "DEBRIS BELT",
                              "STORM WALL", "VOID THROAT"};
constexpr int BOSS_EVERY = 5;          // a boss every 5th stage

// World scroll (right -> left). Climbs with stage; player has no throttle here.
constexpr float SCROLL_BASE = 130.0f;

using SL = std::vector<std::string>;

Color darker(Color c, float f) { return Color(int(c.r * f), int(c.g * f), int(c.b * f)); }

bool overlap(Rect a, Rect b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

std::string zpad(int v, int width) {
    std::string s = std::to_string(v < 0 ? 0 : v);
    if ((int)s.size() < width) s = std::string(width - (int)s.size(), '0') + s;
    return s;
}

// Soft circular glow halo.
void glow_circle(Surface& surface, Color color, int cx, int cy, int r, int alpha) {
    for (int gr = r + 8; gr > r; gr -= 2) {
        float f = (gr - r) / 8.0f;
        int a = int(alpha * f * f * 0.5f);
        Surface gs(gr * 2, gr * 2, true);
        draw::circle(gs, Color(color.r, color.g, color.b, a), gr, gr, gr);
        surface.blit(gs, cx - gr, cy - gr);
    }
}

// Soft rounded-rect glow.
void glow_rect(Surface& surface, Rect rect, Color color, int radius, int alpha) {
    for (int r = radius; r > 0; --r) {
        float f = float(r) / radius;
        int a = int(alpha * std::pow(f, 3.5f));
        Surface s(rect.w + r * 2, rect.h + r * 2, true);
        draw::rect(s, Color(color.r, color.g, color.b, a),
                   {0, 0, rect.w + r * 2, rect.h + r * 2}, 0, r + 3);
        surface.blit(s, rect.x - r, rect.y - r);
    }
}

// A horizontal glow bolt travelling rightward (player shots fire right, not up,
// so the shared draw_glow_bolt — which is vertical — doesn't fit). x0<x1.
void glow_bolt_h(Surface& s, Color c, int y, int x0, int x1, int half_h) {
    for (int g = half_h + 3; g > 0; --g) {
        int a = int(70 * std::pow(g / float(half_h + 3), 2.0f));
        draw::line(s, Color(c.r, c.g, c.b, a), x0, y, x1, y, g * 2);
    }
    draw::line(s, WHITE, x0, y, x1, y, std::max(1, half_h - 1));
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

// Sprite cache: build the two frames (each as a glowing surface) once per type.
struct EnemyArt {
    std::shared_ptr<Surface> frame[2];
    std::shared_ptr<Surface> flash[2];   // white silhouette for hit-flash
    int cw = 0, ch = 0;   // unscaled cell dims (for centring)
    int scale = 0;
};
// '@' cells render as a hot near-white cockpit/highlight; '#' as the body color.
Surface build_enemy_frame(const SL& grid, Color body, int scale) {
    Surface s = make_sprite_surface(grid, body, scale);
    // overlay '@' highlight cells (cockpit / core glow)
    Color hi(std::min(255, body.r + 110), std::min(255, body.g + 110),
             std::min(255, body.b + 110));
    for (int r = 0; r < (int)grid.size(); ++r)
        for (int c = 0; c < (int)grid[r].size(); ++c)
            if (grid[r][c] == '@')
                draw::rect(s, hi, {c * scale, r * scale, scale, scale});
    return s;
}
EnemyArt make_enemy_art(const SL& f0, const SL& f1, Color body, int scale,
                        int glow_r = 6, float glow_i = 0.8f) {
    EnemyArt a;
    a.scale = scale;
    a.cw = (int)f0[0].size();
    a.ch = (int)f0.size();
    Surface s0 = build_enemy_frame(f0, body, scale);
    Surface s1 = build_enemy_frame(f1, body, scale);
    a.frame[0] = glow_surface(s0, body, glow_r, glow_i);
    a.frame[1] = glow_surface(s1, body, glow_r, glow_i);
    // White silhouettes (same body shape) for the hit-flash flicker.
    Surface w0 = make_sprite_surface(f0, WHITE, scale);
    Surface w1 = make_sprite_surface(f1, WHITE, scale);
    a.flash[0] = glow_surface(w0, WHITE, glow_r, glow_i);
    a.flash[1] = glow_surface(w1, WHITE, glow_r, glow_i);
    return a;
}
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

// Blit a cached enemy frame centred on (cx,cy), tinting white on hit-flash. The
// glow surface already carries a halo, so this is a single blit.
void blit_enemy(Surface& s, const EnemyArt& art, int frame, int cx, int cy,
                float hit_flash) {
    const Surface& g = *art.frame[frame & 1];
    int gx = cx - g.width() / 2, gy = cy - g.height() / 2;
    s.blit(g, gx, gy);
    if (hit_flash > 0) {
        // overlay the matching white silhouette at the flash's alpha — a clean,
        // shape-correct impact flicker that costs no per-frame allocation.
        Surface& f = *art.flash[frame & 1];
        f.set_alpha(int(170 * std::min(1.0f, hit_flash)));
        s.blit(f, gx, gy);
        f.set_alpha(-1);
    }
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
        if (beam)
            glow_bolt_h(s, color, int(y), int(x), int(x + w), h / 2);
        else
            glow_bolt_h(s, color, int(y), int(x), int(x + w), std::max(2, h / 2));
    }
};

struct EnemyShot {
    float x, y, vx, vy, t_ = 0;
    bool alive = true;
    Color color = ENEMY_SHOT_COLOR;
    int r = 5;
    EnemyShot(float px, float py, float angle, float spd)
        : x(px), y(py), vx(std::cos(angle) * spd), vy(std::sin(angle) * spd) {}
    Rect rect() const { return {int(x - r), int(y - r), r * 2, r * 2}; }
    void update(float dt, int sw, int sh) {
        t_ += dt;
        x += vx * dt;
        y += vy * dt;
        if (x < -30 || x > sw + 30 || y < -30 || y > sh + 30) alive = false;
    }
    void draw(Surface& s) const {
        if (!alive) return;
        int cx = int(x), cy = int(y);
        float pulse = 0.8f + 0.2f * std::sin(t_ * 18);
        int rr = std::max(2, int(r * pulse));
        Color col(color.r, int(color.g * pulse), int(color.b * pulse));
        glow_circle(s, col, cx, cy, rr + 2, 60);
        draw::circle(s, col, cx, cy, rr);
        draw::circle(s, Color(255, 230, 210), cx, cy, std::max(1, rr / 2));
    }
};

// ── Power-ups ────────────────────────────────────────────────────────────────
enum class PU { Spread, Speed, Shield, Option, Life, Mystery };
struct PUInfo { const char* label; Color color; };
PUInfo pu_info(PU k) {
    switch (k) {
        case PU::Spread: return {"W", ORANGE};
        case PU::Speed:  return {"S", CYAN};
        case PU::Shield: return {"O", GREEN};
        case PU::Option: return {"D", GOLD};
        case PU::Life:   return {"+1", WHITE};
        default:         return {"?", WHITE};
    }
}
// Power-ups drift left with the scroll; the ship flies into them.
struct PowerUp {
    float x, y, t = 0;
    PU kind;
    bool alive = true;
    Rect rect() const { return {int(x - 15), int(y - 13), 30, 26}; }
    void update(float dt, float scroll_speed, int sw) {
        t += dt;
        x -= (scroll_speed * 0.6f + 30) * dt;
        y += std::sin(t * 2.5f) * 18 * dt;
        if (x < -30) alive = false;
        (void)sw;
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
        draw::rect(s, Color(c.r, c.g, c.b, int(70 * pulse)), {int(x) - 19, int(y) - 17, 38, 34}, 0, 8);
        draw::rect(s, Color(8, 14, 20), {int(x) - 15, int(y) - 13, 30, 26}, 0, 6);
        draw::rect(s, c, {int(x) - 15, int(y) - 13, 30, 26}, 2, 6);
        draw_text_centered(s, f, in.label, int(x), int(y) - 9, c);
    }
};

// ── TerrainObstacle ───────────────────────────────────────────────────────────
// A chunky platform jutting from the ceiling (top) or floor (bottom). Turrets
// reference these by stable `id`. When a turret dies it carves a gap so you can
// fly through the platform.
struct TerrainObstacle {
    Rect rect;
    bool ceiling;        // true: hangs from top; false: rises from bottom
    float x;             // float x for smooth scroll
    bool registered = false;
    int id = 0;
    std::vector<Rect> gap_rects;

    TerrainObstacle(Rect r, bool ceil, int id_)
        : rect(r), ceiling(ceil), x(float(r.x)), id(id_) {}

    void scroll(float dx) {
        x -= dx;
        rect.x = int(x);
        for (auto& g : gap_rects) g.x = rect.x;
    }
    void open_gap(int cy, int gap_h = 48) {
        Rect& r = rect;
        int gy = std::max(r.y, std::min(r.y + r.h - gap_h, cy - gap_h / 2));
        gap_rects.push_back({r.x, gy, r.w, gap_h});
    }
    bool blocks(Rect pr) const {
        if (!overlap(rect, pr)) return false;
        for (auto& g : gap_rects)
            if (pr.y >= g.y && pr.y + pr.h <= g.y + g.h) return false;
        return true;
    }
    bool off_screen() const { return rect.x + rect.w < -40; }
};

// ── Terrain ───────────────────────────────────────────────────────────────────
// Ceiling and floor are columns of chunky strata that scroll left. Heights ease
// toward random targets, opening and pinching the corridor (telegraphed: the
// targets move slowly so a chokepoint is visible well before it arrives).
constexpr int COL_W = 12;                 // strata column width
const Color TERR_COLORS[3] = {Color(120, 60, 25), Color(150, 80, 30), Color(90, 45, 18)};
const Color TERR_GLOW(255, 130, 50);
constexpr int PLAT_H = 16;                // platform thickness for turret stations

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
    int choke_cd = 90, plat_cd = 70;
    int col_seq = 0, obs_seq = 1;
    bool blocked = false;     // boss in the corridor: stop new features

    Terrain(int sw, int sh) : W(sw), H(sh) {
        int n = W / COL_W + 4;
        for (int i = 0; i < n; ++i) append_col(float(i * COL_W));
    }

    TerrainObstacle* find_obstacle(int id) {
        for (auto& o : obstacles) if (o.id == id) return &o;
        return nullptr;
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
        // ease current toward targets so chokes are telegraphed
        top_h += (target_top - top_h) * 0.10f;
        bot_h += (target_bot - bot_h) * 0.10f;

        if (!blocked) {
            if (rnd01() < 0.05f) target_top = rnd_uniform(40, H * 0.34f);
            if (rnd01() < 0.05f) target_bot = rnd_uniform(40, H * 0.34f);
            // keep a guaranteed navigable corridor: at least 220px clear
            float clear = H - target_top - target_bot;
            if (clear < 220) {
                float over = 220 - clear;
                target_top = std::max(30.0f, target_top - over / 2);
                target_bot = std::max(30.0f, target_bot - over / 2);
            }
        }

        Column c{int(top_h), int(bot_h), x, col_seq++};
        cols.push_back(c);

        if (blocked) return;

        // Platforms (turret stations) jut from ceiling or floor occasionally.
        plat_cd -= 1;
        if (plat_cd <= 0) {
            plat_cd = rnd_int(55, 110);
            bool ceil = rnd01() < 0.5f;
            int pw = rnd_int(70, 130);
            int base = ceil ? int(top_h) : H - int(bot_h) - PLAT_H;
            obstacles.emplace_back(Rect{int(x), base, pw, PLAT_H}, ceil, obs_seq++);
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

    // Corridor clearance at a given screen x (for the bot to aim safely).
    void corridor_at(float x, int& y_top, int& y_bot) const {
        int th = int(top_h), bh = int(bot_h);
        for (auto& c : cols) {
            if (x >= c.x && x < c.x + COL_W) { th = c.top_h; bh = c.bot_h; break; }
        }
        y_top = th + 18;
        y_bot = H - bh - 18;
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
        for (auto& o : obstacles) {
            Rect r = o.rect;
            if (o.gap_rects.empty()) {
                draw::rect(surface, STEEL, r);
                draw::rect(surface, TERR_GLOW, {r.x, r.y, r.w, 3});
                draw::rect(surface, TERR_GLOW, {r.x, r.y + r.h - 3, r.w, 3});
            } else {
                // draw the platform minus its gap bands (vertical gaps)
                std::vector<std::pair<int,int>> gap_ys;
                for (auto& g : o.gap_rects) gap_ys.emplace_back(g.y, g.y + g.h);
                std::sort(gap_ys.begin(), gap_ys.end());
                int cursor = r.y;
                std::vector<std::pair<int,int>> spans;
                for (auto& gp : gap_ys) {
                    if (cursor < gp.first) spans.emplace_back(cursor, gp.first);
                    cursor = std::max(cursor, gp.second);
                }
                if (cursor < r.y + r.h) spans.emplace_back(cursor, r.y + r.h);
                for (auto& sp : spans) {
                    int sh = sp.second - sp.first;
                    draw::rect(surface, STEEL, {r.x, sp.first, r.w, sh});
                    draw::rect(surface, TERR_GLOW, {r.x, sp.first, r.w, 3});
                }
                for (auto& g : o.gap_rects) {
                    draw::line(surface, ORANGE, r.x, g.y, r.x + r.w, g.y, 2);
                    draw::line(surface, ORANGE, r.x, g.y + g.h, r.x + r.w, g.y + g.h, 2);
                }
            }
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
    float x, y;
    bool alive = true;
    float invincible = 0, fire_cd = 0;
    int w = PLAYER_W, h = PLAYER_H;
    float exhaust_t = 0;
    std::shared_ptr<Surface> surf;

    // progression (reset on death except options carry one)
    int spread_ = 0;
    int speed_ = 0;
    float shield_timer_ = 0;     // active reflective shield bubble
    int shield_hp_ = 0;          // shield bar (soaks hits before a life is lost)
    std::vector<OptionPod> options_;   // trailing drones echoing fire
    std::vector<std::pair<float,float>> trail_;  // history for option following

    // charge beam
    float charge_ = 0;           // 0..1 charge level
    bool charging_ = false;

    bool bot = false; float bdx = 0, bdy = 0; bool bfire = false; bool bcharge = false;

    Player(int px, int py) : x(float(px)), y(float(py)) {
        surf = std::make_shared<Surface>(make_sprite_surface(PLAYER_SHAPE, PLAYER_COLOR, 2));
    }
    Rect rect() const { return {int(x), int(y), w, h}; }
    std::pair<int, int> center() const { return {int(x + w / 2.0f), int(y + h / 2.0f)}; }
    std::pair<int, int> nose() const { return {int(x + w), int(y + h / 2.0f)}; }

    float move_speed() const { return SPEED * (1.0f + 0.16f * speed_); }
    float fire_rate() const { return 0.16f; }
    bool safe() const { return invincible > 0 || shield_timer_ > 0; }

    void reset_on_death() {
        spread_ = 0; speed_ = 0; shield_timer_ = 0; shield_hp_ = 0;
        charge_ = 0; charging_ = false;
        if (!options_.empty()) options_.resize(1);   // keep one option as mercy
    }

    void update(float dt, int sw, int sh) {
        if (!alive) return;
        if (invincible > 0) invincible -= dt;
        if (fire_cd > 0) fire_cd -= dt;
        if (shield_timer_ > 0) shield_timer_ -= dt;
        exhaust_t += dt;

        float mx, my;
        if (bot) { mx = bdx; my = bdy; }
        else {
            // movement supplied by the game (so input lives in one place)
            mx = bdx; my = bdy;
        }
        float spd = move_speed();
        x += mx * spd * dt;
        y += my * spd * dt;
        x = std::max(0.0f, std::min(float(sw - w), x));
        y = std::max(0.0f, std::min(float(sh - h), y));

        // record trail for options to follow
        trail_.insert(trail_.begin(), {x, y});
        if (trail_.size() > 90) trail_.resize(90);
        for (size_t i = 0; i < options_.size(); ++i) {
            size_t idx = std::min(trail_.size() - 1, (i + 1) * 18);
            auto [tx, ty] = trail_[idx];
            options_[i].x = tx; options_[i].y = ty;
        }
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

    // Release the charge beam: a fat, fast, piercing lance.
    int release_beam(std::vector<PlayerShot>& out) {
        float c = charge_;
        charge_ = 0; charging_ = false;
        if (c < 0.25f) return tap_fire(out);   // not enough charge -> normal shot
        bool full = c >= 1.0f;                 // only a max charge clears enemy fire
        auto [gx, gy] = nose();
        int beam_h = int(10 + 28 * c);
        int dmg = int(3 + 9 * c);
        PlayerShot s{float(gx), float(gy), 1100.0f, 60, beam_h};
        s.beam = true; s.full = full; s.color = BEAM_COLOR; s.damage = dmg; s.pierce = 999;
        out.push_back(s);
        for (auto& o : options_) {
            // Option pods fire at ~1/3 the main beam's power, so stacking pods on
            // a charged beam stays strong but not abusive.
            PlayerShot os{o.x + w, o.y + h / 2.0f, 1100.0f, 40, std::max(7, beam_h - 8)};
            os.beam = true; os.full = full; os.color = BEAM_COLOR; os.damage = std::max(1, dmg / 3);
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
    }

    void draw(Surface& surface, ParticleSystem& particles) const {
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
        int ex = int(x), ey = int(y + h / 2.0f);
        particles.ball_trail(ex, ey, Color(80, 180, 255));
        glow_rect(surface, rect(), PLAYER_COLOR, 3, 35);
        surface.blit(*surf, int(x), int(y));
        // charge glow on the nose
        if (charging_ && charge_ > 0.05f) {
            auto [gx, gy] = nose();
            float p = 0.5f + 0.5f * std::sin(exhaust_t * 30);
            int rad = int(4 + 14 * charge_);
            Color cc = lerp_color(CYAN, WHITE, charge_);
            glow_circle(surface, cc, gx, gy, rad, int(120 * (0.5f + 0.5f * charge_)));
            draw::circle(surface, cc, gx, gy, std::max(2, int(rad * 0.5f * (0.7f + 0.3f * p))));
            if (charge_ >= 1.0f)  // fully charged spark ring
                draw::circle(surface, WHITE, gx, gy, rad + 2, 1);
        }
        // shield bubble
        if (shield_timer_ > 0 || shield_hp_ > 0) {
            auto [cx, cy] = center();
            float pulse = 0.6f + 0.4f * std::sin(exhaust_t * 20);
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
    virtual ~Enemy() = default;
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
        blit_enemy(s, turret_art(), 0, cx, cy, hit_flash);
        // central optic / core
        float p = 0.5f + 0.5f * std::sin(angle * 2);
        glow_circle(s, col, cx, cy, int(4 + 2 * p), 70);
        draw::circle(s, WHITE, cx, cy, 2);
        // HP pips on the mount side
        for (int i = 0; i < hp; ++i)
            draw::circle(s, YELLOW, cx - (hp - 1) * 3 + i * 6,
                         cy - (ceiling ? -(R + 8) : R + 8), 2);
    }
};

// ── Enemy update impls (need full Player) ──────────────────────────────────────
void Darter::update(float dt, float scroll_speed, Player& player,
                    std::vector<EnemyShot>& shots) {
    tick_flash(dt); t_ += dt;
    x -= (SPEED + scroll_speed * 0.4f) * dt;
    fire_cd -= dt;
    if (fire_cd <= 0 && player.alive && x > 60) {
        fire_cd = rnd_uniform(1.4f, 3.0f);
        auto [cx, cy] = center();
        shots.emplace_back(float(cx - 8), float(cy), PI, 230.0f);
    }
}
void Weaver::update(float dt, float scroll_speed, Player& player,
                    std::vector<EnemyShot>& shots) {
    tick_flash(dt); t_ += dt;
    x -= (SPEED + scroll_speed * 0.4f) * dt;
    y = base_y + std::sin(t_ * 3.0f) * amp;
    fire_cd -= dt;
    if (fire_cd <= 0 && player.alive && x > 60) {
        fire_cd = rnd_uniform(1.0f, 2.4f);
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
    fire_cd -= dt;
    if (fire_cd <= 0 && player.alive && x > 80) {
        fire_cd = rnd_uniform(1.6f, 3.0f);
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
    fire_cd -= dt;
    if (fire_cd <= 0 && player.alive && x > 40) {
        fire_cd = rnd_uniform(1.4f, 2.8f);
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
    float home_x;

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

    static EnemyShot bb(float bx, float by, float a, float spd) {
        EnemyShot s(bx, by, a, spd);
        s.color = ORANGE; s.r = 6;
        return s;
    }

    void take_damage(int dmg, ParticleSystem& parts, bool weak = false) {
        (void)parts;
        if (!damageable()) return;
        if (weak) dmg *= 3;
        hp -= dmg; hit_flash = 0.14f;
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
        cur_attack = a; attack_t = 0; sub_t = 99; ram_phase = 0; shock_done = false;
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
            case 4: {  // WALL — vertical curtain with ONE safe gap
                for (int i = 0; i < 11; ++i) {
                    if (std::abs(i - wall_gap) <= 1) continue;
                    float py = (i + 0.5f) * (sh / 11.0f);
                    EnemyShot s(bx, py, PI, 200);
                    s.color = ORANGE; s.r = 6; out.push_back(s);
                }
                cur_attack = -1; break;
            }
            case 5:  // SPIRAL
                if (sub_t >= 0.07f) { sub_t = 0; spin += 0.5f;
                    out.push_back(bb(bx, by, spin, 200));
                    out.push_back(bb(bx, by, spin + PI, 200)); }
                if (attack_t >= 1.7f) cur_attack = -1; break;
            case 6:  // RAIN — bullets from random points along the body, leftward
                if (sub_t >= 0.09f) { sub_t = 0;
                    out.push_back(bb(x + rnd_uniform(0, w * 0.4f), y + rnd_uniform(0, h), PI, 235)); }
                if (attack_t >= 1.5f) cur_attack = -1; break;
            case 7:  // LASER — sweeping beam of fast bolts
                laser_angle = PI - 0.7f + (attack_t / 1.7f) * 1.4f;
                if (sub_t >= 0.025f) { sub_t = 0; out.push_back(bb(bx, by, laser_angle, 380)); }
                if (attack_t >= 1.7f) cur_attack = -1; break;
            case 8: {  // RAM — wind up, lunge left, spray on impact, retreat
                float target = home_x - 240;
                if (ram_phase == 0) { if (attack_t >= 0.45f) ram_phase = 1; }
                else if (ram_phase == 1) { x -= 780 * dt; if (x <= target) { x = target; ram_phase = 2; } }
                else if (ram_phase == 2) {
                    if (!shock_done) { shock_done = true;
                        for (int i = 0; i < 14; ++i)
                            out.push_back(bb(x, y + h / 2, PI - 1.5f + i * (3.0f / 13), 240)); }
                    ram_phase = 3;
                } else { x += 560 * dt; if (x >= home_x) { x = home_x; cur_attack = -1; } }
                break;
            }
            default: cur_attack = -1; break;
        }
    }

    void update(float dt, float pcx, float pcy, int sw, int sh,
                std::vector<EnemyShot>& out, ParticleSystem& parts) {
        bob += dt; weak_t += dt;
        if (entering) {
            x -= 130 * dt;
            if (x <= battle_x) { x = battle_x; entering = false; invuln_ = 1.4f; }
            return;
        }
        if (hit_flash > 0) hit_flash -= dt * 6;
        if (invuln_ > 0) invuln_ -= dt;
        if (dying) {
            die_t += dt;
            if (rnd01() < 0.6f) {
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

        if (telegraph > 0) {
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
        draw::circle(s, WHITE, ex, ey, er);
        float dx = pcx - ex, dy = pcy - ey, d = std::max(1.0f, std::hypot(dx, dy));
        int off = er / 2;
        int px = ex + int(dx / d * off), py = ey + int(dy / d * off);
        if (hit_flash > 0.06f) {
            draw::line(s, BLACK, ex - er / 2, ey - er / 2, ex + er / 2, ey + er / 2, 3);
            draw::line(s, BLACK, ex - er / 2, ey + er / 2, ex + er / 2, ey - er / 2, 3);
        } else {
            draw::circle(s, BLACK, px, py, std::max(2, er / 2));
        }
    }

    void draw(Surface& s, float pcx, float pcy) const {
        if (x > s.width()) return;
        float sc = dying ? std::max(0.0f, 1 - die_t / DEATH_DUR) : 1.0f;
        int bw = int(w * sc), bh = int(h * sc);
        int bx = int(x + (w - bw) / 2 + std::sin(bob * 2) * 3), by = int(y + (h - bh) / 2);
        if (bw < 4 || bh < 4) return;
        Color body = hit_flash > 0 ? lerp_color(color, WHITE, 0.6f) : color;
        draw::rect(s, Color(body.r, body.g, body.b, 70), {bx - 6, by - 6, bw + 12, bh + 12}, 0, cell + 4);
        draw_boss_alien(s, face, int(bob * 3) % 2, bx, by, bw, bh, body);
        if (invuln_ > 0) {
            float p = 0.5f + 0.5f * std::sin(bob * 18);
            draw::rect(s, Color(120, 200, 255, int(130 * p)), {bx - 8, by - 8, bw + 16, bh + 16}, 3, cell + 6);
        }
        if (telegraph > 0) {
            float p = 0.4f + 0.6f * std::sin(bob * 30);
            draw::rect(s, Color(255, 60, 60, int(150 * p)), {bx - 7, by - 7, bw + 14, bh + 14}, 3, cell + 5);
        }
        if (cur_attack == 7) {  // LASER beam
            int mx = bx + int(bw * 0.16f), my = by + bh / 2;
            int ex = mx + int(std::cos(laser_angle) * 1100), ey = my + int(std::sin(laser_angle) * 1100);
            draw::line(s, Color(255, 80, 40, 90), mx, my, ex, ey, 9);
            draw::line(s, Color(255, 200, 120), mx, my, ex, ey, 3);
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

// ── Flash ─────────────────────────────────────────────────────────────────────
struct Flash { std::string text; float x, y, life, max_life; Color color; bool big; };

// ── Spawn table ────────────────────────────────────────────────────────────────
struct SpawnKind { const char* name; int weight; };
struct SpawnTable { float lo, hi; std::vector<SpawnKind> enemies; };
const SpawnTable SPAWN_TABLES[5] = {
    {1.4f, 2.4f, {{"darter", 10}}},
    {1.1f, 2.0f, {{"darter", 8}, {"weaver", 5}}},
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
    ParticleSystem particles_;
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
    float intro_t_ = 0, dead_t_ = 0, enemy_cd_ = 1.5f, stage_t_ = 0;
    bool submitted_ = false, boss_active_ = false;
    int kills_since_drop_ = 0, kills_this_stage_ = 0, kills_needed_ = 18;

    bool bot_ = false; bool fire_held_ = false;
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

    std::string loadout() const {
        return "w" + std::to_string(player_->spread_) + "s" + std::to_string(player_->speed_) +
               "o" + std::to_string((int)player_->options_.size()) +
               "sh" + std::to_string(player_->shield_hp_);
    }

    void new_game() {
        score_ = 0; stage_ = 1; lives_ = 3;
        scroll_speed_ = SCROLL_BASE;
        entry_started_ = false; submitted_ = false;
        player_ = std::make_unique<Player>(140, ctx_.H / 2 - PLAYER_H / 2);
        terrain_ = std::make_unique<Terrain>(ctx_.W, ctx_.H);
        parallax_ = std::make_unique<Parallax>(ctx_.W, ctx_.H);
        boss_.reset();
        enemies_.clear(); enemy_shots_.clear(); player_shots_.clear();
        powerups_.clear(); particles_.clear(); flash_.clear(); turret_pend_.clear();
        kills_since_drop_ = 0; kills_this_stage_ = 0;
        boss_active_ = false;
        charge_loop_stop(); chirp_cd_ = 0;
        intro_t_ = 2.2f; dead_t_ = 0; enemy_cd_ = 1.5f; stage_t_ = 0;
        state_ = St::Intro;
        game_t_ = 0;
        kills_needed_ = 18;

        bot_ = std::getenv("VOID_BOT") != nullptr || ctx_.demo;
        if (const char* e = std::getenv("VOID_WEAPON")) {
            int t = atoi(e);
            player_->spread_ = std::min(3, t);
            player_->speed_ = std::min(3, t);
            if (t >= 2) player_->options_.push_back(OptionPod(player_->x, player_->y));
        }
        if (const char* e = std::getenv("VOID_STAGE")) stage_ = std::max(1, atoi(e));
        if (std::getenv("VOID_BOSS")) { stage_ = BOSS_EVERY; state_ = St::Play; intro_t_ = 0; start_boss(); }
        tele(J().add("ev", "stage_start").add("stage", stage_).add("load", loadout()).add("lives", lives_));
    }

    void add_flash(const std::string& t, float x, float y, Color c, float life = 0.9f, bool big = false) {
        flash_.push_back({t, x, y, life, life, c, big});
    }

    void submit_scores() {
        if (ctx_.demo) return;
        if (ctx_.scores && !submitted_) {
            submitted_ = true;
            tele(J().add("ev", "game_over").add("stage", stage_).add("score", score_)
                    .add("t", (double)game_t_).add("load", loadout()));
            bool hi = ctx_.scores->submit_void_storm(score_, stage_);
            if (hi) add_flash("NEW HI-SCORE!", ctx_.W / 2.0f, ctx_.H / 2.0f - 40, YELLOW, 2.5f);
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
            else if (r < 82) k = PU::Shield; else if (r < 92) k = PU::Speed;
            else k = PU::Life;
        } else {
            if (r < 30) k = PU::Spread; else if (r < 52) k = PU::Speed;
            else if (r < 72) k = PU::Shield; else if (r < 88) k = PU::Option;
            else if (r < 94) k = PU::Life; else k = PU::Mystery;
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
        switch (k) {
            case PU::Spread: player_->spread_ = std::min(3, player_->spread_ + 1); msg = "SPREAD SHOT!"; break;
            case PU::Speed:  player_->speed_ = std::min(3, player_->speed_ + 1); msg = "SPEED UP!"; break;
            case PU::Shield: player_->shield_hp_ = std::min(3, player_->shield_hp_ + 1);
                             player_->shield_timer_ = 6.0f; msg = "SHIELD UP!"; break;
            case PU::Option:
                if ((int)player_->options_.size() < 3) {
                    player_->options_.push_back(OptionPod(player_->x, player_->y));
                    msg = "OPTION POD!";
                } else msg = "OPTIONS MAXED!";
                break;
            case PU::Life:   lives_++; msg = "EXTRA LIFE!"; break;
            default: break;
        }
        add_flash(msg, player_->x + player_->w / 2, player_->y - 26, YELLOW, 1.2f);
        if (ctx_.sfx) ctx_.sfx->powerup_collect();
        if (ctx_.fx) ctx_.fx->rumble_pickup();
        tele(J().add("ev", "powerup").add("stage", stage_).add("load", loadout()));
    }

    int kill_mult() { return ctx_.combo ? ctx_.combo->hit() : 1; }

    void start_boss() {
        boss_ = std::make_unique<Boss>(stage_ / BOSS_EVERY, ctx_.W, ctx_.H);
        boss_active_ = true;
        terrain_->blocked = true;
        enemies_.clear(); enemy_shots_.clear();
        add_flash("WARNING: " + boss_->name + " RISES FROM THE VOID!",
                  ctx_.W / 2.0f, 110, RED, 2.4f, true);
        if (ctx_.sfx) ctx_.sfx->player_death();   // ominous sting
        if (ctx_.fx) ctx_.fx->shake(14);
        tele(J().add("ev", "boss_start").add("stage", stage_).add("name", boss_->name));
    }
    void end_boss() {
        float rx = boss_->x + boss_->w / 2, ry = boss_->y + boss_->h / 2;
        score_ += 800 * (stage_ / BOSS_EVERY);
        powerups_.push_back({rx, ry, 0, PU::Option});
        powerups_.push_back({rx - 50, ry - 30, 0, PU::Spread});
        powerups_.push_back({rx - 50, ry + 30, 0, PU::Shield});
        powerups_.push_back({rx, ry, 0, PU::Life});
        add_flash(boss_->name + " SHATTERS!", ctx_.W / 2.0f, ctx_.H / 2.0f - 20, YELLOW, 2.2f, true);
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
        advance_stage();
    }

    void advance_stage() {
        stage_++;
        kills_this_stage_ = 0;
        kills_needed_ = 16 + stage_ * 2;
        scroll_speed_ = SCROLL_BASE + std::min(120.0f, stage_ * 8.0f);
        add_flash("STAGE " + std::to_string(stage_) + "  " +
                  STAGE_NAMES[std::min(stage_ - 1, 4) % 5], ctx_.W / 2.0f, ctx_.H / 2.0f, GOLD, 2.2f, true);
        if (ctx_.fx) ctx_.fx->palette_hue = 0.16f * std::sin(stage_ * 0.7f);
        tele(J().add("ev", "stage_start").add("stage", stage_).add("load", loadout()).add("lives", lives_));
        if (stage_ % BOSS_EVERY == 0) start_boss();
    }

    void update_play(float dt, const Input& in);
    void bot_decide(const Input& in);
    void spawn_enemies(float dt);
    void register_stations();
    void check_player_shots();
    void check_enemy_shots();
    void player_die();
    void draw_hud(Surface& s);
    void draw_flash(Surface& s);
};

void VoidStormGame::update(float dt, const Input& in) {
    for (auto k : in.pressed)
        if (k == SDLK_p && state_ == St::Play) pause_.toggle();
    if (pause_.active()) {
        charge_loop_stop();   // never let the hum run under the pause overlay
        if (pause_.update(in) == PauseResult::QuitToMenu) quit_to_menu = true;
        return;
    }
    // Outside active play the charge hum has no business running.
    if (state_ != St::Play) charge_loop_stop();
    for (auto k : in.pressed)
        if (k == SDLK_ESCAPE) { quit_to_menu = true; return; }

    star_t_ += dt;
    if (star_t_ >= 0.15f) { star_t_ = 0; particles_.ambient_star(ctx_.W, ctx_.H); }
    particles_.update(dt);
    if (parallax_) parallax_->update(dt, state_ == St::Play ? scroll_speed_ : scroll_speed_ * 0.4f);
    for (auto& f : flash_) f.life -= dt;
    flash_.erase(std::remove_if(flash_.begin(), flash_.end(),
                 [](const Flash& f) { return f.life <= 0; }), flash_.end());

    if (state_ == St::Intro) {
        intro_t_ -= dt;
        terrain_->update(dt, scroll_speed_ * 0.4f, stage_);
        if (intro_t_ <= 0) { state_ = St::Play; if (ctx_.sfx) ctx_.sfx->spawn_chime(); }
    } else if (state_ == St::Play) {
        game_t_ += dt;
        update_play(dt, in);
    } else if (state_ == St::Dead) {
        dead_t_ -= dt;
        terrain_->update(dt, scroll_speed_ * 0.35f, stage_);
        // Scroll the enemies at the SAME rate as the terrain so turrets stay
        // bolted to their platforms during the death drift (they don't fire —
        // the player is dead).
        for (auto& e : enemies_) e->update(dt, scroll_speed_ * 0.35f, *player_, enemy_shots_);
        if (dead_t_ <= 0) {
            if (lives_ > 0) {
                player_->respawn(140, ctx_.H / 2 - PLAYER_H / 2);
                enemy_shots_.clear();
                state_ = St::Play;
                if (ctx_.sfx) ctx_.sfx->spawn_chime();
            } else { state_ = St::GameOver; submit_scores(); }
        }
    } else if (state_ == St::GameOver) {
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
                if (initials_.update(in)) ctx_.scores->add_score(gid, initials_.name(), score_);
                return;
            }
        }
        for (auto k : in.pressed)
            if (k == SDLK_RETURN || k == SDLK_SPACE) new_game();
    }
}

void VoidStormGame::bot_decide(const Input& in) {
    (void)in;
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
    // avoid terrain: keep inside the corridor at our column
    int yt2, yb2; terrain_->corridor_at(pcx, yt2, yb2);
    ty = std::max(float(yt2), std::min(float(yb2), ty));
    // also peek a column to the right so we react before hitting a wall
    int yt3, yb3; terrain_->corridor_at(pcx + 60, yt3, yb3);
    ty = std::max(float(yt3 + 6), std::min(float(yb3 - 6), ty));

    p.bdx = tx > pcx + 8 ? 1.f : tx < pcx - 8 ? -1.f : 0.f;
    p.bdy = ty > pcy + 8 ? 1.f : ty < pcy - 8 ? -1.f : 0.f;

    // fire: charge occasionally when many enemies line up, else tap
    int ahead = 0;
    for (auto& e : enemies_) if (e->alive && e->center().first > pcx) ahead++;
    bool want_charge = (boss_ && boss_->active()) || ahead >= 3;
    if (want_charge && p.charge_ < 1.0f) { p.charging_ = true; p.bcharge = true; p.bfire = false; }
    else { p.bcharge = false; p.bfire = true; }
}

void VoidStormGame::register_stations() {
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
}

void VoidStormGame::spawn_enemies(float dt) {
    if (boss_active_) { turret_pend_.clear(); return; }
    if (chirp_cd_ > 0) chirp_cd_ -= dt;
    int zi = std::min(stage_ - 1, 4);
    const SpawnTable& table = SPAWN_TABLES[zi];
    enemy_cd_ -= dt;
    if (enemy_cd_ <= 0) {
        enemy_cd_ = rnd_uniform(table.lo, table.hi);
        std::string kind = weighted_choice(table.enemies);
        int y_top, y_bot;
        terrain_->corridor_at(ctx_.W - 10.0f, y_top, y_bot);
        if (y_bot - y_top < 60) { y_top = 120; y_bot = ctx_.H - 120; }
        float sy = rnd_uniform(float(y_top), float(y_bot));
        float sx = ctx_.W + 30;
        if (kind == "darter") enemies_.push_back(std::make_unique<Darter>(sx, sy));
        else if (kind == "weaver") enemies_.push_back(std::make_unique<Weaver>(sx, sy));
        else if (kind == "diver") enemies_.push_back(std::make_unique<Diver>(sx, sy));
        else if (kind == "carrier") enemies_.push_back(std::make_unique<Carrier>(sx, sy));
        // spawn vocalisation — panned hard right (they enter from the right),
        // throttled so a busy stage doesn't turn into a chorus.
        if (chirp_cd_ <= 0 && ctx_.sfx) {
            chirp_cd_ = rnd_uniform(1.1f, 2.2f);
            ctx_.sfx->pan_next(std::clamp(sx / float(ctx_.W), 0.0f, 1.0f));
            ctx_.sfx->alien_chirp(rnd_int(0, 2));
        }
    }
    // place pending turrets
    for (auto& pt : turret_pend_)
        enemies_.push_back(std::make_unique<Turret>(pt.x, pt.y, pt.obs_id, pt.ceiling));
    turret_pend_.clear();
}

void VoidStormGame::update_play(float dt, const Input& in) {
    // ── input / bot ──
    float mx = 0, my = 0;
    bool fire_down = false, beam_release = false;
    if (bot_) {
        bot_decide(in);
        mx = player_->bdx; my = player_->bdy;
        if (player_->bcharge) { player_->charging_ = true; }
        else if (fire_held_ && !player_->bfire) {}  // n/a
        // bot: when not charging, request a tap each frame (fire_cd gates it)
        if (player_->bcharge) { fire_down = false; }
        else {
            if (player_->charging_) { beam_release = true; player_->charging_ = false; }
            fire_down = true;
        }
        if (player_->bcharge) player_->charging_ = true;
    } else {
        mx = (in.down(SDLK_RIGHT) || in.down(SDLK_d)) - (in.down(SDLK_LEFT) || in.down(SDLK_a));
        my = (in.down(SDLK_DOWN) || in.down(SDLK_s)) - (in.down(SDLK_UP) || in.down(SDLK_w));
        bool space = in.down(SDLK_SPACE) || in.down(SDLK_LCTRL);
        if (space) {
            player_->charging_ = true;
            fire_down = false;
        } else if (fire_held_) {
            // just released
            if (player_->charging_) { beam_release = true; player_->charging_ = false; }
        }
        fire_held_ = space;
    }
    player_->bdx = mx; player_->bdy = my;

    // build charge while charging
    if (player_->charging_) {
        player_->charge_ = std::min(1.0f, player_->charge_ + dt * 0.9f);
        if (ctx_.fx) ctx_.fx->rumble_charge(player_->charge_);
        // bot releases beam when fully charged
        if (bot_ && player_->charge_ >= 1.0f) { beam_release = true; player_->charging_ = false; }
    }

    player_->update(dt, ctx_.W, ctx_.H);

    // ── charge hum loop: on while actively spinning up, off the instant we stop ──
    if (player_->charging_ && !beam_release) charge_loop_start();
    else charge_loop_stop();

    // ── firing ──
    if (beam_release) {
        float charge_at_release = player_->charge_;   // release_beam zeroes it
        charge_loop_stop();
        int n = player_->release_beam(player_shots_);
        // >=0.25 is the beam threshold in release_beam(); below that it degrades
        // to an ordinary tap, so match the audio to what actually fired.
        if (n > 0 && ctx_.sfx) {
            auto [nx, ny] = player_->nose();
            (void)ny;
            ctx_.sfx->pan_next(nx / float(ctx_.W));
            if (charge_at_release >= 0.25f) ctx_.sfx->charged_blast();  // the payoff
            else ctx_.sfx->shoot();
        }
        if (ctx_.fx) ctx_.fx->shake(charge_at_release >= 0.25f ? 8 : 4);
    } else if (fire_down && !player_->charging_) {
        int n = player_->tap_fire(player_shots_);
        if (n > 0 && ctx_.sfx) ctx_.sfx->shoot();
    }

    // ── terrain ──
    terrain_->update(dt, scroll_speed_, stage_);
    register_stations();

    // ── player vs terrain ──
    if (player_->alive && !player_->safe()) {
        if (terrain_->wall_blocks(player_->rect())) player_die();
    }
    if (!player_->alive) return;

    // ── spawn / enemies ──
    spawn_enemies(dt);
    size_t shots_before = enemy_shots_.size();
    for (auto& e : enemies_) e->update(dt, scroll_speed_, *player_, enemy_shots_);
    // a single panned enemy_shot() per frame in which any enemy fired (so a
    // wall of fire reads as one gritty volley, not a machine-gun of clicks).
    if (enemy_shots_.size() > shots_before && ctx_.sfx) {
        const EnemyShot& ns = enemy_shots_[shots_before];
        ctx_.sfx->pan_next(std::clamp(ns.x / float(ctx_.W), 0.0f, 1.0f));
        ctx_.sfx->enemy_shot();
    }
    // cull dead / offscreen-left enemies
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
                   [&](const std::unique_ptr<Enemy>& e) {
                       if (!e->alive) return true;
                       auto [cx, cy] = e->center();
                       return cx < -60 || cy < -80 || cy > ctx_.H + 80;
                   }), enemies_.end());

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
        if (player_->alive && !player_->safe() && !boss_->dying && !boss_->entering &&
            overlap(player_->rect(), boss_->rect())) { player_die(); return; }
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
        p.update(dt, scroll_speed_, ctx_.W);
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

    // ── stage progression: clear a quota of kills, then advance (unless boss) ──
    stage_t_ += dt;
    if (!boss_active_ && kills_this_stage_ >= kills_needed_) {
        advance_stage();
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
            bool killed = e->hit(s.damage);
            auto [cx, cy] = e->center();
            if (killed) {
                int pts = e->pts * kill_mult();
                score_ += pts;
                kills_this_stage_++;
                particles_.explode_invader(cx, cy, e->color, e->is_heavy() ? 22 : 14);
                add_flash("+" + std::to_string(pts), cx, cy - 16, YELLOW);
                if (ctx_.sfx) {
                    ctx_.sfx->pan_next(cx / float(ctx_.W));
                    if (s.beam) {
                        // charged beam destruction = spectacular (once per pass)
                        if (!big_played) { ctx_.sfx->big_explosion(); big_played = true; }
                        else ctx_.sfx->boss_explosion();
                    } else if (e->is_heavy()) {
                        ctx_.sfx->boss_explosion();
                    } else {
                        ctx_.sfx->invader_hit();
                    }
                }
                if (ctx_.fx) ctx_.fx->shake(s.beam ? 9 : e->is_heavy() ? 6 : 2);
                if (e->carrier) spawn_powerup(float(cx), float(cy), true);
                else roll_drop(float(cx), float(cy), e->is_heavy());
            } else {
                particles_.explode_invader(cx, cy, e->color, 6);
                if (ctx_.sfx) {
                    ctx_.sfx->pan_next(cx / float(ctx_.W));
                    s.beam ? ctx_.sfx->zap() : ctx_.sfx->bunker_hit();
                }
            }
            // pierce: beams pass through; normal shots stop unless they have pierce
            if (s.pierce > 0) { if (!s.beam) s.pierce--; }
            else { s.alive = false; break; }
        }
        if (!s.alive) continue;
        // boss
        if (boss_ && boss_->damageable() && overlap(sr, boss_->rect())) {
            bool weak = overlap(sr, boss_->weak_rect()) && boss_->weak_vulnerable();
            boss_->take_damage(s.damage, particles_, weak);
            score_ += 4 * s.damage;
            auto [tx, ty] = std::pair<int,int>{int(s.x), int(s.y)};
            particles_.explode_invader(tx, ty, weak ? YELLOW : boss_->color, weak ? 10 : 6);
            if (ctx_.sfx) ctx_.sfx->invader_hit();
            if (ctx_.fx) ctx_.fx->shake(weak ? 5 : 2);
            if (!s.beam) s.alive = false;  // beams pierce the boss too
        }
        if (!s.alive) continue;
        // terrain stops non-beam shots; beams burn through
        if (!s.beam && terrain_->wall_blocks(sr)) {
            particles_.bunker_hit(s.x, s.y, 4);
            s.alive = false;
        }
    }
}

void VoidStormGame::check_enemy_shots() {
    bool safe = player_->safe();
    for (auto& s : enemy_shots_) {
        if (!s.alive) continue;
        if (player_->alive && !safe && overlap(s.rect(), player_->rect())) {
            s.alive = false;
            if (player_->take_hit()) {  // shield soaked it
                if (ctx_.sfx) {
                    ctx_.sfx->pan_next(std::clamp((player_->x + player_->w / 2) / float(ctx_.W), 0.0f, 1.0f));
                    ctx_.sfx->shield_hit();
                }
                if (ctx_.fx) ctx_.fx->rumble_hit();
                add_flash("SHIELD!", player_->x + player_->w / 2, player_->y - 24, GREEN, 0.7f);
            } else {
                player_die();
            }
            return;
        }
        // enemy shots are stopped by terrain
        if (terrain_->wall_blocks(s.rect())) { s.alive = false; }
    }
}

void VoidStormGame::player_die() {
    charge_loop_stop();   // dying mid-charge must silence the hum immediately
    auto [cx, cy] = player_->center();
    particles_.explode_player(cx, cy);
    bool had = player_->spread_ || player_->speed_ || player_->shield_hp_;
    if (had) add_flash("WEAPONS LOST!", cx, cy - 40, RED, 1.6f, true);
    player_->reset_on_death();
    player_->kill();
    lives_--;
    state_ = St::Dead; dead_t_ = 2.0f;
    if (ctx_.combo) ctx_.combo->reset();
    enemy_shots_.clear();
    tele(J().add("ev", "death").add("stage", stage_).add("t", (double)game_t_)
            .add("load", loadout()).add("lives", lives_));
    if (ctx_.sfx) ctx_.sfx->player_death();
    if (ctx_.fx) { ctx_.fx->shake(16); ctx_.fx->hitstop(0.09f); ctx_.fx->rumble_death(); }
}

// ── drawing ──────────────────────────────────────────────────────────────────
void VoidStormGame::draw(Surface& s) {
    s.fill(Color(8, 5, 12));
    if (parallax_) parallax_->draw(s);
    particles_.draw(s);
    terrain_->draw(s);
    for (auto& e : enemies_) e->draw(s);
    if (boss_) {
        auto [pcx, pcy] = player_->center();
        boss_->draw(s, float(pcx), float(pcy));
    }
    for (auto& sh : enemy_shots_) sh.draw(s);
    for (auto& p : powerups_) p.draw(s, *ctx_.fonts.small);
    player_->draw(s, particles_);
    for (auto& sh : player_shots_) sh.draw(s);

    draw_hud(s);
    draw_flash(s);

    if (state_ == St::Intro) {
        float a = std::min(1.0f, intro_t_ * 1.5f);
        draw_text_centered(s, *ctx_.fonts.big,
                           "STAGE " + std::to_string(stage_), ctx_.W / 2, ctx_.H / 2 - 24,
                           Color(int(80 + 175 * a), int(150 + 60 * a), 255));
        draw_text_centered(s, *ctx_.fonts.med, STAGE_NAMES[std::min(stage_ - 1, 4) % 5],
                           ctx_.W / 2, ctx_.H / 2 + 26, ORANGE);
        draw_text_centered(s, *ctx_.fonts.small, "HOLD  SPACE  TO CHARGE BEAM",
                           ctx_.W / 2, ctx_.H / 2 + 64, DIM);
    }

    if (state_ == St::GameOver) {
        draw_text_centered(s, *ctx_.fonts.big, "GAME  OVER", ctx_.W / 2, ctx_.H / 2 - 50, RED);
        draw_text_centered(s, *ctx_.fonts.med, "SCORE  " + zpad(score_, 6), ctx_.W / 2, ctx_.H / 2 + 10, WHITE);
        draw_text_centered(s, *ctx_.fonts.small, "PRESS SPACE TO RETRY", ctx_.W / 2, ctx_.H / 2 + 60, DIM);
        if (initials_.active()) initials_.draw(s);
    }
    pause_.draw(s);
}

void VoidStormGame::draw_hud(Surface& s) {
    draw_text(s, *ctx_.fonts.med, "SCORE  " + zpad(score_, 6), 16, 8, GREEN);
    draw_text_centered(s, *ctx_.fonts.med, "HI  " + zpad(high_score_, 6), ctx_.W / 2, 8, YELLOW);
    draw_text_right(s, *ctx_.fonts.med, "STAGE  " + std::to_string(stage_), ctx_.W - 16, 8, CYAN);

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
        int bw = 420, bx = ctx_.W / 2 - bw / 2, by = 40;
        draw_text_centered(s, *ctx_.fonts.small, boss_->name, ctx_.W / 2, by - 16, boss_->color);
        draw::rect(s, Color(20, 20, 30), {bx, by, bw, 12}, 0, 4);
        int fillw = int(bw * boss_->hp_ratio());
        Color hc = boss_->hp_ratio() > 0.5f ? GREEN : boss_->hp_ratio() > 0.25f ? YELLOW : RED;
        draw::rect(s, hc, {bx, by, fillw, 12}, 0, 4);
        draw::rect(s, boss_->color, {bx, by, bw, 12}, 1, 4);
    }

    // charge meter (bottom-centre)
    if (state_ == St::Play && (player_->charging_ || player_->charge_ > 0.01f)) {
        int bw = 160, bx = ctx_.W / 2 - bw / 2, by = ctx_.H - 24;
        draw::rect(s, Color(20, 30, 40), {bx, by, bw, 8}, 0, 4);
        Color cc = player_->charge_ >= 1.0f ? WHITE : lerp_color(CYAN, GOLD, player_->charge_);
        draw::rect(s, cc, {bx, by, int(bw * player_->charge_), 8}, 0, 4);
        draw_text_centered(s, *ctx_.fonts.small, "CHARGE", ctx_.W / 2, by - 14, DIM);
    }

    // weapon pips (bottom-left)
    int hx = 16, hy = ctx_.H - 46;
    draw_text(s, *ctx_.fonts.small, "PWR", hx, hy, DIM);
    auto pip = [&](int n, Color c, int slot) {
        for (int i = 0; i < 3; ++i)
            draw::rect(s, i < n ? c : Color(30, 40, 30), {hx + 44 + slot * 16, hy + 11 - i * 4, 12, 3});
    };
    pip(player_->spread_, ORANGE, 0);
    pip(player_->speed_, CYAN, 1);
    pip((int)player_->options_.size(), GOLD, 2);

    // lives
    draw_text(s, *ctx_.fonts.small, "SHIPS", 16, ctx_.H - 28, DIM);
    for (int i = 0; i < lives_; ++i)
        draw::polygon(s, CYAN, {{84 + i * 22, ctx_.H - 22}, {84 + i * 22 + 14, ctx_.H - 16},
                                {84 + i * 22, ctx_.H - 10}});

    // shield bar (bottom-right)
    if (player_->shield_hp_ > 0)
        draw_text_right(s, *ctx_.fonts.small, "SHIELD x" + std::to_string(player_->shield_hp_),
                        ctx_.W - 12, ctx_.H - 28, GREEN);
    else
        draw_text_right(s, *ctx_.fonts.small, "P PAUSE", ctx_.W - 12, ctx_.H - 28, Color(30, 60, 30));
}

void VoidStormGame::draw_flash(Surface& s) {
    for (auto& f : flash_) {
        Color c = f.color;
        float a = std::max(0.0f, std::min(1.0f, f.life / std::max(0.1f, f.max_life)));
        c.a = uint8_t(255 * a);
        Font& font = f.big ? *ctx_.fonts.med : *ctx_.fonts.small;
        draw_text_centered(s, font, f.text, int(f.x), int(f.y), c);
    }
}

}  // namespace

std::unique_ptr<Scene> make_void_storm(const Context& ctx) {
    return std::make_unique<VoidStormGame>(ctx);
}
