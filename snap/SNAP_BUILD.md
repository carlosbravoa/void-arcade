# Building the Void Arcade snap (native C++ build)

This packages the native C++/SDL2/OpenGL bundle (`native/`) as a strictly-confined
desktop-game snap named `void-arcade`.

## Layout this snap produces

- `$SNAP/bin/void_arcade`      — the game binary (command)
- `$SNAP/bin/assets/`          — shaders + font (found via `SDL_GetBasePath()`)
- `$SNAP_USER_COMMON/`         — writable `scores.json` / `settings.json`
                                 (via `RETRO_ARCADE_DATA`, set in the app env)

The `gnome` extension provides Mesa/OpenGL, Wayland/X11, and desktop integration,
so the binary just links `libGL`/SDL2 normally — no launcher script needed.

## Prerequisites (one-time, needs sudo)

```bash
sudo snap install snapcraft --classic
sudo snap install lxd
sudo lxd init --minimal
# add yourself to the lxd group, then re-login (or: newgrp lxd)
sudo usermod -aG lxd "$USER"
```

## Build

From the `native/` directory (the one containing `snap/`):

```bash
cd native
snapcraft            # builds in an LXD container; produces void-arcade_2.0.0_amd64.snap
```

Tips:
- `snapcraft --verbose` for detailed logs.
- `snapcraft --debug` drops you into the build container shell on failure.
- For fast iteration without re-priming everything: `snapcraft prime && sudo snap try prime/`.

## Install locally (unsigned) and run

```bash
sudo snap install void-arcade_2.0.0_amd64.snap --dangerous
void-arcade
```

## Interfaces

Auto-connected on desktop: `opengl`, `audio-playback`, `wayland`, `x11`,
`desktop`, `desktop-legacy`.

The **gamepad** interface is NOT auto-connected — connect it once for controller
support (keyboard works regardless):

```bash
sudo snap connect void-arcade:joystick
```

## Where saves go

```
$HOME/snap/void-arcade/common/scores.json
$HOME/snap/void-arcade/common/settings.json
```

## Publishing (later)

```bash
snapcraft login
snapcraft register void-arcade        # one-time name registration
snapcraft upload void-arcade_2.0.0_amd64.snap --release=edge
# promote when happy:
snapcraft release void-arcade <revision> stable
```

## Notes / possible tweaks

- If OpenGL fails at runtime under strict confinement (black screen / GL init
  failed), stage the Mesa runtime explicitly by adding to the part's
  `stage-packages`: `libgl1 libgl1-mesa-dri libglx-mesa0 libegl1 libegl-mesa0`.
  (Usually unnecessary with the `gnome` extension.)
- To also build arm64, add `arm64:` under `platforms:` and build on an arm64 host
  (or with `snapcraft remote-build`).
