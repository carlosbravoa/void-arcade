# The Void Arcade — "make it awesome" review

Second-pass review now that strict fidelity-to-the-originals is no longer a goal.
The CRT neon identity stays; everything else is fair game. Status: ✅ done · 🔜 next · 💡 idea.

Everything below except the **Bigger swings** section has now shipped.

## Engine-wide (benefits every game)
- ✅ Juice (shake/hit-stop/rumble), CRT phosphor afterglow, soft outer-glow sprites, additive particles.
- ✅ Telemetry + self-play bots + `tools/analyze.py` difficulty reports.
- ✅ **Screen-space shockwave on big hits** — uniform-driven expanding-ring distortion + bloom flash in
  `crt.frag`, fired on boss death / nuke / boss-stomp via `Juice::shockwave(cx,cy)`.
- ✅ **Palette shift per zone/wave** — global hue rotation in the CRT pass, driven by `Juice::palette_hue`
  (each game sets it per wave/zone; boss waves tint warm).
- ✅ **Combo/multiplier system** shared across games (`engine/combo.h`) — chain hits for a rising score
  multiplier + HUD readout in all four games.
- ✅ **Characterful rumble patterns** — a small timed-segment pattern player in `Juice` with per-event
  presets (tap / hit / pickup / explosion / boss-step / death / charge).

## Void Invaders — the reference
- ✅ Progression, stacking weapons, NUKE, armored fleet, burst-fire breathers.
- ✅ **Boss variety**: HP phases (calm→aggressive→enrage), a telegraphed attack pool
  (lob, fan, radial, aimed, **bullet-wall-with-a-gap**, **rotating spiral**, **rain**,
  **sweeping laser**, **charge-slam dive**), tier-gated so the first boss is gentle and
  later bosses terrify. Red wind-up ring telegraphs every attack.
- ✅ **Mini-boss UFOs** — a tougher UFO variant that drops a guaranteed power-up.
- ✅ **Destructible-into-cover** — REBUILD power-up restores a bunker chunk.
- ✅ **Branching waves** — every few waves rolls "elite" (fewer, tankier) vs "swarm" (more, weaker).

## Void Squadrons (Galaga)
- ✅ Progression, power-ups, armored, MegaBoss, tractor/dual-fighter kept.
- ✅ **Boss variety** (phase+pool system, galaga-flavored: tractor sweeps, dive-bomb runs, minion waves).
- ✅ **Bonus "challenge" stages** — no-fire-back formations flying patterns for big points.
- ✅ **Capture risk/reward** — capture/dual-fighter rescue loop retained and rewarded.
- ✅ **Formation morphs** — formations that rotate / breathe / shift.

## Void Raid
- ✅ Progression, weapon/shield/fuel power-ups, armored, rift-blocking bosses. (Fixed a turret use-after-free crash.)
- ✅ **Boss variety** — multi-phase rift bosses: bullet patterns → **rift crush** (walls squeeze) → **chase**,
  tier-gated, with weak-points.
- ✅ **Better terrain readability** — chunky strata + parallax background layers + debris.
- ✅ **Risk/reward throttle scoring** — score multiplier that scales with scroll speed.
- ✅ **Branching rifts** — telegraphed fork: left=safe/less score, right=turret gauntlet + bonus & guaranteed power-up.
- ✅ **Fuel as resource economy** — LSHIFT overdrive spends energy for a burst of speed + firepower.

## Void Breaker (Arkanoid)
- ✅ Endless procedural levels, fire-ball, **giant-invader** brick bosses.
- ✅ **Feel fixes** — level 1 is all one-hit bricks; the ball launches at a random angle for instant variance; faster base speed.
- ✅ **Cross-game cameo** — a giant invader from the other games crashes normal levels, crushing bricks and
  firing at the paddle; the ball kills it for a power-up rain.
- ✅ **Boss variety** — bosses summon brick-minions, raise shields (break a specific brick to drop), avalanche/split surprises.
- ✅ **Gravity/wind wells** — pulsing field sources bend the ball's path; **moving bricks** drift on later levels.
- ✅ **Paddle SMASH** — hold SHIFT to charge, release to launch a ball at near-max speed with brief pierce.
- ✅ **2-ball juggling bonus** — score multiplier while keeping multiple balls alive (stacks, capped, with HUD).

## Bigger swings (whole-bundle)
- ✅ **Three new games** (the bundle is now SEVEN) — all built clean (Release + ASan), bot-playable
  (so they appear in attract/demo mode), with void bosses, combo, and the full juice/CRT identity:
  - ✅ **Void Drift** — vector Asteroids with inertial drift, splitting wireframe rocks, a sniping void
    saucer, hyperspace panic-teleport, 3-phase giant void-alien boss.
  - ✅ **Void Gunner** — first-person pseudo-3D rail shooter: welded center crosshair, depth-decay
    projection (enemies bloom out of a warping starfield), twin converging lasers, energy bar + gold
    tanks, pan-to-aim/dodge, looming "VOID WARDEN" boss with a pulsing weak point.
  - ✅ **Void Storm** — R-Type/Gradius side-scroller: charge beam, scrolling storm terrain with
    gap-carving turrets, Gradius "option" pod + 5 power-ups, multi-phase void boss.
- ✅ **Launcher carousel** — rebuilt to scale past 4 games (focused card centered + neighbours peeking +
  page dots); animated previews + stats for all seven.
- 💡 **Local co-op** (second gamepad already pluggable) for Invaders/Squadrons.
- 💡 **Daily challenge seed** + the existing leaderboard = a reason to come back.
- 💡 **Meta-progression** — earn currency across games to unlock starting perks / cosmetics.

> The bundle is now SEVEN games, all sharing the CRT identity, attract/demo mode, combo, and void bosses.
> Remaining "Bigger swings" ideas (co-op, daily seed, meta-progression) are open — pick one and I'll build it.
