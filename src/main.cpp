// main.cpp — The Void Arcade (native C++/SDL2 port).
//
// SDL2 + OpenGL 3.3, vsync-locked. Games render onto a CPU Surface that is
// uploaded as a texture and run through the original CRT shader, letterboxed.

#include <SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>

#include <cerrno>   // errno — distinguish "blocked" from "absent" joystick nodes
#include <fcntl.h>  // open()
#include <unistd.h> // close()

#include "engine/crt_renderer.h"
#include "engine/font.h"
#include "engine/game.h"
#include "engine/gl_loader.h"
#include "engine/input.h"
#include "engine/juice.h"
#include "engine/rng.h"
#include "engine/combo.h"
#include "engine/scores.h"
#include "engine/sound.h"
#include "engine/surface.h"
#include "engine/telemetry.h"
#include "engine/text.h"
#include "games/registry.h"
#include "launcher.h"
#include "options.h"
#include "engine/settings.h"
#include "stb_image_write.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr int kW = 900;
constexpr int kH = 720;
const char* kTitle = "THE  VOID  ARCADE  //  CRT  EDITION";

// Is a controller physically present but unreachable because we're a strictly
// confined snap whose `joystick` interface isn't connected?
//
// Inside the snap, /dev/input/js* is mediated by AppArmor + the device cgroup.
// When the interface is NOT connected, opening the node is *denied*
// (EACCES/EPERM) — the device exists, we're just blocked. When no controller is
// plugged in at all, the node is simply *absent* (ENOENT). That errno
// difference lets us tell "go connect the interface" apart from "nothing's
// plugged in", so we only nag when it's actually fixable. Only meaningful inside
// a snap ($SNAP set); open() never blocks, so this can't hang.
bool snap_pad_blocked() {
    if (!std::getenv("SNAP")) return false;
    bool blocked = false;
    for (int i = 0; i < 4; ++i) {
        char path[32];
        std::snprintf(path, sizeof path, "/dev/input/js%d", i);
        int fd = ::open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) ::close(fd);                                 // readable: fine
        else if (errno == EACCES || errno == EPERM) blocked = true;  // present but blocked
        // ENOENT (and anything else) → treat as "not blocked".
    }
    return blocked;
}

// Map a gamepad button to the keycode the games already understand.
SDL_Keycode map_pad_button(Uint8 b) {
    switch (b) {
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_X:          return SDLK_SPACE;
        // B/Back intentionally unmapped: B sits right next to A (fire), so it kept
        // closing the game by accident. Quitting is keyboard-only now (ESC); on a
        // pad, use Start to pause and choose "quit to menu" from there.
        case SDL_CONTROLLER_BUTTON_Y:          return SDLK_b;   // NUKE (the 'B' bomb key)
        case SDL_CONTROLLER_BUTTON_START:      return SDLK_p;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return SDLK_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return SDLK_RIGHT;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:    return SDLK_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return SDLK_DOWN;
        default:                               return SDLK_UNKNOWN;
    }
}

struct Viewport { int x, y, w, h; };
Viewport letterbox(int win_w, int win_h) {
    float ga = float(kW) / float(kH), wa = float(win_w) / float(win_h);
    Viewport v;
    if (wa >= ga) { v.h = win_h; v.w = int(win_h * ga); v.x = (win_w - v.w) / 2; v.y = 0; }
    else { v.w = win_w; v.h = int(win_w / ga); v.x = 0; v.y = (win_h - v.h) / 2; }
    return v;
}
}  // namespace

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

    int win_w = kW, win_h = kH;
    if (const char* ws = std::getenv("VOID_WIN")) {  // dev: "1920x1080"
        int a = 0, b = 0;
        if (std::sscanf(ws, "%dx%d", &a, &b) == 2 && a >= 320 && b >= 240) { win_w = a; win_h = b; }
    }
    SDL_Window* win = SDL_CreateWindow(kTitle, SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, win_w, win_h,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                           SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { std::fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx || !gl::load()) { std::fprintf(stderr, "GL init failed\n"); return 1; }
    SDL_GL_SetSwapInterval((std::getenv("VOID_BOT") && !std::getenv("VOID_SHOT")) ? 0 : 1);

    std::string base = SDL_GetBasePath() ? SDL_GetBasePath() : "./";
    CRTRenderer crt(kW, kH, base + "assets/shaders");
    Font fbig(base + "assets/fonts/DejaVuSansMono.ttf", 40);
    Font fmed(base + "assets/fonts/DejaVuSansMono.ttf", 22);
    Font fsmall(base + "assets/fonts/DejaVuSansMono.ttf", 15);

    // A fresh seed per launch so no two sessions play out identically.
    // VOID_SEED pins it for reproducible balance runs.
    if (const char* sd = std::getenv("VOID_SEED")) rng().seed(unsigned(std::atol(sd)));
    else rng().seed(unsigned(SDL_GetPerformanceCounter() ^ (Uint64(std::random_device{}()) << 1)));

    SoundManager sfx;
    sfx.init();
    ScoreManager scores;
    Juice juice;
    Combo combo;
    Telemetry tele;
    if (const char* tp = std::getenv("VOID_TELEMETRY")) tele.open(tp);

    // Player-tunable look/feel, persisted to settings.json. Applied to the CRT,
    // sound, and window every frame so the Options screen previews changes live.
    Settings settings;
    settings.load();
    std::unique_ptr<OptionsMenu> options;  // non-null while the Options screen is open

    // Accelerated self-play: fixed timestep + no vsync so the bot plays many
    // game-seconds per real-second for balance data collection. Disabled when
    // capturing a screenshot (VOID_SHOT) so rendering still happens.
    bool accel = std::getenv("VOID_BOT") && !std::getenv("VOID_SHOT");

    Context gctx;
    gctx.W = kW; gctx.H = kH;
    gctx.fonts = {&fbig, &fmed, &fsmall};
    gctx.sfx = &sfx;
    gctx.scores = &scores;
    gctx.fx = &juice;
    gctx.tele = &tele;
    gctx.combo = &combo;

    // Open the first available game controller (hot-plug handled in the loop).
    SDL_GameController* pad = nullptr;
    auto open_first_pad = [&] {
        for (int i = 0; i < SDL_NumJoysticks() && !pad; ++i)
            if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);
    };
    open_first_pad();
    if (pad) std::printf("[pad] %s connected\n", SDL_GameControllerName(pad));
    gctx.pad_blocked = !pad && snap_pad_blocked();
    if (gctx.pad_blocked)
        std::printf("[pad] controller present but blocked — run: "
                    "snap connect void-arcade:joystick\n");

    auto launcher = std::make_unique<Launcher>(gctx);
    std::unique_ptr<Scene> game;
    if (const char* g = std::getenv("VOID_GAME")) game = make_game(g, gctx);

    Surface game_surf(kW, kH);
    std::string capture_path;  // set to request a post-shader framebuffer dump
    const char* shot_env = std::getenv("VOID_SHOT");
    int frame = 0;
    bool fullscreen = false, running = true;

    // ── Attract "demo" mode ──────────────────────────────────────────────────
    // After a long idle at the menu, the arcade plays itself: a bot-driven game
    // runs as a live demo with a "PRESS ANY KEY" overlay. Any input or a time
    // cap drops back to the menu, cycling to the next game on the following demo.
    const float DEMO_IDLE = [] {            // seconds idle at menu before a demo
        const char* e = std::getenv("VOID_DEMO_S");
        return e ? float(std::atof(e)) : 90.0f;
    }();
    const float DEMO_MAX = 42.0f;           // seconds of demo before back to menu
    float menu_idle = 0.0f;                 // idle time on the menu (not in a game)
    bool  demo_mode = false;                // a demo game is currently running
    float demo_t = 0.0f;                    // elapsed time in the current demo
    Uint64 prev = SDL_GetPerformanceCounter();
    double freq = double(SDL_GetPerformanceFrequency());

    auto present = [&](Surface& surf, float dt) {
        int ww, wh;
        SDL_GL_GetDrawableSize(win, &ww, &wh);
        Viewport vp = letterbox(ww, wh);
        // Screen shake: jitter the letterbox viewport, decaying toward zero.
        if (juice.shake_mag > 0.1f) {
            float s = juice.shake_mag * (vp.w / float(kW));  // scale to window
            vp.x += int(rnd_uniform(-s, s));
            vp.y += int(rnd_uniform(-s, s));
            juice.shake_mag *= std::exp(-dt * 11.0f);
        } else {
            juice.shake_mag = 0.0f;
        }
        crt.set_hue(juice.palette_hue);
        if (juice.shock_req) {
            crt.trigger_shockwave(juice.shock_cx, juice.shock_cy);
            juice.shock_req = false;
        }
        crt.present(surf, dt, ww, wh, vp.x, vp.y, vp.w, vp.h);
        if (!capture_path.empty()) {
            stbi_write_png((capture_path + ".raw.png").c_str(), kW, kH, 4,
                           surf.data(), kW * 4);
            std::vector<uint8_t> buf(size_t(ww) * wh * 4);
            glReadPixels(0, 0, ww, wh, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
            std::vector<uint8_t> flip(buf.size());  // GL origin is bottom-left
            for (int y = 0; y < wh; ++y)
                std::memcpy(&flip[size_t(y) * ww * 4],
                            &buf[size_t(wh - 1 - y) * ww * 4], size_t(ww) * 4);
            stbi_write_png(capture_path.c_str(), ww, wh, 4, flip.data(), ww * 4);
            std::printf("[shot] wrote %s (%dx%d)\n", capture_path.c_str(), ww, wh);
            capture_path.clear();
        }
        SDL_GL_SwapWindow(win);
    };

    auto draw_demo_banner = [&](Surface& surf) {
        // Centered translucent plate in the lower-mid dead zone (below bunkers,
        // above the player) so it never hides the HUD or the action.
        const int pw = 446, ph = 52, px = kW / 2 - pw / 2, py = kH - 138;
        draw::rect(surf, Color(0, 0, 0, 150), {px, py, pw, ph}, 0, 8);
        draw::rect(surf, Color(120, 255, 160, 80), {px, py, pw, ph}, 2, 8);
        draw_text_centered(surf, fsmall, "ATTRACT  DEMO", kW / 2, py + 9, Color(120, 255, 160));
        if (std::fmod(demo_t, 1.0f) < 0.6f)  // blink the call to action
            draw_text_centered(surf, fmed, "PRESS ANY KEY TO PLAY", kW / 2, py + 25,
                               Color(255, 220, 40));
    };

    // Key presses that arrive while the sim is frozen (hit-stop) are held here
    // and delivered on the next simulated frame instead of being lost.
    std::vector<SDL_Keycode> carried;
    int stick_x = 0, stick_y = 0;  // last digital stick direction, for edge events
    bool trigger_held = false;

    while (running) {
        Uint64 now = SDL_GetPerformanceCounter();
        // Frame limiter: vsync normally paces us, but when it is off or ignored
        // (some drivers, minimised windows) don't spin a core at 1000s of fps.
        if (!accel) {
            const double min_frame = 1.0 / 250.0;
            double el = double(now - prev) / freq;
            if (el < min_frame) {
                SDL_Delay(Uint32((min_frame - el) * 1000.0));
                now = SDL_GetPerformanceCounter();
            }
        }
        float dt = accel ? (1.0f / 120.0f) : float((now - prev) / freq);
        if (dt > 0.05f) dt = 0.05f;
        prev = now;

        // Apply player settings every frame so the Options screen previews live.
        crt.set_glow(settings.glow);
        crt.set_scanline(settings.scanline);
        crt.set_persistence(settings.persistence);
        sfx.set_master_volume(settings.volume);
        sfx.set_music_volume(settings.music);
        if (settings.fullscreen != fullscreen) {
            fullscreen = settings.fullscreen;
            SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        }

        ++frame;
        combo.update(dt);
        if (shot_env) {
            const char* at = std::getenv("VOID_SHOT_AT");
            int shot_at = at ? std::atoi(at) : 40;  // settle animations first
            if (frame == shot_at) capture_path = shot_env;
            if (frame > shot_at + 5) running = false;
        }

        Input input;
        input.pressed.swap(carried);
        SDL_Event e;
        auto handle_key = [&](SDL_Keycode k) {
            if (k == SDLK_F11) {
                settings.fullscreen = !settings.fullscreen;  // per-frame sync applies it
            } else if (k == SDLK_m) {
                sfx.toggle_mute();
            } else if (k == SDLK_MINUS || k == SDLK_KP_MINUS) {
                settings.volume = std::max(0, settings.volume - 10);
            } else if (k == SDLK_EQUALS || k == SDLK_KP_PLUS) {
                settings.volume = std::min(128, settings.volume + 10);
            } else if (k != SDLK_UNKNOWN) {
                input.pressed.push_back(k);
            }
        };
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_KEYDOWN && e.key.repeat == 0) handle_key(e.key.keysym.sym);
            else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                // B is "back" inside the Options screen only. In games it stays a
                // held button (it sits next to fire, so it must never quit).
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B && options)
                    handle_key(SDLK_ESCAPE);
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                    if (!game && !options) handle_key(SDLK_o);  // Options, menu only
                } else
                    handle_key(map_pad_button(e.cbutton.button));
            } else if (e.type == SDL_CONTROLLERDEVICEADDED && !pad) {
                pad = SDL_GameControllerOpen(e.cdevice.which);
                if (pad) gctx.pad_blocked = false;
            } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
                // Only react when it is *our* pad that went away.
                if (pad && e.cdevice.which == SDL_JoystickInstanceID(
                               SDL_GameControllerGetJoystick(pad))) {
                    SDL_GameControllerClose(pad);
                    pad = nullptr;
                    open_first_pad();
                }
            }
        }
        input.state = SDL_GetKeyboardState(nullptr);

        // Fold gamepad held-state onto the virtual controls.
        if (pad) {
            const int DZ = 8000;
            auto h = [&](SDL_GameControllerButton b) {
                return SDL_GameControllerGetButton(pad, b) != 0;
            };
            int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
            int ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
            input.pad[PadLeft]  = h(SDL_CONTROLLER_BUTTON_DPAD_LEFT)  || lx < -DZ;
            input.pad[PadRight] = h(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || lx >  DZ;
            input.pad[PadUp]    = h(SDL_CONTROLLER_BUTTON_DPAD_UP)    || ly < -DZ;
            input.pad[PadDown]  = h(SDL_CONTROLLER_BUTTON_DPAD_DOWN)  || ly >  DZ;
            input.pad[PadFire]  = h(SDL_CONTROLLER_BUTTON_A) || h(SDL_CONTROLLER_BUTTON_X) ||
                SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > DZ;
            input.pad[PadThrUp]   = h(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
            input.pad[PadThrDown] = h(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
            input.pad[PadBtnB]    = h(SDL_CONTROLLER_BUTTON_B);  // held B (e.g. Void Drift thrust)
            input.pad_connected = true;

            // A stick flick counts as a press, like the d-pad, so menus and
            // edge-triggered actions work from the analog stick too. Wider
            // threshold + hysteresis than the held deadzone to avoid chatter.
            const int ON = 16000, OFF = 9000;
            auto dir = [&](int v, int prev_dir) {
                if (v > ON) return 1;
                if (v < -ON) return -1;
                if (std::abs(v) < OFF) return 0;
                return prev_dir;
            };
            int nx = dir(lx, stick_x), ny = dir(ly, stick_y);
            if (nx != stick_x && nx != 0) input.pressed.push_back(nx < 0 ? SDLK_LEFT : SDLK_RIGHT);
            if (ny != stick_y && ny != 0) input.pressed.push_back(ny < 0 ? SDLK_UP : SDLK_DOWN);
            stick_x = nx; stick_y = ny;
            // Same for the right trigger: pulling it is a fire *press*.
            bool trig = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > DZ;
            if (trig && !trigger_held) input.pressed.push_back(SDLK_SPACE);
            trigger_held = trig;
        } else {
            stick_x = stick_y = 0;
            trigger_held = false;
        }

        // Advance any active characterful rumble pattern, then consume. The
        // attract demo is "hands-off" — never buzz the controller during it.
        juice.tick_rumble(dt);
        if (pad && !demo_mode && juice.rumble_ms > 0)
            SDL_GameControllerRumble(pad, Uint16(juice.rumble_low * 65535),
                                     Uint16(juice.rumble_high * 65535), juice.rumble_ms);
        juice.rumble_low = juice.rumble_high = 0; juice.rumble_ms = 0;

        // Did the user actually do something this frame? (wakes the attract demo)
        bool any_input = !input.pressed.empty();
        if (pad) for (int b = 0; b < PadCount; ++b) any_input = any_input || input.pad[b];

        // ── Options screen: a live-preview settings editor, modal over the menu ──
        if (options) {
            options->update(dt, input);
            if (options->done) { settings.save(); options.reset(); menu_idle = 0.0f; }
            else { options->draw(game_surf); present(game_surf, dt); continue; }
        }

        // ── attract demo in progress: bot plays, any input or time-cap exits ──
        if (demo_mode && game) {
            demo_t += dt;
            bool woke = any_input;
            bool capped = demo_t >= DEMO_MAX || game->quit_to_menu;
            if (woke || capped) {
                sfx.stop_all();
                game.reset();
                juice = Juice{};   // no leftover hue drift / shake / rumble in the menu
                demo_mode = false;
                menu_idle = 0.0f;
                // On a natural time-out, cycle to the next game so a long idle
                // walks the whole roster; on a wake, stay on the current card.
                launcher->on_demo_end(capped && !woke);
                launcher->draw(game_surf);
                present(game_surf, dt);
                continue;
            }
            bool frozen = juice.hitstop_t > 0.0f;
            if (frozen) { juice.hitstop_t -= dt; present(game_surf, dt); continue; }
            game->update(dt, input);
            game->draw(game_surf);
            draw_demo_banner(game_surf);
            present(game_surf, dt);
            continue;
        }

        if (!game) {
            sfx.start_music(7);  // calm menu loop (no-op while already playing)
            launcher->update(dt, input);
            if (launcher->options_requested) {
                launcher->options_requested = false;
                options = std::make_unique<OptionsMenu>(kW, kH, gctx.fonts, settings);
                menu_idle = 0.0f;
                continue;
            }
            if (launcher->quit_requested) { running = false; }
            else if (!launcher->selection_id.empty()) {
                combo.reset();
                game = make_game(launcher->selection_id, gctx);
                launcher->selection_id.clear();
                menu_idle = 0.0f;
            }
            if (!game) {
                // Idle bookkeeping → launch a live bot-played demo after a long wait.
                menu_idle = any_input ? 0.0f : (menu_idle + dt);
                if (menu_idle >= DEMO_IDLE) {
                    // Demo the game the player is hovering. VOID_DEMO_GAME pins it
                    // to one id (debug / preview).
                    const char* forced = std::getenv("VOID_DEMO_GAME");
                    std::string pick = (forced && *forced) ? std::string(forced)
                                                           : launcher->current_id();
                    combo.reset();
                    gctx.demo = true;
                    game = make_game(pick, gctx);
                    gctx.demo = false;
                    if (game) { demo_mode = true; demo_t = 0.0f; }
                    else menu_idle = 0.0f;  // unknown id → stay in the menu
                    continue;  // begin rendering the demo (or menu) next iteration
                }
                launcher->draw(game_surf);
                present(game_surf, dt);
                continue;
            }
        }

        if (game) {
            // Hit-stop: freeze the simulation (and held frame) for a beat,
            // but keep presenting so shake still plays.
            bool frozen = juice.hitstop_t > 0.0f;
            if (frozen) {
                juice.hitstop_t -= dt;
                carried = input.pressed;  // deliver these once the sim resumes
                if (!accel) present(game_surf, dt);
                continue;
            }
            game->update(dt, input);
            if (game->quit_to_menu) {
                game->on_exit();
                sfx.stop_all();
                scores.load();
                game.reset();
                juice = Juice{};   // no leftover hue drift / shake / rumble in the menu
                menu_idle = 0.0f;
                launcher = std::make_unique<Launcher>(gctx);
                launcher->draw(game_surf);
                present(game_surf, dt);
                continue;
            }
            // In accelerated self-play, render only occasionally so the update
            // loop (the part we measure) runs many game-seconds per real-second.
            if (!accel || frame % 24 == 0) {
                game->draw(game_surf);
                present(game_surf, dt);
            }
        }
    }

    settings.save();   // persist any F11 / volume changes made outside the Options screen
    sfx.stop_all();
    sfx.shutdown();
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
