#include "options.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "engine/input.h"
#include "engine/settings.h"
#include "engine/surface.h"
#include "engine/text.h"
#include "engine/ui.h"

namespace {
const Color BLACK(0, 0, 0), WHITE(255, 255, 255), YELLOW(255, 220, 40);
const Color CYAN(90, 220, 255), GREEN(90, 255, 130), ORANGE(255, 150, 50);
const Color DIMC(50, 90, 70);

enum Row { ROW_GLOW, ROW_AFTERGLOW, ROW_SCANLINE, ROW_VOLUME, ROW_MUSIC, ROW_FULLSCREEN,
           ROW_RESET, ROW_BACK, ROW_COUNT };

float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
}  // namespace

void OptionsMenu::update(float dt, const Input& in) {
    t_ += dt;
    pad_ = in.pad_connected;
    if (in.just(SDLK_UP) || in.just(SDLK_w)) sel_ = (sel_ - 1 + ROW_COUNT) % ROW_COUNT;
    if (in.just(SDLK_DOWN) || in.just(SDLK_s)) sel_ = (sel_ + 1) % ROW_COUNT;
    if (in.just(SDLK_ESCAPE)) { done = true; return; }

    bool left = in.down(SDLK_LEFT) || in.down(SDLK_a);
    bool right = in.down(SDLK_RIGHT) || in.down(SDLK_d);
    float dir = (right ? 1.0f : 0.0f) - (left ? 1.0f : 0.0f);
    bool activate = in.just(SDLK_RETURN) || in.just(SDLK_SPACE);
    bool tap_lr = in.just(SDLK_LEFT) || in.just(SDLK_RIGHT) || in.just(SDLK_a) || in.just(SDLK_d);

    switch (sel_) {
        case ROW_GLOW:
            settings_.glow = clampf(settings_.glow + dir * 1.3f * dt, Settings::GLOW_MIN, Settings::GLOW_MAX);
            break;
        case ROW_AFTERGLOW:
            settings_.persistence = clampf(settings_.persistence + dir * 0.7f * dt, Settings::PERS_MIN, Settings::PERS_MAX);
            break;
        case ROW_SCANLINE:
            settings_.scanline = clampf(settings_.scanline + dir * 0.45f * dt, Settings::SCAN_MIN, Settings::SCAN_MAX);
            break;
        case ROW_VOLUME:
            vol_f_ = clampf((vol_row_ == ROW_VOLUME ? vol_f_ : float(settings_.volume)) + dir * 90.0f * dt,
                            Settings::VOL_MIN, Settings::VOL_MAX);
            vol_row_ = ROW_VOLUME;
            settings_.volume = int(vol_f_ + 0.5f);
            break;
        case ROW_MUSIC:
            vol_f_ = clampf((vol_row_ == ROW_MUSIC ? vol_f_ : float(settings_.music)) + dir * 90.0f * dt,
                            Settings::VOL_MIN, Settings::VOL_MAX);
            vol_row_ = ROW_MUSIC;
            settings_.music = int(vol_f_ + 0.5f);
            break;
        case ROW_FULLSCREEN:
            if (tap_lr || activate) settings_.fullscreen = !settings_.fullscreen;
            break;
        case ROW_RESET:
            if (activate) { settings_.reset_defaults(); vol_row_ = -1; }
            break;
        case ROW_BACK:
            if (activate) done = true;
            break;
        default: break;
    }
}

void OptionsMenu::draw(Surface& s) {
    s.fill(Color(3, 6, 9));
    for (int x = 0; x < W_; x += 60) draw::line(s, Color(3, 10, 6), x, 0, x, H_, 1);
    for (int y = 0; y < H_; y += 60) draw::line(s, Color(3, 10, 6), 0, y, W_, y, 1);

    draw_text_centered(s, *f_.big, "OPTIONS", W_ / 2, 34, CYAN);

    // ── live preview strip: bright neon samples so glow/scanline/afterglow read
    // their changes in real time (the whole surface is run through the CRT).
    int py = 110;
    draw_text_centered(s, *f_.small, "PREVIEW", W_ / 2, py - 22, ui::LABEL);
    Color sw[5] = {CYAN, ORANGE, GREEN, YELLOW, WHITE};
    for (int i = 0; i < 5; ++i) {
        int cx = W_ / 2 - 4 * 60 + i * 120;
        draw::circle(s, sw[i], cx, py, 13);
        draw::rect(s, sw[i], {cx - 34, py + 22, 68, 6}, 0, 2);
    }
    // a bright moving dot so afterglow/persistence trails are visible while tuning
    int mx = int(W_ / 2 + std::sin(t_ * 2.2f) * 220);
    draw::circle(s, WHITE, mx, py + 44, 5);

    // ── option rows ──
    const char* labels[ROW_COUNT] = {"GLOW", "AFTERGLOW", "SCANLINES", "VOLUME", "MUSIC",
                                     "FULLSCREEN", "RESET TO DEFAULTS", "BACK"};
    int y0 = 222, rowh = 44, lx = W_ / 2 - 230, bar_x = W_ / 2 - 30, bar_w = 200;
    for (int i = 0; i < ROW_COUNT; ++i) {
        int y = y0 + i * rowh;
        bool seld = (i == sel_);
        Color lc = seld ? WHITE : Color(120, 170, 150);
        if (seld) draw_text(s, *f_.med, "\xE2\x96\xB8", lx - 30, y - 4, YELLOW);  // ▸
        draw_text(s, *f_.med, labels[i], lx, y - 4, lc);

        float frac = -1.0f;
        std::string val;
        switch (i) {
            case ROW_GLOW: frac = (settings_.glow - Settings::GLOW_MIN) / (Settings::GLOW_MAX - Settings::GLOW_MIN);
                           val = std::to_string(int(settings_.glow * 100)) + "%"; break;
            case ROW_AFTERGLOW: frac = (settings_.persistence - Settings::PERS_MIN) / (Settings::PERS_MAX - Settings::PERS_MIN);
                           val = std::to_string(int(settings_.persistence * 100)) + "%"; break;
            case ROW_SCANLINE: frac = (settings_.scanline - Settings::SCAN_MIN) / (Settings::SCAN_MAX - Settings::SCAN_MIN);
                           val = std::to_string(int(settings_.scanline * 100)) + "%"; break;
            case ROW_VOLUME: frac = settings_.volume / float(Settings::VOL_MAX);
                           val = std::to_string(int(settings_.volume * 100 / Settings::VOL_MAX)) + "%"; break;
            case ROW_MUSIC: frac = settings_.music / float(Settings::VOL_MAX);
                           val = std::to_string(int(settings_.music * 100 / Settings::VOL_MAX)) + "%"; break;
            case ROW_FULLSCREEN: val = settings_.fullscreen ? "ON" : "OFF"; break;
            default: break;
        }
        if (frac >= 0.0f) {  // slider rows
            draw::rect(s, Color(30, 50, 45), {bar_x, y, bar_w, 6}, 0, 3);
            Color fillc = seld ? CYAN : Color(60, 140, 120);
            draw::rect(s, fillc, {bar_x, y, int(bar_w * clampf(frac, 0, 1)), 6}, 0, 3);
            int kx = bar_x + int(bar_w * clampf(frac, 0, 1));
            draw::circle(s, seld ? WHITE : fillc, kx, y + 3, seld ? 7 : 5);
            draw_text(s, *f_.small, val, bar_x + bar_w + 20, y - 5, seld ? WHITE : ui::LABEL);
        } else if (i == ROW_FULLSCREEN) {
            draw_text(s, *f_.med, val, bar_x, y - 4, settings_.fullscreen ? GREEN : Color(120, 80, 80));
        }
    }

    draw_text_centered(s, *f_.small,
                       pad_ ? "\xE2\x86\x91\xE2\x86\x93 SELECT   \xE2\x86\x90\xE2\x86\x92 ADJUST   A TOGGLE   B BACK"
                            : "\xE2\x86\x91\xE2\x86\x93 SELECT   \xE2\x86\x90\xE2\x86\x92 ADJUST   ENTER TOGGLE   ESC BACK",
                       W_ / 2, H_ - 40, ui::HINT);
    for (int yy = 0; yy < H_; yy += 4) draw::rect(s, Color(0, 0, 0, 12), {0, yy, W_, 2});
}
