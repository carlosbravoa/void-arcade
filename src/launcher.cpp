#include "launcher.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "engine/input.h"
#include "engine/rng.h"
#include "engine/scores.h"
#include "engine/surface.h"
#include "engine/text.h"

namespace {
constexpr float TAU = 6.28318530717958647692f;
const Color BLACK(0, 0, 0);
const Color YELLOW(255, 220, 40);

struct GameDef {
    const char* id;
    const char* title;
    const char* subtitle;
    Color color;
    Color accent;
};
const GameDef GAMES[] = {
    {"space_invaders", "VOID INVADERS", "DEFEND THE SECTOR", {100, 255, 140}, {200, 100, 255}},
    {"galaga",         "VOID SQUADRONS", "FIGHT THE FORMATION", {255, 200, 40}, {255, 80, 200}},
    {"void_raid",      "VOID  RAID",     "SHOOT TO SURVIVE",   {80, 200, 255}, {255, 220, 40}},
    {"arkanoid",       "VOID BREAKER",   "SHATTER THE BLOCKS", {80, 200, 255}, {255, 220, 40}},
    {"void_drift",     "VOID DRIFT",     "DODGE THE DRIFT",    {180, 255, 200}, {120, 200, 255}},
    {"void_gunner",    "VOID GUNNER",    "INTO THE VOID",      {120, 200, 255}, {255, 110, 110}},
    {"void_storm",     "VOID STORM",     "RIDE THE STORM",     {255, 160, 80}, {255, 80, 120}},
};
constexpr int NGAMES = 7;

// Idle seconds before the attract marquee kicks in (overridable for testing).
const float kAttractIdle = [] {
    const char* e = std::getenv("VOID_ATTRACT_S");
    return e ? float(atof(e)) : 12.0f;
}();

float pmod(float a, float b) {
    float r = std::fmod(a, b);
    return r < 0 ? r + b : r;
}
Color scale(Color c, float f) {
    return Color(int(c.r * f), int(c.g * f), int(c.b * f));
}
void px2(Surface& s, Color c, int x, int y) { draw::rect(s, c, {x, y, 2, 2}); }
}  // namespace

// ── Star ─────────────────────────────────────────────────────────────────────
void Star::reset(int w, int h) {
    x = rnd_uniform(-w / 2.0f, w / 2.0f);
    y = rnd_uniform(-h / 2.0f, h / 2.0f);
    z = 1.0f;
}
void Star::update(float dt, int w, int h, float speed) {
    z -= speed * dt;
    if (z <= 0.01f) { reset(w, h); return; }
    float sx = x / z + w / 2.0f;
    float sy = y / z + h / 2.0f;
    if (sx < 0 || sx > w || sy < 0 || sy > h) { reset(w, h); return; }
    int bright = int(255 * (1.0f - z));
    size = std::max(1, int((1.0f - z) * 3));
    color = Color(bright, bright, std::min(255, bright + 30));
}
void Star::draw(Surface& s, int w, int h) const {
    int sx = int(x / z + w / 2.0f);
    int sy = int(y / z + h / 2.0f);
    if (sx < 0 || sx >= w || sy < 0 || sy >= h) return;
    if (size <= 1) {
        s.set_at(sx, sy, color);
    } else {
        draw::circle(s, color, sx, sy, size);
        draw::circle(s, Color(color.r, color.g, color.b, 35), sx, sy, size * 2);
    }
}

// ── preview renderers ────────────────────────────────────────────────────────
static void preview_invaders(Surface& s, Rect r, float t) {
    int x0 = r.x, y0 = r.y, w = r.w, h = r.h;
    int rows = 3, cols = 6, cell = 18;
    int dx_total = w - cols * cell - 8;
    int x_off = int(std::fabs(std::sin(t * 0.8f)) * dx_total);
    Color colors[3] = {{200, 100, 255}, {100, 220, 255}, {100, 255, 140}};
    const int base[][2] = {
        {-3,-3},{-2,-3},{-1,-3},{0,-3},{1,-3},{2,-3},{3,-3},
        {-4,-2},{-3,-2},{3,-2},{4,-2},{-4,-1},{-1,-1},{0,-1},{1,-1},{4,-1},
        {-3,0},{-2,0},{-1,0},{0,0},{1,0},{2,0},{3,0},{-2,1},{2,1}};
    int frm = int(t * 2) % 2;
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            int cx = x0 + x_off + col * cell + cell / 2;
            int cy = y0 + 14 + row * cell;
            Color c = colors[row];
            for (auto& p : base) px2(s, c, cx + p[0], cy + p[1]);
            if (frm == 0) { px2(s, c, cx - 3, cy + 2); px2(s, c, cx + 3, cy + 2); }
            else { px2(s, c, cx - 2, cy + 2); px2(s, c, cx + 2, cy + 2); }
        }
    int px = x0 + w / 2 + int(std::sin(t * 1.5f) * w * 0.35f);
    int py = y0 + h - 14;
    const int ship[][2] = {{0,0},{0,-2},{-2,0},{2,0},{-4,2},{4,2},{-6,4},{6,4},{0,-4}};
    for (auto& o : ship) px2(s, Color(80, 200, 255), px + o[0], py + o[1]);
    if (int(t * 3) % 5 == 0) draw::rect(s, Color(80, 200, 255), {px, py - 16, 2, 8});
}

static void preview_galaga(Surface& s, Rect r, float t) {
    int x0 = r.x, y0 = r.y, w = r.w, h = r.h;
    Color colors[3] = {{255, 200, 40}, {255, 80, 200}, {100, 220, 255}};
    int rows_counts[3] = {4, 6, 8};
    int cell = 16;
    const int body[][2] = {{-3,-2},{-2,-3},{-1,-3},{0,-3},{1,-3},{2,-3},{3,-2},
        {-4,-1},{4,-1},{-4,0},{4,0},{-3,1},{3,1},{-2,2},{2,2},{-3,3},{3,3}};
    for (int row = 0; row < 3; ++row) {
        int count = rows_counts[row];
        Color c = colors[row];
        int total_w = count * cell;
        int sx = x0 + (w - total_w) / 2 + int(std::sin(t * 0.7f + row * 0.5f) * 10);
        for (int col = 0; col < count; ++col) {
            int ex = sx + col * cell + cell / 2;
            int ey = y0 + 14 + row * 14;
            int frm = int(t * 2 + col) % 2;
            for (auto& p : body) px2(s, c, ex + p[0], ey + p[1]);
            if (frm == 1) { px2(s, c, ex - 1, ey + 2); px2(s, c, ex + 1, ey + 2); }
        }
    }
    int dive_x = x0 + w / 2 + int(std::sin(t * 1.8f) * w * 0.38f);
    int dive_y = y0 + 12 + int(std::fabs(std::sin(t * 0.9f)) * h * 0.65f);
    Color dc(255, 200, 40);
    const int dbody[][2] = {{-3,-2},{-2,-3},{0,-3},{2,-3},{3,-2},{-4,-1},{4,-1},
        {-4,0},{4,0},{-3,1},{3,1},{-2,2},{2,2}};
    for (auto& p : dbody) px2(s, dc, dive_x + p[0], dive_y + p[1]);
    if (std::sin(t * 1.5f) > 0.7f) {
        int beam_a = int(60 * (std::sin(t * 1.5f) - 0.7f) / 0.3f);
        draw::polygon(s, Color(120, 80, 255, beam_a),
                      {{dive_x, dive_y + 8}, {dive_x - 9, dive_y + 38},
                       {dive_x + 9, dive_y + 38}});
    }
    int px = x0 + w / 2 + int(std::sin(t * 2.1f) * w * 0.3f);
    int py = y0 + h - 14;
    const int ship[][2] = {{0,0},{0,-2},{-2,0},{2,0},{-4,2},{4,2},{-6,4},{6,4},{0,-4}};
    for (auto& o : ship) px2(s, Color(80, 200, 255), px + o[0], py + o[1]);
}

static void preview_void_raid(Surface& s, Rect r, float t) {
    int x0 = r.x, y0 = r.y, w = r.w, h = r.h;
    int wall_w = int(w * 0.18f);
    Color wall_col(20, 40, 80), glow_col(60, 140, 255);
    for (int row = 0; row < 8; ++row) {
        int oy = y0 + int(pmod(row * 14 + t * 40, h));
        draw::rect(s, wall_col, {x0, oy, wall_w, 13});
        draw::rect(s, glow_col, {x0 + wall_w - 2, oy, 2, 13});
        draw::rect(s, wall_col, {x0 + w - wall_w, oy, wall_w, 13});
        draw::rect(s, glow_col, {x0 + w - wall_w, oy, 2, 13});
    }
    int sx = x0 + w / 2 + int(std::sin(t * 1.3f) * w * 0.22f);
    int sy = y0 + int(h * 0.72f);
    draw::polygon(s, Color(80, 210, 255), {{sx, sy - 10}, {sx - 7, sy + 6}, {sx + 7, sy + 6}});
    draw::polygon(s, Color(200, 240, 255), {{sx, sy - 10}, {sx - 7, sy + 6}, {sx + 7, sy + 6}}, 1);
    for (int i = 0; i < 3; ++i) {
        int ex = x0 + wall_w + (i + 1) * ((w - wall_w * 2) / 4);
        int ey = y0 + int(pmod(i * 30 + t * 55, h));
        draw::polygon(s, Color(255, 100, 40), {{ex, ey - 6}, {ex + 5, ey}, {ex, ey + 6}, {ex - 5, ey}});
    }
    int cx = x0 + w / 2 + int(std::sin(t * 2.0f + 1) * w * 0.18f);
    int cy = y0 + int(pmod(t * 45 + 20, h));
    float pulse = 0.7f + 0.3f * std::sin(t * 5);
    draw::circle(s, Color(int(255 * pulse), int(200 * pulse), 40), cx, cy, int(5 * pulse));
    int bx = sx, by = sy - 12;
    for (int i = 0; i < 4; ++i)
        draw::rect(s, Color(80, 210, 255, 180 - i * 40), {bx - 1, by - i * 8, 3, 6});
}

static void preview_arkanoid(Surface& s, Rect r, float t) {
    int x0 = r.x, y0 = r.y, w = r.w, h = r.h;
    Color brick_colors[5] = {{255, 80, 80}, {255, 160, 40}, {255, 220, 40}, {80, 220, 80}, {80, 180, 255}};
    int bw = 18, bh = 7, gap = 2, cols = 7;
    for (int row = 0; row < 5; ++row)
        for (int col = 0; col < cols; ++col) {
            int bx = x0 + 4 + col * (bw + gap);
            int by = y0 + 8 + row * (bh + gap);
            Color c = brick_colors[row];
            int seed = row * 100 + col;
            if (int(t * 0.4f + seed * 0.17f) % 7 == 0) continue;
            draw::rect(s, c, {bx, by, bw, bh}, 0, 1);
            draw::rect(s, Color(std::min(255, c.r + 50), std::min(255, c.g + 50), std::min(255, c.b + 50)),
                       {bx + 1, by + 1, bw - 2, 2}, 0, 1);
        }
    int bx = x0 + w / 2 + int(std::sin(t * 2.1f) * w * 0.38f);
    int by = y0 + 28 + int(std::fabs(std::sin(t * 1.7f)) * h * 0.52f);
    for (int i = 4; i > 0; --i) {
        int ox = int(-std::sin(t * 2.1f) * i * 3);
        int oy = int((std::cos(t * 1.7f) > 0 ? -1 : 1) * std::fabs(std::sin(t * 1.7f)) * i * 3);
        draw::circle(s, Color(255, 255, 180, int(50 * (i / 4.0f))), bx + ox, by + oy, 4);
    }
    draw::circle(s, Color(255, 255, 180), bx, by, 4);
    draw::circle(s, Color(255, 255, 255), bx - 1, by - 1, 1);
    int pw = 34;
    int ppx = std::max(x0 + 2, std::min(x0 + w - pw - 2,
                       x0 + w / 2 + int(std::sin(t * 1.9f) * w * 0.38f) - pw / 2));
    int ppy = y0 + h - 12;
    draw::rect(s, Color(80, 200, 255), {ppx, ppy, pw, 7}, 0, 3);
    draw::rect(s, Color(180, 240, 255), {ppx + 2, ppy + 1, pw - 4, 2}, 0, 1);
}

static void preview_void_drift(Surface& s, Rect r, float t) {
    int x0 = r.x, y0 = r.y, w = r.w, h = r.h;
    for (int i = 0; i < 4; ++i) {  // drifting wireframe asteroids
        float a = t * 0.3f + i * 1.7f;
        int ax = x0 + int(pmod(i * 40 + t * 22, float(w)));
        int ay = y0 + int(pmod(i * 27 + t * 15 + i * 9, float(h)));
        int rad = 9 + (i % 3) * 3;
        std::vector<std::pair<int, int>> pts;
        for (int k = 0; k < 7; ++k) {
            float ang = a + k * (TAU / 7);
            float rr = rad * (0.7f + 0.3f * std::sin(k * 2.3f + i));
            pts.push_back({ax + int(std::cos(ang) * rr), ay + int(std::sin(ang) * rr)});
        }
        draw::polygon(s, Color(150, 220, 180), pts, 1);
    }
    int cx = x0 + w / 2, cy = y0 + h / 2;  // rotating ship triangle
    float sa = t * 1.6f;
    std::vector<std::pair<int, int>> ship;
    for (int k = 0; k < 3; ++k) {
        float ang = sa + k * (TAU / 3);
        int rr = (k == 0 ? 11 : 8);
        ship.push_back({cx + int(std::cos(ang) * rr), cy + int(std::sin(ang) * rr)});
    }
    draw::polygon(s, Color(120, 255, 160), ship, 1);
    for (int b = 0; b < 3; ++b) {  // bullets
        float bt = pmod(t * 1.5f + b * 0.5f, 1.0f);
        px2(s, Color(255, 255, 180), cx + int(std::cos(sa) * bt * w * 0.5f),
            cy + int(std::sin(sa) * bt * h * 0.5f));
    }
}

static void preview_void_gunner(Surface& s, Rect r, float t) {
    int x0 = r.x, y0 = r.y, w = r.w, h = r.h, cx = x0 + w / 2, cy = y0 + h / 2;
    for (int i = 0; i < 14; ++i) {  // warping starfield streaks from center
        float ang = i * (TAU / 14) + i * 0.13f;
        float z = pmod(t * 0.6f + i * 0.07f, 1.0f);
        float r0 = z * z, r1 = std::min(1.0f, r0 + 0.12f);
        int br = int(80 + 175 * z);
        draw::line(s, Color(br, br, std::min(255, br + 30)),
                   cx + int(std::cos(ang) * r0 * w * 0.5f), cy + int(std::sin(ang) * r0 * h * 0.5f),
                   cx + int(std::cos(ang) * r1 * w * 0.5f), cy + int(std::sin(ang) * r1 * h * 0.5f), 1);
    }
    for (int e = 0; e < 2; ++e) {  // enemies blooming out of the void
        float z = pmod(t * 0.5f + e * 0.5f, 1.0f);
        int sz = int(2 + z * z * 10);
        int ex = cx + int(std::cos(e * 2.1f + t * 0.3f) * z * w * 0.32f);
        int ey = cy + int(std::sin(e * 2.1f + t * 0.3f) * z * h * 0.32f);
        draw::polygon(s, Color(255, 120, 120),
                      {{ex, ey - sz}, {ex + sz, ey}, {ex, ey + sz}, {ex - sz, ey}}, z > 0.6f ? 0 : 1);
    }
    if (int(t * 3) % 3 == 0) {  // converging laser streams
        draw::line(s, Color(120, 220, 255), x0 + 4, y0 + h - 2, cx, cy, 1);
        draw::line(s, Color(120, 220, 255), x0 + w - 4, y0 + h - 2, cx, cy, 1);
    }
    draw::circle(s, Color(120, 200, 255), cx, cy, 7, 1);  // welded crosshair
    draw::line(s, Color(120, 200, 255), cx - 10, cy, cx - 4, cy, 1);
    draw::line(s, Color(120, 200, 255), cx + 4, cy, cx + 10, cy, 1);
    draw::line(s, Color(120, 200, 255), cx, cy - 10, cx, cy - 4, 1);
    draw::line(s, Color(120, 200, 255), cx, cy + 4, cx, cy + 10, 1);
}

static void preview_void_storm(Surface& s, Rect r, float t) {
    int x0 = r.x, y0 = r.y, w = r.w, h = r.h;
    for (int i = 0; i < 10; ++i) {  // scrolling ground + ceiling strata
        int gx = x0 + int(pmod(i * 22 - t * 60, float(w + 20))) - 10;
        draw::rect(s, Color(60, 30, 20), {gx, y0 + h - 10, 16, 10});
        draw::rect(s, Color(255, 140, 60), {gx, y0 + h - 10, 16, 2});
        draw::rect(s, Color(60, 30, 20), {gx + 8, y0, 16, 8});
    }
    int px = x0 + 18, py = y0 + h / 2 + int(std::sin(t * 2) * h * 0.2f);  // player ship
    draw::polygon(s, Color(120, 200, 255), {{px - 8, py - 5}, {px + 10, py}, {px - 8, py + 5}});
    for (int b = 0; b < 3; ++b) {  // shots streaking right
        int bx = px + 12 + int(pmod(t * 200 + b * 40, float(w)));
        if (bx < x0 + w) draw::rect(s, Color(255, 220, 40), {bx, py - 1, 8, 2});
    }
    for (int e = 0; e < 3; ++e) {  // enemies entering from the right
        int ex = x0 + w - int(pmod(t * 70 + e * 40, float(w)));
        int ey = y0 + 12 + e * (h - 24) / 3;
        if (ex > x0) draw::polygon(s, Color(255, 100, 120), {{ex + 6, ey}, {ex - 5, ey - 5}, {ex - 5, ey + 5}});
    }
}

// ── Launcher ─────────────────────────────────────────────────────────────────
Launcher::Launcher(const Context& ctx) : Scene(ctx) {
    card_anim_.assign(NGAMES, 0.0f);
    stars_.resize(260);
    for (auto& s : stars_) {
        s.x = rnd_uniform(-ctx.W / 2.0f, ctx.W / 2.0f);
        s.y = rnd_uniform(-ctx.H / 2.0f, ctx.H / 2.0f);
        s.z = rnd_uniform(0.05f, 1.0f);
    }
    title_ = "THE  VOID  ARCADE";
    for (size_t i = 0; i < title_.size(); ++i)
        char_phase_.push_back(rnd_uniform(0, TAU));
}

std::string Launcher::current_id() const { return GAMES[selected_].id; }

void Launcher::on_demo_end(bool advance) {
    if (advance) selected_ = (selected_ + 1) % NGAMES;  // walk the roster while idle
    scroll_ = float(selected_);   // snap the carousel onto it (no slide)
    idle_t_ = 0.0f;               // fresh menu, not the static high-score marquee
    attract_ = false;
    attract_t_ = 0.0f;
}

void Launcher::update(float dt, const Input& in) {
    t_ += dt;
    for (auto& s : stars_) s.update(dt, ctx_.W, ctx_.H);
    for (int i = 0; i < NGAMES; ++i) {
        float target = (i == selected_) ? 1.0f : 0.0f;
        card_anim_[i] += (target - card_anim_[i]) * dt * 9;
    }
    scroll_ += (selected_ - scroll_) * std::min(1.0f, dt * 12);  // glide the carousel
    if (!in.pressed.empty()) idle_t_ = 0.0f; else idle_t_ += dt;

    // Attract mode: after a spell of idleness, cycle a high-score marquee.
    if (attract_) {
        attract_t_ += dt;
        if (attract_t_ > 4.0f) { attract_t_ = 0; attract_game_ = (attract_game_ + 1) % NGAMES; }
        if (!in.pressed.empty()) attract_ = false;  // any key dismisses
        return;
    }
    if (idle_t_ > kAttractIdle) { attract_ = true; attract_t_ = 0; attract_game_ = 0; return; }

    for (auto k : in.pressed) {
        if (k == SDLK_LEFT || k == SDLK_a) selected_ = std::max(0, selected_ - 1);
        else if (k == SDLK_RIGHT || k == SDLK_d) selected_ = std::min(NGAMES - 1, selected_ + 1);
        else if (k == SDLK_RETURN || k == SDLK_SPACE) selection_id = GAMES[selected_].id;
        else if (k == SDLK_o) options_requested = true;
        else if (k == SDLK_ESCAPE) quit_requested = true;
    }
}

void Launcher::draw(Surface& s) {
    if (attract_) { draw_attract(s); return; }
    s.fill(BLACK);
    for (auto& st : stars_) st.draw(s, ctx_.W, ctx_.H);
    for (int x = 0; x < ctx_.W; x += 60) draw::line(s, Color(3, 8, 3), x, 0, x, ctx_.H, 1);
    for (int y = 0; y < ctx_.H; y += 60) draw::line(s, Color(3, 8, 3), 0, y, ctx_.W, y, 1);
    draw_title(s);
    draw_cards(s);
    draw_text_centered(s, *ctx_.fonts.small, "◀  A/D or ARROW KEYS  ▶  |  ESC to quit",
                       ctx_.W / 2, ctx_.H - 38, Color(30, 65, 30));
    draw_text_centered(s, *ctx_.fonts.small, "O  OPTIONS   |   F11  FULLSCREEN", ctx_.W / 2,
                       ctx_.H - 20, Color(22, 48, 22));
    for (int y = 0; y < ctx_.H; y += 4) draw::rect(s, Color(0, 0, 0, 12), {0, y, ctx_.W, 2});
}

void Launcher::draw_attract(Surface& s) {
    s.fill(BLACK);
    for (auto& st : stars_) st.draw(s, ctx_.W, ctx_.H);
    const GameDef& g = GAMES[attract_game_];

    float pulse = 0.6f + 0.4f * std::sin(t_ * 3.0f);
    draw_text_centered(s, *ctx_.fonts.big, "HIGH SCORES", ctx_.W / 2, 70,
                       Color(int(80 + 120 * pulse), 255, int(80 + 80 * pulse)));
    draw_text_centered(s, *ctx_.fonts.med, g.title, ctx_.W / 2, 150, g.color);

    std::vector<ScoreManager::Entry> board;
    if (ctx_.scores) board = ctx_.scores->leaderboard(g.id);

    auto commas = [](int v) {
        std::string n = std::to_string(v), out;
        int c = 0;
        for (int i = (int)n.size() - 1; i >= 0; --i) {
            out.insert(out.begin(), n[i]);
            if (++c % 3 == 0 && i > 0) out.insert(out.begin(), ',');
        }
        return out;
    };

    int y = 230, cx = ctx_.W / 2;
    if (board.empty()) {
        draw_text_centered(s, *ctx_.fonts.med, "NO SCORES YET", cx, 330, Color(60, 120, 60));
        draw_text_centered(s, *ctx_.fonts.small, "BE THE FIRST", cx, 366, Color(40, 90, 40));
    } else {
        for (size_t i = 0; i < board.size(); ++i) {
            Color rc = i == 0 ? YELLOW : (i < 3 ? Color(200, 240, 200) : Color(90, 150, 90));
            int lx = cx - 150;
            draw_text(s, *ctx_.fonts.med, std::to_string(i + 1) + ".", lx, y, rc);
            draw_text(s, *ctx_.fonts.med, board[i].name, lx + 50, y, rc);
            draw_text_right(s, *ctx_.fonts.med, commas(board[i].score), cx + 150, y, rc);
            y += 34;
        }
    }

    if (int(t_ * 2) % 2 == 0)
        draw_text_centered(s, *ctx_.fonts.small, "PRESS ANY KEY", cx, ctx_.H - 56, Color(70, 140, 70));
    draw_text_centered(s, *ctx_.fonts.small, "INSERT COIN  //  THE VOID ARCADE", cx, ctx_.H - 32, Color(40, 80, 40));
    for (int yy = 0; yy < ctx_.H; yy += 4) draw::rect(s, Color(0, 0, 0, 12), {0, yy, ctx_.W, 2});
}

void Launcher::draw_title(Surface& s) {
    std::vector<std::shared_ptr<Surface>> chars;
    for (size_t i = 0; i < title_.size(); ++i) {
        float ph = char_phase_[i];
        float tv = t_ * 2.5f + ph;
        int r = int(80 + 120 * (0.5f + 0.5f * std::sin(tv)));
        int g = int(200 + 55 * (0.5f + 0.5f * std::sin(tv + 2.1f)));
        int b = int(80 + 100 * (0.5f + 0.5f * std::sin(tv + 4.2f)));
        chars.push_back(ctx_.fonts.big->render(std::string(1, title_[i]), Color(r, g, b)));
    }
    int total_w = 0;
    for (auto& c : chars) total_w += c->width();
    int x = ctx_.W / 2 - total_w / 2;
    for (size_t i = 0; i < chars.size(); ++i) {
        float bob = std::sin(t_ * 2.5f + char_phase_[i]) * 5;
        s.blit(*chars[i], x, int(28 + bob));
        x += chars[i]->width();
    }
    draw_text_centered(s, *ctx_.fonts.small, "SELECT YOUR GAME", ctx_.W / 2, 80, Color(50, 110, 50));
}

void Launcher::draw_cards(Surface& s) {
    // Carousel: the selected game sits centered and full-size; neighbours peek
    // from the sides, scaled down and dimmed. Scales to any number of games.
    const int cw = 210, ch = 300;
    const float spacing = 250.0f;
    int cxc = ctx_.W / 2;
    int cyc = ctx_.H / 2 - ch / 2 + 18;

    // Draw far cards first so the focused one overlaps its neighbours.
    std::vector<int> order;
    for (int i = 0; i < NGAMES; ++i) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return std::fabs(a - scroll_) > std::fabs(b - scroll_);
    });
    for (int i : order) {
        float off = i - scroll_;
        if (std::fabs(off) > 2.4f) continue;  // cull off-screen cards
        float dist = std::fabs(off);
        float sc = std::max(0.5f, 1.0f - dist * 0.24f);
        float dim = std::max(0.4f, 1.0f - dist * 0.38f);
        int w = int(cw * sc), h = int(ch * sc);
        int cx = cxc + int(off * spacing) - w / 2;
        int cy = cyc + (ch - h) / 2;  // keep cards vertically centred
        draw_card(s, i, cx, cy, w, h, i == selected_, dim);
    }

    // Page dots.
    int dy = cyc + ch + 16, dx0 = ctx_.W / 2 - (NGAMES - 1) * 9;
    for (int i = 0; i < NGAMES; ++i) {
        Color c = (i == selected_) ? GAMES[i].color : Color(40, 60, 50);
        draw::circle(s, c, dx0 + i * 18, dy, i == selected_ ? 4 : 3);
    }
}

void Launcher::draw_card(Surface& s, int gi, int cx, int cy, int cw, int ch,
                         bool focus, float dim) {
    const GameDef& game = GAMES[gi];
    Color color = game.color, accent = game.accent;
    bool full = focus && cw >= 200;  // full content only once near full size
    auto dc = [&](Color c) { return scale(c, dim); };

    draw::rect(s, Color(0, 0, 0, int(90 * dim)), {cx + 5, cy + 8, cw + 10, ch + 10});
    if (focus)
        for (int r = 10; r > 0; r -= 2) {
            int a = int(130 * std::pow(r / 10.0f, 2));
            draw::rect(s, Color(color.r, color.g, color.b, a),
                       {cx - r, cy - r, cw + r * 2, ch + r * 2}, 0, 6);
        }
    draw::rect(s, Color(8, 18, 12, focus ? 250 : 200), {cx, cy, cw, ch});
    draw::rect(s, dc(color), {cx, cy, cw, ch}, 2, 4);

    int pad = std::max(6, cw / 22);
    Rect pr{cx + pad, cy + pad, cw - 2 * pad, int(ch * 0.36f)};
    draw::rect(s, BLACK, pr);
    draw::rect(s, Color(color.r, color.g, color.b, int(70 * dim)), pr, 1);
    std::string id = game.id;
    if (id == "space_invaders") preview_invaders(s, pr, t_);
    else if (id == "galaga") preview_galaga(s, pr, t_);
    else if (id == "void_raid") preview_void_raid(s, pr, t_);
    else if (id == "arkanoid") preview_arkanoid(s, pr, t_);
    else if (id == "void_drift") preview_void_drift(s, pr, t_);
    else if (id == "void_gunner") preview_void_gunner(s, pr, t_);
    else preview_void_storm(s, pr, t_);

    int ty = cy + pr.h + pad + 8;
    Font& tf = full ? *ctx_.fonts.med : *ctx_.fonts.small;
    draw_text_centered(s, tf, game.title, cx + cw / 2, ty, dc(color));
    if (full) {
        draw_text_centered(s, *ctx_.fonts.small, game.subtitle, cx + cw / 2, ty + 26, accent);
        draw_stats(s, gi, cx, cy + int(ch * 0.60f), cw, 1.0f);
        if (int(t_ * 3) % 2 == 0)
            draw_text_centered(s, *ctx_.fonts.small, "▶  PRESS SPACE  ◀", cx + cw / 2,
                               cy + ch - 22, YELLOW);
    }
}

void Launcher::draw_stats(Surface& s, int gi, int cx, int y, int cw, float anim) {
    if (!ctx_.scores) return;
    const GameDef& game = GAMES[gi];
    Color color = game.color;
    Color dim = scale(color, 0.4f);
    draw::line(s, Color(color.r, color.g, color.b, int(70 * anim)), cx + 16, y, cx + cw - 16, y, 1);
    y += 9;
    std::string gid = game.id;
    int hi = ctx_.scores->high_score(gid);
    int plays = ctx_.scores->total_plays(gid);
    auto commas = [](int v) {
        std::string n = std::to_string(v), out;
        int c = 0;
        for (int i = (int)n.size() - 1; i >= 0; --i) {
            out.insert(out.begin(), n[i]);
            if (++c % 3 == 0 && i > 0) out.insert(out.begin(), ',');
        }
        return out;
    };
    struct Row { std::string label, value; Color col; };
    std::vector<Row> rows;
    auto row3 = [&](const char* label, int val) {
        rows = {{"HI-SCORE", commas(hi), YELLOW}, {label, std::to_string(val), color},
                {"GAMES", std::to_string(plays), dim}};
    };
    if (gid == "space_invaders")   row3("BEST WAVE",  ctx_.scores->best_wave());
    else if (gid == "galaga")      row3("BEST STAGE", ctx_.scores->best_stage());
    else if (gid == "void_raid")   row3("BEST ZONE",  ctx_.scores->best_zone());
    else if (gid == "void_drift")  row3("BEST WAVE",  ctx_.scores->best_drift_wave());
    else if (gid == "void_gunner") row3("BEST WAVE",  ctx_.scores->best_gunner_wave());
    else if (gid == "void_storm")  row3("BEST STAGE", ctx_.scores->best_storm_stage());
    else                           row3("BEST LEVEL", ctx_.scores->best_level());  // arkanoid
    float fa = std::min(1.0f, anim * 1.5f);
    for (auto& row : rows) {
        draw_text(s, *ctx_.fonts.small, row.label, cx + 16, y, scale(Color(100, 100, 100), fa));
        draw_text_right(s, *ctx_.fonts.small, row.value, cx + cw - 16, y, scale(row.col, fa));
        y += 18;
    }
}
