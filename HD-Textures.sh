#!/usr/bin/env bash
# Builds HD textures from YOUR copy of the game, on your PC (Linux).
# Step 1: play - every texture the game shows is saved once.
# Step 2: the textures are upscaled into Game/textures (loaded at the next start).
# Nothing is uploaded; the results are derived from your game files and stay local.
set -euo pipefail
cd -- "$(dirname -- "$(readlink -f -- "$0")")"

GAME="Game/Splatoon"
[ -x "$GAME" ] || { echo "Run ./setup.sh first (Game/Splatoon is missing)."; exit 1; }
if pgrep -x Splatoon >/dev/null 2>&1; then echo "Close Splatoon first."; exit 1; fi
command -v python3 >/dev/null 2>&1 || { echo "Python 3 is needed (for example: sudo apt install python3 python3-pip)."; exit 1; }

DUMP="$PWD/Game/texture-dump"
mkdir -p -- "$DUMP"
echo
echo "Step 1 of 2: the game starts. Visit the places you want in HD"
echo "(plaza, Octo Valley, levels), then close the game."
echo
(cd Game && RBREW_TEXTURE_DUMP="$DUMP" ./Splatoon) || true
echo
echo "Step 2 of 2: upscaling (a few minutes with an NVIDIA card, much longer on the CPU)."
echo "You can stop with Ctrl+C and run this script again later; finished textures are kept."
echo
python3 scripts/hd_textures.py "$DUMP" "Game/textures"
echo
echo "Finished. The HD textures load the next time you start the game."
