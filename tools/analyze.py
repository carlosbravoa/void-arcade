#!/usr/bin/env python3
"""
analyze.py — difficulty observability for The Void Arcade telemetry.

Ingests a JSON-lines telemetry file (produced by VOID_TELEMETRY=<file>) and
reports the difficulty curve: per-level clear time, death rate, shots/kill,
loadout progression, and flags levels that look too easy / too hard / spiky.

Works across games: it auto-detects the "level key" (wave|stage|level|zone)
from the *_clear events, and maps death/powerup events to the current level.

Usage:  python3 analyze.py <telemetry.jsonl> [game-label]
"""
import sys, json, statistics
from collections import defaultdict, OrderedDict

# Difficulty thresholds (seconds / rates) — tune to taste.
TOO_EASY_DT   = 8.0     # cleared this fast with no deaths -> trivial
TOO_HARD_DT   = 40.0    # took this long -> a slog
HARD_DEATHRATE = 0.6    # >60% of attempts died here -> wall
EASY_DEATHRATE = 0.02

LEVEL_KEYS = ("wave", "stage", "level", "zone")
CLEAR_EVS  = ("wave_clear", "stage_clear", "level_clear", "zone_clear", "boss_clear")
START_EVS  = ("wave_start", "stage_start", "level_start", "zone_start")
DEATH_EVS  = ("death", "life_lost")


def load(path):
    rows = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line:
                try: rows.append(json.loads(line))
                except json.JSONDecodeError: pass
    return rows


def level_key(rows):
    for r in rows:
        if r.get("ev") in CLEAR_EVS:
            for k in LEVEL_KEYS:
                if k in r: return k
    for r in rows:
        if r.get("ev") in START_EVS:
            for k in LEVEL_KEYS:
                if k in r: return k
    return "wave"


def analyze(rows, label):
    lk = level_key(rows)
    starts   = defaultdict(int)   # attempts at a level
    clears   = defaultdict(list)  # clear events
    deaths   = defaultdict(int)
    powerups = defaultdict(int)
    cur = 1
    runs = 0
    overruns = []  # game_over level/score/time
    for r in rows:
        ev = r.get("ev")
        if ev in START_EVS:
            cur = r.get(lk, cur); starts[cur] += 1
        elif ev in CLEAR_EVS:
            lvl = r.get(lk, cur); clears[lvl].append(r); cur = lvl
        elif ev in DEATH_EVS:
            deaths[cur] += 1
        elif ev == "powerup":
            powerups[cur] += 1
        elif ev == "nuke":
            pass
        elif ev == "game_over":
            runs += 1
            overruns.append(r)

    levels = sorted(set(list(starts) + list(clears)), key=lambda x: (isinstance(x, str), x))
    maxlvl = max([c for c in clears] or [0], key=lambda x: x if isinstance(x, int) else 0) if clears else 0

    print(f"\n===== {label}  (level key='{lk}') =====")
    reached = max([r.get(lk, 0) for r in rows if isinstance(r.get(lk), int)], default=0)
    print(f"runs(game_overs)={runs}  furthest {lk}={reached}  attempts logged={sum(starts.values())}")
    if runs:
        go_lvls = [o.get(lk, o.get('level', 0)) for o in overruns]
        go_lvls = [g for g in go_lvls if isinstance(g, int)]
        if go_lvls:
            print(f"died-out at {lk}: mean={statistics.mean(go_lvls):.1f} "
                  f"min={min(go_lvls)} max={max(go_lvls)}")

    hdr = f"{lk:>5} {'att':>3} {'clr':>3} {'avg_dt':>7} {'deaths':>6} {'death%':>6} {'sh/kill':>7} {'flags'}"
    print(hdr); print("-" * len(hdr))
    flags_summary = defaultdict(int)
    for lvl in levels:
        cl = clears.get(lvl, [])
        att = starts.get(lvl, len(cl))
        d = deaths.get(lvl, 0)
        dr = d / att if att else 0.0
        dts = [c.get("dt", 0) for c in cl]
        avg_dt = statistics.mean(dts) if dts else 0.0
        ks = sum(c.get("kills", 0) for c in cl); ss = sum(c.get("shots", 0) for c in cl)
        spk = (ss / ks) if ks else 0.0
        isboss = any(c.get("ev") == "boss_clear" for c in cl)
        flags = []
        if isboss: flags.append("BOSS")
        if avg_dt and avg_dt < TOO_EASY_DT and dr <= EASY_DEATHRATE: flags.append("trivial"); flags_summary["trivial"] += 1
        if avg_dt > TOO_HARD_DT: flags.append("slog"); flags_summary["slog"] += 1
        if dr >= HARD_DEATHRATE: flags.append("WALL"); flags_summary["WALL"] += 1
        print(f"{str(lvl):>5} {att:>3} {len(cl):>3} {avg_dt:>7.1f} {d:>6} {dr*100:>5.0f}% {spk:>7.1f}  {' '.join(flags)}")

    # difficulty-curve smoothness: stdev of clear time across levels
    curve = [statistics.mean([c.get("dt", 0) for c in clears[l]]) for l in levels if clears.get(l)]
    if len(curve) >= 3:
        print(f"\ncurve: clear-time mean={statistics.mean(curve):.1f}s "
              f"stdev={statistics.pstdev(curve):.1f}s (lower=smoother)")
    if flags_summary:
        print("balance flags:", dict(flags_summary))
    else:
        print("balance flags: none (within thresholds)")


def main():
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(1)
    rows = load(sys.argv[1])
    label = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1]
    if not rows:
        print(f"{label}: no telemetry events."); return
    analyze(rows, label)


if __name__ == "__main__":
    main()
