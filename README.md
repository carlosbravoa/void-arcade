# The Void Arcade

Seven original retro arcade games in one neon **CRT cabinet** — native
C++17 / SDL2 / OpenGL, with a hand-written phosphor post-processing shader and
fully procedural sound (no audio files; every sound is synthesised at runtime).

> Threats come from the void. Every game is "Void ___".

![bundle](https://img.shields.io/badge/games-7-44ffaa) ![license](https://img.shields.io/badge/license-GPL--3.0--or--later-blue) ![platform](https://img.shields.io/badge/platform-Linux-orange)

## The games

| Game | Inspired by | The hook |
|------|-------------|----------|
| **Void Invaders**  | Space Invaders | Descend-and-defend, stacking power-ups, armoured fleets, branching elite/swarm waves, giant boss every 5th wave |
| **Void Squadrons** | Galaga | Diving formations, tractor-beam capture, dual-fighter rescue, challenge stages |
| **Void Raid**      | River Raid | Fly the asteroid rift, manage fuel, gap-carving turrets, branching routes, overdrive |
| **Void Breaker**   | Arkanoid | Brick-breaker with power-ups, paddle smash, multiball, cross-game boss cameos |
| **Void Drift**     | Asteroids | Vector rocks with inertial drift, splitting, a sniping void saucer, hyperspace |
| **Void Gunner**    | Starblade / Space Harrier | First-person pseudo-3D rail shooter: welded crosshair, radar, laser-energy bar, enemy formations |
| **Void Storm**     | R-Type / Gradius | Horizontal scroller with a chargeable piercing beam, option pods, scrolling terrain |

All seven share one CRT/phosphor pipeline, a combo/multiplier system, void-alien
bosses, gamepad support, an in-game **Options** screen, and a self-playing
**attract demo** when the menu sits idle.

## Why it feels native

- **No interpreter** — a single ELF binary: instant startup, no GC stutter.
- **Vsync-locked** frame pacing from the high-res performance counter.
- **Low-latency audio** — SDL2_mixer at 44.1 kHz / 512-sample buffer (~11 ms),
  with a 32-channel mixer (reserved loop channels + steal-oldest) so nothing drops.
- **Real GL pipeline** — SDL2 makes an OpenGL 3.3 core context and runs the
  `crt.vert`/`crt.frag` shader: barrel warp, chromatic aberration, multi-pass
  bloom, scanlines, vignette, and phosphor-persistence afterglow.

## Build from source

Dependencies (Ubuntu/Debian): `g++` (C++17), `cmake`, `libsdl2-dev`,
`libsdl2-mixer-dev`, and system OpenGL (`libgl-dev`). No GLEW/GLAD/SDL2_ttf —
`stb_truetype`, `stb_image_write` and `json.hpp` are vendored under `third_party/`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/void_arcade
```

Assets are copied next to the binary at build time, so it runs from anywhere.

## Install as a snap (Linux)

A strictly-confined desktop snap is provided under `snap/`:

```sh
snapcraft                                   # builds void-arcade_<ver>_amd64.snap
sudo snap install void-arcade_*.snap --dangerous
sudo snap connect void-arcade:joystick      # gamepad (keyboard works without it)
void-arcade
```

Saves live in `~/snap/void-arcade/common/`. See `snap/SNAP_BUILD.md` for details.

### Gamepad on the snap

A strict snap can't reach `/dev/input/js*` until the **`joystick`** interface is
connected, and snapd does **not** auto-connect it. So after install (or after
plugging in a pad), run once:

```sh
sudo snap connect void-arcade:joystick
```

It persists across reboots and game launches. Check with
`snap connections void-arcade` — the `joystick` row should show a slot, not `-`.
If a controller is plugged in but the interface isn't connected, the launcher
detects it (the node is *present but access-denied*, vs. simply absent when
nothing's plugged in) and shows a hint with the exact command; relaunch after
connecting. Keyboard play needs none of this.

> Auto-connecting `joystick` on install requires a store-granted snap
> declaration (request it on the Snapcraft forum for the published snap); until
> then the manual `snap connect` above is the expected one-time step.

## Controls

| Action | Key |
|---|---|
| Move / Aim / Select | **Arrows** or **A/D** (+ **W/S** in Void Gunner) |
| Fire / Launch / Confirm | **SPACE** (hold to autofire; hold to charge in Void Storm) |
| Pause / Mute | **P** / **M** |
| Options (at the menu) / Fullscreen | **O** / **F11** |
| Volume / Back to launcher | **−** **=** / **ESC** |

Per-game extras: **B** (keyboard) detonates a NUKE in Invaders/Squadrons,
**Q/E** throttle in Void Raid, **DOWN** is hyperspace in Void Drift,
**SHIFT** charges the paddle SMASH in Void Breaker.

**Gamepad** (SDL_GameController, hot-pluggable): stick / d-pad move, **A/X** fire,
**Start** pause, shoulders throttle (Void Raid). Rumble on hits/deaths.

## Options screen

Press **O** at the menu for live, persisted sliders: **glow** (bloom intensity),
**afterglow** (phosphor persistence / motion trails), **scanlines**, **volume**,
plus a fullscreen toggle. Changes preview in real time and save to `settings.json`.

## Layout

```
src/
  engine/   surface + draw, font (stb_truetype), sprite+glow, crt_renderer,
            sound (procedural, SDL2_mixer), particles, juice, combo, pause,
            scores/settings (json), boss_art, input, rng
  games/    space_invaders, galaga, void_raid, arkanoid,
            void_drift, void_gunner, void_storm  (+ registry)
  launcher.*  carousel game-select with live previews + attract demo
  options.*   in-game settings screen
  main.cpp    SDL2/GL window, vsync loop, letterbox, app state machine
assets/     shaders/ (crt.vert, crt.frag) + fonts/ (DejaVuSansMono.ttf)
third_party/ stb_truetype.h, stb_image_write.h, json.hpp  (vendored)
snap/       snapcraft.yaml + desktop entry + icon
```

## Developer env vars

- `VOID_GAME=<id>` — boot straight into a game (`space_invaders`, `galaga`,
  `void_raid`, `arkanoid`, `void_drift`, `void_gunner`, `void_storm`).
- `VOID_SHOT=out.png` (+ `VOID_SHOT_AT=<frame>`) — render one frame, dump the
  post-CRT framebuffer (and `out.png.raw.png` pre-shader), then exit.
- `VOID_BOT=1` — self-play bot (used for balancing / the attract demo).
- Quick-jump hooks: `VOID_WAVE` / `VOID_STAGE` / `VOID_LEVEL`, `VOID_BOSS` /
  `VOID_GBOSS`, `VOID_WEAPON`, `VOID_DEMO_S` / `VOID_DEMO_GAME`.

## License

GPL-3.0-or-later — see [LICENSE](LICENSE).

The bundled font (DejaVu Sans Mono) is under the DejaVu Fonts License (permissive).
The `stb_*` headers are public domain / MIT; `json.hpp` (nlohmann/json) is MIT.
