// void_raid.cpp — Void Raid.
// A progression scroller: stacking power-ups build toward "massive destruction",
// armored swarms get meaner each zone, energy/fuel tension stays central, and
// every few zones a big, fat, blocky, googly-eyed boss BLOCKS the rift and does
// something silly. NUKE (B) is the great equaliser. Death resets weapons (keeps
// nukes); pierce only carries through enemies it one-shot KILLS.
#include "void_raid.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "engine/initials.h"
#include "engine/boss_art.h"
#include "engine/combo.h"
#include "engine/enemy_art.h"
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
const Color GREEN(80, 255, 120), CYAN(80, 200, 255), YELLOW(255, 220, 40);
const Color RED(255, 60, 60), ORANGE(255, 140, 40), DIM(30, 40, 60);
const Color PURPLE(200, 100, 255), STEEL(170, 185, 205);

const Color PLAYER_COLOR(80, 210, 255);
const Color SHOT_COLOR(80, 210, 255);
const Color ENEMY_SHOT_COLOR(255, 80, 40);
const Color CORE_COLOR(255, 220, 60);

const Color C_SCOUT(255, 100, 40);
const Color C_FLANKER(200, 80, 255);
const Color C_CRUISER(255, 200, 40);
const Color C_TURRET(80, 255, 120);
const Color C_DIVER(255, 70, 110);   // homing kamikaze (ported from Void Storm)

// Zone thresholds (score)
const int ZONE_SCORES[5] = {0, 3000, 7000, 13000, 22000};
const char* ZONE_NAMES[5] = {"DEEP SPACE", "ASTEROID BELT", "STATION SECTOR",
                             "NEBULA CLOUD", "VOID CORE"};
constexpr int N_ZONES = 5;

// Scroll speed: player-controlled between MIN and MAX
constexpr float SCROLL_MIN = 100.0f;
constexpr float SCROLL_MAX = 280.0f;
constexpr float SCROLL_ACCEL = 120.0f;

constexpr int CHECKPOINT_SCORE = 5000;

constexpr float PI = 3.14159265358979323846f;
constexpr float TAU = 6.28318530717958647692f;

using SL = std::vector<std::string>;

Color darker(Color c, float f) { return Color(int(c.r * f), int(c.g * f), int(c.b * f)); }

// ── helpers ───────────────────────────────────────────────────────────────────
bool overlap(Rect a, Rect b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}

std::string zpad(int v, int width) {
    bool neg = v < 0;
    std::string s = std::to_string(neg ? -v : v);
    if ((int)s.size() < width) s = std::string(width - (int)s.size(), '0') + s;
    return neg ? "-" + s : s;
}

// Soft circular glow halo (mirrors entities._glow_circle).
void glow_circle(Surface& surface, Color color, int cx, int cy, int r, int alpha) {
    for (int gr = r + 8; gr > r; gr -= 2) {
        float f = (gr - r) / 8.0f;
        int a = int(alpha * f * f * 0.5f);
        Surface gs(gr * 2, gr * 2, true);
        draw::circle(gs, Color(color.r, color.g, color.b, a), gr, gr, gr);
        surface.blit(gs, cx - gr, cy - gr);
    }
}

// Soft rounded-rect glow (mirrors entities._glow_rect).
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

// ── Parallax background ───────────────────────────────────────────────────────
// Two slow-scrolling layers of dim neon specks behind the rift so depth reads.
// The far layer drifts slowest (tiny dark dots); the near layer drifts faster
// (slightly brighter, occasional debris streaks). Both scroll a fraction of the
// rift's scroll speed and recycle off the bottom — kept deliberately subtle.
struct Parallax {
    struct Speck { float x, y; int size; Color color; bool streak; };
    int W, H;
    std::vector<Speck> far_, near_;
    Parallax(int sw, int sh) : W(sw), H(sh) {
        // far layer: many tiny, very dim blue/green dots
        for (int i = 0; i < 90; ++i) {
            int g = rnd_int(20, 45);
            far_.push_back({rnd_uniform(0, W), rnd_uniform(0, H), 1,
                            Color(g / 2, g, g + 10), false});
        }
        // near layer: fewer, a touch brighter, some short debris streaks
        for (int i = 0; i < 40; ++i) {
            bool streak = rnd01() < 0.25f;
            int b = rnd_int(40, 75);
            near_.push_back({rnd_uniform(0, W), rnd_uniform(0, H),
                             streak ? rnd_int(4, 9) : 2,
                             Color(b / 2, b, b + 15), streak});
        }
    }
    void recycle(std::vector<Speck>& layer) {
        for (auto& s : layer)
            if (s.y > H + 12) { s.y -= H + 24; s.x = rnd_uniform(0, W); }
    }
    void update(float dt, float scroll_speed) {
        // far moves at ~12% of scroll, near at ~32% — both well under the rift's.
        float vf = scroll_speed * 0.12f, vn = scroll_speed * 0.32f;
        for (auto& s : far_) s.y += vf * dt;
        for (auto& s : near_) s.y += vn * dt;
        recycle(far_);
        recycle(near_);
    }
    void draw(Surface& surface) const {
        for (auto& s : far_)
            draw::rect(surface, s.color, {int(s.x), int(s.y), s.size, s.size});
        for (auto& s : near_) {
            if (s.streak)
                draw::line(surface, s.color, int(s.x), int(s.y),
                           int(s.x), int(s.y) + s.size, 1);
            else
                draw::rect(surface, s.color, {int(s.x), int(s.y), s.size, s.size});
        }
    }
};

// ── Player ship sprite (pixel art, facing UP, 9 wide × 14 tall, scale 2) ──────
const SL PLAYER_SHAPE = {
    "....#....", "...###...", "..#####..", "..#####..", ".#######.",
    "##.###.##", "#..###..#", "#..###..#", "...###...", "..#.#.#..",
    ".##...##.", "###...###", "#.#...#.#", "...###...",
};
constexpr int PLAYER_W = 9 * 2;   // 18
constexpr int PLAYER_H = 14 * 2;  // 28

// ── Shots ─────────────────────────────────────────────────────────────────────
// Now supports angled travel (vx), piercing and per-shot damage so weapon
// power-ups can stack toward "massive destruction".
struct PlayerShot {
    static constexpr float SPEED = 580;
    int W = 3, H = 18;
    float x, y, vx = 0;
    bool alive = true;
    int pierce = 0;   // enemies it can pass THROUGH (only after a kill)
    int damage = 1;
    Color color = SHOT_COLOR;
    PlayerShot(float px, float py) : x(px), y(py) {}
    Rect rect() const { return {int(x - W / 2 - 1), int(y - H), W + 2, H}; }
    void update(float dt, int screen_w) {
        x += vx * dt;
        y -= SPEED * dt;
        if (y < -20 || x < -20 || x > screen_w + 20) alive = false;
    }
    void draw(Surface& s) const {
        if (!alive) return;
        draw_glow_bolt(s, color, int(x), int(y - H), int(y), std::max(2, W / 2));
    }
};

struct EnemyShot {
    static constexpr float SPEED = 260;
    static constexpr int R = 5;
    float x, y, vx, vy, t_ = 0;
    bool alive = true;
    Color color = ENEMY_SHOT_COLOR;
    int r = R;
    EnemyShot(float px, float py, float angle = PI / 2, float spd = SPEED)
        : x(px), y(py),
          vx(std::cos(angle) * spd), vy(std::sin(angle) * spd) {}
    Rect rect() const { return {int(x - r), int(y - r), r * 2, r * 2}; }
    void update(float dt, int screen_w, int screen_h) {
        t_ += dt;
        x += vx * dt;
        y += vy * dt;
        if (y > screen_h + 20 || y < -20 || x < -20 || x > screen_w + 20)
            alive = false;
    }
    void draw(Surface& s) const {
        if (!alive) return;
        int cx = int(x), cy = int(y);
        float pulse = 0.8f + 0.2f * std::sin(t_ * 18);
        int rr = std::max(2, int(r * pulse));
        Color col(color.r, int(color.g * pulse), int(color.b * pulse));
        glow_circle(s, col, cx, cy, rr + 2, 60);
        draw::circle(s, col, cx, cy, rr);
        draw::circle(s, Color(255, 220, 200), cx, cy, std::max(1, rr / 2));
    }
};

// ── Power-ups ────────────────────────────────────────────────────────────────
enum class PU { Rapid, Spread, Pierce, Shield, Fuel, Life, Nuke, Tank, Mystery };
struct PUInfo { const char* label; Color color; };
PUInfo pu_info(PU k) {
    switch (k) {
        case PU::Rapid:  return {"R", YELLOW};
        case PU::Spread: return {"W", ORANGE};
        case PU::Pierce: return {"P", PURPLE};
        case PU::Shield: return {"O", GREEN};
        case PU::Fuel:   return {"F", CORE_COLOR};
        case PU::Life:   return {"+1", WHITE};
        case PU::Nuke:   return {"N", RED};
        case PU::Tank:   return {"E+", CYAN};
        default:         return {"?", WHITE};
    }
}
// Power-ups drift gently downward with the scroll, but home toward the ship
// when it gets close so collection feels generous.
struct PowerUp {
    float x, y, t = 0;
    PU kind;
    bool alive = true;
    static constexpr float VY = 70;
    Rect rect() const { return {int(x - 14), int(y - 12), 28, 24}; }
    std::pair<int, int> center() const { return {int(x), int(y)}; }
    void update(float dt, float scroll_speed, int sh, float /*pcx*/, float /*pcy*/) {
        // drift down with the scroll only — fly to them; they're easy to miss now
        t += dt;
        y += (VY + scroll_speed * 0.4f) * dt;
        if (y > sh + 30) alive = false;
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
        draw::rect(s, Color(c.r, c.g, c.b, int(70 * pulse)), {int(x) - 18, int(y) - 16, 36, 32}, 0, 8);
        draw::rect(s, Color(8, 12, 18), {int(x) - 14, int(y) - 12, 28, 24}, 0, 6);
        draw::rect(s, c, {int(x) - 14, int(y) - 12, 28, 24}, 2, 6);
        draw_text_centered(s, f, in.label, int(x), int(y) - 9, c);
    }
};

// ── TerrainObstacle ───────────────────────────────────────────────────────────
struct TerrainObstacle {
    Rect rect;
    std::string kind;  // "station" | "asteroid"
    float y;
    bool alive = true;
    bool registered = false;
    int id = 0;  // stable id: referenced by turrets instead of a raw pointer
    std::vector<Rect> gap_rects;

    TerrainObstacle(Rect r, std::string k, int id_)
        : rect(r), kind(std::move(k)), y(float(r.y)), id(id_) {}

    void scroll(float dy) {
        y += dy;
        rect.y = int(y);
        for (auto& g : gap_rects) g.y = rect.y;
    }
    void open_gap(int cx, int gap_w = 40) {
        Rect& r = rect;
        int gx = std::max(r.x, std::min(r.x + r.w - gap_w, cx - gap_w / 2));
        gap_rects.push_back({gx, r.y, gap_w, r.h});
    }
    bool blocks(Rect pr) const {
        if (!overlap(rect, pr)) return false;
        for (auto& g : gap_rects)
            if (pr.x >= g.x && pr.x + pr.w <= g.x + g.w) return false;
        return true;
    }
    bool off_screen() const { return rect.y > 1400; }
};

// ── Terrain ───────────────────────────────────────────────────────────────────
const Color WALL_COLORS[5][3] = {
    {Color(20, 40, 80), Color(30, 60, 120), Color(15, 30, 60)},
    {Color(80, 55, 25), Color(110, 75, 35), Color(60, 40, 15)},
    {Color(50, 50, 70), Color(70, 70, 100), Color(35, 35, 55)},
    {Color(80, 20, 80), Color(110, 40, 110), Color(55, 15, 55)},
    {Color(100, 20, 20), Color(140, 40, 40), Color(70, 10, 10)},
};
const Color WALL_GLOW_COLORS[5] = {
    Color(60, 140, 255), Color(255, 140, 40), Color(140, 100, 255),
    Color(255, 80, 200), Color(255, 60, 60),
};
constexpr int SLICE_H = 8;
constexpr float MIN_RIFT_W = 0.30f;
constexpr float MAX_RIFT_W = 0.82f;
const Color STATION_COLOR(80, 80, 110), STATION_GLOW(120, 120, 180);
const Color ASTEROID_COLOR(90, 70, 50), ASTEROID_GLOW(180, 120, 60);
constexpr int STATION_H = 18;

struct Slice {
    int left_w, right_w;
    float y;
    int tex = 0;  // stable per-slice texture id so terrain colour scrolls WITH it
    // Branching rift: when div_w > 0 a central divider wall splits the rift into
    // a LEFT and a RIGHT channel for this slice. div_x is its left edge (px).
    int div_x = 0, div_w = 0;
};

struct Terrain {
    int W, H;
    std::vector<Slice> slices;
    std::vector<TerrainObstacle> obstacles;
    int zone = 0;
    float rift_cx = 0.5f, rift_w = 0.65f;
    float target_cx = 0.5f, target_w = 0.65f;
    int choke_cd = 180, station_cd = 120, asteroid_cd = 80;
    int choke_rem = 0;
    int slice_seq = 0, obs_seq = 1;
    float normal_w = 0.65f;

    // ── Branching rift ────────────────────────────────────────────────────────
    // Every so often the rift FORKS: a chunky central divider splits the channel
    // into LEFT (safe, wide, sparse) and RIGHT (gauntlet, narrow, turret-heavy)
    // for a stretch, then merges back. The state machine below is driven slice by
    // slice in append_slice(); the game reads it to telegraph, bias spawns toward
    // the right channel, and award a bonus for braving the gauntlet.
    enum class Fork { None, Telegraph, Open, Merging };
    Fork fork_state = Fork::None;
    int fork_cd = 900;        // slices until the next fork is allowed (~ once/zone)
    int fork_rem = 0;         // slices remaining in the current fork phase
    int fork_tele_rem = 0;    // telegraph slices left before the split appears
    float fork_div_cx = 0.5f; // centre of the divider, as a fraction of width
    float fork_div_frac = 0;  // current divider width as a fraction (eases in/out)
    bool fork_pending_hint = false;  // one-shot: game shows the "<< SAFE RISKY >>" hint
    bool fork_just_opened = false;   // one-shot: the split has just materialised
    bool fork_just_closed = false;   // one-shot: the channels have merged back
    bool forking() const { return fork_state != Fork::None; }
    // The divider's screen-space x-range at a given y (only valid while forked).
    bool divider_at(float y, int& dx, int& dw) const {
        const Slice& s = nearest_slice(y);
        if (s.div_w <= 0) return false;
        dx = s.div_x; dw = s.div_w; return true;
    }

    TerrainObstacle* find_obstacle(int id) {
        for (auto& o : obstacles) if (o.id == id) return &o;
        return nullptr;
    }
    // As zones advance the rift tightens: the baseline width shrinks.
    float zone_width_scale = 1.0f;
    bool blocked = false;  // boss in the rift: stop spawning new terrain features

    // Boss "rift crush": when > 0 the boss is squeezing the walls inward. The
    // value is the target rift fraction the walls march toward; 0 means "off"
    // and the rift relaxes back to its normal width. Set by the Boss each frame.
    float boss_squeeze = 0.0f;

    Terrain(int sw, int sh) : W(sw), H(sh) {
        for (int i = 0; i < H / SLICE_H + 4; ++i) append_slice(float(i * SLICE_H));
    }

    void update(float dt, float scroll_speed, int z) {
        zone = z;
        zone_width_scale = std::max(0.72f, 1.0f - z * 0.06f);
        float dy = scroll_speed * dt;
        for (auto& s : slices) s.y += dy;
        for (auto& o : obstacles) o.scroll(dy);

        slices.erase(std::remove_if(slices.begin(), slices.end(),
                     [&](const Slice& s) { return !(s.y < H + SLICE_H * 2); }),
                     slices.end());
        obstacles.erase(std::remove_if(obstacles.begin(), obstacles.end(),
                        [](const TerrainObstacle& o) { return o.off_screen(); }),
                        obstacles.end());

        float top = 0;
        if (!slices.empty()) {
            top = slices[0].y;
            for (auto& s : slices) top = std::min(top, s.y);
        }
        while (top > -SLICE_H) {
            top -= SLICE_H;
            append_slice(top);
        }
    }

    const Slice& nearest_slice(float y) const {
        const Slice* best = &slices[0];
        float best_d = std::abs(best->y - y);
        for (auto& s : slices) {
            float d = std::abs(s.y - y);
            if (d < best_d) { best_d = d; best = &s; }
        }
        return *best;
    }
    int left_wall_w(float y) const { return nearest_slice(y).left_w; }
    int right_wall_w(float y) const { return nearest_slice(y).right_w; }

    std::vector<Rect> wall_rects_near(Rect rect) const {
        std::vector<Rect> rects;
        for (auto& s : slices) {
            if (s.y + SLICE_H < rect.y - 4) continue;
            if (s.y > rect.y + rect.h + 4) continue;
            if (s.left_w > 0) rects.push_back({0, int(s.y), s.left_w, SLICE_H});
            if (s.right_w > 0)
                rects.push_back({W - s.right_w, int(s.y), s.right_w, SLICE_H});
            if (s.div_w > 0)  // central divider behaves like a wall
                rects.push_back({s.div_x, int(s.y), s.div_w, SLICE_H});
        }
        return rects;
    }

    void append_slice(float y) {
        // Boss rift-crush: the walls march toward boss_squeeze and the centre
        // re-centres so the squeeze is symmetric and readable. Skip the random
        // wander / choke logic while the boss owns the walls.
        if (boss_squeeze > 0.0f) {
            target_w = boss_squeeze;
            target_cx = 0.5f;
            // crush faster than the normal lerp so the squeeze reads as a threat
            rift_cx += (target_cx - rift_cx) * 0.10f;
            rift_w += (target_w - rift_w) * 0.10f;
            float rift_px = rift_w * W;
            float cx_px = rift_cx * W;
            int lw = std::max(20, int(cx_px - rift_px / 2));
            int rw = std::max(20, int(W - cx_px - rift_px / 2));
            slices.insert(slices.begin(), Slice{lw, rw, y, slice_seq++});
            return;  // boss owns the rift; no fresh terrain features either
        }

        // ── Branching rift state machine ──────────────────────────────────────
        // Only run forks during normal terrain (never mid-boss, never mid-choke).
        if (!blocked) step_fork();

        rift_cx += (target_cx - rift_cx) * 0.08f;
        rift_w += (target_w - rift_w) * 0.06f;

        // While a fork is active we steer the rift wide and centred so two real
        // channels exist either side of the divider; skip the random wander/choke.
        if (forking()) {
            target_cx = 0.5f;
            target_w = std::min(MAX_RIFT_W, 0.74f * zone_width_scale);
            // ease toward the wide target a touch faster so the split reads clean
            rift_cx += (target_cx - rift_cx) * 0.06f;
            rift_w += (target_w - rift_w) * 0.06f;
        } else {
            if (rnd01() < 0.04f) target_cx = rnd_uniform(0.25f, 0.75f);
            if (rnd01() < 0.03f) target_w = rnd_uniform(MIN_RIFT_W, MAX_RIFT_W) * zone_width_scale;
        }

        if (forking()) {
            // forks own the rift width; chokes pause until the channels merge.
        } else if (choke_rem > 0) {
            choke_rem -= 1;
            rift_w = std::max(MIN_RIFT_W, rift_w - 0.002f);
            if (choke_rem == 0) target_w = normal_w;
        } else {
            choke_cd -= 1;
            if (choke_cd <= 0) {
                normal_w = rift_w;
                choke_rem = rnd_int(40, 80);
                choke_cd = rnd_int(100, 200);
                target_w = MIN_RIFT_W + 0.05f;
            }
        }

        float rift_px = rift_w * W;
        float cx_px = rift_cx * W;
        int left_w = std::max(20, int(cx_px - rift_px / 2));
        int right_w = std::max(20, int(W - cx_px - rift_px / 2));

        if (W - left_w - right_w < W * MIN_RIFT_W) {
            int excess = int(W * MIN_RIFT_W - (W - left_w - right_w));
            left_w = std::max(20, left_w - excess / 2);
            right_w = std::max(20, right_w - excess / 2);
        }

        Slice sl{left_w, right_w, y, slice_seq++};

        // Branching rift: carve a central divider out of the open rift. The
        // channels live either side of it. Width eases in/out via fork_div_frac.
        int div_x = 0, div_w = 0;
        if (fork_div_frac > 0.01f) {
            int rift_x = left_w;
            int rift_w_px = W - left_w - right_w;
            div_w = std::max(8, int(rift_w_px * fork_div_frac));
            int cx_px = int(fork_div_cx * W);
            div_x = std::max(rift_x + 24, std::min(rift_x + rift_w_px - 24 - div_w,
                                                   cx_px - div_w / 2));
            sl.div_x = div_x; sl.div_w = div_w;
        }
        slices.insert(slices.begin(), sl);

        if (blocked) return;  // boss owns the rift; no fresh stations/asteroids

        // While forked, channel boundaries are: left channel [left_w, div_x],
        // right channel [div_x+div_w, W-right_w]. Bias hazards toward the RIGHT
        // (gauntlet); keep the LEFT (safe) channel clean.
        if (div_w > 0) {
            int right_cl_x = div_x + div_w;
            int right_cl_w = (W - right_w) - right_cl_x;
            // turret stations and asteroids cluster in the right channel only
            station_cd -= 1;
            asteroid_cd -= 1;
            if (station_cd <= 0 && right_cl_w > 90) {
                station_cd = rnd_int(70, 120);   // denser than normal
                int sw = std::max(60, int(right_cl_w * rnd_uniform(0.5f, 0.85f)));
                int sx = right_cl_x + rnd_int(0, std::max(1, right_cl_w - sw));
                obstacles.emplace_back(Rect{sx, int(y) - STATION_H, sw, STATION_H},
                                       "station", obs_seq++);
            } else if (asteroid_cd <= 0 && right_cl_w > 60) {
                asteroid_cd = rnd_int(40, 80);
                int aw = rnd_int(18, 36), ah = rnd_int(14, 30);
                int ax = right_cl_x + rnd_int(0, std::max(1, right_cl_w - aw));
                obstacles.emplace_back(Rect{ax, int(y) - ah, aw, ah}, "asteroid", obs_seq++);
            }
            return;
        }

        station_cd -= 1;
        asteroid_cd -= 1;
        if (station_cd <= 0) {
            station_cd = rnd_int(90, 160);
            try_spawn_station(left_w, right_w, y);
        } else if (asteroid_cd <= 0) {
            asteroid_cd = rnd_int(50, 100);
            try_spawn_asteroid(left_w, right_w, y);
        }
    }

    // ── Branching rift: advance the fork state machine one slice ───────────────
    void step_fork() {
        fork_pending_hint = false;
        fork_just_opened = false;
        fork_just_closed = false;
        switch (fork_state) {
            case Fork::None:
                if (--fork_cd <= 0 && zone >= 1) {
                    fork_state = Fork::Telegraph;
                    fork_tele_rem = 46;     // ~ a couple seconds of warning
                    fork_pending_hint = true;
                    // pick which side is the divider's centre; the rift is wide
                    // and centred, so a near-centre divider yields two real lanes.
                    fork_div_cx = rnd_uniform(0.46f, 0.54f);
                }
                break;
            case Fork::Telegraph:
                if (--fork_tele_rem <= 0) {
                    fork_state = Fork::Open;
                    fork_rem = rnd_int(130, 190);  // length of the gauntlet stretch
                    fork_just_opened = true;
                }
                break;
            case Fork::Open:
                // ease the divider open, then hold it full
                fork_div_frac = std::min(0.18f, fork_div_frac + 0.012f);
                if (--fork_rem <= 0) fork_state = Fork::Merging;
                break;
            case Fork::Merging:
                fork_div_frac = std::max(0.0f, fork_div_frac - 0.012f);
                if (fork_div_frac <= 0.01f) {
                    fork_div_frac = 0;
                    fork_state = Fork::None;
                    fork_just_closed = true;
                    fork_cd = rnd_int(820, 1100);  // ~ once per zone-ish cadence
                }
                break;
        }
    }

    void try_spawn_station(int left_w, int right_w, float y) {
        int rift_x = left_w;
        int rift_w_px = W - left_w - right_w;
        if (rift_w_px < 160) return;
        int margin = int(rift_w_px * 0.15f);
        int sw = int(rift_w_px * rnd_uniform(0.35f, 0.65f));
        int sx = rift_x + margin + rnd_int(0, rift_w_px - sw - margin * 2);
        obstacles.emplace_back(Rect{sx, int(y) - STATION_H, sw, STATION_H},
                               "station", obs_seq++);
    }

    void try_spawn_asteroid(int left_w, int right_w, float y) {
        int rift_x = left_w;
        int rift_w_px = W - left_w - right_w;
        if (rift_w_px < 120) return;
        int count = rnd_int(1, 3);
        for (int i = 0; i < count; ++i) {
            int aw = rnd_int(18, 40);
            int ah = rnd_int(14, 32);
            int ax = rift_x + rnd_int(20, std::max(21, rift_w_px - aw - 20));
            obstacles.emplace_back(Rect{ax, int(y) - ah, aw, ah}, "asteroid", obs_seq++);
        }
    }

    void draw(Surface& surface) const {
        int zi = std::min(zone, 4);
        const Color* colors = WALL_COLORS[zi];
        Color glow_c = WALL_GLOW_COLORS[zi];

        for (auto& s : slices) {
            int sy = int(s.y);
            if (s.left_w > 0) {
                int lw = s.left_w;
                Color c = colors[(s.tex / 4) % 3];  // chunky strata, bound to the slice
                draw::rect(surface, c, {0, sy, lw, SLICE_H + 1});
                draw::rect(surface, glow_c, {lw - 3, sy, 3, SLICE_H + 1});
            }
            if (s.right_w > 0) {
                int rw = s.right_w;
                int rx = W - rw;
                Color c = colors[((s.tex + 6) / 4) % 3];
                draw::rect(surface, c, {rx, sy, rw, SLICE_H + 1});
                draw::rect(surface, glow_c, {rx, sy, 3, SLICE_H + 1});
            }
            if (s.div_w > 0) {  // central divider — chunky strata, glow both edges
                Color c = colors[((s.tex + 3) / 4) % 3];
                draw::rect(surface, c, {s.div_x, sy, s.div_w, SLICE_H + 1});
                draw::rect(surface, glow_c, {s.div_x, sy, 3, SLICE_H + 1});
                draw::rect(surface, glow_c, {s.div_x + s.div_w - 3, sy, 3, SLICE_H + 1});
            }
        }

        for (auto& obs : obstacles) {
            Rect r = obs.rect;
            if (obs.kind == "station") {
                if (obs.gap_rects.empty()) {
                    draw::rect(surface, STATION_COLOR, r);
                    draw::rect(surface, STATION_GLOW, {r.x, r.y, r.w, 3});
                    draw::rect(surface, STATION_GLOW, {r.x, r.y + r.h - 3, r.w, 3});
                } else {
                    std::vector<std::pair<int, int>> gap_xs;
                    for (auto& g : obs.gap_rects)
                        gap_xs.emplace_back(g.x, g.x + g.w);
                    std::sort(gap_xs.begin(), gap_xs.end());
                    std::vector<std::pair<int, int>> spans;
                    int cursor = r.x;
                    for (auto& gp : gap_xs) {
                        if (cursor < gp.first) spans.emplace_back(cursor, gp.first);
                        cursor = gp.second;
                    }
                    if (cursor < r.x + r.w) spans.emplace_back(cursor, r.x + r.w);
                    for (auto& sp : spans) {
                        int sw = sp.second - sp.first;
                        draw::rect(surface, STATION_COLOR, {sp.first, r.y, sw, r.h});
                        draw::rect(surface, STATION_GLOW, {sp.first, r.y, sw, 3});
                        draw::rect(surface, STATION_GLOW,
                                   {sp.first, r.y + r.h - 3, sw, 3});
                    }
                    for (auto& g : obs.gap_rects) {
                        draw::line(surface, ORANGE, g.x, r.y, g.x, r.y + r.h, 2);
                        draw::line(surface, ORANGE, g.x + g.w, r.y,
                                   g.x + g.w, r.y + r.h, 2);
                    }
                }
            } else {  // asteroid
                draw::rect(surface, ASTEROID_COLOR, r, 0, 4);
                draw::rect(surface, ASTEROID_GLOW, r, 2, 4);
            }
        }
    }
};

// ── Enemy sprite art (pixel-art ships, ported in spirit from Void Storm) ───────
// Void Raid descends, so every craft faces DOWN (nose at the bottom). '@' cells
// are the hot cockpit/core highlight; '#' the body colour; '.' transparent.
const SL RAID_SCOUT_F0 = {
    "...###...", "..#@@@#..", ".##@@@##.", "#########",
    "#########", ".#######.", "..#####..", "...###...", "....#....",
};
const SL RAID_SCOUT_F1 = {
    ".#.#.#.#.", "..#@@@#..", ".##@@@##.", "#########",
    "#########", ".#######.", "..#####..", "...###...", "....#....",
};
// Flanker — a purple manta sweeper with broad flapping wings.
const SL RAID_FLANKER_F0 = {
    "###.......###", ".###.....###.", "..####.####..", "...#######...",
    "...#@@@@@#...", "....#####....", ".....###.....", "......#......",
};
const SL RAID_FLANKER_F1 = {
    ".###.....###.", "..###...###..", "...#######...", "...#######...",
    "...#@@@@@#...", "....#####....", ".....###.....", "......#......",
};
// Cruiser — a bulky gold gunship with twin cannon pods.
const SL RAID_CRUISER_F0 = {
    "###.........###", "###############", "##@@@#####@@@##", "###############",
    ".#############.", ".#############.", "..###########..", "...#########...",
    "....#######....",
};
const SL RAID_CRUISER_F1 = {
    "###.........###", "###############", "##@@@#####@@@##", "###############",
    ".#############.", ".#############.", "..###########..", "...#########...",
    ".....#####.....",
};
// Diver — a hot-red dart/raptor: rear fins, a glowing core, a sharp nose.
const SL RAID_DIVER_F0 = {
    "#.......#", "##.....##", ".#######.", ".#######.", ".#@@@@@#.",
    "..#####..", "...###...", "...###...", "....#....",
};
const SL RAID_DIVER_F1 = {
    ".#.....#.", "##.....##", ".#######.", ".#######.", ".#@@@@@#.",
    "..#####..", "...###...", "...###...", "....#....",
};
// Turret — an armoured gun-pod dome; the swivelling barrel is drawn in code.
const SL RAID_TURRET_BASE = {
    "..######..", ".########.", "##@@@@@@##", "##@@@@@@##", "##########", "##########",
};
const EnemyArt& raid_scout_art()   { static EnemyArt a = make_enemy_art(RAID_SCOUT_F0, RAID_SCOUT_F1, C_SCOUT, 2); return a; }
const EnemyArt& raid_flanker_art() { static EnemyArt a = make_enemy_art(RAID_FLANKER_F0, RAID_FLANKER_F1, C_FLANKER, 2); return a; }
const EnemyArt& raid_cruiser_art() { static EnemyArt a = make_enemy_art(RAID_CRUISER_F0, RAID_CRUISER_F1, C_CRUISER, 3, 7); return a; }
const EnemyArt& raid_cruiser_armored_art() { static EnemyArt a = make_enemy_art(RAID_CRUISER_F0, RAID_CRUISER_F1, STEEL, 3, 7); return a; }
const EnemyArt& raid_diver_art()   { static EnemyArt a = make_enemy_art(RAID_DIVER_F0, RAID_DIVER_F1, C_DIVER, 2, 7, 0.9f); return a; }
const EnemyArt& raid_turret_art()  { static EnemyArt a = make_enemy_art(RAID_TURRET_BASE, RAID_TURRET_BASE, C_TURRET, 3); return a; }

// ── Enemies ───────────────────────────────────────────────────────────────────
struct Enemy {
    float x, y;
    bool alive = true;
    int hp = 1;
    Color color;
    int pts = 0;
    int max_hp = 1;
    float hit_flash = 0;
    bool armored = false;  // tougher, sparks white when chipped
    virtual ~Enemy() = default;
    virtual Rect rect() const = 0;
    virtual std::pair<int, int> center() const = 0;
    virtual void update(float dt, float scroll_speed,
                        struct Player& player, std::vector<EnemyShot>& shots) = 0;
    virtual void draw(Surface& s) const = 0;
    // Returns true if THIS hit killed the enemy (so pierce only carries on kills).
    bool hit(int dmg) {
        hp -= dmg;
        hit_flash = 1.0f;
        if (hp <= 0) { alive = false; return true; }
        return false;
    }
    void tick_flash(float dt) { if (hit_flash > 0) hit_flash -= dt * 5; }
    // Turret hook: override returns the station this turret sits on.
    virtual int station_id() const { return -1; }  // turret's platform, by stable id
    virtual bool is_heavy() const { return max_hp > 2; }
    // Kamikaze hook: ramming enemies damage the player on body contact.
    virtual bool rams() const { return false; }
};

struct Player;  // fwd

struct Scout : Enemy {
    static constexpr int W = 22, H = 18;
    static constexpr float SPEED = 160.0f;
    float t_ = 0, fire_cd;
    float bx_ = 0, amp_ = 0, freq_ = 0;   // sine weave so it isn't a straight-liner
    Scout(float px, float py) {
        x = px; y = py; hp = 1; max_hp = 1; pts = 100;
        color = C_SCOUT;
        fire_cd = rnd_uniform(0.5f, 2.0f);
        bx_ = px; amp_ = rnd_uniform(18.0f, 40.0f); freq_ = rnd_uniform(1.8f, 3.0f);
    }
    Rect rect() const override { return {int(x), int(y), W, H}; }
    std::pair<int, int> center() const override {
        return {int(x + W / 2.0f), int(y + H / 2.0f)};
    }
    void update(float dt, float scroll_speed, Player& player,
                std::vector<EnemyShot>& shots) override;
    void draw(Surface& s) const override {
        auto [cx, cy] = center();
        blit_enemy(s, raid_scout_art(), int(t_ * 16) & 1, cx, cy, hit_flash);
    }
};

struct Flanker : Enemy {
    static constexpr int W = 26, H = 14;
    int direction;
    float sweep_vx, fire_cd, t_ = 0;
    Flanker(float px, float py, int dir) {
        x = px; y = py; hp = 1; max_hp = 1; pts = 150;
        color = C_FLANKER;
        direction = dir;
        sweep_vx = 140.0f * dir;
        fire_cd = rnd_uniform(0.3f, 1.5f);
    }
    Rect rect() const override { return {int(x), int(y), W, H}; }
    std::pair<int, int> center() const override {
        return {int(x + W / 2.0f), int(y + H / 2.0f)};
    }
    void update(float dt, float scroll_speed, Player& player,
                std::vector<EnemyShot>& shots) override;
    void draw(Surface& s) const override {
        auto [cx, cy] = center();
        int frame = std::sin(t_ * 9) > 0 ? 0 : 1;   // wing flap
        blit_enemy(s, raid_flanker_art(), frame, cx, cy, hit_flash);
    }
};

struct Cruiser : Enemy {
    static constexpr int W = 46, H = 22;
    static constexpr float SPEED = 55.0f;
    float fire_cd, t_ = 0;
    Cruiser(float px, float py) {
        x = px; y = py; hp = 3; max_hp = 3; pts = 500;
        color = C_CRUISER;
        fire_cd = rnd_uniform(1.5f, 3.0f);
    }
    Rect rect() const override { return {int(x), int(y), W, H}; }
    std::pair<int, int> center() const override {
        return {int(x + W / 2.0f), int(y + H / 2.0f)};
    }
    void update(float dt, float scroll_speed, Player& player,
                std::vector<EnemyShot>& shots) override;
    void draw(Surface& s) const override {
        Rect r = rect();
        auto [cx, cy] = center();
        blit_enemy(s, armored ? raid_cruiser_armored_art() : raid_cruiser_art(),
                   int(t_ * 8) & 1, cx, cy, hit_flash);
        int pips = std::min(hp, 6);   // damage pips along the hull
        for (int i = 0; i < pips; ++i)
            draw::circle(s, WHITE, r.x + 10 + i * 8, cy, 2);
    }
};

struct Turret : Enemy {
    static constexpr int R = 12;
    float angle = PI / 2, fire_cd;
    int station_id_ = -1;
    Turret(float px, float py, int sid) {
        x = px; y = py; hp = 2; max_hp = 2; pts = 200;
        color = C_TURRET;
        fire_cd = rnd_uniform(1.0f, 2.5f);
        station_id_ = sid;
    }
    int station_id() const override { return station_id_; }
    Rect rect() const override {
        // forgiving hitbox, larger than the R=12 visual so turrets are hittable
        constexpr int HIT = 19;
        return {int(x - HIT), int(y - HIT), HIT * 2, HIT * 2};
    }
    std::pair<int, int> center() const override { return {int(x), int(y)}; }
    bool is_heavy() const override { return true; }  // turrets always pop big
    void update(float dt, float scroll_speed, Player& player,
                std::vector<EnemyShot>& shots) override;
    void draw(Surface& s) const override {
        int cx = int(x), cy = int(y);
        // swivelling barrel (tracks the player), drawn under the dome
        int bx = cx + int(std::cos(angle) * (R + 8));
        int by = cy + int(std::sin(angle) * (R + 8));
        draw::line(s, STEEL, cx, cy, bx, by, 4);
        glow_circle(s, RED, bx, by, 3, 70);   // hot muzzle
        draw::circle(s, Color(255, 200, 120), bx, by, 2);
        // armoured gun-pod dome (sprite)
        blit_enemy(s, raid_turret_art(), 0, cx, cy, hit_flash);
        for (int i = 0; i < hp; ++i)
            draw::circle(s, Color(255, 255, 0), cx - (hp - 1) * 3 + i * 6, cy - R - 6, 2);
    }
};

// Diver — a homing kamikaze (ported from Void Storm). Drifts in, then LOCKS a
// commit trajectory straight at where the player is and dives. No shots; the
// threat is the dive itself, and it CAN be dodged once committed (it won't
// re-aim), which rewards a sidestep. A telegraphed lock makes it readable.
struct Diver : Enemy {
    static constexpr int W = 20, H = 20;
    float t_ = 0, vx = 0, vy = 0, lock_telegraph_ = 0;
    bool locked = false;
    Diver(float px, float py) {
        x = px; y = py; hp = 2; max_hp = 2; pts = 250;
        color = C_DIVER;
    }
    Rect rect() const override { return {int(x), int(y), W, H}; }
    std::pair<int, int> center() const override {
        return {int(x + W / 2.0f), int(y + H / 2.0f)};
    }
    bool rams() const override { return true; }
    void update(float dt, float scroll_speed, Player& player,
                std::vector<EnemyShot>& shots) override;
    void draw(Surface& s) const override {
        Rect r = rect();
        auto [cx, cy] = center();
        // Reuse the white-flash overlay to flare it the instant before it commits,
        // so the dive stays telegraphed.
        float flash = std::max(hit_flash, lock_telegraph_ > 0 ? 0.8f : 0.0f);
        blit_enemy(s, raid_diver_art(), int(t_ * 20) & 1, cx, cy, flash);
        float p = 0.5f + 0.5f * std::sin(t_ * 20);  // hot eye glow on the nose
        glow_circle(s, Color(255, 120, 120), cx, r.y + r.h, int(2 + 2 * p), 80);
    }
};

// ── Energy Core ───────────────────────────────────────────────────────────────
struct EnergyCore {
    static constexpr int R = 10;
    float x, y, t_ = 0;
    bool alive = true;
    EnergyCore(float px, float py) : x(px), y(py) {}
    Rect rect() const { return {int(x - R), int(y - R), R * 2, R * 2}; }
    std::pair<int, int> center() const { return {int(x), int(y)}; }
    void update(float dt, float scroll_speed) {
        t_ += dt;
        y += scroll_speed * dt;
        if (y > 900) alive = false;
    }
    void draw(Surface& s) const {
        int cx = int(x), cy = int(y);
        float pulse = 0.7f + 0.3f * std::sin(t_ * 5);
        int r = int(R * pulse);
        Color col(int(255 * pulse), int(200 * pulse), int(40 * pulse));
        glow_circle(s, col, cx, cy, r + 4, 80);
        draw::circle(s, col, cx, cy, r);
        draw::circle(s, WHITE, cx, cy, r, 1);
        draw::line(s, WHITE, cx - 4, cy, cx + 4, cy, 1);
        draw::line(s, WHITE, cx, cy - 4, cx, cy + 4, 1);
    }
};

// ── Player ────────────────────────────────────────────────────────────────────
struct Player {
    static constexpr float SPEED = 280.0f;
    float x, y;
    bool alive = true;
    float invincible = 0, fire_cd = 0;
    float energy = 1.0f;
    float energy_max_ = 1.0f;   // ENERGY TANK power-ups grow the reserve (and the bar)
    float energy_drain = 0.065f;
    int w = PLAYER_W, h = PLAYER_H;
    float exhaust_t = 0;
    std::shared_ptr<Surface> surf;

    // weapon progression (persists across the run; reset on death)
    int w_rapid_ = 0, w_spread_ = 0, w_pierce_ = 0;
    float shield_ = 0;
    bool bot = false; float bdx = 0, bdy = 0;  // self-play movement override

    // ── OVERDRIVE: spend energy fast for a short burst of speed + firepower ──
    // Hold LSHIFT to engage while you have enough energy. Drains hard, gives a
    // faster fire-rate and snappier movement; auto-disengages when energy runs low.
    float overdrive_ = 0;          // remaining boost timer (s), > 0 while active
    float overdrive_cd_ = 0;       // cooldown before it can re-engage
    static constexpr float OVERDRIVE_DUR = 3.0f;
    static constexpr float OVERDRIVE_DRAIN = 0.22f;   // energy/sec while boosting
    static constexpr float OVERDRIVE_COST_MIN = 0.18f;  // need this much to start
    bool overdrive_active() const { return overdrive_ > 0; }

    Player(int px, int py) : x(float(px)), y(float(py)) {
        surf = std::make_shared<Surface>(
            make_sprite_surface(PLAYER_SHAPE, PLAYER_COLOR, 2));
    }
    Rect rect() const { return {int(x), int(y), w, h}; }
    std::pair<int, int> center() const {
        return {int(x + w / 2.0f), int(y + h / 2.0f)};
    }
    std::pair<int, int> gun_tip() const { return {int(x + w / 2.0f), int(y)}; }

    int weapon_tier() const { return w_rapid_ + w_spread_ + w_pierce_; }
    void reset_weapons() { w_rapid_ = w_spread_ = w_pierce_ = 0; shield_ = 0;
                           overdrive_ = 0; overdrive_cd_ = 0; }

    // Try to engage overdrive. Returns true if it kicked in (enough energy, not
    // already running, off cooldown).
    bool engage_overdrive() {
        if (overdrive_ > 0 || overdrive_cd_ > 0) return false;
        if (energy < OVERDRIVE_COST_MIN) return false;
        overdrive_ = OVERDRIVE_DUR;
        return true;
    }

    Color tier_color() const {
        int t = weapon_tier();
        if (t <= 1) return SHOT_COLOR;
        if (t <= 3) return lerp_color(SHOT_COLOR, YELLOW, (t - 1) / 2.0f);
        return lerp_color(YELLOW, WHITE, std::min(1.0f, (t - 3) / 3.0f));
    }
    float fire_rate() const {
        float base = std::max(0.07f, 0.18f * (1.0f - 0.10f * w_rapid_));
        return overdrive_active() ? std::max(0.04f, base * 0.55f) : base;
    }

    void update(float dt, const Input& in, int screen_w, int screen_h,
                float scroll_speed) {
        if (!alive) return;
        if (invincible > 0) invincible -= dt;
        if (fire_cd > 0) fire_cd -= dt;
        if (shield_ > 0) shield_ -= dt;
        if (overdrive_cd_ > 0) overdrive_cd_ -= dt;

        // Overdrive burns energy hard; ends on its own or when energy runs out.
        if (overdrive_ > 0) {
            overdrive_ -= dt;
            energy = std::max(0.0f, energy - OVERDRIVE_DRAIN * dt);
            if (overdrive_ <= 0 || energy <= 0.0f) {
                overdrive_ = 0;
                overdrive_cd_ = 1.5f;  // brief lockout after a burst
            }
        }

        float mx, my;
        if (bot) {
            mx = bdx; my = bdy;
        } else {
            mx = (in.down(SDLK_RIGHT) || in.down(SDLK_d)) - (in.down(SDLK_LEFT) || in.down(SDLK_a));
            my = (in.down(SDLK_DOWN) || in.down(SDLK_s)) - (in.down(SDLK_UP) || in.down(SDLK_w));
        }
        float spd = overdrive_active() ? SPEED * 1.4f : SPEED;
        x += mx * spd * dt;
        y += my * spd * dt;

        x = std::max(0.0f, std::min(float(screen_w - w), x));
        y = std::max(0.0f, std::min(float(screen_h - h), y));

        float drain = energy_drain * (scroll_speed / 180.0f) * dt;
        energy = std::max(0.0f, energy - drain);
        exhaust_t += dt;
    }

    // Fire the current loadout into `out`; returns how many shots were emitted.
    int shoot(std::vector<PlayerShot>& out) {
        if (fire_cd > 0 || !alive) return 0;
        fire_cd = fire_rate();
        auto [gx, gy] = gun_tip();
        Color col = tier_color();
        int dmg = 1 + weapon_tier() / 3;
        int wide = 3 + std::min(3, weapon_tier() / 2);
        auto add = [&](float vx) {
            PlayerShot s{float(gx), float(gy)};
            s.vx = vx; s.color = col; s.damage = dmg;
            s.pierce = w_pierce_; s.W = wide;
            out.push_back(s);
        };
        add(0);
        for (int i = 1; i <= w_spread_; ++i) { add(-90.0f * i); add(90.0f * i); }
        return 1 + 2 * w_spread_;
    }
    void refuel(float amount = 0.40f) { energy = std::min(energy_max_, energy + amount); }
    // 0..1 fraction of the (possibly enlarged) reserve — use this for warnings/tints.
    float energy_ratio() const { return energy_max_ > 0 ? energy / energy_max_ : 0.0f; }
    bool safe() const { return invincible > 0 || shield_ > 0; }
    bool take_hit() {
        energy -= 0.35f * energy_max_;  // a hit costs ~35% of the bar, whatever its size
        invincible = 1.2f;
        if (energy <= 0) { energy = 0.0f; return true; }
        return false;
    }
    void kill() { alive = false; invincible = 0.0f; }
    void respawn(int nx, int ny) {
        x = float(nx); y = float(ny);
        alive = true;
        invincible = 2.5f;
        energy = std::max(energy, 0.35f * energy_max_);
        fire_cd = 0.0f;
    }
    void draw(Surface& surface, ParticleSystem& particles) const {
        if (!alive) return;
        if (invincible > 0 && int(invincible * 8) % 2 == 0) return;
        int ex = int(x + w / 2.0f);
        int ey = int(y + h);
        if (overdrive_active()) {
            // hot orange afterburner plume + brighter trail while boosting
            particles.ball_trail(ex, ey, Color(255, 160, 40));
            float p = 0.6f + 0.4f * std::sin(exhaust_t * 40);
            glow_circle(surface, Color(255, int(140 * p), 30), ex, ey + 6, 10, 120);
            draw::line(surface, Color(255, 220, 120), ex, ey, ex,
                       ey + int(14 + 6 * p), 3);
            glow_rect(surface, rect(), Color(255, 170, 60), 5, 55);
        } else {
            particles.ball_trail(ex, ey, Color(80, 180, 255));
            glow_rect(surface, rect(), PLAYER_COLOR, 3, 35);
        }
        surface.blit(*surf, int(x), int(y));
        if (shield_ > 0) {
            auto [cx, cy] = center();
            float pulse = 0.6f + 0.4f * std::sin(exhaust_t * 20 + shield_ * 4);
            int rr = w;
            draw::circle(surface, Color(80, 220, 255, int(90 * pulse)), cx, cy, rr, 2);
            draw::circle(surface, Color(80, 220, 255, int(30 * pulse)), cx, cy, rr - 2, 1);
        }
    }
};

// Enemy update implementations (need full Player def) ──────────────────────────
void Scout::update(float dt, float scroll_speed, Player& player,
                   std::vector<EnemyShot>& shots) {
    tick_flash(dt);
    t_ += dt;
    y += (SPEED + scroll_speed * 0.5f) * dt;
    x = bx_ + std::sin(t_ * freq_) * amp_;   // gentle weave
    fire_cd -= dt;
    if (fire_cd <= 0 && player.alive) {
        fire_cd = rnd_uniform(1.5f, 3.5f);
        auto [cx, cy] = center();
        shots.emplace_back(float(cx), float(cy + H / 2));
    }
}

void Flanker::update(float dt, float scroll_speed, Player& player,
                     std::vector<EnemyShot>& shots) {
    tick_flash(dt);
    t_ += dt;
    x += sweep_vx * dt;
    y += scroll_speed * 0.3f * dt;
    fire_cd -= dt;
    if (fire_cd <= 0 && player.alive) {
        fire_cd = rnd_uniform(1.0f, 2.5f);
        auto [cx, cy] = center();
        auto [px, py] = player.center();
        float angle = std::atan2(float(py - cy), float(px - cx));
        shots.emplace_back(float(cx), float(cy), angle);
    }
}

void Cruiser::update(float dt, float scroll_speed, Player& player,
                     std::vector<EnemyShot>& shots) {
    tick_flash(dt);
    t_ += dt;
    y += (SPEED + scroll_speed * 0.4f) * dt;
    fire_cd -= dt;
    if (fire_cd <= 0 && player.alive) {
        fire_cd = rnd_uniform(2.0f, 4.0f);
        auto [cx, cy] = center();
        for (float angle : {PI / 2 - 0.3f, PI / 2, PI / 2 + 0.3f})
            shots.emplace_back(float(cx), float(cy + H / 2), angle);
    }
}

void Turret::update(float dt, float scroll_speed, Player& player,
                    std::vector<EnemyShot>& shots) {
    tick_flash(dt);
    y += scroll_speed * dt;
    if (player.alive) {
        auto [px, py] = player.center();
        float target = std::atan2(float(py) - y, float(px) - x);
        float diff = target - angle;
        diff = std::fmod(diff + PI, TAU);
        if (diff < 0) diff += TAU;
        diff -= PI;
        angle += diff * 3.5f * dt;
    }
    fire_cd -= dt;
    if (fire_cd <= 0 && player.alive) {
        fire_cd = rnd_uniform(1.5f, 3.0f);
        shots.emplace_back(x, y, angle);
    }
}

void Diver::update(float dt, float scroll_speed, Player& player,
                   std::vector<EnemyShot>& /*shots*/) {
    tick_flash(dt);
    t_ += dt;
    if (lock_telegraph_ > 0) lock_telegraph_ -= dt;
    if (!locked) {
        // ease in with the scroll until it has a clean line on the player below it
        y += (95.0f + scroll_speed * 0.5f) * dt;
        auto [cx, cy] = center();
        auto [px, py] = player.center();
        if (player.alive && cy > 36 && py > cy + 8) {
            locked = true;
            lock_telegraph_ = 0.18f;
            float a = std::atan2(float(py - cy), float(px - cx));
            float spd = 240.0f + scroll_speed * 0.4f;
            vx = std::cos(a) * spd;
            vy = std::sin(a) * spd;
        }
    } else {
        x += vx * dt;
        y += vy * dt;
    }
}

// ── Zone Boss ─────────────────────────────────────────────────────────────────
// A giant fat blocky thing that drops into the rift and BLOCKS it. You must
// destroy it while dodging its bullet patterns and managing energy. The fight is
// built like Void Invaders' boss: HP phases that swap the attack pool, tempo and
// face; telegraphed picks (a readable red wind-up ring) before every attack; and
// a pool of generic patterns (radial / aimed / wall-with-a-gap / spiral / rain /
// sweeping-laser / slam) mixed with RIFT SIGNATURES — it crushes the rift walls
// inward, chases the ship horizontally, seeds drifting mine hazards, and exposes
// a glowing CORE weak-point you can pop for bonus damage. Tiers gate the scary
// stuff (early bosses stay gentle, later ones unlock laser / crush / enrage). A
// one-time surprise: it FAKES death, submerges into the wall, then bursts back
// out with a full-rift barrage that leaves exactly one safe lane.
struct BossDef { const char* name; Color color; int w, h, hp; float atk_iv; int attack; int face; int cell; };
const BossDef BOSSES[4] = {
    {"BLOATWARD",   Color(90, 230, 120), 240, 150, 36, 1.5f,  0, 0, 18},
    {"THE GULPER",  Color(200, 90, 255), 320, 160, 64, 1.2f,  1, 1, 20},
    {"RIFT WARDEN", Color(90, 170, 255), 360, 190, 100, 1.05f, 2, 2, 22},
    {"VOID TITAN",  Color(255, 80, 80),  440, 240, 168, 0.9f, 3, 3, 26},
};

struct Boss {
    std::string name;
    Color color;
    int w, h, cell, attack, face;
    int hp, max_hp;
    float x, y, vx;
    float atk_t = 0, atk_iv, hit_flash = 0, mouth_open = 0, bob = 0;
    bool dying = false, finished = false, faking = false, did_fake = false;
    bool fake_started = false;  // one-shot flag read by the game for the gag banner
    float die_t = 0, fake_t = 0;
    static constexpr float DEATH_DUR = 1.6f;
    // grand entrance: drops down from the top into the rift
    bool entering = false;
    float battle_y = 70, invuln_ = 0;

    // ── attack system: HP phases + telegraphed pick + sustained patterns ──
    int tier = 1, phase = 1, cur_attack = -1, pending_attack = -1, slam_phase = 0;
    float attack_t = 0, telegraph = 0, sub_t = 0, spin = 0, laser_angle = 0;
    bool shock_done = false;
    float chase_vx = 0;            // horizontal chase velocity during CHASE attack
    int laser_lane = 0;            // safe lane index for the WALL / barrage surprise

    // ── rift signatures (outputs the game reads / applies each frame) ──
    float want_squeeze = 0.0f;     // >0: game feeds this to terrain.boss_squeeze
    float crush_t = 0;             // how long the current crush has run

    // ── weak-point CORE: pulses open in phase 2+, takes bonus damage ──
    float weak_t = 0;              // animation clock
    bool weak_open_ = false;       // currently vulnerable (extra damage zone)

    // ── one-time SURPRISE: fake death -> submerge -> burst-out barrage ──
    int surprise_phase = 0;        // 0 idle, 1 sinking, 2 lurking, 3 rising+barrage
    float surprise_t = 0;
    bool did_surprise = false;
    bool surprise_started = false; // one-shot banner flag, read by the game

    bool damageable() const {
        return !dying && !faking && !entering && invuln_ <= 0 && surprise_phase < 2;
    }
    bool active() const { return !finished; }
    bool submerged() const { return surprise_phase == 2; }  // hidden in the wall

    Boss(int tier_, int screen_w) {  // tier_ = 1,2,3,...
        tier = tier_;
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
        vx = (60 + tier * 8) * (rnd01() < 0.5f ? 1 : -1);
        entering = true; battle_y = 70;
    }
    Rect rect() const { return {int(x), int(y), w, h}; }
    float hp_ratio() const { return std::max(0.0f, float(hp) / max_hp); }

    // Only the later bosses (RIFT WARDEN, VOID TITAN — idx 2,3 of each loop) use
    // a weak-point. The first bosses (BLOATWARD, THE GULPER) take damage anywhere
    // on the body, with no target at all.
    bool uses_core() const { return (tier - 1) % 4 >= 2; }

    // The glowing core weak-point: a SMALL spot deep inside the body (centred),
    // pulses open/closed for rhythm. Full damage when open; the body only chips.
    Rect weak_rect() const {
        int ww = std::max(16, w / 8), wh = std::max(16, h / 8);
        return {int(x + w / 2 - ww / 2), int(y + h * 0.50f - wh / 2), ww, wh};
    }
    bool weak_vulnerable() const { return weak_open_ && damageable(); }

    static EnemyShot bb(float bx, float by, float a, float spd) {
        EnemyShot s(bx, by, PI / 2);  // direction overwritten below
        s.vx = spd * std::sin(a);
        s.vy = spd * std::cos(a);
        s.color = ORANGE; s.r = 6;
        return s;
    }
    // A slow, fat, magenta "mine" hazard — reuses EnemyShot collision so it can
    // hurt the ship; drifts down-rift so the player must weave around it.
    static EnemyShot mine(float bx, float by, float vx) {
        EnemyShot s(bx, by, PI / 2);
        s.vx = vx;
        s.vy = 70.0f;
        s.color = Color(255, 70, 220); s.r = 11;
        return s;
    }

    void take_damage(int dmg, ParticleSystem& parts, bool weak = false) {
        if (!damageable()) return;
        if (uses_core()) {
            // Later bosses are armoured: the CORE is the real damage path, body
            // hits only chip (forgiving, not a wall). Early bosses fall through
            // and take full damage anywhere.
            if (weak) dmg *= 2;                  // core pop: satisfying
            else dmg = std::max(1, dmg / 4);     // body: chip damage only
        }
        hp -= dmg; hit_flash = 0.14f;
        if (hp <= 0) { dying = true; die_t = 0; hp = 0; want_squeeze = 0; return; }
        // The unexpected: a one-time fake-out death gag around a quarter health.
        // Later-tier bosses turn it into a full submerge-and-ambush surprise.
        if (!did_fake && hp <= int(max_hp * 0.27f) && rnd01() < 0.55f) {
            did_fake = true;
            if (tier >= 2 && !did_surprise) {     // big bosses: real ambush
                did_surprise = true; surprise_started = true;
                surprise_phase = 1; surprise_t = 0;
                cur_attack = -1; telegraph = 0; want_squeeze = 0;
            } else {                              // small bosses: comedic fake-out
                faking = true; fake_t = 0; fake_started = true;
            }
        }
    }

    // 0 LOB 1 FAN 2 RADIAL 3 AIMED 4 WALL | 5 SPIRAL 6 RAIN 7 LASER 8 SLAM
    // 9 CRUSH 10 CHASE 11 MINES  (rift signatures gated to phase 2 / tier)
    int pick_attack() {
        std::vector<int> pool = {attack, 0, 2};      // signature, LOB, RADIAL
        if (phase >= 2) {
            pool.insert(pool.end(), {1, 3, 11});     // FAN, AIMED, MINES
            if (tier >= 2) pool.insert(pool.end(), {4, 5, 10});   // WALL, SPIRAL, CHASE
        }
        if (phase >= 3) {
            pool.push_back(3);                       // weight AIMED up (enrage)
            pool.push_back(11);                      // more mines
            if (tier >= 2) { pool.push_back(6); pool.push_back(8); pool.push_back(9); }  // RAIN, SLAM, CRUSH
            if (tier >= 3) pool.push_back(7);        // LASER
        }
        return pool[rnd_int(0, (int)pool.size() - 1)];
    }
    void begin_attack(int a) {
        cur_attack = a; attack_t = 0; sub_t = 99; slam_phase = 0; shock_done = false;
        crush_t = 0; chase_vx = (x + w / 2 < 450 ? 1 : -1) * (120 + tier * 18);
        if (a == 1 || a == 7) mouth_open = 0.7f;
        if (a == 4) laser_lane = rnd_int(0, 10);   // pre-roll the safe lane
    }
    void run_attack(float dt, float pcx, float pcy, int screen_w,
                    std::vector<EnemyShot>& out, ParticleSystem& parts) {
        (void)parts;
        attack_t += dt; sub_t += dt;
        float bx = x + w / 2, by = y + h - 8;
        switch (cur_attack) {
            case 0:  // LOB — lazy three-bullet arc
                for (float a : {-0.32f, 0.0f, 0.32f}) out.push_back(bb(bx, by, a, 215));
                cur_attack = -1; break;
            case 1:  // FAN — opens up, spits a spread
                for (int i = -2; i <= 2; ++i) out.push_back(bb(bx, by, i * 0.17f, 250));
                cur_attack = -1; break;
            case 2:  // RADIAL — wide sweep across the rift
                for (int i = 0; i < 11; ++i) out.push_back(bb(bx, by, -1.3f + i * (2.6f / 10), 210));
                cur_attack = -1; break;
            case 3: {  // AIMED — tight volley straight at the ship
                float aim = std::atan2(pcx - bx, std::max(20.0f, pcy - by));
                for (int i = -1; i <= 1; ++i) out.push_back(bb(bx, by, aim + i * 0.12f, 300));
                cur_attack = -1; break;
            }
            case 4: {  // WALL — a curtain with ONE safe gap to find
                for (int i = 0; i < 11; ++i) {
                    if (std::abs(i - laser_lane) <= 1) continue;
                    float px = x + (i + 0.5f) * (w / 11.0f);
                    EnemyShot s(px, by, PI / 2); s.vx = 0; s.vy = 200;
                    s.color = ORANGE; s.r = 6; out.push_back(s);
                }
                cur_attack = -1; break;
            }
            case 5:  // SPIRAL — rotating twin spray
                if (sub_t >= 0.07f) { sub_t = 0; spin += 0.5f;
                    out.push_back(bb(bx, by, spin, 200));
                    out.push_back(bb(bx, by, spin + PI, 200)); }
                if (attack_t >= 1.7f) cur_attack = -1; break;
            case 6:  // RAIN — bullets dripping from random points along the body
                if (sub_t >= 0.09f) { sub_t = 0; out.push_back(bb(rnd_uniform(x, x + w), by, 0, 235)); }
                if (attack_t >= 1.5f) cur_attack = -1; break;
            case 7:  // LASER — a sweeping beam of fast bolts
                laser_angle = -0.7f + (attack_t / 1.7f) * 1.4f;
                if (sub_t >= 0.025f) { sub_t = 0; out.push_back(bb(bx, by, laser_angle, 380)); }
                if (attack_t >= 1.7f) cur_attack = -1; break;
            case 8: {  // SLAM — wind up, dive down the rift, shockwave, return
                float target = battle_y + 280;
                if (slam_phase == 0) { if (attack_t >= 0.45f) slam_phase = 1; }
                else if (slam_phase == 1) { y += 820 * dt; if (y >= target) { y = target; slam_phase = 2; } }
                else if (slam_phase == 2) {
                    if (!shock_done) { shock_done = true;
                        for (int i = 0; i < 14; ++i)
                            out.push_back(bb(x + w / 2, y + h - 8, -1.5f + i * (3.0f / 13), 240)); }
                    slam_phase = 3;
                } else { y -= 640 * dt; if (y <= battle_y) { y = battle_y; cur_attack = -1; } }
                break;
            }
            case 9: {  // RIFT CRUSH — squeeze the walls inward, lob shots in the pinch
                crush_t += dt;
                // walls march in toward a tight rift, then release. Always leaves
                // a dodgeable channel (>= MIN_RIFT_W) so it's tense, not lethal.
                float t = crush_t;
                want_squeeze = t < 1.6f ? (0.62f - 0.30f * std::min(1.0f, t / 1.6f))
                                        : (0.32f + 0.30f * std::min(1.0f, (t - 1.6f) / 1.0f));
                if (sub_t >= 0.30f) { sub_t = 0;
                    for (float a : {-0.2f, 0.2f}) out.push_back(bb(bx, by, a, 200)); }
                if (crush_t >= 2.7f) { want_squeeze = 0; cur_attack = -1; }
                break;
            }
            case 10: {  // CHASE — slide across the rift to loom over the ship
                float cx = x + w / 2;
                chase_vx = (pcx > cx ? 1 : -1) * (130 + tier * 18);
                x += chase_vx * dt;
                if (x < 6) x = 6;
                if (x + w > screen_w - 6) x = screen_w - 6 - w;
                if (sub_t >= 0.34f) { sub_t = 0;
                    out.push_back(bb(x + w / 2, by, 0, 230)); }
                if (attack_t >= 1.6f) cur_attack = -1; break;
            }
            case 11: {  // MINES — seed slow drifting hazards across the rift
                if (sub_t >= 0.22f && attack_t < 1.3f) { sub_t = 0;
                    float mx = rnd_uniform(x + 10, x + w - 10);
                    out.push_back(mine(mx, by, rnd_uniform(-50, 50))); }
                if (attack_t >= 1.5f) cur_attack = -1; break;
            }
            default: cur_attack = -1; break;
        }
    }

    // Drive the one-time submerge-and-ambush surprise. Returns nothing; reads/sets
    // its own phase. The boss hides in the wall, then bursts up with a full-rift
    // barrage that leaves exactly ONE safe lane (telegraphed by weak_open glow).
    void run_surprise(float dt, int screen_w, std::vector<EnemyShot>& out) {
        surprise_t += dt;
        if (surprise_phase == 1) {            // sinking out of sight
            y += 360 * dt;
            if (y >= 720) { surprise_phase = 2; surprise_t = 0; }
        } else if (surprise_phase == 2) {     // lurking below; brief, ominous pause
            // recentre while hidden so the re-emergence is symmetric
            x += (((screen_w - w) / 2.0f) - x) * std::min(1.0f, 4 * dt);
            if (surprise_t >= 1.4f) {
                surprise_phase = 3; surprise_t = 0; y = -float(h);
                laser_lane = rnd_int(1, 9);   // the single safe lane
            }
        } else if (surprise_phase == 3) {     // rising back up + the barrage
            y += 520 * dt;
            // fire the full-rift curtain once, the instant it clears the top
            if (!shock_done && y >= -h * 0.2f) {
                shock_done = true;
                for (int i = 0; i < 13; ++i) {
                    if (std::abs(i - laser_lane) <= 1) continue;  // the safe lane
                    float px = (i + 0.5f) * (screen_w / 13.0f);
                    EnemyShot s(px, 0, PI / 2); s.vx = 0; s.vy = 210;
                    s.color = Color(255, 90, 60); s.r = 6; out.push_back(s);
                }
            }
            if (y >= battle_y) {              // back home; resume the fight, enraged
                y = battle_y; surprise_phase = 0; shock_done = false;
                hp = std::max(1, int(max_hp * 0.42f));   // pops back with some HP
                invuln_ = 0.4f;
            }
        }
    }

    void update(float dt, float player_cx, float player_cy, int screen_w,
                std::vector<EnemyShot>& out, ParticleSystem& parts) {
        bob += dt; weak_t += dt;
        want_squeeze = 0;  // default off; only the CRUSH attack turns it on
        if (entering) {  // slide down into the rift
            y += 90 * dt;
            if (y >= battle_y) { y = battle_y; entering = false; invuln_ = 1.6f; }
            return;
        }
        if (hit_flash > 0) hit_flash -= dt * 6;
        if (invuln_ > 0) invuln_ -= dt;
        if (dying) {
            die_t += dt;
            if (rnd01() < 0.6f) {  // confetti / chained blasts
                float rx = x + rnd_uniform(0, w), ry = y + rnd_uniform(0, h);
                parts.ufo_explode(rx, ry, 10);
            }
            if (die_t >= DEATH_DUR) finished = true;
            return;
        }
        if (surprise_phase > 0) { run_surprise(dt, screen_w, out); return; }
        if (faking) {  // deflate, sulk, then bounce back at half health
            fake_t += dt;
            if (fake_t >= 1.1f) { faking = false; hp = int(max_hp * 0.5f); }
            return;
        }

        // ── phases: faster & nastier as health drops; face + tempo change ──
        phase = hp_ratio() > 0.6f ? 1 : hp_ratio() > 0.3f ? 2 : 3;
        float aggr = (tier <= 1) ? 0.85f : 0.55f;  // first boss stays calm even low
        float iv = atk_iv * (phase == 3 ? aggr : phase == 2 ? 0.85f : 1.0f);

        // weak-point CORE (later bosses only): pulses open/closed for rhythm.
        // Available from the start so the fight isn't a slog — open a good share
        // of the time. Telegraphed by the glow in draw().
        weak_open_ = uses_core() && (std::sin(weak_t * 2.0f) > -0.2f);

        // sway across the rift (chase attack overrides x itself)
        if (cur_attack != 10) {
            x += vx * dt;
            if (x < 6) { x = 6; vx = std::fabs(vx); }
            if (x + w > screen_w - 6) { x = screen_w - 6 - w; vx = -std::fabs(vx); }
        }
        if (mouth_open > 0) mouth_open -= dt;

        if (telegraph > 0) {                 // winding up (readable red warning ring)
            telegraph -= dt;
            if (telegraph <= 0) begin_attack(pending_attack);
        } else if (cur_attack >= 0) {        // a (possibly sustained) attack is running
            run_attack(dt, player_cx, player_cy, screen_w, out, parts);
        } else {                             // idle: pick the next attack after a beat
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
        if (hit_flash > 0.06f) {  // dazed eyes when freshly smacked
            draw::line(s, BLACK, ex - er / 2, ey - er / 2, ex + er / 2, ey + er / 2, 3);
            draw::line(s, BLACK, ex - er / 2, ey + er / 2, ex + er / 2, ey - er / 2, 3);
        } else {
            draw::circle(s, BLACK, px, py, std::max(2, er / 2));
        }
    }

    void draw(Surface& s, float pcx, float pcy) const {
        if (y + h < 0) return;  // still fully above the screen (incl. submerged/rising)
        float sc = dying ? std::max(0.0f, 1 - die_t / DEATH_DUR)
                         : faking ? 0.6f : 1.0f;
        int bw = int(w * sc), bh = int(h * sc);
        int bx = int(x + (w - bw) / 2), by = int(y + (h - bh) / 2 + std::sin(bob * 2) * 4);
        if (bw < 4 || bh < 4) return;
        Color body = hit_flash > 0 ? lerp_color(color, WHITE, 0.6f) : color;
        // soft outer glow (flood-fill halo, not a hard blocky rect)
        // soft outer glow — cheap layered translucent rounded rects (glow_rect
        // allocated a surface per layer each frame, which lagged big-boss fights).
        for (int gi = 4; gi >= 1; --gi) {
            int pad = gi * 3;
            draw::rect(s, Color(body.r, body.g, body.b, 22),
                       {bx - pad, by - pad, bw + 2 * pad, bh + 2 * pad}, 0, cell + pad);
        }
        // a GIANT INVADER body (shared pixel-alien art)
        draw_boss_alien(s, face, int(bob * 3) % 2, bx, by, bw, bh, body);
        if (invuln_ > 0) {  // spawn-shield shimmer: can't be hurt yet
            float p = 0.5f + 0.5f * std::sin(bob * 18);
            draw::rect(s, Color(120, 200, 255, int(130 * p)), {bx - 8, by - 8, bw + 16, bh + 16}, 3, cell + 6);
        }
        if (telegraph > 0) {  // wind-up warning ring (red, pulsing) so attacks read
            float p = 0.4f + 0.6f * std::sin(bob * 30);
            draw::rect(s, Color(255, 60, 60, int(150 * p)), {bx - 7, by - 7, bw + 14, bh + 14}, 3, cell + 5);
        }
        if (cur_attack == 7) {  // LASER: draw the sweeping beam
            int mx = bx + bw / 2, my = by + bh - 6;
            int ex = mx + int(std::sin(laser_angle) * 900), ey = my + int(std::cos(laser_angle) * 900);
            draw::line(s, Color(255, 80, 40, 90), mx, my, ex, ey, 9);
            draw::line(s, Color(255, 200, 120), mx, my, ex, ey, 3);
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
        int eyl = bx + bw / 3, eyr = bx + 2 * bw / 3, eyy = by + int(bh * 0.30f);
        if (face == 2) {  // cyclops
            draw_eye(s, bx + bw / 2, eyy, er * 2, pcx, pcy);
        } else {
            draw_eye(s, eyl, eyy, er, pcx, pcy);
            draw_eye(s, eyr, eyy, er, pcx, pcy);
        }

        // ── weak-point CORE (later bosses only): a small glowing spot deep in
        // the body. No crosshair/target — just a soft glow that flares bright gold
        // when open (full damage) and dims red when closed (body only chips). ──
        if (uses_core() && sc > 0.5f) {
            Rect wr = weak_rect();
            int wcx = wr.x + wr.w / 2, wcy = wr.y + wr.h / 2, wrad = std::max(5, wr.w / 2);
            bool open = weak_open_;
            Color core = open ? Color(255, 240, 80) : Color(150, 70, 70);
            float pulse = 0.6f + 0.4f * std::sin(weak_t * (open ? 9.0f : 3.0f));
            glow_circle(s, core, wcx, wcy, wrad + (open ? 6 : 2), open ? 140 : 60);
            draw::circle(s, Color(core.r, int(core.g * pulse), int(core.b * pulse)), wcx, wcy, wrad);
            draw::circle(s, WHITE, wcx, wcy, std::max(2, wrad / 2));
        }

        // mouth changes with health / face (drawn above the core)
        int mx = bx + bw / 2, my = by + int(bh * 0.82f), mw = bw / 3;
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

// ── Spawner ───────────────────────────────────────────────────────────────────
struct SpawnKind {
    const char* name;
    int weight;
};
struct SpawnTable {
    float interval_lo, interval_hi;
    std::vector<SpawnKind> enemies;
    float core_lo, core_hi;
};
const SpawnTable SPAWN_TABLES[5] = {
    {2.2f, 4.0f, {{"scout", 10}}, 5.0f, 8.0f},
    {1.8f, 3.2f, {{"scout", 8}, {"flanker", 4}, {"diver", 2}}, 5.5f, 9.0f},
    {1.4f, 2.8f, {{"scout", 6}, {"flanker", 5}, {"turret", 3}, {"diver", 3}}, 6.0f, 10.0f},
    {1.2f, 2.4f,
     {{"scout", 5}, {"flanker", 5}, {"turret", 4}, {"cruiser", 2}, {"diver", 4}}, 6.5f, 11.0f},
    {0.9f, 1.8f,
     {{"scout", 4}, {"flanker", 5}, {"turret", 4}, {"cruiser", 3}, {"diver", 5}}, 7.0f, 12.0f},
};

std::string weighted_choice(const std::vector<SpawnKind>& kinds) {
    int total = 0;
    for (auto& k : kinds) total += k.weight;
    int r = rnd_int(0, total - 1);
    for (auto& k : kinds) {
        if (r < k.weight) return k.name;
        r -= k.weight;
    }
    return kinds.back().name;
}

struct PendingTurret {
    int x, y, obs_id;
};

struct Spawner {
    int W, H;
    float enemy_cd = 2.0f, core_cd = 6.0f;
    bool paused = false;  // boss fight: no normal enemies, only cores
    std::vector<PendingTurret> turret_pend;

    Spawner(int sw, int sh) : W(sw), H(sh) {}

    void register_station(const TerrainObstacle& obs) {
        Rect r = obs.rect;
        if (r.w > 120) {
            for (int tx : {r.x + r.w / 4, r.x + r.w - r.w / 4})
                turret_pend.push_back({tx, r.y, obs.id});
        } else {
            turret_pend.push_back({r.x + r.w / 2, r.y, obs.id});
        }
    }

    // `zone` scales aggression: shorter intervals and (later) armored grunts.
    void update(float dt, int zone, Terrain& terrain, Player& /*player*/,
                std::vector<std::unique_ptr<Enemy>>& enemies,
                std::vector<EnergyCore>& cores,
                std::vector<EnemyShot>& /*shots*/) {
        int zi = std::min(zone, 4);
        const SpawnTable& table = SPAWN_TABLES[zi];

        // Each zone past the last table tightens spawn cadence a little more.
        float extra = std::max(0.55f, 1.0f - std::max(0, zone - 4) * 0.08f);
        // Armor: appears from zone 2, grows with zone.
        float armor_p = std::min(0.55f, std::max(0.0f, (zone - 1) * 0.10f));
        int armor_bonus = 1 + zone / 3;

        if (!paused) {
            enemy_cd -= dt;
            if (enemy_cd <= 0) {
                enemy_cd = rnd_uniform(table.interval_lo, table.interval_hi) * extra;
                std::string kind = weighted_choice(table.enemies);

                int lw = terrain.left_wall_w(80);
                int rw = terrain.right_wall_w(80);
                int rift_x = lw;
                int rift_w = W - lw - rw;
                // During a fork: the RIGHT (gauntlet) channel gets most grunts and
                // the full mix (the score route), while the LEFT (fuel route) gets
                // only a light sprinkle of easy scouts — present, but fewer and
                // gentler, so the safe lane isn't barren but stays the calm option.
                int dx = 0, dw = 0;
                bool forking = terrain.forking() && terrain.divider_at(80.0f, dx, dw);
                bool fork_right = forking;  // flanker edge math: gauntlet is on the right
                if (forking) {
                    bool spawn_left = rnd01() < 0.30f;
                    if (spawn_left) {
                        kind = "scout";          // safe lane: easy descenders only
                        rift_x = lw;
                        rift_w = dx - rift_x;
                        fork_right = false;
                    } else {
                        rift_x = dx + dw;
                        rift_w = (W - rw) - rift_x;
                    }
                }

                if (rift_w >= 60) {
                    int spawn_x = rift_x + rnd_int(10, std::max(11, rift_w - 40));
                    int spawn_y = -30;
                    bool armored = (zone >= 2) && rnd01() < armor_p;

                    Enemy* made = nullptr;
                    if (kind == "scout") {
                        auto e = std::make_unique<Scout>(spawn_x, spawn_y);
                        made = e.get(); enemies.push_back(std::move(e));
                    } else if (kind == "flanker") {
                        // sweep from the channel's own left/right edge so the fork
                        // keeps grunts inside the gauntlet.
                        int fl_left = fork_right ? dx + dw + 2 : lw + 2;
                        if (rnd01() < 0.5f) {
                            auto e = std::make_unique<Flanker>(fl_left, rnd_int(60, 200), 1);
                            made = e.get(); enemies.push_back(std::move(e));
                        } else {
                            auto e = std::make_unique<Flanker>(W - rw - 28, rnd_int(60, 200), -1);
                            made = e.get(); enemies.push_back(std::move(e));
                        }
                    } else if (kind == "cruiser") {
                        auto e = std::make_unique<Cruiser>(spawn_x - 23, spawn_y);
                        made = e.get(); enemies.push_back(std::move(e));
                    } else if (kind == "diver") {
                        auto e = std::make_unique<Diver>(spawn_x - 10, spawn_y);
                        made = e.get(); enemies.push_back(std::move(e));
                    }
                    // "turret" -> from pending station placements only
                    if (made && armored && kind != "turret") {
                        made->armored = true;
                        made->hp += armor_bonus;
                        made->max_hp += armor_bonus;
                        made->pts = int(made->pts * 1.6f);
                    }
                }
            }

            for (auto& p : turret_pend) {
                auto e = std::make_unique<Turret>(p.x, p.y, p.obs_id);
                if (zone >= 3 && rnd01() < armor_p) {
                    e->armored = true; e->hp += 1; e->max_hp += 1; e->pts = int(e->pts * 1.5f);
                }
                enemies.push_back(std::move(e));
            }
            turret_pend.clear();
        } else {
            turret_pend.clear();
        }

        // Cores keep flowing even mid-boss (energy management stays central).
        // During a fork the LEFT (safe) channel is the FUEL route: cores cluster
        // there, a touch more often — so the calm lane's reward is energy, while
        // the gauntlet's reward is score. That makes the choice a real trade-off.
        core_cd -= dt;
        if (core_cd <= 0) {
            const int CORE_R = 12;
            int margin = CORE_R + 8;
            int dx = 0, dw = 0;
            if (terrain.forking() && terrain.divider_at(80.0f, dx, dw)) {
                core_cd = rnd_uniform(table.core_lo * 0.7f, table.core_hi * 0.7f);
                int lw = terrain.left_wall_w(80);
                int lch_x = lw + margin;
                int lch_w = dx - lch_x - margin;
                if (lch_w > CORE_R * 2) {
                    int cx = lch_x + rnd_int(0, std::max(1, lch_w - 1));
                    cores.emplace_back(float(cx), -20.0f);
                }
            } else {
                core_cd = rnd_uniform(table.core_lo, table.core_hi);
                int lw = terrain.left_wall_w(80);
                int rw = terrain.right_wall_w(80);
                int rift_x = lw + margin;
                int rift_w = W - lw - rw - margin * 2;
                if (rift_w > CORE_R * 2) {
                    int cx = rift_x + rnd_int(0, std::max(1, rift_w - 1));
                    cores.emplace_back(float(cx), -20.0f);
                }
            }
        }
    }
};

// ── Flash ─────────────────────────────────────────────────────────────────────
struct Flash {
    std::string text;
    float x, y, life, max_life;
    Color color;
    bool big;
};

// ── Game ──────────────────────────────────────────────────────────────────────
enum class St { Ready, Play, Dead, GameOver };

class VoidRaidGame : public Scene {
public:
    explicit VoidRaidGame(const Context& ctx)
        : Scene(ctx), pause_(ctx.W, ctx.H, ctx.fonts),
          initials_(ctx.W, ctx.H, ctx.fonts) {
        high_score_ = ctx.scores ? ctx.scores->high_score("void_raid") : 0;
        new_game();
        if (ctx_.sfx) ctx_.sfx->start_ambient(1);  // rift
    }

    void update(float dt, const Input& in) override;
    void draw(Surface& s) override;
    void on_exit() override { submit_scores(); if (ctx_.sfx) ctx_.sfx->stop_ambient(); }

private:
    static constexpr float AMBIENT_T = 0.15f;
    PauseMenu pause_;
    InitialsEntry initials_;
    bool entry_started_ = false;
    ParticleSystem particles_;
    int high_score_ = 0;
    float star_t_ = 0;

    int score_, lives_, zone_;
    int checkpoint_;
    float checkpoint_energy_;
    float scroll_speed_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<Terrain> terrain_;
    std::unique_ptr<Parallax> parallax_;
    std::unique_ptr<Spawner> spawner_;
    std::unique_ptr<Boss> boss_;
    std::vector<std::unique_ptr<Enemy>> enemies_;
    std::vector<EnemyShot> enemy_shots_;
    std::vector<PlayerShot> player_shots_;
    std::vector<EnergyCore> cores_;
    std::vector<PowerUp> powerups_;
    std::vector<Flash> flash_;
    St state_;
    float ready_t_, dead_t_, low_energy_beep_;
    bool submitted_ = false;

    // ── branching rift tracking ──
    // While forked we award a per-frame score trickle in the RIGHT (gauntlet)
    // channel and a fat bonus + guaranteed power-up for exiting still on the right.
    bool fork_active_ = false;     // a fork is currently open/merging
    bool was_right_ = false;       // player held the right channel last frame
    float right_time_ = 0;         // seconds spent in the right channel this fork
    int right_bonus_accum_ = 0;    // points banked while in the right channel

    // progression / boss tracking
    int nukes_ = 0;
    int kills_since_drop_ = 0;
    float nuke_flash_ = 0;
    int boss_tier_ = 0;        // how many bosses fought (drives difficulty)
    int next_boss_zone_ = 2;   // first boss appears entering zone 2
    bool boss_active_ = false;

    // telemetry
    float game_t_ = 0, zone_t0_ = 0;
    bool bot_ = false;
    int zone_kills_ = 0, zone_shots_ = 0, zone_pu_ = 0, zone_deaths_ = 0;
    int zone_nukes_ = 0, zone_cores_ = 0;

    std::string loadout() const {
        return "r" + std::to_string(player_->w_rapid_) +
               "w" + std::to_string(player_->w_spread_) +
               "p" + std::to_string(player_->w_pierce_) +
               "n" + std::to_string(nukes_);
    }
    void tele(const J& j) { if (ctx_.tele) ctx_.tele->event(j); }

    // ── risk/reward throttle scoring ──
    // The faster you scroll, the higher the score multiplier on kills and cores.
    // Ranges 1.0x (crawling at SCROLL_MIN) up to 3.0x (pinned at SCROLL_MAX), so
    // the throttle becomes a real risk dial: speed = more drain, more points.
    float speed_mult() const {
        float frac = (scroll_speed_ - SCROLL_MIN) / (SCROLL_MAX - SCROLL_MIN);
        frac = std::max(0.0f, std::min(1.0f, frac));
        return 1.0f + 2.0f * frac;
    }
    // Combined per-kill multiplier: combo chain × speed bonus (rounded, >= 1).
    int kill_mult() {
        int c = ctx_.combo ? ctx_.combo->hit() : 1;
        return std::max(1, int(std::lround(c * speed_mult())));
    }

    void new_game() {
        score_ = 0;
        entry_started_ = false;
        lives_ = 3;
        zone_ = 0;
        checkpoint_ = 0;
        checkpoint_energy_ = 1.0f;
        scroll_speed_ = SCROLL_MIN;
        player_ = std::make_unique<Player>(ctx_.W / 2 - 11, ctx_.H - 160);
        terrain_ = std::make_unique<Terrain>(ctx_.W, ctx_.H);
        parallax_ = std::make_unique<Parallax>(ctx_.W, ctx_.H);
        spawner_ = std::make_unique<Spawner>(ctx_.W, ctx_.H);
        boss_.reset();
        enemies_.clear();
        enemy_shots_.clear();
        player_shots_.clear();
        cores_.clear();
        powerups_.clear();
        particles_.clear();
        flash_.clear();
        ready_t_ = 2.5f;
        dead_t_ = 0.0f;
        low_energy_beep_ = 0.0f;
        state_ = St::Ready;
        submitted_ = false;
        nukes_ = 0;
        fork_active_ = false;
        was_right_ = false;
        right_time_ = 0;
        right_bonus_accum_ = 0;
        kills_since_drop_ = 0;
        nuke_flash_ = 0;
        boss_tier_ = 0;
        next_boss_zone_ = 2;
        boss_active_ = false;
        game_t_ = 0; zone_t0_ = 0;
        reset_zone_counters();

        bot_ = std::getenv("VOID_BOT") != nullptr || ctx_.demo;
        if (const char* e = std::getenv("VOID_WEAPON")) {
            int t = atoi(e);
            player_->w_rapid_ = std::min(4, t);
            player_->w_spread_ = std::min(3, t);
            player_->w_pierce_ = std::min(2, t);
            nukes_ = t;
        }
        tele(J().add("ev", "zone_start").add("zone", zone_).add("t", (double)game_t_)
                .add("load", loadout()).add("lives", lives_));
        if (const char* bv = std::getenv("VOID_BOSS")) {  // debug: verify boss (VOID_BOSS=N -> tier N)
            int t = atoi(bv);
            if (t > 1) boss_tier_ = t - 1;
            state_ = St::Play; ready_t_ = 0; start_boss();
        }
        if (std::getenv("VOID_FORK")) {  // debug: force a fork right away
            zone_ = 1; state_ = St::Play; ready_t_ = 0; terrain_->fork_cd = 2;
        }
    }

    void reset_zone_counters() {
        zone_t0_ = game_t_;
        zone_kills_ = zone_shots_ = zone_pu_ = zone_deaths_ = 0;
        zone_nukes_ = zone_cores_ = 0;
    }

    void save_checkpoint() {
        checkpoint_ = score_;
        checkpoint_energy_ = player_->energy;
    }
    void restore_checkpoint() {
        player_->respawn(ctx_.W / 2 - 11, ctx_.H - 160);
        // Respawn with a FULL tank — energy is too scarce to drop back into a
        // later zone half-empty (the checkpoint energy felt punishing).
        player_->energy = player_->energy_max_;
        enemies_.clear();
        enemy_shots_.clear();
        player_shots_.clear();
        cores_.clear();
        powerups_.clear();
        // A boss in progress survives the respawn — you have to finish it.
    }

    void add_flash(const std::string& t, float x, float y, Color c,
                   float life = 0.9f, bool big = false) {
        flash_.push_back({t, x, y, life, life, c, big});
    }

    void submit_scores() {
        if (ctx_.demo) return;  // attract demo never touches the leaderboard
        if (ctx_.scores && !submitted_) {
            submitted_ = true;
            tele(J().add("ev", "game_over").add("zone", zone_).add("score", score_)
                    .add("t", (double)game_t_).add("load", loadout()));
            bool hi = ctx_.scores->submit_void_raid(score_, zone_ + 1);
            if (hi)
                add_flash("NEW HI-SCORE!", ctx_.W / 2.0f, ctx_.H / 2.0f - 40,
                          YELLOW, 2.5f);
            high_score_ = ctx_.scores->high_score("void_raid");
        }
    }

    // ── power-up economy (inverse-power drop scaling + pity timer) ──
    float drop_penalty() const {
        return std::clamp(1.0f - player_->weapon_tier() * 0.16f, 0.2f, 1.0f);
    }
    void spawn_powerup(float x, float y) {
        int r = rnd_int(0, 99);
        PU k;
        if (r < 24) k = PU::Rapid; else if (r < 44) k = PU::Spread;
        else if (r < 57) k = PU::Pierce; else if (r < 68) k = PU::Shield;
        else if (r < 79) k = PU::Fuel; else if (r < 86) k = PU::Nuke;
        else if (r < 90) k = PU::Life; else if (r < 96) k = PU::Tank;
        else k = PU::Mystery;
        powerups_.push_back({x, y, 0, k});
    }
    void maybe_drop(float x, float y, float base) {
        if (rnd01() < base * drop_penalty()) spawn_powerup(x, y);
    }
    // Per-kill drop with a pity timer so the first power-up never drags.
    void roll_enemy_drop(float x, float y, bool heavy) {
        ++kills_since_drop_;
        float chance = (heavy ? 0.30f : 0.12f) * drop_penalty();
        bool drop = (kills_since_drop_ >= 12 && player_->weapon_tier() < 2) ||
                    rnd01() < chance;
        if (drop) { kills_since_drop_ = 0; spawn_powerup(x, y); }
    }
    void apply_powerup(PU k) {
        if (k == PU::Mystery)
            k = rnd_choice<PU>({PU::Rapid, PU::Spread, PU::Pierce, PU::Nuke, PU::Shield, PU::Fuel});
        const char* msg = "";
        switch (k) {
            case PU::Rapid:  player_->w_rapid_ = std::min(4, player_->w_rapid_ + 1); msg = "RAPID FIRE!"; break;
            case PU::Spread: player_->w_spread_ = std::min(3, player_->w_spread_ + 1); msg = "SPREAD SHOT!"; break;
            case PU::Pierce: player_->w_pierce_ = std::min(2, player_->w_pierce_ + 1); msg = "PIERCING ROUNDS!"; break;
            case PU::Shield: player_->shield_ = 7.0f; msg = "SHIELD UP!"; break;
            case PU::Fuel:   player_->refuel(0.55f); msg = "FUEL CELL!"; break;
            case PU::Life:   lives_++; msg = "EXTRA LIFE!"; break;
            case PU::Nuke:   nukes_++; msg = "NUKE ACQUIRED!"; break;
            case PU::Tank:   player_->energy_max_ = std::min(2.5f, player_->energy_max_ + 0.5f);
                             player_->energy = player_->energy_max_;  // top off into the bigger tank
                             msg = "ENERGY TANK!"; break;
            default: break;
        }
        add_flash(msg, player_->x + player_->w / 2, player_->y - 30, YELLOW, 1.2f);
        zone_pu_++;
        tele(J().add("ev", "powerup").add("zone", zone_).add("t", (double)game_t_)
                .add("load", loadout()).add("nukes", nukes_));
        if (ctx_.sfx) ctx_.sfx->powerup_collect();
        if (ctx_.fx) ctx_.fx->rumble_pickup();
    }
    void detonate_nuke() {
        if (nukes_ <= 0 || state_ != St::Play) return;
        nukes_--;
        nuke_flash_ = 1.0f;
        enemy_shots_.clear();
        zone_nukes_++;
        tele(J().add("ev", "nuke").add("zone", zone_).add("t", (double)game_t_));
        // Clears weak enemies, heavily chips the rest.
        for (auto& e : enemies_) {
            if (!e->alive) continue;
            auto [cx, cy] = e->center();
            bool killed = e->hit(3);
            if (killed) {
                zone_kills_++; score_ += e->pts;
                particles_.explode_invader(cx, cy, e->color, 18);
                int sid = e->station_id();
                TerrainObstacle* st = sid > 0 ? terrain_->find_obstacle(sid) : nullptr;
                if (st) st->open_gap(cx, 48);
            } else {
                particles_.explode_invader(cx, cy, e->color, 8);
            }
        }
        if (boss_ && boss_->active()) {
            int dmg = std::max(25, boss_->max_hp / 5 + 20);
            boss_->take_damage(dmg, particles_);
            add_flash("DIRECT HIT!", boss_->x + boss_->w / 2, boss_->y + boss_->h / 2, YELLOW, 1.2f, true);
        }
        add_flash("* NUKE *", ctx_.W / 2.0f, ctx_.H / 2.0f - 30, RED, 1.4f, true);
        if (ctx_.sfx) ctx_.sfx->big_explosion();
        if (ctx_.fx) {
            ctx_.fx->shake(26); ctx_.fx->hitstop(0.07f);
            ctx_.fx->rumble_explosion();
            ctx_.fx->shockwave(0.5f, 0.5f);
        }
    }

    void start_boss() {
        boss_tier_++;
        boss_ = std::make_unique<Boss>(boss_tier_, ctx_.W);
        boss_active_ = true;
        spawner_->paused = true;
        terrain_->blocked = true;
        enemies_.clear();  // clear the rift for the showdown
        add_flash("WARNING: " + boss_->name + " BLOCKS THE RIFT!",
                  ctx_.W / 2.0f, 132, RED, 2.2f, true);
        if (ctx_.sfx) ctx_.sfx->player_death();  // ominous sting
        if (ctx_.fx) ctx_.fx->shake(14);
        tele(J().add("ev", "boss_start").add("zone", zone_).add("tier", boss_tier_)
                .add("name", boss_->name).add("t", (double)game_t_).add("load", loadout()));
    }
    void end_boss() {
        // deflate death rains power-ups
        float rx = boss_->x + boss_->w / 2, ry = boss_->y + boss_->h / 2;
        score_ += 500 * boss_tier_;
        powerups_.push_back({rx, ry, 0, PU::Nuke});
        powerups_.push_back({rx - 60, ry, 0, PU::Spread});
        powerups_.push_back({rx + 60, ry, 0, PU::Rapid});
        powerups_.push_back({rx, ry - 40, 0, PU::Fuel});
        add_flash(boss_->name + " GOES SPLAT!", ctx_.W / 2.0f, ctx_.H / 2.0f - 20, YELLOW, 2.2f, true);
        if (ctx_.sfx) ctx_.sfx->big_explosion();
        if (ctx_.fx) {
            ctx_.fx->shake(30); ctx_.fx->hitstop(0.10f);
            ctx_.fx->rumble_explosion();
            ctx_.fx->shockwave(rx / float(ctx_.W), ry / float(ctx_.H));
        }
        tele(J().add("ev", "boss_clear").add("zone", zone_).add("tier", boss_tier_)
                .add("dt", (double)(game_t_ - zone_t0_)).add("name", boss_->name)
                .add("load", loadout()).add("lives", lives_));
        boss_.reset();
        boss_active_ = false;
        spawner_->paused = false;
        terrain_->blocked = false;
        terrain_->boss_squeeze = 0.0f;  // release the rift crush
        next_boss_zone_ = zone_ + 2;  // next boss two zones later
        if (ctx_.sfx) ctx_.sfx->level_clear();
    }

    void update_ready(float dt, const Input& in);
    void update_play(float dt, const Input& in);
    void update_fork(float dt);
    void check_player_shots();
    void check_enemy_shots();
    void check_cores();
    void check_powerups(float dt);
    void player_die();
    void draw_hud(Surface& s);
    void draw_flash(Surface& s);
};

void VoidRaidGame::update(float dt, const Input& in) {
    // Pause toggle
    for (auto k : in.pressed)
        if (k == SDLK_p && (state_ == St::Play || state_ == St::Ready))
            pause_.toggle();
    if (pause_.active()) {
        if (pause_.update(in) == PauseResult::QuitToMenu) quit_to_menu = true;
        return;
    }
    // ESC -> menu
    for (auto k : in.pressed)
        if (k == SDLK_ESCAPE) { quit_to_menu = true; return; }

    star_t_ += dt;
    if (star_t_ >= AMBIENT_T) {
        star_t_ = 0;
        particles_.ambient_star(ctx_.W, ctx_.H);
    }
    particles_.update(dt);
    // Parallax drifts whenever the rift scrolls (gentler while dead/ready).
    if (parallax_) {
        float pscroll = (state_ == St::Play) ? scroll_speed_ : scroll_speed_ * 0.35f;
        parallax_->update(dt, pscroll);
    }
    if (nuke_flash_ > 0) nuke_flash_ -= dt * 1.6f;
    for (auto& f : flash_) f.life -= dt;
    flash_.erase(std::remove_if(flash_.begin(), flash_.end(),
                 [](const Flash& f) { return f.life <= 0; }), flash_.end());

    if (state_ == St::Ready) {
        update_ready(dt, in);
    } else if (state_ == St::Play) {
        game_t_ += dt;
        update_play(dt, in);
    } else if (state_ == St::Dead) {
        dead_t_ -= dt;
        terrain_->update(dt, scroll_speed_ * 0.3f, zone_);
        // Scroll enemies with the terrain so turrets stay attached to their
        // platforms during the death drift (they don't fire — player is dead).
        for (auto& e : enemies_) e->update(dt, scroll_speed_ * 0.3f, *player_, enemy_shots_);
        if (dead_t_ <= 0) {
            if (lives_ > 0) {
                restore_checkpoint();
                state_ = St::Play;
                if (ctx_.sfx) ctx_.sfx->spawn_chime();
            } else {
                state_ = St::GameOver;
                submit_scores();
            }
        }
    } else if (state_ == St::GameOver) {
        if (bot_) { new_game(); return; }  // self-play loops for data
        if (ctx_.scores) {
            const std::string gid = "void_raid";
            if (!entry_started_ && ctx_.scores->qualifies(gid, score_)) {
                entry_started_ = true;
                int rank = 1;
                for (auto& e : ctx_.scores->leaderboard(gid)) if (e.score > score_) rank++;
                initials_.begin("VOID RAID", score_, rank);
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

void VoidRaidGame::update_ready(float dt, const Input& /*in*/) {
    ready_t_ -= dt;
    terrain_->update(dt, SCROLL_MIN * 0.4f, zone_);
    if (ready_t_ <= 0) {
        state_ = St::Play;
        if (ctx_.sfx) ctx_.sfx->spawn_chime();
    }
}

void VoidRaidGame::update_play(float dt, const Input& in) {
    if (bot_ && player_->alive) {  // self-play: navigate rift, grab cores, dodge, shoot
        float pcx = player_->x + player_->w / 2.0f, pcy = player_->y + player_->h / 2.0f;
        float lw = terrain_->left_wall_w(pcy), rw = terrain_->right_wall_w(pcy);
        float tx = (lw + (ctx_.W - rw)) / 2.0f, ty = ctx_.H * 0.62f;
        float best = 1e9f;
        for (auto& c : cores_) {
            if (!c.alive) continue;
            float d = std::hypot(c.x - pcx, c.y - pcy);
            if (d < best && d < 280) { best = d; tx = c.x; ty = c.y; }
        }
        for (auto& p : powerups_) {
            if (!p.alive) continue;
            if (std::hypot(p.x - pcx, p.y - pcy) < 170) { tx = p.x; ty = p.y; }
        }
        for (auto& s : enemy_shots_) {
            if (!s.alive) continue;
            float dx = s.x - pcx, dy = s.y - pcy;
            if (std::fabs(dx) < 42 && dy > -30 && dy < 170) tx = pcx + (dx > 0 ? -140 : 140);
        }
        // avoid terrain obstacles scrolling toward us (steer to the clearer side)
        for (auto& o : terrain_->obstacles) {
            if (!o.alive) continue;
            float oy = o.rect.y + o.rect.h / 2.0f;
            if (oy < pcy - 120 || oy > pcy + 60) continue;          // only ones about to reach us
            if (pcx + 30 < o.rect.x || pcx - 30 > o.rect.x + o.rect.w) continue;  // not in our column
            float room_left = o.rect.x - lw, room_right = (ctx_.W - rw) - (o.rect.x + o.rect.w);
            tx = room_left > room_right ? o.rect.x - 40.0f : o.rect.x + o.rect.w + 40.0f;
        }
        tx = std::max(lw + 22.0f, std::min(ctx_.W - rw - 22.0f, tx));
        player_->bot = true;
        player_->bdx = tx > pcx + 6 ? 1.f : tx < pcx - 6 ? -1.f : 0.f;
        player_->bdy = ty > pcy + 6 ? 1.f : ty < pcy - 6 ? -1.f : 0.f;
        if (player_->energy_ratio() < 0.3f)
            scroll_speed_ = std::max(SCROLL_MIN, scroll_speed_ - SCROLL_ACCEL * dt);
        int n = player_->shoot(player_shots_);
        if (n > 0) { zone_shots_ += n; if (ctx_.sfx) ctx_.sfx->shoot(); }
        int near = 0;
        for (auto& s : enemy_shots_)
            if (s.alive && std::fabs(s.x - pcx) < 150 && std::fabs(s.y - pcy) < 190) near++;
        if (nukes_ > 0 && near >= 8) detonate_nuke();
    }
    // Throttle: Q = faster, E = slower
    if (in.down(SDLK_q) || in.down(SDLK_PAGEUP))
        scroll_speed_ = std::min(SCROLL_MAX, scroll_speed_ + SCROLL_ACCEL * dt);
    else if (in.down(SDLK_e) || in.down(SDLK_PAGEDOWN))
        scroll_speed_ = std::max(SCROLL_MIN, scroll_speed_ - SCROLL_ACCEL * dt);

    // OVERDRIVE: hold LSHIFT to burn energy for a burst of speed + firepower.
    if ((in.down(SDLK_LSHIFT) || in.down(SDLK_RSHIFT)) && player_->alive) {
        if (player_->engage_overdrive()) {
            add_flash("OVERDRIVE!", player_->x + player_->w / 2,
                      player_->y - 30, ORANGE, 1.0f, true);
            if (ctx_.sfx) ctx_.sfx->powerup_collect();
            if (ctx_.fx) { ctx_.fx->shake(5); ctx_.fx->rumble(0.5f, 0.6f, 200); }
        }
    }

    // Shoot (held key, gated by fire_cd) — fires the full stacked loadout.
    if (in.down(SDLK_SPACE) || in.down(SDLK_LCTRL)) {
        int n = player_->shoot(player_shots_);
        if (n > 0) {
            zone_shots_ += n;
            if (ctx_.sfx) ctx_.sfx->shoot();
        }
    }
    // Nuke (B)
    for (auto k : in.pressed) if (k == SDLK_b) detonate_nuke();

    // Zone progression (score-driven). Slightly faster forced drain per zone.
    int new_zone = -1;
    for (int i = 0; i < N_ZONES; ++i)
        if (score_ >= ZONE_SCORES[i]) ++new_zone;
    // beyond the table, keep advancing zones on a fixed score cadence
    if (score_ >= ZONE_SCORES[N_ZONES - 1])
        new_zone = (N_ZONES - 1) + (score_ - ZONE_SCORES[N_ZONES - 1]) / 12000;
    if (new_zone > zone_) {
        int prev = zone_;
        zone_ = new_zone;
        add_flash(ZONE_NAMES[std::min(zone_, N_ZONES - 1)], ctx_.W / 2.0f,
                  ctx_.H / 2.0f, YELLOW, 2.5f);
        // Palette drift: each zone gets its own hue so the rift feels different.
        if (ctx_.fx) ctx_.fx->palette_hue = 0.16f * std::sin(zone_ * 0.7f);
        // forced energy drain ramps with zone (tighter the deeper you go)
        player_->energy_drain = 0.065f + zone_ * 0.006f;
        // per-zone telemetry: close out previous, open this one
        tele(J().add("ev", "zone_clear").add("zone", prev)
                .add("dt", (double)(game_t_ - zone_t0_))
                .add("kills", zone_kills_).add("shots", zone_shots_)
                .add("pu", zone_pu_).add("deaths", zone_deaths_)
                .add("nukes_used", zone_nukes_).add("cores", zone_cores_)
                .add("energy", (double)player_->energy)
                .add("load", loadout()).add("lives", lives_));
        reset_zone_counters();
        tele(J().add("ev", "zone_start").add("zone", zone_).add("t", (double)game_t_)
                .add("load", loadout()).add("lives", lives_));
        // boss appears when crossing into a designated boss zone
        if (!boss_active_ && zone_ >= next_boss_zone_) start_boss();
    }

    // Terrain
    terrain_->update(dt, scroll_speed_, zone_);

    // Register new station obstacles for turret placement
    for (auto& obs : terrain_->obstacles)
        if (obs.kind == "station" && !obs.registered) {
            obs.registered = true;
            spawner_->register_station(obs);
        }

    // Branching rift: telegraph, channel tracking, exit bonus.
    update_fork(dt);

    // Player
    player_->update(dt, in, ctx_.W, ctx_.H, scroll_speed_);

    // Wall / obstacle collision
    if (player_->alive && !player_->safe()) {
        Rect pr = player_->rect();
        bool hit = false;
        for (auto& wr : terrain_->wall_rects_near(pr))
            if (overlap(pr, wr)) { hit = true; break; }
        if (!hit)
            for (auto& obs : terrain_->obstacles)
                if (obs.blocks(pr)) { hit = true; break; }
        if (hit) {
            bool destroyed = player_->take_hit();
            auto [cx, cy] = player_->center();
            particles_.explode_invader(cx, cy, PLAYER_COLOR, 12);
            if (destroyed) {
                player_die();
                return;
            } else {
                player_->x += (ctx_.W / 2.0f - cx) * 0.15f;
                add_flash("HIT!", cx, cy - 20, Color(255, 100, 40), 0.6f);
                if (ctx_.sfx) ctx_.sfx->brick_hit();
                if (ctx_.fx) { ctx_.fx->shake(6); ctx_.fx->rumble_hit(); }
            }
        }
    }

    // Energy out -> death
    if (player_->energy <= 0 && player_->alive) {
        add_flash("ENERGY DEPLETED!", ctx_.W / 2.0f, ctx_.H / 2.0f, RED, 1.8f);
        player_die();
        return;
    }

    // Low energy warning — faster beeps as energy drops
    if (player_->energy_ratio() < 0.25f) {
        float beep_interval = 0.15f + (player_->energy_ratio() / 0.25f) * 0.25f;
        low_energy_beep_ -= dt;
        if (low_energy_beep_ <= 0) {
            low_energy_beep_ = beep_interval;
            if (ctx_.sfx) ctx_.sfx->low_energy_pip();
        }
    }

    // Spawner
    spawner_->update(dt, zone_, *terrain_, *player_, enemies_, cores_,
                     enemy_shots_);

    // Enemies
    size_t es_before = enemy_shots_.size();
    for (auto& e : enemies_)
        e->update(dt, scroll_speed_, *player_, enemy_shots_);
    if (enemy_shots_.size() > es_before && ctx_.sfx)  // one gritty burst per volley
        ctx_.sfx->enemy_shot();
    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(),
                   [&](const std::unique_ptr<Enemy>& e) {
                       return !(e->alive && e->y < ctx_.H + 60);
                   }), enemies_.end());

    // Kamikaze ramming: Divers damage the player on body contact (then die).
    if (player_->alive && !player_->safe()) {
        Rect pr = player_->rect();
        for (auto& e : enemies_) {
            if (!e->alive || !e->rams() || !overlap(pr, e->rect())) continue;
            e->alive = false;
            auto [cx, cy] = player_->center();
            particles_.explode_invader(cx, cy, e->color, 16);
            bool destroyed = player_->take_hit();
            if (destroyed) { player_die(); return; }
            add_flash("RAMMED!", cx, cy - 20, C_DIVER, 0.7f);
            if (ctx_.sfx) ctx_.sfx->brick_hit();
            if (ctx_.fx) { ctx_.fx->shake(7); ctx_.fx->rumble(0.5f, 0.6f, 160); }
            break;
        }
    }

    // Boss
    if (boss_) {
        auto [pcx, pcy] = player_->center();
        boss_->update(dt, float(pcx), float(pcy), ctx_.W, enemy_shots_, particles_);
        // Rift-crush: the boss drives the wall squeeze; feed it to the terrain.
        terrain_->boss_squeeze = boss_->want_squeeze;
        if (boss_->fake_started) {
            boss_->fake_started = false;
            add_flash("...JUST KIDDING!", boss_->x + boss_->w / 2, boss_->y - 10, YELLOW, 1.6f, true);
        }
        if (boss_->surprise_started) {
            boss_->surprise_started = false;
            add_flash("IT'S NOT DEAD!", ctx_.W / 2.0f, ctx_.H / 2.0f - 30, RED, 1.8f, true);
            if (ctx_.sfx) ctx_.sfx->player_death();
            if (ctx_.fx) ctx_.fx->shake(18);
        }
        // Boss body blocks the rift: touching it hurts (unless safe).
        if (boss_->damageable() && player_->alive && !player_->safe() &&
            overlap(player_->rect(), boss_->rect())) {
            bool destroyed = player_->take_hit();
            auto [cx, cy] = player_->center();
            particles_.explode_invader(cx, cy, PLAYER_COLOR, 10);
            if (destroyed) { player_die(); return; }
            else {
                player_->y += 26;  // shoved back down out of the body
                add_flash("HIT!", cx, cy - 20, Color(255, 100, 40), 0.6f);
                if (ctx_.sfx) ctx_.sfx->brick_hit();
                if (ctx_.fx) { ctx_.fx->shake(8); ctx_.fx->rumble_hit(); }
            }
        }
        if (boss_->finished) end_boss();
    }

    // Player shots
    for (auto& s : player_shots_) {
        s.update(dt, ctx_.W);
        particles_.bullet_trail(int(s.x), int(s.y), s.color, true);
    }
    player_shots_.erase(std::remove_if(player_shots_.begin(), player_shots_.end(),
                        [](const PlayerShot& s) { return !s.alive; }),
                        player_shots_.end());

    // Enemy shots
    for (auto& s : enemy_shots_) s.update(dt, ctx_.W, ctx_.H);
    enemy_shots_.erase(std::remove_if(enemy_shots_.begin(), enemy_shots_.end(),
                       [](const EnemyShot& s) { return !s.alive; }),
                       enemy_shots_.end());

    // Energy cores
    for (auto& c : cores_) c.update(dt, scroll_speed_);
    cores_.erase(std::remove_if(cores_.begin(), cores_.end(),
                 [](const EnergyCore& c) { return !c.alive; }), cores_.end());

    // Power-ups (tractor toward ship, collect on touch)
    check_powerups(dt);

    // Collisions
    check_player_shots();
    check_enemy_shots();
    check_cores();

    // Checkpoint
    if (score_ - checkpoint_ >= CHECKPOINT_SCORE) {
        save_checkpoint();
        add_flash("CHECKPOINT", ctx_.W / 2.0f, 80, GREEN, 1.5f);
    }
}

// ── Branching rift: telegraph the fork, track which channel the ship is in,
// trickle a gauntlet bonus and pay out a fat reward + power-up for exiting on
// the right. The divider geometry/collision lives in Terrain; this is purely the
// readability + reward layer that the player feels.
void VoidRaidGame::update_fork(float dt) {
    Terrain& t = *terrain_;

    // Telegraph a moment before the split materialises.
    if (t.fork_pending_hint) {
        add_flash("RIFT FORK AHEAD", ctx_.W / 2.0f, ctx_.H * 0.34f, CYAN, 1.6f, true);
        add_flash("<< FUEL        SCORE >>", ctx_.W / 2.0f, ctx_.H * 0.40f,
                  YELLOW, 1.6f, true);
        if (ctx_.sfx) ctx_.sfx->low_energy_pip();
    }
    if (t.fork_just_opened) {
        fork_active_ = true;
        right_time_ = 0;
        right_bonus_accum_ = 0;
        was_right_ = false;
    }

    if (fork_active_) {
        // Which channel is the ship in? Probe the divider at the ship's nose.
        auto [pcx, pcy] = player_->center();
        int dx = 0, dw = 0;
        bool in_right = false;
        if (t.divider_at(float(pcy), dx, dw)) {
            in_right = pcx >= dx + dw / 2;  // right of the divider's midline
        } else {
            // divider hasn't reached the ship yet (entry) or has merged — keep
            // the last read so the bonus persists across the easing edges.
            in_right = was_right_;
        }
        was_right_ = in_right;

        if (in_right) {
            right_time_ += dt;
            // steady gauntlet trickle, scaled by the risk/reward speed multiplier
            float acc = 30.0f * speed_mult() * dt;
            int whole = int(acc);
            right_bonus_accum_ += whole;
            score_ += whole;
        }

        // Fork resolved: pay out if they braved the gauntlet for real.
        if (t.fork_just_closed) {
            fork_active_ = false;
            if (right_time_ > 0.6f) {
                int bonus = 400 + boss_tier_ * 100 + right_bonus_accum_ / 2;
                score_ += bonus;
                high_score_ = std::max(high_score_, score_);
                add_flash("GAUNTLET CLEARED  +" + std::to_string(bonus),
                          ctx_.W / 2.0f, ctx_.H * 0.42f, ORANGE, 2.0f, true);
                // guaranteed reward for the risk
                auto [pcx, pcy] = player_->center();
                spawn_powerup(float(pcx), float(pcy) - 40);
                if (ctx_.sfx) ctx_.sfx->powerup_collect();
                if (ctx_.fx) ctx_.fx->shake(6);
                tele(J().add("ev", "fork_right").add("zone", zone_).add("t", (double)game_t_)
                        .add("bonus", bonus).add("rt", (double)right_time_));
            } else {
                add_flash("FUEL ROUTE", ctx_.W / 2.0f, ctx_.H * 0.42f, GREEN, 1.4f);
                tele(J().add("ev", "fork_left").add("zone", zone_).add("t", (double)game_t_));
            }
        }
    }
}

void VoidRaidGame::check_player_shots() {
    for (auto& shot : player_shots_) {
        if (!shot.alive) continue;
        Rect sr = shot.rect();
        // ── boss ──
        if (boss_ && boss_->damageable() && overlap(sr, boss_->rect())) {
            // CORE weak-point: a hit on the open core does triple damage + bonus
            // score and a satisfying pop. Otherwise it's a normal body hit.
            bool weak = boss_->weak_vulnerable() && overlap(sr, boss_->weak_rect());
            boss_->take_damage(shot.damage, particles_, weak);
            score_ += (weak ? 25 : 5) * shot.damage;
            if (weak) {
                particles_.explode_invader(int(shot.x), int(shot.y - shot.H), Color(255, 240, 80), 16);
                add_flash("CORE HIT!", shot.x, shot.y - 24, YELLOW, 0.7f, true);
                if (ctx_.sfx) ctx_.sfx->boss_explosion();
                if (ctx_.fx) { ctx_.fx->shake(6); ctx_.fx->hitstop(0.04f); }
            } else {
                particles_.explode_invader(int(shot.x), int(shot.y - shot.H), boss_->color, 6);
                if (ctx_.sfx) ctx_.sfx->invader_hit();
                if (ctx_.fx) ctx_.fx->shake(2);
            }
            shot.alive = false;  // bolts always stop on the boss (no pierce-through)
            continue;
        }
        // ── enemies ──
        for (auto& e : enemies_) {
            if (!e->alive) continue;
            if (overlap(sr, e->rect())) {
                auto [cx, cy] = e->center();
                bool destroyed = e->hit(shot.damage);
                if (destroyed) {
                    // COMBO chain × risk/reward speed bonus scale the points.
                    int mult = kill_mult();
                    int pts = e->pts * mult;
                    score_ += pts;
                    zone_kills_++;
                    high_score_ = std::max(high_score_, score_);
                    particles_.explode_invader(cx, cy, e->color, 18);
                    Color fc = mult >= 6 ? RED : mult >= 3 ? ORANGE : YELLOW;
                    std::string tag = "+" + std::to_string(pts);
                    if (mult > 1) tag += "  x" + std::to_string(mult);
                    add_flash(tag, cx, cy - 16, fc);
                    // Turret -> blast a gap in its platform (look up by id; the
                    // obstacle may have scrolled off and been freed)
                    int sid = e->station_id();
                    TerrainObstacle* st = sid > 0 ? terrain_->find_obstacle(sid) : nullptr;
                    if (st != nullptr) {
                        st->open_gap(cx, 48);
                        add_flash("GAP!", cx, cy - 32, ORANGE, 1.0f);
                    }
                    roll_enemy_drop(cx, cy, e->is_heavy());
                    if (ctx_.sfx) {
                        if (e->is_heavy()) ctx_.sfx->boss_explosion();
                        else ctx_.sfx->invader_hit();
                    }
                    if (ctx_.fx) {
                        if (e->is_heavy()) { ctx_.fx->shake(9); ctx_.fx->hitstop(0.05f); ctx_.fx->rumble(0.5f, 0.6f, 180); }
                        else { ctx_.fx->shake(3); ctx_.fx->rumble(0.2f, 0.35f, 80); }
                    }
                    // Pierce only carries through enemies it one-shot KILLS.
                    if (shot.pierce > 0) shot.pierce--; else shot.alive = false;
                } else {
                    particles_.explode_invader(cx, cy, e->color, 6);
                    if (ctx_.sfx) ctx_.sfx->brick_hit();
                    shot.alive = false;  // chipped (not killed) -> bolt stops
                }
                break;
            }
        }

        if (shot.alive) {
            for (auto& obs : terrain_->obstacles) {
                if (overlap(sr, obs.rect)) {
                    shot.alive = false;
                    particles_.explode_invader(obs.rect.x + obs.rect.w / 2,
                                               obs.rect.y + obs.rect.h / 2,
                                               Color(180, 120, 60), 8);
                    break;
                }
            }
        }
    }
}

void VoidRaidGame::check_enemy_shots() {
    if (!player_->alive || player_->safe()) return;
    Rect pr = player_->rect();
    for (auto& s : enemy_shots_) {
        if (overlap(s.rect(), pr)) {
            s.alive = false;
            bool destroyed = player_->take_hit();
            auto [cx, cy] = player_->center();
            particles_.explode_invader(cx, cy, PLAYER_COLOR, 10);
            if (destroyed) {
                player_die();
            } else {
                add_flash("HIT!", cx, cy - 20, Color(255, 100, 40), 0.6f);
                if (ctx_.sfx) ctx_.sfx->brick_hit();
                if (ctx_.fx) { ctx_.fx->shake(6); ctx_.fx->rumble(0.4f, 0.5f, 150); }
            }
            return;
        }
    }
}

void VoidRaidGame::check_cores() {
    if (!player_->alive) return;
    Rect pr = player_->rect();
    for (auto& c : cores_) {
        if (!c.alive) continue;
        // Collect by touch
        if (overlap(pr, c.rect())) {
            c.alive = false;
            player_->refuel();
            zone_cores_++;
            // Risk/reward: cores are worth more the faster you're scrolling.
            int cpts = int(50 * speed_mult());
            score_ += cpts;
            high_score_ = std::max(high_score_, score_);
            auto [cx, cy] = c.center();
            particles_.explode_invader(cx, cy, Color(255, 220, 60), 14);
            add_flash("ENERGY +" + std::to_string(cpts), cx, cy - 16, YELLOW);
            tele(J().add("ev", "core_collected").add("zone", zone_).add("t", (double)game_t_)
                    .add("energy", (double)player_->energy));
            if (ctx_.sfx) { ctx_.sfx->pan_next(cx / float(ctx_.W)); ctx_.sfx->energy_refill(); }
            continue;
        }
        // Shoot core — also refuels
        for (auto& shot : player_shots_) {
            if (shot.alive && overlap(shot.rect(), c.rect())) {
                shot.alive = false;
                c.alive = false;
                player_->refuel();
                zone_cores_++;
                int cpts = int(50 * speed_mult());
                score_ += cpts;
                high_score_ = std::max(high_score_, score_);
                auto [cx, cy] = c.center();
                particles_.explode_invader(cx, cy, Color(255, 220, 60), 18);
                add_flash("ENERGY +" + std::to_string(cpts), cx, cy - 16, YELLOW);
                tele(J().add("ev", "core_collected").add("zone", zone_).add("t", (double)game_t_)
                        .add("energy", (double)player_->energy));
                if (ctx_.sfx) { ctx_.sfx->pan_next(cx / float(ctx_.W)); ctx_.sfx->energy_refill(); }
                break;
            }
        }
    }
}

void VoidRaidGame::check_powerups(float dt) {
    auto [pcx, pcy] = player_->center();
    for (auto& p : powerups_) {
        if (!p.alive) continue;
        p.update(dt, scroll_speed_, ctx_.H, float(pcx), float(pcy));
        if (p.alive && player_->alive && overlap(p.rect(), player_->rect())) {
            p.alive = false;
            apply_powerup(p.kind);
        }
    }
    powerups_.erase(std::remove_if(powerups_.begin(), powerups_.end(),
                    [](const PowerUp& p) { return !p.alive; }), powerups_.end());
}

void VoidRaidGame::player_die() {
    if (!player_->alive) return;
    auto [cx, cy] = player_->center();
    particles_.explode_player(cx, cy, 60);
    // Death costs your arsenal — the weapon climb restarts (nukes survive).
    bool had_weapons = player_->weapon_tier() > 0;
    player_->reset_weapons();
    if (had_weapons) add_flash("WEAPONS LOST!", cx, cy - 44, RED, 1.8f, true);
    player_->kill();
    lives_ -= 1;
    zone_deaths_++;
    state_ = St::Dead;
    dead_t_ = 2.5f;
    tele(J().add("ev", "death").add("zone", zone_).add("t", (double)game_t_)
            .add("load", loadout()).add("lives_left", lives_)
            .add("energy", (double)player_->energy));
    if (ctx_.sfx) ctx_.sfx->player_death();
    if (ctx_.fx) {
        ctx_.fx->shake(16); ctx_.fx->hitstop(0.09f);
        ctx_.fx->rumble_death();
        ctx_.fx->shockwave(cx / float(ctx_.W), cy / float(ctx_.H));
    }
}

// ── draw ──────────────────────────────────────────────────────────────────────
void VoidRaidGame::draw(Surface& s) {
    s.fill(Color(2, 4, 14));  // deep space black

    if (parallax_) parallax_->draw(s);  // slow depth layers behind the rift
    particles_.draw(s);
    terrain_->draw(s);

    for (auto& c : cores_) c.draw(s);
    for (auto& e : enemies_) e->draw(s);
    if (boss_) {
        auto [pcx, pcy] = player_->center();
        boss_->draw(s, float(pcx), float(pcy));
    }
    for (auto& sh : enemy_shots_) sh.draw(s);
    for (auto& sh : player_shots_) sh.draw(s);
    for (auto& p : powerups_) p.draw(s, *ctx_.fonts.small);
    player_->draw(s, particles_);

    draw_hud(s);
    if (nuke_flash_ > 0)
        draw::rect(s, Color(255, 255, 255, int(200 * std::min(1.0f, nuke_flash_))),
                   {0, 0, ctx_.W, ctx_.H});
    draw_flash(s);

    if (state_ == St::Ready) {
        draw_text_centered(s, *ctx_.fonts.big, "VOID  RAID", ctx_.W / 2,
                           ctx_.H / 2 - 50, CYAN);
        draw_text_centered(s, *ctx_.fonts.small,
                           "ARROWS / WASD  MOVE    SPACE  FIRE    B  NUKE", ctx_.W / 2,
                           ctx_.H / 2 + 10, DIM);
        draw_text_centered(s, *ctx_.fonts.small,
                           "Q / E  THROTTLE    SHOOT CORES TO REFUEL", ctx_.W / 2,
                           ctx_.H / 2 + 36, YELLOW);
        draw_text_centered(s, *ctx_.fonts.small,
                           "LSHIFT  OVERDRIVE (BURNS ENERGY)    FASTER = MORE SCORE",
                           ctx_.W / 2, ctx_.H / 2 + 62, ORANGE);
    } else if (state_ == St::GameOver) {
        draw_text_centered(s, *ctx_.fonts.big, "GAME  OVER", ctx_.W / 2,
                           ctx_.H / 2 - 50, RED);
        draw_text_centered(s, *ctx_.fonts.med, "SCORE  " + zpad(score_, 7),
                           ctx_.W / 2, ctx_.H / 2 + 10, WHITE);
        draw_text_centered(s, *ctx_.fonts.small, "PRESS SPACE TO RETRY",
                           ctx_.W / 2, ctx_.H / 2 + 60, DIM);
        if (initials_.active()) initials_.draw(s);
    }

    pause_.draw(s);
}

void VoidRaidGame::draw_hud(Surface& s) {
    draw_text(s, *ctx_.fonts.med, "SCORE  " + zpad(score_, 7), 16, 8, GREEN);
    draw_text_centered(s, *ctx_.fonts.med, "HI  " + zpad(high_score_, 7),
                       ctx_.W / 2, 8, YELLOW);
    draw_text_right(s, *ctx_.fonts.med, ZONE_NAMES[std::min(zone_, N_ZONES - 1)],
                    ctx_.W - 16, 8, CYAN);

    // COMBO indicator (chain multiplier from shared engine combo)
    if (ctx_.combo && ctx_.combo->active() && state_ == St::Play) {
        int m = ctx_.combo->mult();
        Color cc = m >= 6 ? RED : m >= 4 ? ORANGE : YELLOW;
        draw_text_centered(s, *ctx_.fonts.med, "x" + std::to_string(m) + " COMBO",
                           ctx_.W / 2, 34, cc);
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

    // Energy bar (bottom). The bar GROWS with the tank's capacity (ENERGY TANK
    // power-ups) — base width per unit of reserve — so a bigger reserve reads as
    // a literally wider bar. Colour/fill use the 0..1 ratio.
    int base_w = 220, bar_h = 14;
    int bar_w = int(base_w * player_->energy_max_);
    int bar_x = ctx_.W / 2 - bar_w / 2;
    int bar_y = ctx_.H - 30;
    float e = player_->energy_ratio();

    draw::rect(s, Color(20, 30, 40), {bar_x - 1, bar_y - 1, bar_w + 2, bar_h + 2},
               0, 4);
    Color col = e > 0.5f
                    ? Color(int((1 - e) * 2 * 255), 255, 60)
                    : Color(255, int(e * 2 * 255), 60);
    int fill_w = std::max(0, int(bar_w * e));
    if (fill_w > 0) {
        draw::rect(s, col, {bar_x, bar_y, fill_w, bar_h}, 0, 3);
        // glow on bar
        Surface gs(fill_w, bar_h, true);
        gs.fill(Color(col.r, col.g, col.b, 50));
        s.blit(gs, bar_x, bar_y);
    }
    // unit-segment dividers so an enlarged tank reads as discrete capacity tiers
    for (int u = 1; u < int(player_->energy_max_ + 0.001f); ++u)
        draw::line(s, Color(10, 20, 30, 180), bar_x + base_w * u, bar_y,
                   bar_x + base_w * u, bar_y + bar_h, 1);
    draw::rect(s, Color(60, 80, 100), {bar_x, bar_y, bar_w, bar_h}, 1, 3);

    // Label
    draw_text_right(s, *ctx_.fonts.small, "ENERGY", bar_x - 8, bar_y,
                    Color(80, 100, 120));

    // Lives
    for (int i = 0; i < lives_; ++i)
        draw::polygon(s, CYAN,
                      {{bar_x + bar_w + 18 + i * 22, ctx_.H - 18},
                       {bar_x + bar_w + 26 + i * 22, ctx_.H - 30},
                       {bar_x + bar_w + 34 + i * 22, ctx_.H - 18}});

    // weapon loadout pips (bottom-left, above the throttle line)
    int hx = 16, hy = ctx_.H - 52;
    draw_text(s, *ctx_.fonts.small, "PWR", hx, hy, DIM);
    auto pip = [&](int n, int cap, Color c, int slot) {
        for (int i = 0; i < cap; ++i)
            draw::rect(s, i < n ? c : Color(30, 40, 30), {hx + 44 + slot * 16, hy + 11 - i * 3, 12, 2});
    };
    pip(player_->w_rapid_, 4, YELLOW, 0);
    pip(player_->w_spread_, 3, ORANGE, 1);
    pip(player_->w_pierce_, 2, PURPLE, 2);

    // Scroll speed indicator + risk/reward multiplier (bottom left)
    float spd_frac = (scroll_speed_ - SCROLL_MIN) / (SCROLL_MAX - SCROLL_MIN);
    std::string arrows(1 + int(spd_frac * 3), '>');
    draw_text(s, *ctx_.fonts.small, "Q/E  SPD " + arrows, 16, ctx_.H - 28,
              Color(60, 90, 120));
    // the faster you scroll, the bigger the score multiplier — show it
    float sm = speed_mult();
    int sm10 = int(std::lround(sm * 10));
    std::string sm_txt = "x" + std::to_string(sm10 / 10) + "." +
                         std::to_string(sm10 % 10) + " SCORE";
    Color sm_c = sm >= 2.4f ? RED : sm >= 1.7f ? ORANGE : Color(120, 160, 90);
    draw_text(s, *ctx_.fonts.small, sm_txt, 150, ctx_.H - 28, sm_c);

    // OVERDRIVE telegraph (bottom-left, above PWR row)
    if (player_->overdrive_active()) {
        float p = 0.5f + 0.5f * std::sin(player_->exhaust_t * 24);
        Color oc(255, int(120 + 100 * p), 30);
        draw_text(s, *ctx_.fonts.small, "** OVERDRIVE **", 16, ctx_.H - 68, oc);
    } else {
        Color oc = player_->overdrive_cd_ > 0 ? Color(60, 50, 40)
                 : player_->energy >= Player::OVERDRIVE_COST_MIN ? Color(150, 100, 40)
                 : Color(60, 50, 40);
        draw_text(s, *ctx_.fonts.small, "LSHIFT OVERDRIVE", 16, ctx_.H - 68, oc);
    }

    // nukes + pause (bottom-right)
    if (nukes_ > 0)
        draw_text_right(s, *ctx_.fonts.small, "NUKE x" + std::to_string(nukes_) + "  [B]",
                        ctx_.W - 12, ctx_.H - 44, RED);
    draw_text_right(s, *ctx_.fonts.small, "P PAUSE", ctx_.W - 12, ctx_.H - 26,
                    Color(25, 40, 60));
}

void VoidRaidGame::draw_flash(Surface& s) {
    for (auto& f : flash_) {
        Color c = f.color;
        float a = std::max(0.0f, std::min(1.0f, f.life / std::max(0.1f, f.max_life)));
        c.a = uint8_t(255 * a);
        Font& font = f.big ? *ctx_.fonts.med : *ctx_.fonts.small;
        draw_text_centered(s, font, f.text, int(f.x), int(f.y), c);
    }
}

}  // namespace

std::unique_ptr<Scene> make_void_raid(const Context& ctx) {
    return std::make_unique<VoidRaidGame>(ctx);
}
