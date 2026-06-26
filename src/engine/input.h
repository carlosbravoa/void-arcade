// input.h — per-frame input snapshot handed to games each update.
// `down()` is held-key state (keyboard OR gamepad); `just()`/`pressed` is keys
// (and mapped gamepad buttons) pressed this frame. Games are gamepad-agnostic:
// controller input is folded onto the same keycodes they already read.
#pragma once

#include <SDL.h>

#include <vector>

// Virtual held controls fed from the active gamepad (stick + d-pad + shoulders).
enum PadBtn { PadLeft, PadRight, PadUp, PadDown, PadFire, PadThrUp, PadThrDown, PadCount };

struct Input {
    const Uint8* state = nullptr;        // SDL_GetKeyboardState, by scancode
    std::vector<SDL_Keycode> pressed;    // KEYDOWN / button-down this frame
    bool pad[PadCount] = {};             // held gamepad state for this frame

    bool down(SDL_Keycode k) const {
        if (state && state[SDL_GetScancodeFromKey(k)]) return true;
        switch (k) {
            case SDLK_LEFT:  case SDLK_a: return pad[PadLeft];
            case SDLK_RIGHT: case SDLK_d: return pad[PadRight];
            case SDLK_UP:    case SDLK_w: return pad[PadUp];
            case SDLK_DOWN:  case SDLK_s: return pad[PadDown];
            case SDLK_SPACE:              return pad[PadFire];
            case SDLK_q:                  return pad[PadThrUp];
            case SDLK_e:                  return pad[PadThrDown];
            default: return false;
        }
    }
    bool just(SDL_Keycode k) const {
        for (auto x : pressed)
            if (x == k) return true;
        return false;
    }
};
