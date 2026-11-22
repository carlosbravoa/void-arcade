#include "pause.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "input.h"
#include "surface.h"
#include "text.h"
#include "ui.h"

#include <SDL.h>

namespace {
const std::array<const char*, 2> OPTIONS = {"RESUME", "QUIT TO MENU"};
const Color YELLOW(255, 220, 40);
const Color DIM(95, 140, 105);
}  // namespace

PauseMenu::PauseMenu(int w, int h, Fonts fonts) : W_(w), H_(h), f_(fonts) {}

void PauseMenu::toggle() {
    active_ = !active_;
    selected_ = 0;
    // The game toggles this on the same 'P' keydown it then forwards to update();
    // swallow that first frame so the opening press doesn't instantly resume.
    if (active_) { alpha_ = 0.0f; t_ = 0.0f; swallow_open_ = true; last_ticks_ = SDL_GetTicks(); }
}

PauseResult PauseMenu::update(const Input& in) {
    // Real elapsed time (update() takes no dt), so the fade and pulse run at
    // the same speed on any refresh rate.
    Uint32 now = SDL_GetTicks();
    float dt = std::min(0.1f, (now - last_ticks_) / 1000.0f);
    last_ticks_ = now;
    t_ += dt;
    alpha_ = std::min(1.0f, alpha_ + dt * 7.5f);
    pad_ = in.pad_connected;
    if (swallow_open_) { swallow_open_ = false; return PauseResult::None; }
    int n = (int)OPTIONS.size();
    for (auto k : in.pressed) {
        if (k == SDLK_UP || k == SDLK_w) selected_ = (selected_ - 1 + n) % n;
        else if (k == SDLK_DOWN || k == SDLK_s) selected_ = (selected_ + 1) % n;
        else if (k == SDLK_RETURN || k == SDLK_SPACE) {
            active_ = false;
            return selected_ == 0 ? PauseResult::Resume : PauseResult::QuitToMenu;
        } else if (k == SDLK_p || k == SDLK_ESCAPE) {
            active_ = false;
            return PauseResult::Resume;
        }
    }
    return PauseResult::None;
}

void PauseMenu::draw(Surface& s) {
    if (!active_) return;

    // darkened overlay + scanline texture
    draw::rect(s, Color(0, 0, 0, int(180 * alpha_)), {0, 0, W_, H_});
    for (int y = 0; y < H_; y += 4)
        draw::rect(s, Color(0, 0, 0, 20), {0, y, W_, 2});

    int cx = W_ / 2, cy = H_ / 2;
    int pw = 380, ph = 200;
    int px = cx - pw / 2, py = cy - ph / 2;

    draw::rect(s, Color(8, 20, 10, int(220 * alpha_)), {px, py, pw, ph});
    for (int r = 6; r > 0; r -= 2) {
        int a = int(120 * alpha_ * std::pow(r / 6.0f, 2));
        draw::rect(s, Color(80, 255, 80, a),
                   {px - r, py - r, pw + r * 2, ph + r * 2}, 1, 4);
    }
    draw::rect(s, Color(80, 255, 80), {px, py, pw, ph}, 1, 4);

    float pulse = 0.85f + 0.15f * std::fabs(std::sin(t_ * 3));
    Color tc(int(80 * pulse), int(255 * pulse), int(80 * pulse));
    draw_text_centered(s, *f_.big, "PAUSED", cx, py + 22, tc);

    draw::line(s, Color(40, 100, 40), px + 20, py + 74, px + pw - 20, py + 74, 1);

    for (int i = 0; i < (int)OPTIONS.size(); ++i) {
        bool sel = (i == selected_);
        Color col = sel ? YELLOW : DIM;
        std::string txt = std::string(sel ? "▶ " : "  ") + OPTIONS[i];
        auto ts = f_.med->render(txt, col);
        int oy = py + 95 + i * 44;
        s.blit(*ts, cx - ts->width() / 2, oy);
        if (sel) {
            for (int r = 4; r > 0; --r) {
                int a = int(60 * std::pow(r / 4.0f, 2));
                draw::rect(s, Color(YELLOW.r, YELLOW.g, YELLOW.b, a),
                           {cx - ts->width() / 2 - r, oy - r,
                            ts->width() + r * 2, ts->height() + r * 2});
            }
        }
    }

    draw_text_centered(s, *f_.small,
                       pad_ ? "↑↓ SELECT   A CONFIRM   START RESUME"
                            : "↑↓ SELECT   SPACE CONFIRM   P RESUME",
                       cx, py + ph - 22, ui::HINT);
}
