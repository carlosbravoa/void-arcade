#!/usr/bin/env bash
# balance_sweep.sh — run the self-play bot across all games, collect telemetry,
# and print a consolidated difficulty report. Observability for balance tuning.
#
# Usage: tools/balance_sweep.sh [seconds] [outdir]
set -u
HERE="$(cd "$(dirname "$0")/.." && pwd)"        # native/
BIN="$HERE/build/void_arcade"
SECS="${1:-30}"
OUT="${2:-/tmp/void_balance}"
mkdir -p "$OUT"

GAMES=(space_invaders galaga arkanoid void_raid)

if [ ! -x "$BIN" ]; then echo "build first: cmake --build $HERE/build"; exit 1; fi

echo "== Void Arcade balance sweep =="
echo "binary: $BIN   per-game: ${SECS}s accelerated self-play   out: $OUT"
for g in "${GAMES[@]}"; do
  f="$OUT/$g.jsonl"; rm -f "$f"
  VOID_GAME="$g" VOID_BOT=1 VOID_TELEMETRY="$f" timeout "$SECS" "$BIN" >/dev/null 2>&1
  n=$(wc -l < "$f" 2>/dev/null || echo 0)
  echo "  $g: $n events"
done

echo
for g in "${GAMES[@]}"; do
  python3 "$HERE/tools/analyze.py" "$OUT/$g.jsonl" "$g"
done
