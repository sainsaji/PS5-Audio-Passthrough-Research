#!/usr/bin/env bash
# Generates the sample app's test clips: royalty-free 5.1 channel-ID tones.
#
# Each of the six channels beeps in turn (FL, FR, C, LFE, SL, SR) for 1.2 s at
# its own pitch, then the cycle repeats. If passthrough works, the receiver
# decodes the stream and every speaker beeps in order; a wrong channel map or
# a missing speaker is easy to hear.
#
# Needs an ffmpeg with the ac3, eac3 and dca encoders.
# usage: tools/make-clips.sh [out dir]   (default: sample-app/assets/clips)
set -euo pipefail
out=${1:-"$(dirname "$0")/../sample-app/assets/clips"}
mkdir -p "$out"

secs=21.6   # three full cycles of six 1.2 s slots
beep() {    # channel index, frequency -> one aevalsrc expression
    echo "0.5*sin(2*PI*$2*t)*between(mod(t,7.2),$1*1.2,$1*1.2+0.9)"
}
expr="$(beep 0 440)|$(beep 1 554)|$(beep 2 659)|$(beep 3 55)|$(beep 4 784)|$(beep 5 988)"
src=(-f lavfi -i "aevalsrc=exprs='${expr}':s=48000:c=5.1:d=${secs}")

ffmpeg -hide_banner -loglevel error -y "${src[@]}" -c:a ac3  -b:a 448k "$out/ac3-5.1-448k.ac3"
ffmpeg -hide_banner -loglevel error -y "${src[@]}" -c:a eac3 -b:a 640k "$out/eac3-5.1-640k.eac3"
ffmpeg -hide_banner -loglevel error -y "${src[@]}" -c:a dca  -b:a 768k -strict -2 "$out/dts-5.1-768k.dts"
ffmpeg -hide_banner -loglevel error -y "${src[@]}" -c:a aac  -b:a 384k -f adts "$out/aac-5.1.aac"

# Optional, never committed: a real Dolby Atmos (E-AC-3 JOC) track cut from a
# file you own. ffmpeg cannot encode Atmos. Pass its path in ATMOS_SOURCE.
if [[ -n ${ATMOS_SOURCE:-} ]]; then
    ffmpeg -hide_banner -loglevel error -y -i "$ATMOS_SOURCE" -map 0:a:0 -c:a copy -t 30 \
        -f eac3 "$out/local-atmos.eac3"
fi

# /app0 cannot be listed on the console, so the app reads these indexes:
# index.txt for the committed clips, index.local.txt for local-* ones.
(cd "$out" && ls -1 *.ac3 *.eac3 *.dts *.aac 2>/dev/null | grep -v '^local-' | LC_ALL=C sort > index.txt)
(cd "$out" && { ls -1 local-* 2>/dev/null || true; } | LC_ALL=C sort > index.local.txt)
ls -la "$out"
