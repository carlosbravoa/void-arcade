# Balance observability tools

Difficulty metrics for The Void Arcade, driven by the in-game telemetry + self-play bot.

## Quick start
```sh
cmake --build build -j           # build the game
tools/balance_sweep.sh 35        # bot every game 35s, print difficulty reports
# or analyze one capture:
VOID_GAME=galaga VOID_BOT=1 VOID_TELEMETRY=/tmp/g.jsonl timeout 35 ./build/void_arcade
python3 tools/analyze.py /tmp/g.jsonl galaga
```

## What the report shows (per level/wave/stage/zone)
- **att / clr** attempts vs clears, **avg_dt** mean clear time, **deaths / death%**,
  **sh/kill** shots per kill, and **flags**: `BOSS`, `trivial` (fast + no deaths),
  `slog` (>40s), `WALL` (death rate ≥60%). Plus a curve-smoothness stdev.

## Telemetry (env-gated, zero cost in normal play)
- `VOID_TELEMETRY=<file>` — append JSON-lines events (wave/stage/level/zone
  start+clear, death, powerup, nuke, boss_clear, game_over) with dt/kills/shots/loadout.
- `VOID_BOT=1` — self-play bot + accelerated fixed-timestep (~60x realtime, renders
  every 24th frame). Add `VOID_SHOT` to force realtime so screenshots still capture.
- `VOID_WAVE` / `VOID_LEVEL` start override, `VOID_WEAPON=<tier>` preloads weapons,
  `VOID_BOSS=1` (void_raid) jumps to a boss.

## Caveats
- **Bot quality varies.** Invaders & Galaga bots aim/dodge well → trustworthy metrics.
  Arkanoid (ball english) and Void Raid (rift navigation + energy) bots are weak, so
  their absolute clear-times/death-rates are inflated — read them as "runs & progresses,"
  not as tuned difficulty. Human playtest is the source of truth for those two.
- Debug builds: `cmake -S . -B build_asan -DCMAKE_CXX_FLAGS="-fsanitize=address -g -O1"`
  catches memory bugs (found a turret use-after-free this way).
