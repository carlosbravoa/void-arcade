// space_invaders.cpp — Void Invaders.
// A progression shooter: stacking power-ups build toward "massive destruction",
// armored swarms get meaner each wave, and every 5th wave is a big, fat, blocky,
// googly-eyed boss that does something silly. NUKE (B) is the great equaliser.
#include "space_invaders.h"

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
const Color BLACK(0, 0, 0), WHITE(255, 255, 255), GREEN(80, 255, 80);
const Color CYAN(80, 200, 255), RED(255, 60, 60), YELLOW(255, 220, 40);
const Color ORANGE(255, 140, 40), STEEL(170, 185, 205), ELITE_GOLD(230, 200, 120);
constexpr float PI = 3.14159265358979323846f;

constexpr int SCALE = 3;

using SL = std::vector<std::string>;

const SL INVADER_A[2] = {
    {"..#####..", ".#######.", "##.###.##", "#########", ".##.#.##.", "..#...#..", ".#.#.#.#.", "#.#...#.#"},
    {"..#####..", ".#######.", "##.###.##", "#########", ".##.#.##.", "..#...#..", ".#.#.#.#.", ".#.....#."}};
const SL INVADER_B[2] = {
    {".#.....#.", "#.#...#.#", "#.#####.#", "###.#.###", "#########", ".#######.", "..#...#..", ".#.....#."},
    {".#.....#.", "..#...#..", ".#.####..", "###.#.###", "#########", ".#######.", ".#.....#.", "#.......#"}};
const SL INVADER_C[2] = {
    {"...###...", "..#####..", ".#.###.#.", "##.###.##", "#########", "###...###", "#.#.#.#.#", "..#...#.."},
    {"...###...", "..#####..", ".#.###.#.", "##.###.##", "#########", "###...###", ".#.#.#.#.", "#.......#"}};
const SL ARMOR_SP[2] = {  // chunky armored grunt
    {"#########", "#.#####.#", "#########", "##.###.##", "#########", "#.#...#.#", "##.###.##", "#.#...#.#"},
    {"#########", "#.#####.#", "#########", "##.###.##", "#########", "#.#...#.#", "#.#...#.#", "##.#.#.##"}};
const SL PLAYER_SP = {"....#....", "...###...", "...###...", ".#######.", "#########", "#########", "#.#.#.#.#", "....#...."};
const SL BUNKER_SP = {"..######..", ".########.", "##########", "##########", "##########", "###....###", "##......##"};
const SL UFO_SP = {"....####....", "..########..", ".##.####.##.", "############", ".#.##.##.#..", "..#......#.."};

Color invader_color(char k) {
    if (k == 'A') return Color(200, 100, 255);
    if (k == 'B') return Color(100, 220, 255);
    if (k == 'D') return STEEL;                  // armored
    if (k == 'E') return ELITE_GOLD;             // elite captain
    return Color(100, 255, 140);                 // C
}

bool overlap(Rect a, Rect b) {
    return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
}
std::string zpad(int v, int width) {
    std::string s = std::to_string(v);
    if ((int)s.size() < width) s = std::string(width - s.size(), '0') + s;
    return s;
}

// ── Bullet (now supports angled travel + piercing + damage) ──────────────────
struct Bullet {
    float x, y, vx = 0;
    bool up;
    Color color;
    float speed;       // vertical magnitude
    bool alive = true;
    int w = 3, h = 14;
    int pierce = 0;    // enemies it can pass through
    int damage = 1;

    Rect rect() const { return {int(x - w / 2 - 1), up ? int(y - h) : int(y), w + 2, h}; }
    std::pair<int, int> tip() const { return {int(x), up ? int(y - h) : int(y + h)}; }
    void update(float dt, int sw, int sh) {
        x += vx * dt;
        y += (up ? -speed : speed) * dt;
        if (y < -20 || y > sh + 20 || x < -20 || x > sw + 20) alive = false;
    }
    void draw(Surface& s) const {
        if (!alive) return;
        int y_tip = up ? int(y - h) : int(y + h);
        draw_glow_bolt(s, color, int(x), y_tip, int(y), std::max(2, w / 2));
    }
};

// ── Power-ups ────────────────────────────────────────────────────────────────
enum class PU { Rapid, Spread, Side, Pierce, Shield, Life, Nuke, Rebuild, Mystery };
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
        case PU::Rebuild: return {"X", Color(120, 230, 160)};  // wrench: rebuild a bunker
        default:         return {"?", Color(255, 255, 255)};
    }
}
struct PowerUp {
    float x, y, t = 0;
    PU kind;
    bool alive = true;
    static constexpr float VY = 140;  // fall a bit quicker — you must commit to a grab
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

// ── Player ───────────────────────────────────────────────────────────────────
struct Player {
    static constexpr float SPEED = 300;
    float x, y;
    bool alive = true;
    float fire_cd = 0, invincible = 0;
    int lives = 3;
    std::shared_ptr<Surface> surf;
    int glow_off = 10, w, h;

    Player(int px, int py) : x(px), y(py) {
        Surface base = make_sprite_surface(PLAYER_SP, CYAN, SCALE);
        surf = glow_surface(base, CYAN, 10);
        auto sz = sprite_size(PLAYER_SP, SCALE);
        w = sz.first; h = sz.second;
    }
    Rect rect() const { return {int(x), int(y), w, h}; }
    std::pair<int, int> center() const { return {int(x + w / 2.0f), int(y + h / 2.0f)}; }
    void update(float dt, float move_dir, int screen_w) {
        if (!alive) return;
        if (invincible > 0) invincible -= dt;
        if (fire_cd > 0) fire_cd -= dt;
        x += move_dir * SPEED * dt;
        x = std::max(0.0f, std::min(float(screen_w - w), x));
    }
    void kill() { alive = false; lives--; }
    void respawn(int nx) { alive = true; x = nx; invincible = 2.0f; fire_cd = 0; }
    void draw(Surface& s) const {
        if (!alive) return;
        if (invincible > 0 && int(invincible * 8) % 2 == 0) return;
        s.blit(*surf, int(x) - glow_off, int(y) - glow_off);
    }
};

// ── Invader ──────────────────────────────────────────────────────────────────
struct InvaderFrames { std::shared_ptr<Surface> f[2], flash[2]; };
struct Invader {
    int col, row;
    float x, y;
    char kind;
    bool alive = true;
    int frame = 0, hp = 1, max_hp = 1;
    float hit_flash = 0;
    int glow_off = 8, w, h;
    InvaderFrames* frames;

    Rect rect() const { return {int(x), int(y), w, h}; }
    std::pair<int, int> center() const { return {int(x + w / 2.0f), int(y + h / 2.0f)}; }
    Color color() const { return invader_color(kind); }
    int points() const {
        int base = kind == 'A' ? 30 : kind == 'B' ? 20 : kind == 'D' ? 50 : kind == 'E' ? 80 : 10;
        return base;
    }
    void toggle_frame() { frame ^= 1; }
    void draw(Surface& s) const {
        if (!alive) return;
        s.blit(*frames->f[frame], int(x) - glow_off, int(y) - glow_off);
        if (hit_flash > 0) {  // shape-correct white flicker when chipped
            Surface& fl = *frames->flash[frame];
            fl.set_alpha(int(200 * std::min(1.0f, hit_flash)));
            s.blit(fl, int(x) - glow_off, int(y) - glow_off);
            fl.set_alpha(-1);
        }
        if (max_hp > 1 && hp < max_hp) {  // armour remaining, once it has been chipped
            int bw = w - 4, fw = std::max(1, bw * hp / max_hp);
            draw::rect(s, Color(30, 34, 40), {int(x) + 2, int(y) + h + 3, bw, 2});
            draw::rect(s, hp * 2 <= max_hp ? ORANGE : WHITE, {int(x) + 2, int(y) + h + 3, fw, 2});
        }
    }
};

// Branching waves: a normal wave, or an ELITE (fewer, all-armored, tankier,
// worth more) or SWARM (more, weaker, faster) variant. Decided per non-boss wave.
enum class WaveKind { Normal, Elite, Swarm };

// ── InvaderGrid (scales with wave) ───────────────────────────────────────────
struct InvaderGrid {
    static constexpr int COLS = 11, ROWS = 5, H_STEP = 52, V_STEP = 46, DESCEND = 16;
    int screen_w, wave;
    int dx = 1;
    float speed;
    float shoot_factor;
    WaveKind kind = WaveKind::Normal;
    float point_mult = 1.0f;  // ELITE invaders are worth more
    std::vector<Invader> invaders;
    std::vector<Bullet> bullets;
    float anim_t = 0, shoot_t = 0, rest_t = 0;
    float slide_left = 0;      // px still to slide down during the wave intro
    int burst_count = 0;
    bool anim_ticked = false;
    InvaderFrames frame_cache[5];  // A, B, C, armored(D), elite captain(E)
    static constexpr float SLIDE = 320.0f;

    // `tier` = the player's current weapon tier: an ELITE wave met with a bare
    // gun gets slightly thinner armour so it isn't a multi-minute slog.
    InvaderGrid(int sw, int top_y, int wv, WaveKind wk = WaveKind::Normal, int tier = 0)
        : screen_w(sw), wave(wv), kind(wk) {
        speed = std::min(180.0f, 50.0f + wv * 7.0f);
        shoot_factor = std::max(0.25f, 0.8f - wv * 0.04f);  // meaner trigger fingers
        // SWARM is faster & jumpier; ELITE is a touch slower but relentless.
        if (kind == WaveKind::Swarm)  { speed *= 1.25f; shoot_factor *= 0.85f; }
        if (kind == WaveKind::Elite)  { speed *= 0.90f; point_mult = 2.0f; }
        const SL* maps[5] = {INVADER_A, INVADER_B, INVADER_C, ARMOR_SP, ARMOR_SP};
        char kinds[5] = {'A', 'B', 'C', 'D', 'E'};
        for (int i = 0; i < 5; ++i) {
            Color c = invader_color(kinds[i]);
            for (int f = 0; f < 2; ++f) {
                frame_cache[i].f[f] = glow_surface(make_sprite_surface(maps[i][f], c, SCALE), c, 8);
                frame_cache[i].flash[f] =
                    glow_surface(make_sprite_surface(maps[i][f], WHITE, SCALE), WHITE, 8);
            }
        }
        int kind_map[ROWS] = {0, 1, 1, 2, 2};
        float armor_p = std::min(0.6f, std::max(0.0f, (wv - 2) * 0.07f));  // more armor later
        int armor_hp = 2 + wv / 6;                                         // and tankier
        // SWARM packs extra columns of fodder; ELITE drops to a smaller, all-armored block.
        int cols = COLS, rows = ROWS;
        int h_step = H_STEP, v_step = V_STEP, x0 = 60;
        if (kind == WaveKind::Swarm) { h_step = 44; v_step = 40; x0 = 36; cols = 13; }
        if (kind == WaveKind::Elite) { cols = 8; x0 = 110; }
        slide_left = SLIDE;
        for (int row = 0; row < rows; ++row)
            for (int col = 0; col < cols; ++col) {
                Invader inv;
                inv.col = col; inv.row = row;
                inv.x = x0 + col * h_step; inv.y = top_y + row * v_step - SLIDE;
                bool armored;
                if (kind == WaveKind::Elite)      armored = true;                       // all tanky
                else if (kind == WaveKind::Swarm) armored = false;                       // all fodder
                else                              armored = (wv >= 3) && rnd01() < armor_p;
                int ki = armored ? 3 : kind_map[row];
                int hp = armored ? armor_hp : 1;
                if (kind == WaveKind::Elite) {
                    hp = armor_hp + (tier >= 2 ? 1 : 0);   // steel rank and file
                    if (row == 0) { ki = 4; hp += 1; }     // a gold row of captains on top
                }
                inv.kind = kinds[ki];
                inv.frames = &frame_cache[ki];
                inv.hp = inv.max_hp = hp;
                auto sz = sprite_size(maps[ki][0], SCALE);
                inv.w = sz.first; inv.h = sz.second;
                invaders.push_back(inv);
            }
    }
    int alive_count() const { int n = 0; for (auto& i : invaders) if (i.alive) ++n; return n; }
    float alive_ratio() const {
        return std::clamp(alive_count() / float(std::max<size_t>(1, invaders.size())), 0.0f, 1.0f);
    }
    float anim_interval() const { return 0.06f + (0.7f - 0.06f) * alive_ratio(); }
    float shoot_interval() const { return (0.4f + (1.8f - 0.4f) * alive_ratio()) * shoot_factor; }
    // Lowest edge of the living swarm (very negative when none are left).
    float bottom() const {
        float b = -1e9f;
        for (auto& i : invaders) if (i.alive) b = std::max(b, i.y + i.h);
        return b;
    }
    // Wave intro: the block glides down into place. `rate` is px/s.
    void slide_in(float dt, float rate) {
        if (slide_left <= 0) return;
        float d = std::min(slide_left, rate * dt);
        slide_left -= d;
        for (auto& i : invaders) i.y += d;
    }
    void finish_slide() { slide_in(1.0f, 1e6f); }

    // `march` = the swarm advances; `fire` = it may shoot. Bullets always fly.
    void update(float dt, int screen_h, float player_x, bool march, bool fire) {
        anim_ticked = false;
        for (auto& i : invaders) if (i.hit_flash > 0) i.hit_flash -= dt * 5;
        std::vector<Invader*> alive;
        for (auto& i : invaders) if (i.alive) alive.push_back(&i);
        if (!alive.empty() && march) {
            float move = speed * dt * dx;
            for (auto* inv : alive) inv->x += move;
            float right = -1e9f, left = 1e9f;
            for (auto* inv : alive) { right = std::max(right, inv->x + inv->w); left = std::min(left, inv->x); }
            // Bounce only when heading INTO that wall, and pull the block back
            // inside it — otherwise a long frame followed by a short one leaves
            // it "still at the edge" and it flips (and drops a row) every frame.
            float over = 0;
            if (dx > 0 && right >= screen_w - 10) over = right - (screen_w - 10);
            else if (dx < 0 && left <= 10)        over = left - 10;
            if (over != 0 || (dx > 0 && right >= screen_w - 10) || (dx < 0 && left <= 10)) {
                for (auto* inv : alive) { inv->x -= over; inv->y += DESCEND; }
                dx = -dx;
                speed = std::min(speed + 4, 240.0f);
            }
            anim_t += dt;
            if (anim_t >= anim_interval()) {
                anim_t = 0; anim_ticked = true;
                for (auto* inv : alive) inv->toggle_frame();
            }
        }
        if (!alive.empty() && march && fire) {
            shoot_t += dt;
            if (rest_t > 0) {
                rest_t -= dt;  // breather between bursts: always a window to escape
            } else if (shoot_t >= shoot_interval()) {
                shoot_t = 0;
                int alive_n = (int)alive.size();
                int volleys = 1 + (wave >= 4 ? 1 : 0) + (wave >= 9 ? 1 : 0) + (wave >= 15 ? 1 : 0);
                volleys = std::min(volleys, alive_n);  // one enemy can't carpet the screen
                // ease the aiming off when few remain so a pinned player can break out
                float aim_p = alive_n <= 3 ? 0.30f : std::min(0.6f, 0.35f + wave * 0.03f);
                for (int v = 0; v < volleys; ++v) {
                    Invader* sh = nullptr;
                    if (rnd01() < aim_p) {  // the invader above the player drops one straight down
                        float best = 1e9f;
                        for (auto* inv : alive) {
                            float d = std::fabs(inv->x + inv->w / 2 - player_x);
                            if (d < best) { best = d; sh = inv; }
                        }
                    } else {
                        sh = alive[rnd_int(0, alive_n - 1)];
                    }
                    Color c = sh->color();
                    Color bc(std::min(255, c.r + 100), std::max(0, c.g - 80), std::max(0, c.b - 100));
                    Bullet b; b.x = sh->x + sh->w / 2.0f; b.y = sh->y + sh->h;
                    b.up = false; b.color = bc; b.speed = 220 + wave * 7;
                    bullets.push_back(b);
                }
                // fire in bursts, then take a beat (a longer one when few remain)
                if (++burst_count >= rnd_int(8, 12)) {
                    burst_count = 0;
                    rest_t = alive_n <= 2 ? 1.6f : 0.9f;
                }
            }
        }
        for (auto& b : bullets) b.update(dt, screen_w, screen_h);
        bullets.erase(std::remove_if(bullets.begin(), bullets.end(),
                      [](const Bullet& b) { return !b.alive; }), bullets.end());
    }
    void draw(Surface& s) const {
        for (auto& inv : invaders) inv.draw(s);
        for (auto& b : bullets) b.draw(s);
    }
};

// ── BunkerGrid ───────────────────────────────────────────────────────────────
struct BunkerCell { Rect rect; bool alive; int bunker; };
struct BunkerGrid {
    static constexpr int BSCALE = 4, NBUNKERS = 4;
    std::vector<BunkerCell> cells;
    float center_x[NBUNKERS] = {0};  // for picking the nearest bunker to rebuild
    int top_y = 0;
    BunkerGrid(int screen_w, int screen_h) {
        int bw = int(BUNKER_SP[0].size()) * BSCALE;
        int gap = screen_w / 5, y = screen_h - 160;
        top_y = y;
        for (int b = 0; b < NBUNKERS; ++b) {
            int bx = gap * (b + 1) - bw / 2;
            center_x[b] = bx + bw / 2.0f;
            for (int ri = 0; ri < (int)BUNKER_SP.size(); ++ri)
                for (int ci = 0; ci < (int)BUNKER_SP[ri].size(); ++ci)
                    if (BUNKER_SP[ri][ci] == '#')
                        cells.push_back({{bx + ci * BSCALE, y + ri * BSCALE, BSCALE, BSCALE}, true, b});
        }
    }
    // Restore a chunk of the bunker nearest to px: revive up to `amount` dead
    // cells (preferring lower rows so the cap regrows). Returns true if anything
    // was rebuilt. Used by the REBUILD power-up.
    bool rebuild_near(float px, int amount, ParticleSystem& particles) {
        int best = 0; float bd = 1e9f;
        for (int b = 0; b < NBUNKERS; ++b) {
            float d = std::fabs(center_x[b] - px);
            if (d < bd) { bd = d; best = b; }
        }
        std::vector<BunkerCell*> dead;
        for (auto& c : cells) if (c.bunker == best && !c.alive) dead.push_back(&c);
        if (dead.empty()) return false;
        // lower y (higher on screen) first so the silhouette fills top-down
        std::sort(dead.begin(), dead.end(),
                  [](const BunkerCell* a, const BunkerCell* b) { return a->rect.y < b->rect.y; });
        int n = std::min((int)dead.size(), amount);
        for (int i = 0; i < n; ++i) {
            dead[i]->alive = true;
            particles.bunker_hit(dead[i]->rect.x + dead[i]->rect.w / 2.0f,
                                 dead[i]->rect.y + dead[i]->rect.h / 2.0f, 4);
        }
        return true;
    }
    bool check_bullet(Bullet& bullet, ParticleSystem& particles) {
        for (auto& cell : cells) {
            if (!cell.alive) continue;
            if (overlap(cell.rect, bullet.rect())) {
                cell.alive = false; bullet.alive = false;
                particles.bunker_hit(cell.rect.x + cell.rect.w / 2.0f, cell.rect.y + cell.rect.h / 2.0f);
                return true;
            }
        }
        return false;
    }
    // Gouge a hole of the given radius at the bullet's impact (used by both
    // power-scaled player shots and wave-scaled enemy shots).
    bool blast(Bullet& b, int radius, ParticleSystem& particles) {
        int ix = 0, iy = 0; bool hit = false;
        for (auto& cell : cells)
            if (cell.alive && overlap(cell.rect, b.rect())) {
                ix = cell.rect.x + cell.rect.w / 2; iy = cell.rect.y + cell.rect.h / 2; hit = true; break;
            }
        if (!hit) return false;
        for (auto& cell : cells) {
            if (!cell.alive) continue;
            int dx = cell.rect.x + cell.rect.w / 2 - ix, dy = cell.rect.y + cell.rect.h / 2 - iy;
            if (dx * dx + dy * dy <= radius * radius) cell.alive = false;
        }
        particles.bunker_hit(float(ix), float(iy));
        return true;
    }
    // Anything solid ploughing through (a descending invader, a slamming boss)
    // flattens the cells it covers. Returns how many were destroyed.
    int crush(Rect r, ParticleSystem& particles) {
        int n = 0;
        for (auto& cell : cells) {
            if (!cell.alive || !overlap(cell.rect, r)) continue;
            cell.alive = false;
            if (++n % 4 == 1)
                particles.bunker_hit(cell.rect.x + cell.rect.w / 2.0f, cell.rect.y + cell.rect.h / 2.0f, 4);
        }
        return n;
    }
    void draw(Surface& s) const {
        for (auto& cell : cells) {
            if (!cell.alive) continue;
            Rect r = cell.rect;
            draw::rect(s, Color(80, 255, 80, 40), {r.x - 2, r.y - 2, r.w + 4, r.h + 4});
            draw::rect(s, Color(80, 255, 80), r);
        }
    }
};

// ── MysteryShip ──────────────────────────────────────────────────────────────
// Usually a quick fly-by for bonus points. Occasionally spawns as a MINI-BOSS:
// bigger, gold, with HP, that fires aimed shots down as it crosses and drops a
// GUARANTEED power-up when destroyed. Telegraphed by size, colour and a pulsing halo.
struct MysteryShip {
    static constexpr float SPEED = 160, MINI_SPEED = 90;
    static const Color MINI_COLOR;
    int screen_w;
    bool active = false;
    float x = 0, y = 38;   // its own lane: below the HUD text, above the swarm
    int dx = 1, score_val = 100;
    float spawn_cd;
    std::shared_ptr<Surface> surf, mini_surf;
    int glow_off = 12, w, h, mini_glow = 16, mini_w, mini_h;
    // mini-boss state
    bool mini = false;
    int hp = 0, max_hp = 0;
    float pulse_t = 0, fire_t = 0;
    int wave = 1;

    MysteryShip(int sw, int wv = 1) : screen_w(sw), wave(wv) {
        spawn_cd = rnd_uniform(12, 24);
        surf = glow_surface(make_sprite_surface(UFO_SP, RED, SCALE), RED, 12);
        auto sz = sprite_size(UFO_SP, SCALE); w = sz.first; h = sz.second;
        mini_surf = glow_surface(make_sprite_surface(UFO_SP, MINI_COLOR, SCALE + 2), MINI_COLOR, 16);
        auto ms = sprite_size(UFO_SP, SCALE + 2); mini_w = ms.first; mini_h = ms.second;
    }
    Rect rect() const {
        if (mini) return {int(x), int(y), mini_w, mini_h};
        return {int(x), int(y), w, h};
    }
    std::pair<int, int> center() const {
        int cw = mini ? mini_w : w, ch = mini ? mini_h : h;
        return {int(x + cw / 2.0f), int(y + ch / 2.0f)};
    }
    // out/parts let the mini-boss spit aimed bullets at the player. Returns true
    // the moment it spawns as a mini-boss (so the game can telegraph it).
    // `can_spawn` / `can_fire` are false outside live play (intro, wave clear).
    bool update(float dt, float player_x, std::vector<Bullet>& out, bool can_spawn, bool can_fire) {
        pulse_t += dt;
        if (!active) {
            if (!can_spawn) return false;
            spawn_cd -= dt;
            if (spawn_cd <= 0) {
                active = true; dx = rnd_choice<int>({-1, 1});
                // mini-boss chance climbs a little with the wave, capped.
                float mini_p = std::min(0.30f, 0.10f + wave * 0.02f);
                mini = (wave >= 3) && rnd01() < mini_p;
                if (mini) {
                    int cw = mini_w;
                    x = dx == 1 ? -cw : screen_w;
                    max_hp = hp = 6 + wave;             // tanky vs. a 1-shot fly-by
                    score_val = 500;
                    fire_t = 0.6f;
                } else {
                    x = dx == 1 ? -w : screen_w;
                    score_val = rnd_choice<int>({50, 100, 150, 300});
                }
                return mini;
            }
            return false;
        }
        float spd = mini ? MINI_SPEED : SPEED;
        x += dx * spd * dt;
        if (mini && can_fire) {  // lob a few aimed shots down as it crosses
            fire_t -= dt;
            if (fire_t <= 0) {
                fire_t = rnd_uniform(0.7f, 1.1f);
                auto [cx, cy] = center();
                float aim = std::atan2(player_x - cx, 260.0f);
                Bullet b; b.x = float(cx); b.y = float(cy) + mini_h / 2.0f; b.up = false;
                b.speed = 220 * std::cos(aim); b.vx = 220 * std::sin(aim);
                b.color = MINI_COLOR; b.w = 5; b.h = 12;
                out.push_back(b);
            }
        }
        int cw = mini ? mini_w : w;
        if (x > screen_w + cw || x < -cw * 2) reset_idle();
        return false;
    }
    void reset_idle() {
        active = false; mini = false; spawn_cd = rnd_uniform(12, 24);
    }
    // Returns score on kill, -1 if it merely took a hit (mini-boss soaking).
    int hit() {
        if (mini && --hp > 0) return -1;
        int v = score_val;
        reset_idle();
        return v;
    }
    void draw(Surface& s) const {
        if (!active) return;
        if (mini) {
            // pulsing warning halo so it reads as a threat, not a freebie
            float p = 0.5f + 0.5f * std::sin(pulse_t * 12);
            draw::rect(s, Color(255, 200, 60, int(90 * p)),
                       {int(x) - 10, int(y) - 10, mini_w + 20, mini_h + 20}, 0, 10);
            s.blit(*mini_surf, int(x) - mini_glow, int(y) - mini_glow);
            // health pips above it
            int n = std::max(0, max_hp);
            for (int i = 0; i < n; ++i) {
                bool on = i < hp;
                draw::rect(s, on ? MINI_COLOR : Color(40, 40, 40),
                           {int(x) + i * 7, int(y) - 6, 5, 3});
            }
            return;
        }
        s.blit(*surf, int(x) - glow_off, int(y) - glow_off);
    }
};
const Color MysteryShip::MINI_COLOR(255, 200, 60);  // gold = special

// ── Boss ─────────────────────────────────────────────────────────────────────
struct BossDef { const char* name; Color color; int w, h, hp; float atk_iv; int attack; int face; int cell; };
const BossDef BOSSES[4] = {
    {"GLOOPID",    Color(90, 230, 120), 210, 150, 30, 1.5f,  0, 0, 18},
    {"THE MAW",    Color(200, 90, 255), 300, 150, 52, 1.2f,  1, 1, 20},
    {"OVERSEER",   Color(90, 170, 255), 340, 180, 84, 1.05f, 2, 2, 22},
    {"VOID TITAN", Color(255, 80, 80),  430, 240, 150, 0.9f, 3, 3, 26},
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
    static constexpr float DEATH_DUR = 1.5f;
    // grand entrance: a few seconds in, the boss stomps down through the swarm
    bool entering = false, just_stomped = false;
    float enter_delay = 0, stomp_t = 0, battle_y = 80, invuln_ = 0;

    // attack system: HP phases, telegraphed pick, one-shot + sustained patterns
    int tier = 1, phase = 1, cur_attack = -1, pending_attack = -1, slam_phase = 0, sprite_idx = 0;
    float attack_t = 0, telegraph = 0, sub_t = 0, spin = 0, laser_angle = 0;
    bool shock_done = false;

    bool damageable() const { return !dying && !faking && !entering && invuln_ <= 0; }

    Boss(int wave, int screen_w) {
        tier = wave / 5;                      // 1,2,3,...
        int idx = (tier - 1) % 4;
        sprite_idx = idx;
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
        entering = true; enter_delay = 2.6f; battle_y = 80;
    }
    Rect rect() const { return {int(x), int(y), w, h}; }
    float hp_ratio() const { return std::max(0.0f, float(hp) / max_hp); }

    static Bullet bb(float bx, float by, float a, float spd) {
        Bullet b; b.x = bx; b.y = by; b.up = false;
        b.speed = spd * std::cos(a); b.vx = spd * std::sin(a);
        b.color = ORANGE; b.w = 5; b.h = 12; b.damage = 1;
        return b;
    }
    // 0 LOB 1 FAN 2 RADIAL 3 AIMED 4 WALL(one-shot) | 5 SPIRAL 6 RAIN 7 LASER 8 SLAM(sustained)
    int pick_attack() {
        // pool grows with HP-phase; the scary sustained attacks are gated by the
        // boss TIER so the wave-5 first boss stays gentle and later bosses terrify.
        std::vector<int> pool = {attack, 0, 2};       // signature, LOB, RADIAL (simple, dodgeable)
        if (phase >= 2) {
            pool.insert(pool.end(), {1, 3});          // FAN, AIMED — tier-1 boss tops out here
            if (tier >= 2) pool.insert(pool.end(), {4, 5});  // WALL, SPIRAL
        }
        if (phase >= 3) {
            pool.push_back(3);                        // weight aimed up
            if (tier >= 2) { pool.push_back(6); pool.push_back(8); }  // RAIN, SLAM
            if (tier >= 3) pool.push_back(7);         // LASER
        }
        return pool[rnd_int(0, (int)pool.size() - 1)];
    }
    void begin_attack(int a) {
        cur_attack = a; attack_t = 0; sub_t = 0; slam_phase = 0; shock_done = false;
        if (a == 1 || a == 7) mouth_open = 0.7f;
    }
    void run_attack(float dt, float pcx, float pcy, std::vector<Bullet>& out) {
        attack_t += dt; sub_t += dt;
        float bx = x + w / 2, by = y + h - 8;
        switch (cur_attack) {
            case 0:  // LOB
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
            case 4: {  // WALL of bullets with ONE safe gap to find
                int gap = rnd_int(1, 9);
                for (int i = 0; i < 11; ++i) {
                    if (std::abs(i - gap) <= 1) continue;
                    float px = x + (i + 0.5f) * (w / 11.0f);
                    Bullet b; b.x = px; b.y = by; b.up = false; b.vx = 0; b.speed = 195;
                    b.color = ORANGE; b.w = 5; b.h = 12; out.push_back(b);
                }
                cur_attack = -1; break;
            }
            // Sustained patterns emit on a fixed cadence (`sub_t -= interval`) so the
            // bullet density is the same at 60, 144 or 240 Hz.
            case 5:  // SPIRAL: rotating spray
                while (sub_t >= 0.07f) {
                    sub_t -= 0.07f; spin += 0.5f;
                    out.push_back(bb(bx, by, spin, 200));
                    out.push_back(bb(bx, by, spin + 3.14159f, 200));
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
            case 8: {  // SLAM: wind up, dive onto the player's row, shockwave, return
                // The plunge is measured from the player's row, so the body really
                // does come down on the ship (and through any bunker in the way).
                float target = std::max(battle_y + 120.0f, pcy + 6.0f - h);
                if (slam_phase == 0) { if (attack_t >= 0.45f) slam_phase = 1; }
                else if (slam_phase == 1) { y += 900 * dt; if (y >= target) { y = target; slam_phase = 2; } }
                else if (slam_phase == 2) {
                    if (!shock_done) {
                        shock_done = true;
                        for (int i = 0; i < 14; ++i)
                            out.push_back(bb(x + w / 2, y + h - 8, -1.5f + i * (3.0f / 13), 240));
                    }
                    slam_phase = 3;
                } else { y -= 700 * dt; if (y <= battle_y) { y = battle_y; cur_attack = -1; } }
                break;
            }
            default: cur_attack = -1; break;
        }
    }

    void take_damage(int dmg) {
        if (!damageable()) return;
        hp -= dmg; hit_flash = 0.14f;
        if (hp <= 0) { dying = true; die_t = 0; hp = 0; return; }
        // The unexpected: a one-time fake-out death gag around a quarter health.
        if (!did_fake && hp <= max_hp * 0.27f && rnd01() < 0.45f) {
            did_fake = true; faking = true; fake_t = 0; fake_started = true;
        }
    }

    void update(float dt, float player_cx, float player_cy, int screen_w,
                std::vector<Bullet>& out, ParticleSystem& parts) {
        if (entering) {  // lurk above, then stomp down in heavy steps
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
            if (!finished && rnd01() < 36.0f * dt) {  // confetti / chained blasts (dt-scaled)
                float rx = x + rnd_uniform(0, w), ry = y + rnd_uniform(0, h);
                parts.ufo_explode(rx, ry, 10);
            }
            if (die_t >= DEATH_DUR) finished = true;
            return;
        }
        if (faking) {  // deflate, sulk, then bounce back bigger
            fake_t += dt;
            if (fake_t >= 1.1f) { faking = false; hp = int(max_hp * 0.5f); }
            return;
        }
        // sway
        x += vx * dt;
        if (x < 6) { x = 6; vx = std::fabs(vx); }
        if (x + w > screen_w - 6) { x = screen_w - 6 - w; vx = -std::fabs(vx); }
        if (mouth_open > 0) mouth_open -= dt;

        // ── phases: the boss gets faster & nastier as its health drops ──
        phase = hp_ratio() > 0.6f ? 1 : hp_ratio() > 0.3f ? 2 : 3;
        float aggr = (tier <= 1) ? 0.85f : 0.55f;  // the first boss stays calm even at low HP
        float iv = atk_iv * (phase == 3 ? aggr : phase == 2 ? 0.85f : 1.0f);

        if (telegraph > 0) {                 // winding up the next attack (readable warning ring)
            telegraph -= dt;
            if (telegraph <= 0) begin_attack(pending_attack);
        } else if (cur_attack >= 0) {        // a sustained attack is running
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

    void draw(Surface& s, float pcx, float pcy) const {
        if (y + h < 0) return;  // still fully above the screen
        float sc = dying ? std::max(0.0f, 1 - die_t / DEATH_DUR)
                         : faking ? (1.0f - 0.4f * std::sin(PI * std::min(1.0f, fake_t / 1.1f)))
                                  : 1.0f;
        int bw = int(w * sc), bh = int(h * sc);
        int bx = int(x + (w - bw) / 2), by = int(y + (h - bh) / 2 + std::sin(bob * 2) * 4);
        if (bw < 4 || bh < 4) return;
        Color body = hit_flash > 0 ? lerp_color(color, WHITE, 0.6f) : color;
        // soft outer glow halo
        draw::rect(s, Color(body.r, body.g, body.b, 70), {bx - 8, by - 8, bw + 16, bh + 16}, 0, cell + 4);
        // a GIANT INVADER: scaled-up pixel-art alien, legs animate
        draw_boss_alien(s, sprite_idx, int(bob * 3) % 2, bx, by, bw, bh, body);
        if (invuln_ > 0) {  // spawn-shield shimmer: can't be hurt yet
            float p = 0.5f + 0.5f * std::sin(bob * 18);
            draw::rect(s, Color(120, 200, 255, int(130 * p)), {bx - 8, by - 8, bw + 16, bh + 16}, 3, cell + 6);
        }
        if (telegraph > 0) {  // wind-up warning ring (red, pulsing) so attacks are readable
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

// ── Game ─────────────────────────────────────────────────────────────────────
// The world keeps simulating in every state except GameOver: Intro / Dead / Win
// only gate what the player and the enemies are allowed to do, so nothing ever
// hangs frozen in mid-air and drops stay collectable between waves.
enum class St { Intro, Play, Dead, Win, GameOver };

class SpaceInvadersGame : public Scene {
public:
    explicit SpaceInvadersGame(const Context& ctx)
        : Scene(ctx), pause_(ctx.W, ctx.H, ctx.fonts), initials_(ctx.W, ctx.H, ctx.fonts) {
        high_score_ = ctx.scores ? ctx.scores->high_score("space_invaders") : 0;
        new_game();
    }
    void update(float dt, const Input& in) override;
    void draw(Surface& s) override;
    void on_exit() override {
        submit_scores();
        if (ctx_.sfx) { ctx_.sfx->stop_ufo(); ctx_.sfx->stop_ambient(); }
    }

private:
    static constexpr float AMBIENT_STAR_T = 0.15f;
    static constexpr float INTRO_T = 1.5f;
    static constexpr int GRID_TOP = 78;
    static constexpr int CAP_RAPID = 3, CAP_SPREAD = 2, CAP_SIDE = 2;
    PauseMenu pause_;
    InitialsEntry initials_;
    bool entry_started_ = false;
    ParticleSystem particles_;
    int high_score_ = 0;
    float star_t_ = 0, anim_t_ = 0;
    bool pad_ = false;          // a gamepad is connected → pad-flavoured prompts

    int score_, wave_;
    std::unique_ptr<Player> player_;
    std::unique_ptr<InvaderGrid> grid_;
    std::unique_ptr<BunkerGrid> bunkers_;
    std::unique_ptr<MysteryShip> ufo_;
    std::unique_ptr<Boss> boss_;
    std::vector<Bullet> p_bullets_, boss_bullets_;
    std::vector<PowerUp> powerups_;
    St state_;
    float dead_t_ = 0, win_t_ = 0, intro_t_ = 0, game_over_t_ = 0;
    bool boss_wave_ = false, boss_revealed_ = false, nuke_hint_ = false;
    bool invaded_ = false;      // the swarm landed: this death ends the run
    bool new_hi_ = false;
    WaveKind wave_kind_ = WaveKind::Normal;
    std::vector<Flash> flash_;
    bool submitted_ = false;

    // weapon progression (persists across the run)
    int w_rapid_ = 0, w_spread_ = 0, w_side_ = 0, nukes_ = 0;
    int kills_since_drop_ = 0;
    float shield_ = 0, nuke_flash_ = 0;
    float pierce_t_ = 0;        // PIERCE is a timed buff
    float deflect_fx_ = 0;      // shield / spawn-guard just ate a shot
    float tink_cd_ = 0;         // rate-limits the "deflected" sound

    // bot + telemetry
    bool bot_ = false;
    float game_t_ = 0, wave_t0_ = 0;
    int wave_kills_ = 0, wave_shots_ = 0, wave_pu_ = 0, wave_deaths_ = 0, wave_nukes_ = 0;

    std::string loadout() const {
        return "r" + std::to_string(w_rapid_) + "s" + std::to_string(w_spread_) +
               "w" + std::to_string(w_side_) + "p" + std::to_string(pierce_t_ > 0 ? 1 : 0);
    }
    int bunkers_left() const {
        int n = 0; for (auto& c : bunkers_->cells) if (c.alive) ++n; return n;
    }
    void tele(const J& j) { if (ctx_.tele) ctx_.tele->event(j); }
    void bot_decide(float& move_dir, bool& want_fire, bool& want_nuke);

    int weapon_tier() const { return w_rapid_ + w_spread_ + w_side_; }

    void new_game() {
        score_ = 0; wave_ = 0;
        entry_started_ = false; submitted_ = false; new_hi_ = false;
        w_rapid_ = w_spread_ = w_side_ = nukes_ = 0;
        kills_since_drop_ = 0;
        nuke_hint_ = false; invaded_ = false;
        shield_ = nuke_flash_ = pierce_t_ = deflect_fx_ = tink_cd_ = 0;
        game_over_t_ = 0;
        player_ = std::make_unique<Player>(ctx_.W / 2 - 15, ctx_.H - 70);
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
        if (ctx_.sfx && !ctx_.demo) ctx_.sfx->start_music(0);
        start_wave(start);
    }
    void start_wave(int w) {
        wave_ = w;
        boss_wave_ = (w % 5 == 0);
        if (ctx_.sfx) { ctx_.sfx->stop_ufo(); ctx_.sfx->start_ambient(0); }  // no leftover UFO; keep the bed alive
        p_bullets_.clear(); boss_bullets_.clear(); powerups_.clear();
        ufo_ = std::make_unique<MysteryShip>(ctx_.W, w);
        // Bunkers persist (and stay chipped) across normal stages — they only get
        // rebuilt fresh on the first wave and on the wave right after a boss kill
        // (waves 1, 6, 11, …). Surviving cover is a resource you spend over a run.
        if (!bunkers_ || w % 5 == 1)
            bunkers_ = std::make_unique<BunkerGrid>(ctx_.W, ctx_.H);
        boss_revealed_ = false;
        invaded_ = false;
        // Branching: every few non-boss waves (from wave 3 on) roll ELITE vs SWARM.
        wave_kind_ = WaveKind::Normal;
        if (!boss_wave_ && w >= 3 && (w % 3 == 0)) {
            wave_kind_ = rnd01() < 0.5f ? WaveKind::Elite : WaveKind::Swarm;
        }
        if (boss_wave_) {  // looks like a normal wave... until it doesn't
            grid_ = std::make_unique<InvaderGrid>(ctx_.W, GRID_TOP, w, WaveKind::Normal, weapon_tier());
            boss_ = std::make_unique<Boss>(w, ctx_.W);
        } else {
            grid_ = std::make_unique<InvaderGrid>(ctx_.W, GRID_TOP, w, wave_kind_, weapon_tier());
            boss_.reset();
        }
        // Palette drift: a slow per-wave hue oscillation so deep waves feel
        // different; boss waves get an extra menacing warm push.
        if (ctx_.fx)
            ctx_.fx->palette_hue = 0.18f * std::sin(w * 0.5f) + (boss_wave_ ? -0.12f : 0.0f);
        state_ = St::Intro;
        intro_t_ = INTRO_T;
        wave_t0_ = game_t_;
        wave_kills_ = wave_shots_ = wave_pu_ = wave_deaths_ = wave_nukes_ = 0;
        const char* wk = wave_kind_ == WaveKind::Elite ? "elite"
                       : wave_kind_ == WaveKind::Swarm ? "swarm" : "normal";
        tele(J().add("ev", "wave_start").add("wave", wave_).add("boss", boss_wave_ ? 1 : 0)
                .add("kind", wk)
                .add("t", (double)game_t_).add("load", loadout()).add("lives", player_->lives));
    }
    void add_flash(const std::string& t, float x, float y, Color c, float life = 0.9f, bool big = false) {
        flash_.push_back({t, x, y, life, life, c, big});
    }
    void submit_scores() {
        if (ctx_.demo) return;  // attract demo never touches the leaderboard
        if (submitted_) return;
        submitted_ = true;
        tele(J().add("ev", "game_over").add("wave", wave_).add("score", score_)
                .add("t", (double)game_t_).add("load", loadout()));
        if (ctx_.scores) {
            new_hi_ = ctx_.scores->submit_space_invaders(score_, wave_);
            high_score_ = ctx_.scores->high_score("space_invaders");
        }
    }
    void enter_game_over() {
        state_ = St::GameOver;
        game_over_t_ = 0;
        submit_scores();
        if (ctx_.sfx) { ctx_.sfx->stop_ufo(); ctx_.sfx->game_over(); }
    }

    Color tier_color() const {
        int t = weapon_tier();
        if (t <= 1) return CYAN;
        if (t <= 4) return lerp_color(CYAN, YELLOW, (t - 1) / 3.0f);
        return lerp_color(YELLOW, WHITE, std::min(1.0f, (t - 4) / 4.0f));
    }
    void fire() {
        float delay = std::max(0.16f, 0.50f * (1.0f - 0.09f * w_rapid_));
        if (player_->fire_cd > 0 || !player_->alive) return;
        player_->fire_cd = delay;
        float px = player_->x + player_->w / 2.0f, py = player_->y;
        Color col = tier_color();
        int dmg = 1 + weapon_tier() / 4;
        auto add = [&](float x, float vx) {
            Bullet b; b.x = x; b.y = py; b.vx = vx; b.up = true;
            b.color = col; b.speed = 560; b.pierce = pierce_t_ > 0 ? 2 : 0; b.damage = dmg;
            b.w = 3 + std::min(3, weapon_tier() / 2);
            p_bullets_.push_back(b);
        };
        add(px, 0);
        for (int i = 1; i <= w_side_; ++i) { add(px - i * 11, 0); add(px + i * 11, 0); }
        for (int i = 1; i <= w_spread_; ++i) { add(px, -62.0f * i); add(px, 62.0f * i); }
        wave_shots_ += 1 + 2 * w_side_ + 2 * w_spread_;
        if (ctx_.sfx) ctx_.sfx->shoot();
    }
    // Drops get rarer the stronger you already are — this flattens the
    // "more power -> more kills -> more power" snowball and lets a freshly
    // reset (post-death) player recover fast.
    float drop_penalty() const { return std::clamp(1.0f - weapon_tier() * 0.16f, 0.2f, 1.0f); }
    void spawn_powerup(float x, float y) {
        int r = rnd_int(0, 99);
        PU k;
        if (r < 25) k = PU::Rapid; else if (r < 48) k = PU::Spread; else if (r < 65) k = PU::Side;
        else if (r < 77) k = PU::Pierce; else if (r < 86) k = PU::Shield;
        else if (r < 92) k = PU::Nuke; else if (r < 95) k = PU::Life;
        else if (r < 98) k = PU::Rebuild;  // rare: rebuild a bunker chunk
        else k = PU::Mystery;
        powerups_.push_back({x, y, 0, k});
    }
    void maybe_drop(float x, float y, float base) {
        if (rnd01() < base * drop_penalty()) spawn_powerup(x, y);
    }
    // Per-kill drop with a pity timer so the first power-up never drags.
    void roll_invader_drop(float x, float y) {
        ++kills_since_drop_;
        bool drop = (kills_since_drop_ >= 12 && weapon_tier() < 2) ||
                    rnd01() < 0.10f * drop_penalty();
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
            case PU::Rebuild:
                // Regenerate a chunk of the bunker nearest the player.
                if (bunkers_ && bunkers_->rebuild_near(player_->x + player_->w / 2.0f, 16, particles_)) {
                    msg = "BUNKER REBUILT!";
                    if (ctx_.fx) ctx_.fx->shake(4);
                } else {
                    score_ += 250; msg = "BUNKERS INTACT  +250";
                }
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
        add_flash(msg, player_->x + player_->w / 2, player_->y - 30, YELLOW, 1.2f);
        wave_pu_++;
        tele(J().add("ev", "powerup").add("wave", wave_).add("t", (double)game_t_)
                .add("load", loadout()).add("nukes", nukes_));
        if (ctx_.sfx) ctx_.sfx->powerup_collect();
        if (ctx_.fx) ctx_.fx->rumble_pickup();
    }
    void detonate_nuke() {
        if (nukes_ <= 0 || state_ != St::Play || !player_->alive) return;
        nukes_--;
        nuke_flash_ = 1.0f;
        boss_bullets_.clear();
        wave_nukes_++;
        tele(J().add("ev", "nuke").add("wave", wave_).add("t", (double)game_t_));
        if (grid_) for (auto& b : grid_->bullets) b.alive = false;
        if (grid_) {
            // Vaporises weak invaders; armoured grunts are heavily chipped but
            // survive — a nuke softens an armoured wave, it doesn't skip it.
            for (auto& inv : grid_->invaders)
                if (inv.alive) {
                    inv.hp -= 3;
                    auto [cx, cy] = inv.center();
                    if (inv.hp <= 0) {
                        inv.alive = false; wave_kills_++;
                        score_ += int(inv.points() * grid_->point_mult);
                        particles_.explode_invader(cx, cy, inv.color());
                    } else {
                        inv.hit_flash = 1.0f;
                        particles_.explode_invader(cx, cy, inv.color(), 8);
                    }
                }
        }
        if (boss_ && !boss_->dying) {
            float fx = boss_->x + boss_->w / 2;
            float fy = std::max(110.0f, boss_->y + boss_->h / 2);
            if (boss_->damageable()) {
                int dmg = std::max(25, boss_->max_hp / 5 + 20);
                boss_->take_damage(dmg);
                add_flash("DIRECT HIT!", fx, fy, YELLOW, 1.2f, true);
            } else {  // entrance / spawn-shield / fake-out: say so instead of lying
                add_flash("BOSS IMMUNE!", fx, fy, STEEL, 1.2f, true);
                if (ctx_.sfx) ctx_.sfx->armor_tink();
            }
        }
        add_flash("☢  NUKE  ☢", ctx_.W / 2, ctx_.H / 2 - 30, RED, 1.4f, true);
        if (ctx_.sfx) ctx_.sfx->big_explosion();
        if (ctx_.fx) {
            ctx_.fx->shake(26); ctx_.fx->hitstop(0.07f);
            ctx_.fx->rumble_explosion();
            ctx_.fx->shockwave(0.5f, 0.5f);   // ripple out from screen centre
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
        if (grid_) pop(grid_->bullets);
        pop(boss_bullets_);
    }

    void step_world(float dt, const Input& in);
    void hit_player();
    void invasion();
    void draw_hud(Surface& s);
    void draw_flash(Surface& s);
    void draw_banner(Surface& s);
};

void SpaceInvadersGame::update(float dt, const Input& in) {
    pad_ = in.pad_connected;
    // Pause works everywhere except the game-over screen (where P / START = menu).
    // ESC opens the pause menu too (quit lives there) instead of dropping the run.
    if ((in.just(SDLK_p) || (in.just(SDLK_ESCAPE) && !pause_.active())) && state_ != St::GameOver) {
        pause_.toggle();
        if (ctx_.sfx && pause_.active()) ctx_.sfx->stop_ufo();
    }
    if (pause_.active()) {
        if (pause_.update(in) == PauseResult::QuitToMenu) quit_to_menu = true;
        return;
    }
    if (in.just(SDLK_ESCAPE) && initials_.active()) {
        quit_to_menu = true;
        return;
    }

    game_t_ += dt;
    anim_t_ += dt;
    star_t_ += dt;
    if (star_t_ >= AMBIENT_STAR_T) { star_t_ -= AMBIENT_STAR_T; particles_.ambient_star(ctx_.W, ctx_.H); }
    particles_.update(dt);
    if (nuke_flash_ > 0) nuke_flash_ -= dt * 1.6f;
    for (auto& f : flash_) f.life -= dt;
    flash_.erase(std::remove_if(flash_.begin(), flash_.end(),
                 [](const Flash& f) { return f.life <= 0; }), flash_.end());

    if (state_ == St::Intro) {
        intro_t_ -= dt;
        step_world(dt, in);
        if (state_ == St::Intro && intro_t_ <= 0) {
            if (grid_) grid_->finish_slide();
            state_ = St::Play;
        }
    } else if (state_ == St::Play) {
        step_world(dt, in);
    } else if (state_ == St::Dead) {
        dead_t_ -= dt;
        step_world(dt, in);
        if (state_ == St::Dead && dead_t_ <= 0) {
            if (!invaded_ && player_->lives > 0) {
                clear_enemy_fire();  // respawn into a clean screen, not a wall of fire
                player_->respawn(ctx_.W / 2 - 15);
                state_ = St::Play;
            } else {
                enter_game_over();
            }
        }
    } else if (state_ == St::Win) {
        win_t_ -= dt;
        step_world(dt, in);
        // Hold the next wave until any drops still falling are grabbed or gone.
        if (state_ == St::Win && win_t_ <= 0 && (powerups_.empty() || win_t_ < -5.0f))
            start_wave(wave_ + 1);
    } else if (state_ == St::GameOver) {
        game_over_t_ += dt;
        if (bot_) {  // self-play loops for more data (after a beat on the panel)
            if (game_over_t_ >= 2.5f) new_game();
            return;
        }
        if (ctx_.scores) {
            const std::string gid = "space_invaders";
            if (!entry_started_ && game_over_t_ >= ui::GAME_OVER_LOCK &&
                ctx_.scores->qualifies(gid, score_)) {
                entry_started_ = true;
                int rank = 1;
                for (auto& e : ctx_.scores->leaderboard(gid)) if (e.score > score_) rank++;
                initials_.begin("VOID INVADERS", score_, rank);
                return;  // don't feed the opening frame's key press to the entry
            }
            if (initials_.active()) {
                if (initials_.update(in)) ctx_.scores->add_score(gid, initials_.name(), score_);
                return;
            }
        }
        if (ui::game_over_retry(in, game_over_t_)) new_game();
        else if (ui::game_over_menu(in, game_over_t_)) quit_to_menu = true;
    }
}

void SpaceInvadersGame::hit_player() {
    auto [cx, cy] = player_->center();
    particles_.explode_player(cx, cy);
    // Death costs you your arsenal — start the weapon climb over (nukes survive).
    bool had_weapons = w_rapid_ || w_spread_ || w_side_ || pierce_t_ > 0;
    w_rapid_ = w_spread_ = w_side_ = 0; pierce_t_ = 0;
    shield_ = 0;
    if (had_weapons) add_flash("WEAPONS LOST!", cx, cy - 44, RED, 1.8f, true);
    player_->kill(); state_ = St::Dead; dead_t_ = 2.0f;
    wave_deaths_++;
    tele(J().add("ev", "death").add("wave", wave_).add("t", (double)game_t_)
            .add("load", loadout()).add("lives_left", player_->lives));
    if (grid_) grid_->bullets.clear();
    boss_bullets_.clear();
    if (ctx_.sfx) ctx_.sfx->player_death();
    if (ctx_.fx) { ctx_.fx->shake(16); ctx_.fx->hitstop(0.09f); ctx_.fx->rumble_death(); }
}

// The swarm reached the ground: the ship goes up with it and the run ends,
// whatever lives were left — but with a proper beat instead of a hard cut.
void SpaceInvadersGame::invasion() {
    if (invaded_) return;
    invaded_ = true;
    add_flash("THE SWARM HAS LANDED!", ctx_.W / 2, ctx_.H / 2 + 70, RED, 2.4f, true);
    if (grid_) grid_->bullets.clear();
    boss_bullets_.clear();
    if (player_->alive) {
        auto [cx, cy] = player_->center();
        particles_.explode_player(cx, cy);
        player_->alive = false;
        if (ctx_.sfx) ctx_.sfx->player_death();
        if (ctx_.fx) { ctx_.fx->shake(18); ctx_.fx->hitstop(0.09f); ctx_.fx->rumble_death(); }
    }
    state_ = St::Dead;
    dead_t_ = std::max(dead_t_, 1.8f);
}

// One step of the whole world. Called in Intro / Play / Dead / Win; the state
// only decides who may act:
//   Intro — the swarm glides in, the ship can already move and shoot
//   Play  — everything
//   Dead  — the swarm keeps marching (but holds fire), shots and drops keep moving
//   Win   — no enemies; the ship mops up falling drops
void SpaceInvadersGame::step_world(float dt, const Input& in) {
    const bool playing = state_ == St::Play;
    const bool dead = state_ == St::Dead;
    const bool can_act = player_->alive && !dead;
    float move_dir = 0; bool want_fire = false, want_nuke = false;
    if (can_act) {
        if (bot_) {
            bot_decide(move_dir, want_fire, want_nuke);
        } else {
            move_dir = float(in.down(SDLK_RIGHT) || in.down(SDLK_d)) -
                       float(in.down(SDLK_LEFT) || in.down(SDLK_a));
            want_fire = in.down(SDLK_SPACE) || in.down(SDLK_w) || in.down(SDLK_UP);
            want_nuke = in.just(SDLK_b);   // B on keyboard, Y on a pad — never a direction
        }
    }
    if (want_nuke) detonate_nuke();

    if (playing) {  // timed buffs only burn while the fight is actually on
        if (shield_ > 0) shield_ -= dt;
        if (pierce_t_ > 0) pierce_t_ -= dt;
    }
    if (deflect_fx_ > 0) deflect_fx_ -= dt;
    if (tink_cd_ > 0) tink_cd_ -= dt;
    player_->update(dt, move_dir, ctx_.W);
    if (want_fire) fire();

    const float trail_p = 30.0f * dt;  // ≈ every other frame at 60 Hz, at any refresh rate
    for (auto& b : p_bullets_) {
        b.update(dt, ctx_.W, ctx_.H);
        if (rnd01() < trail_p) { auto [tx, ty] = b.tip(); particles_.bullet_trail(tx, ty, b.color, true); }
    }

    const float pcx = player_->x + player_->w / 2.0f;

    // ── enemies update ──
    if (grid_) {
        if (state_ == St::Intro) grid_->slide_in(dt, InvaderGrid::SLIDE / (INTRO_T - 0.25f));
        bool march = playing || (dead && !invaded_);
        grid_->update(dt, ctx_.H, pcx, march, playing);
        if (ctx_.sfx && grid_->anim_ticked) ctx_.sfx->march_tick();
    }
    if (boss_ && (playing || dead)) {
        auto [bpx, bpy] = player_->center();
        size_t bb_before = boss_bullets_.size();
        boss_->update(dt, bpx, bpy, ctx_.W, boss_bullets_, particles_);
        // give boss attacks a voice so the fight is never eerily silent
        if (boss_bullets_.size() > bb_before && ctx_.sfx) {
            ctx_.sfx->pan_next((boss_->x + boss_->w / 2) / float(ctx_.W));
            ctx_.sfx->enemy_shot();
        }
        if (boss_->just_stomped) {
            boss_->just_stomped = false;
            if (!boss_revealed_) {  // the surprise reveal, mid-wave
                boss_revealed_ = true;
                add_flash("⚠  " + boss_->name + "  ⚠", ctx_.W / 2, 150, RED, 1.8f, true);
                if (ctx_.sfx) ctx_.sfx->boss_alarm();
            }
            if (grid_)  // crush any invader unlucky enough to be underfoot
                for (auto& inv : grid_->invaders)
                    if (inv.alive && overlap(inv.rect(), boss_->rect())) {
                        inv.alive = false;
                        auto [cx, cy] = inv.center();
                        particles_.explode_invader(cx, cy, inv.color(), 12);
                        maybe_drop(cx, cy, 0.30f);
                    }
            for (int i = 0; i < 6; ++i)  // dust kicked up at its feet
                particles_.bunker_hit(boss_->x + rnd_uniform(0, boss_->w), boss_->y + boss_->h);
            if (ctx_.fx) {
                ctx_.fx->shake(15);
                ctx_.fx->rumble_boss_step();   // heavy stomp cadence
                ctx_.fx->shockwave((boss_->x + boss_->w / 2) / float(ctx_.W),
                                   (boss_->y + boss_->h) / float(ctx_.H));
            }
            if (ctx_.sfx) ctx_.sfx->bunker_hit();
        }
        if (boss_->fake_started) {
            boss_->fake_started = false;
            add_flash("...JUST KIDDING!", boss_->x + boss_->w / 2, boss_->y - 10, YELLOW, 1.6f, true);
        }
        // A slamming boss flattens whatever cover it lands on.
        if (boss_->cur_attack == 8 && boss_->slam_phase >= 1 &&
            boss_->y + boss_->h >= bunkers_->top_y) {
            if (bunkers_->crush(boss_->rect(), particles_) > 0 && ctx_.sfx) ctx_.sfx->bunker_hit();
        }
    }

    // The UFO only launches during live play on non-boss waves (the boss bar
    // owns that lane); one already in flight always finishes its pass.
    size_t ufo_before = boss_bullets_.size();
    bool ufo_was = ufo_->active;
    bool mini_spawned = ufo_->update(dt, pcx, boss_bullets_, playing && !boss_wave_, playing);
    if (mini_spawned) {  // telegraph the mini-boss arrival
        add_flash("⚠  MINI-BOSS UFO  ⚠", ctx_.W / 2, 96, MysteryShip::MINI_COLOR, 1.8f, true);
        if (ctx_.fx) ctx_.fx->shake(6);
    }
    if (boss_bullets_.size() > ufo_before && ctx_.sfx) {  // mini-boss fire is audible too
        ctx_.sfx->pan_next(ufo_->x / float(ctx_.W));
        ctx_.sfx->enemy_shot();
    }
    // boss_bullets_ also carries mini-boss UFO fire, so move them every frame.
    for (auto& b : boss_bullets_) b.update(dt, ctx_.W, ctx_.H);
    if (ctx_.sfx) {
        // start_ufo() is a no-op while the loop is already playing, so asking
        // every frame also brings the siren back after a pause.
        if (ufo_->active) { ctx_.sfx->start_ufo(); ctx_.sfx->set_ufo_pan(ufo_->x / float(ctx_.W)); }
        else if (ufo_was) ctx_.sfx->stop_ufo();
    }

    // ── player bullets → things ──
    for (auto& b : p_bullets_) {
        if (!b.alive) continue;
        if (grid_) {
            for (auto& inv : grid_->invaders) {
                if (!inv.alive || !overlap(b.rect(), inv.rect())) continue;
                inv.hp -= b.damage; inv.hit_flash = 1.0f;
                if (inv.hp <= 0) {
                    inv.alive = false;
                    wave_kills_++;
                    int pts = int(inv.points() * grid_->point_mult) *
                              (ctx_.combo ? ctx_.combo->hit() : 1);
                    score_ += pts;
                    auto [cx, cy] = inv.center();
                    particles_.explode_invader(cx, cy, inv.color());
                    add_flash("+" + std::to_string(pts), cx, cy - 18, YELLOW);
                    roll_invader_drop(cx, cy);
                    if (ctx_.sfx) { ctx_.sfx->pan_next(cx / float(ctx_.W)); ctx_.sfx->invader_hit(); }
                } else {
                    // Chipped but ALIVE (armoured): make the hit read — a burst of
                    // armour sparks at the impact + a metallic "tink", so the
                    // player sees the shot landed even though it didn't kill.
                    auto [cx, cy] = inv.center();
                    particles_.bunker_hit(float(cx), float(cy), 7);
                    if (ctx_.sfx) { ctx_.sfx->pan_next(cx / float(ctx_.W)); ctx_.sfx->armor_tink(); }
                }
                // Pierce only carries through enemies it KILLS outright — armored
                // grunts that merely get chipped stop the bolt cold.
                if (inv.hp <= 0 && b.pierce > 0) b.pierce--; else b.alive = false;
                break;
            }
        }
        if (b.alive && boss_ && !boss_->dying && overlap(b.rect(), boss_->rect())) {
            auto [tx, ty] = b.tip();
            if (boss_->damageable()) {
                boss_->take_damage(b.damage);
                score_ += 5 * b.damage;
                particles_.explode_invader(tx, ty, boss_->color, 6);
                if (ctx_.sfx) ctx_.sfx->invader_hit();
                if (ctx_.fx) ctx_.fx->shake(2);
            } else {  // entrance / spawn-shield / fake-out: the bolt visibly bounces
                deflect(float(tx), float(ty), false);
            }
            b.alive = false;  // bolts always stop on the boss (no pierce-through)
        }
        if (b.alive && ufo_->active && overlap(b.rect(), ufo_->rect())) {
            bool was_mini = ufo_->mini;
            auto [cx, cy] = ufo_->center();
            int pts = ufo_->hit();  // -1 = mini-boss merely chipped, still alive
            if (pts < 0) {  // mini-boss soaked a hit
                score_ += 10 * b.damage;
                particles_.ufo_explode(cx, cy, 8);
                if (ctx_.sfx) ctx_.sfx->armor_tink();
                if (ctx_.fx) ctx_.fx->shake(3);
            } else {
                pts *= (ctx_.combo ? ctx_.combo->hit() : 1);
                score_ += pts;
                particles_.ufo_explode(cx, cy);
                add_flash("+" + std::to_string(pts) + "!", cx, cy + 26,
                          was_mini ? MysteryShip::MINI_COLOR : RED);
                if (was_mini) {  // GUARANTEED power-up from a mini-boss kill
                    spawn_powerup(float(cx), float(cy));
                    add_flash("MINI-BOSS DOWN!", ctx_.W / 2, ctx_.H / 2 - 70,
                              MysteryShip::MINI_COLOR, 1.6f, true);
                    if (ctx_.fx) { ctx_.fx->shake(14); ctx_.fx->hitstop(0.06f); }
                    if (ctx_.sfx) ctx_.sfx->boss_explosion();
                } else {
                    maybe_drop(float(cx), float(cy), 0.50f);  // UFOs are generous (still tier-scaled)
                    if (ctx_.fx) { ctx_.fx->shake(7); ctx_.fx->hitstop(0.04f); }
                }
                if (ctx_.sfx) { ctx_.sfx->stop_ufo(); ctx_.sfx->invader_hit(); }
            }
            b.alive = false;
        }
        if (b.alive) {
            int radius = 5 + weapon_tier() * 3;  // stronger guns chew your own cover
            if (bunkers_->blast(b, radius, particles_)) {
                if (b.pierce > 0) b.pierce--; else b.alive = false;
                if (ctx_.sfx) ctx_.sfx->bunker_hit();
            }
        }
    }
    p_bullets_.erase(std::remove_if(p_bullets_.begin(), p_bullets_.end(),
                     [](const Bullet& b) { return !b.alive; }), p_bullets_.end());

    // ── boss finished its death throes → win ──
    if (playing && boss_ && boss_->finished) {
        score_ += 500 * (wave_ / 5);
        auto rainx = boss_->x + boss_->w / 2;
        float rainy = std::max(60.0f, boss_->y + 40);
        powerups_.push_back({rainx, rainy, 0, PU::Nuke});
        powerups_.push_back({rainx - 50, rainy, 0, PU::Side});
        powerups_.push_back({rainx + 50, rainy, 0, PU::Spread});
        add_flash(boss_->name + " GOES SPLAT!", ctx_.W / 2, ctx_.H / 2 + 20, YELLOW, 2.2f, true);
        if (ctx_.sfx) ctx_.sfx->big_explosion();
        if (ctx_.fx) {
            ctx_.fx->shake(30); ctx_.fx->hitstop(0.10f);
            ctx_.fx->rumble_explosion();
            ctx_.fx->shockwave((boss_->x + boss_->w / 2) / float(ctx_.W),
                               (boss_->y + boss_->h / 2) / float(ctx_.H));
        }
        tele(J().add("ev", "boss_clear").add("wave", wave_).add("dt", (double)(game_t_ - wave_t0_))
                .add("name", boss_->name).add("load", loadout()).add("lives", player_->lives));
        boss_.reset();
        // The escort doesn't outlive its boss: every straggler pops with it.
        if (grid_)
            for (auto& inv : grid_->invaders)
                if (inv.alive) {
                    inv.alive = false;
                    auto [cx, cy] = inv.center();
                    particles_.explode_invader(cx, cy, inv.color(), 14);
                    score_ += int(inv.points() * grid_->point_mult);
                }
        clear_enemy_fire();
        state_ = St::Win; win_t_ = 2.2f;
        if (ctx_.sfx) ctx_.sfx->level_clear();
    }

    // ── powerups fall / collect ──
    for (auto& p : powerups_) {
        p.update(dt, ctx_.H);
        if (p.alive && player_->alive && overlap(p.rect(), player_->rect())) {
            p.alive = false;
            apply_powerup(p.kind);
        }
    }
    powerups_.erase(std::remove_if(powerups_.begin(), powerups_.end(),
                    [](const PowerUp& p) { return !p.alive; }), powerups_.end());

    // ── enemy bullets → player / bunkers ──
    const bool safe = player_->invincible > 0 || shield_ > 0;
    auto process_enemy_bullets = [&](std::vector<Bullet>& bs) {
        for (auto& b : bs) {
            if (!b.alive) continue;
            if (player_->alive && overlap(b.rect(), player_->rect())) {
                if (safe) {  // the shield / spawn-guard visibly and audibly eats it
                    b.alive = false;
                    deflect(b.x, b.y, true);
                    continue;
                }
                if (playing) { b.alive = false; hit_player(); return true; }
            }
            if (bunkers_->blast(b, std::min(12, 3 + wave_ / 3), particles_)) b.alive = false;
        }
        return false;
    };
    if (grid_ && process_enemy_bullets(grid_->bullets)) return;
    if (process_enemy_bullets(boss_bullets_)) return;
    boss_bullets_.erase(std::remove_if(boss_bullets_.begin(), boss_bullets_.end(),
                        [](const Bullet& b) { return !b.alive; }), boss_bullets_.end());

    // boss body contact (a shield or spawn-guard shrugs it off)
    if (playing && boss_ && !boss_->dying && !boss_->faking && !boss_->entering && player_->alive &&
        !safe && overlap(player_->rect(), boss_->rect())) { hit_player(); return; }

    // ── the swarm reaches cover / the ship / the ground ──
    if (grid_ && (playing || dead) && !invaded_) {
        float bottom = grid_->bottom();
        if (bottom >= bunkers_->top_y) {  // invaders grind through bunkers, not past them
            int crushed = 0;
            for (auto& inv : grid_->invaders)
                if (inv.alive && inv.y + inv.h >= bunkers_->top_y)
                    crushed += bunkers_->crush(inv.rect(), particles_);
            if (crushed > 0 && ctx_.sfx) ctx_.sfx->bunker_hit();
        }
        if (bottom >= ctx_.H - 60) { invasion(); return; }
        if (playing && player_->alive && !safe && bottom >= player_->y)
            for (auto& inv : grid_->invaders)
                if (inv.alive && overlap(inv.rect(), player_->rect())) { hit_player(); return; }
    }

    // ── wave clear (boss waves end when the boss does) ──
    if (playing && grid_ && !boss_ && grid_->alive_count() == 0) {
        tele(J().add("ev", "wave_clear").add("wave", wave_).add("dt", (double)(game_t_ - wave_t0_))
                .add("kills", wave_kills_).add("shots", wave_shots_).add("pu", wave_pu_)
                .add("deaths", wave_deaths_).add("nukes_used", wave_nukes_)
                .add("load", loadout()).add("lives", player_->lives).add("bunkers", bunkers_left()));
        add_flash("WAVE CLEARED!", ctx_.W / 2, ctx_.H / 2 + 20, YELLOW, 1.6f, true);
        clear_enemy_fire();
        state_ = St::Win; win_t_ = 1.5f;
        if (ctx_.sfx) ctx_.sfx->level_clear();
    }
}

// A modest "competent player" policy: dodge incoming fire, grab power-ups,
// autofire, and panic-nuke when boxed in. Good enough to measure balance.
void SpaceInvadersGame::bot_decide(float& move_dir, bool& want_fire, bool& want_nuke) {
    want_fire = true;
    if (!player_->alive) { move_dir = 0; return; }
    float pcx = player_->x + player_->w / 2.0f, py = player_->y;

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
    if (grid_) scan(grid_->bullets);
    scan(boss_bullets_);

    if (threat_dir != 0) {
        move_dir = threat_dir;
    } else {
        float best = 1e9f, seekx = pcx;
        for (auto& p : powerups_) {
            if (!p.alive) continue;
            float d = std::fabs(p.x - pcx);
            if (d < best) { best = d; seekx = p.x; }
        }
        move_dir = (best < 420) ? (seekx > pcx + 6 ? 1.0f : seekx < pcx - 6 ? -1.0f : 0.0f) : 0.0f;
    }
    want_nuke = nukes_ > 0 && near >= 7;
    if (pcx < 40) move_dir = 1; else if (pcx > ctx_.W - 40) move_dir = -1;
}

// ── drawing ──────────────────────────────────────────────────────────────────
void SpaceInvadersGame::draw(Surface& s) {
    s.fill(BLACK);
    for (int x = 0; x < ctx_.W; x += 40) draw::line(s, Color(8, 18, 8), x, 0, x, ctx_.H, 1);
    for (int y = 0; y < ctx_.H; y += 40) draw::line(s, Color(8, 18, 8), 0, y, ctx_.W, y, 1);
    particles_.draw(s);
    if (grid_) grid_->draw(s);
    if (boss_) {
        auto [pcx, pcy] = player_->center();
        boss_->draw(s, pcx, pcy);
    }
    // boss fire AND mini-boss UFO fire live here — always drawn, boss or not
    for (auto& b : boss_bullets_) b.draw(s);
    bunkers_->draw(s);
    ufo_->draw(s);
    for (auto& p : powerups_) p.draw(s, *ctx_.fonts.small);
    player_->draw(s);
    for (auto& b : p_bullets_) b.draw(s);

    // shield bubble: pulses, flares when it eats a shot, blinks before it drops
    if (player_->alive) {
        auto [cx, cy] = player_->center();
        int rr = player_->w;
        float hit = std::max(0.0f, deflect_fx_ / 0.25f);
        if (shield_ > 0) {
            bool blink_off = shield_ < 1.5f && std::fmod(shield_, 0.25f) < 0.1f;
            if (!blink_off) {
                float pulse = 0.6f + 0.4f * std::sin(anim_t_ * 9 + shield_ * 4);
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
                      "REACHED WAVE " + std::to_string(wave_));
        if (initials_.active()) initials_.draw(s);
    }
    pause_.draw(s);
}

void SpaceInvadersGame::draw_banner(Surface& s) {
    if (state_ != St::Intro) return;  // win text lives in the flash list
    // fade in, hold, fade out — no pop
    float a = std::clamp(std::min((INTRO_T - intro_t_) / 0.25f, intro_t_ / 0.4f), 0.0f, 1.0f);
    int al = int(255 * a);
    int cy = ctx_.H / 2 + 40;   // below the swarm's resting block, above the bunkers
    // Always just "WAVE N" — boss waves keep the stomp-in a surprise.
    draw_text_centered(s, *ctx_.fonts.big, "WAVE " + std::to_string(wave_), ctx_.W / 2, cy,
                       Color(200, 255, 140, al));
    // Branching banner: announce the variant so the player knows what's coming.
    if (wave_kind_ == WaveKind::Elite)
        draw_text_centered(s, *ctx_.fonts.med, "ELITE  —  ARMORED FLEET", ctx_.W / 2, cy + 52,
                           Color(STEEL.r, STEEL.g, STEEL.b, al));
    else if (wave_kind_ == WaveKind::Swarm)
        draw_text_centered(s, *ctx_.fonts.med, "SWARM  —  THEY'RE EVERYWHERE", ctx_.W / 2, cy + 52,
                           Color(ORANGE.r, ORANGE.g, ORANGE.b, al));
    else
        draw_text_centered(s, *ctx_.fonts.small, "GET READY", ctx_.W / 2, cy + 54,
                           Color(ui::HINT.r, ui::HINT.g, ui::HINT.b, al));
    if (wave_ == 1 && !bot_) {  // the only controls the game needs, shown once
        std::string hint = pad_ ? std::string("STICK  MOVE      ") + ui::fire_key(pad_) + "  FIRE"
                                : std::string("← →  MOVE      ") + ui::fire_key(pad_) + "  FIRE";
        draw_text_centered(s, *ctx_.fonts.small, hint, ctx_.W / 2, cy + 82,
                           Color(ui::HINT.r, ui::HINT.g, ui::HINT.b, al));
    }
}

void SpaceInvadersGame::draw_hud(Surface& s) {
    Font& med = *ctx_.fonts.med;
    Font& small = *ctx_.fonts.small;
    // ── top row: score / hi / wave ──
    draw_text(s, med, "SCORE  " + zpad(score_, 6), 16, 8, GREEN);
    draw_text_centered(s, med, "HI  " + zpad(std::max(high_score_, score_), 6), ctx_.W / 2, 8, YELLOW);
    draw_text_right(s, med, "WAVE  " + std::to_string(wave_), ctx_.W - 16, 8, CYAN);

    // ── second row, left: combo (kept out of the centre, which the boss bar owns) ──
    if (ctx_.combo && ctx_.combo->active() && state_ != St::GameOver) {
        int m = ctx_.combo->mult();
        Color cc = m >= 6 ? RED : m >= 4 ? ORANGE : YELLOW;
        draw_text(s, small, "x" + std::to_string(m) + " COMBO", 16, 38, cc);
        draw::rect(s, Color(40, 40, 40), {16, 58, 100, 3});
        draw::rect(s, cc, {16, 58, int(100 * ctx_.combo->ratio()), 3});
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
        draw::polygon(s, CYAN, {{80 + i * 22, row_b + 14}, {80 + i * 22 + 7, row_b + 4}, {80 + i * 22 + 14, row_b + 14}});
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
        seg(xb, row_b, "☢ NUKE x" + std::to_string(nukes_) + "  [" + ui::nuke_key(pad_) + "]", RED);
    if (shield_ > 0)
        seg(xa, row_a, "SHIELD " + std::to_string(int(shield_ + 0.99f)) + "s", Color(80, 220, 255));
    if (pierce_t_ > 0)
        seg(xa, row_a, "PIERCE " + std::to_string(int(pierce_t_ + 0.99f)) + "s", Color(200, 100, 255));
}

void SpaceInvadersGame::draw_flash(Surface& s) {
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

std::unique_ptr<Scene> make_space_invaders(const Context& ctx) {
    return std::make_unique<SpaceInvadersGame>(ctx);
}
