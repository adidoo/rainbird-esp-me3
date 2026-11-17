#!/usr/bin/env bash
# Chained captures from the logic analyser (fx2lafw) on the garage Raspberry Pi.
# The analyser stops by itself after about 13.5 s: 10 s captures are chained.
# Each restart leaves a gap of about 0.2 s (a Rain Bird frame lasts under 0.1 s).
#
# Usage: rb_capture.sh [total duration in s, default 120] [folder, default ~/rainbird/captures/<date>]
# Early stop: Ctrl+C. Channels: D0 = LNK2 requests (GPIO26), D1 = replies (optocoupler output).
# Each file name gives the start time (used by rb_decode.py).

set -u
export PATH=/opt/sigrok/bin:$PATH   # sigrok built from source (the Debian version stops after 1-2 s), also outside an interactive session
DUREE=${1:-120}
DOSSIER=${2:-$HOME/rainbird/captures/$(date +%Y%m%d_%H%M%S)}
FREQ=250k     # 52 samples per 208 µs half-bit: plenty
TRANCHE=10s   # below the analyser's cut-off of about 13.5 s

mkdir -p "$DOSSIER"
FIN=$(( $(date +%s) + DUREE ))
echo "Capture dans $DOSSIER pendant ${DUREE} s"
trap 'echo; echo "Arrêt demandé"; exit 0' INT TERM

while [ "$(date +%s)" -lt "$FIN" ]; do
  NOM="cap_$(date +%Y%m%d_%H%M%S_%3N).sr"
  sigrok-cli -d fx2lafw --config samplerate=$FREQ --time $TRANCHE -C D0,D1 -o "$DOSSIER/$NOM" 2>&1 \
    | grep -v "Device only sent" || true
  echo "  $NOM"
done
echo "Terminé : $(ls "$DOSSIER" | wc -l) fichiers"
