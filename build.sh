#!/usr/bin/env bash
#
# Splatoon 1 PC Port - incremental CMake rebuild (Linux).
#
# Rebuilds what ./setup.sh already configured: the recompiler (RebrewU) and,
# when present, the game itself. CMake re-runs automatically when a
# CMakeLists.txt or .cmake file changes. Run ./setup.sh first (it wires the
# dependency profiles, the game dump and the recompiled sources together).

set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
WORK="$ROOT/build"

JOBS="${JOBS:-}"
if [ -z "$JOBS" ]; then
    JOBS="$(nproc 2>/dev/null || echo 4)"
    avail_kb="$(awk '/MemAvailable/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)"
    mem_jobs=$(( avail_kb / (3 * 1024 * 1024) ))
    [ "$mem_jobs" -lt 1 ] && mem_jobs=1
    [ "$JOBS" -gt "$mem_jobs" ] && JOBS="$mem_jobs"
fi

if [ ! -f "$WORK/rebrewu/CMakeCache.txt" ]; then
    printf 'No configured build found in %s.\nRun ./setup.sh first.\n' "$WORK" >&2
    exit 1
fi

printf '[1/2] Building the recompiler (RebrewU)\n'
cmake --build "$WORK/rebrewu" --target rebrewu --parallel "$JOBS"

if [ -f "$WORK/game/CMakeCache.txt" ]; then
    printf '[2/2] Building the game\n'
    cmake --build "$WORK/game" --target Gambit --parallel "$JOBS"
    exe="$(find "$WORK/game" -maxdepth 2 -type f -name Splatoon -print -quit)"
    printf 'Built: %s\n' "${exe:-$WORK/game/Splatoon}"
else
    printf '[2/2] Game not configured yet - run ./setup.sh to configure and build it.\n'
fi