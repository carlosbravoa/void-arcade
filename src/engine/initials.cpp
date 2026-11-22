#include "initials.h"

#include <algorithm>
#include <cmath>

#include "input.h"
#include "surface.h"
#include "text.h"
#include "ui.h"

#include <SDL.h>

namespace {
const Color YELLOW(255, 220, 40), GREEN(80, 255, 80), CYAN(80, 200, 255);
const Color DIM(40, 80, 40), WHITE(255, 255, 255);
}  // namespace

InitialsEntry::InitialsEntry(int w, int h, Fonts fonts) : W_(w), H_(h), f_(fonts) {}

void InitialsEntry::begin(const std::string& title, int score, int rank) {
    active_ = true;
    chars_[0] = chars_[1] = chars_[2] = 'A';
    cursor_ = 0;
    score_ = score;
    rank_ = rank;
    title_ = title;
    t_ = 0;
    last_ticks_ = SDL_GetTicks();
}

bool InitialsEntry::update(const Input& in) {
    if (!active_) return false;
    Uint32 now = SDL_GetTicks();
    t_ += std::min(0.1f, (now - last_ticks_) / 1000.0f);  // real time, any refresh rate
    last_ticks_ = now;
    pad_ = in.pad_connected;
    for (auto k : in.pressed) {
        if (k >= SDLK_a && k <= SDLK_z) {
            chars_[cursor_] = char('A' + (k - SDLK_a));
            if (cursor_ < 2) cursor_++;
        } else if (k == SDLK_UP) {
            chars_[cursor_] = chars_[cursor_] == 'Z' ? 'A' : chars_[cursor_] + 1;
        } else if (k == SDLK_DOWN) {
            chars_[cursor_] = chars_[cursor_] == 'A' ? 'Z' : chars_[cursor_] - 1;
        } else if (k == SDLK_LEFT) {
            cursor_ = std::max(0, cursor_ - 1);
        } else if (k == SDLK_RIGHT) {
            cursor_ = std::min(2, cursor_ + 1);
        } else if (k == SDLK_SPACE || k == SDLK_RETURN) {
            if (cursor_ < 2) cursor_++;
            else { active_ = false; return true; }
        }
    }
    return false;
}

void InitialsEntry::draw(Surface& s) {
    if (!active_) return;
    int cx = W_ / 2, cy = H_ / 2;
    draw::rect(s, Color(0, 0, 0, 200), {0, 0, W_, H_});
    int pw = 400, ph = 220, px = cx - pw / 2, py = cy - ph / 2;
    draw::rect(s, Color(6, 16, 8, 235), {px, py, pw, ph});
    draw::rect(s, GREEN, {px, py, pw, ph}, 1, 4);

    draw_text_centered(s, *f_.med, "NEW HIGH SCORE", cx, py + 20, YELLOW);
    draw_text_centered(s, *f_.small, "RANK #" + std::to_string(rank_) +
                       "   " + title_, cx, py + 52, CYAN);
    draw_text_centered(s, *f_.small, "SCORE  " + std::to_string(score_), cx, py + 72, GREEN);

    // three big letters with a blinking cursor under the active slot
    int slot_w = 56;
    int start_x = cx - (3 * slot_w) / 2 + slot_w / 2;
    for (int i = 0; i < 3; ++i) {
        int x = start_x + i * slot_w;
        Color c = (i == cursor_) ? WHITE : GREEN;
        draw_text_centered(s, *f_.big, std::string(1, chars_[i]), x, py + 100, c);
        bool blink = (int(t_ * 3) % 2) == 0;
        if (i == cursor_ && blink)
            draw::rect(s, YELLOW, {x - 18, py + 150, 36, 4});
    }
    draw_text_centered(s, *f_.small,
                       pad_ ? "↑↓ LETTER   ◀▶ MOVE   A OK" : "TYPE / ↑↓ LETTER   ◀▶ MOVE   SPACE OK",
                       cx, py + ph - 24, ui::HINT);
}
