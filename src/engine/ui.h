// ui.h — small shared UI vocabulary so every game reads the same: prompt
// colours that stay legible through the CRT pass, key names that follow the
// active input device, and one game-over panel.
#pragma once

#include <cmath>
#include <cstdio>
#include <string>

#include "color.h"
#include "game.h"
#include "input.h"
#include "surface.h"
#include "text.h"

namespace ui {

// Minimum brightness for anything the player is meant to read. Darker greens
// like (40,80,40) vanish under scanlines + vignette.
inline constexpr Color HINT{150, 205, 170};   // prompts, control hints
inline constexpr Color LABEL{115, 160, 135};  // passive HUD labels ("LIVES", "PWR")

// Key names for on-screen prompts. `pad` = Input::pad_connected.
inline const char* fire_key(bool pad) { return pad ? "A" : "SPACE"; }
inline const char* nuke_key(bool pad) { return pad ? "Y" : "B"; }
inline const char* alt_key(bool pad)  { return pad ? "B" : "SHIFT"; }
inline const char* pause_key(bool pad) { return pad ? "START" : "P"; }
inline const char* menu_key(bool pad) { return pad ? "START" : "ESC"; }

// Seconds after game over during which retry/menu input is ignored, so a
// player still hammering fire doesn't skip the screen.
inline constexpr float GAME_OVER_LOCK = 0.8f;

// `t` = seconds since the game-over state began.
inline bool game_over_retry(const Input& in, float t) {
    return t >= GAME_OVER_LOCK && (in.just(SDLK_SPACE) || in.just(SDLK_RETURN));
}
inline bool game_over_menu(const Input& in, float t) {
    return t >= GAME_OVER_LOCK && (in.just(SDLK_ESCAPE) || in.just(SDLK_p));
}

// The shared game-over panel: fades a dim plate over the playfield, then
// stacks GAME OVER / score / hi-score flag / one detail line / the prompt on
// separate rows. Draw it last. `detail` is optional ("WAVE 7", "STAGE 3" …).
inline void game_over(Surface& s, const Fonts& f, int W, int H, float t,
                      int score, bool new_hi, bool pad,
                      const std::string& detail = "") {
    float in = std::fmin(1.0f, t * 2.5f);
    draw::rect(s, Color(0, 0, 0, int(165 * in)), {0, 0, W, H});
    int cx = W / 2, y = H / 2 - 96;
    draw_text_centered(s, *f.big, "GAME OVER", cx, y, Color(255, 70, 70, int(255 * in)));
    char buf[32];
    std::snprintf(buf, sizeof buf, "SCORE  %06d", score);
    draw_text_centered(s, *f.med, buf, cx, y + 62, Color(230, 240, 230, int(255 * in)));
    if (new_hi && std::fmod(t, 0.7f) < 0.5f)
        draw_text_centered(s, *f.med, "NEW HI-SCORE!", cx, y + 94, Color(255, 220, 40));
    if (!detail.empty())
        draw_text_centered(s, *f.small, detail, cx, y + 128, LABEL);
    if (t >= GAME_OVER_LOCK && std::fmod(t, 1.0f) < 0.65f) {
        std::string p = std::string(fire_key(pad)) + "  RETRY        " +
                        menu_key(pad) + "  MENU";
        draw_text_centered(s, *f.small, p, cx, y + 160, HINT);
    }
}

}  // namespace ui
