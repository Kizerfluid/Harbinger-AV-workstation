#!/usr/bin/env bash
set -euo pipefail
command -v ffmpeg >/dev/null
command -v ffprobe >/dev/null
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
# Tiny synthetic movie, with audio retained from the original during final mux.
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "testsrc2=size=160x90:rate=25:duration=6.4" \
  -f lavfi -i "sine=frequency=330:sample_rate=48000:duration=6.4" \
  -c:v libx264 -threads 2 -preset veryfast -pix_fmt yuv420p -c:a aac -shortest "$work/input.mkv"
for i in 0 1 2 3; do
  start=$((i*2)); duration=2
  if [[ "$i" -eq 3 ]]; then duration=0.4; fi
  ffmpeg -hide_banner -loglevel error -y -i "$work/input.mkv" -ss "$start" -t "$duration" \
    -map 0:v:0 -an -sn -dn -c:v libx264 -threads 2 -crf 23 -preset veryfast -pix_fmt yuv420p \
    "$work/part_$(printf %04d "$i").mkv"
done
for i in 0 1 2 3; do printf "file '%s/part_%04d.mkv'\n" "$work" "$i"; done > "$work/concat.txt"
ffmpeg -hide_banner -loglevel error -y -f concat -safe 0 -i "$work/concat.txt" \
    -i "$work/input.mkv" -map 0:v:0 -map 1:a? -map 1:s? -c copy \
    -map_metadata 1 -map_chapters 1 "$work/final.mkv"
python - "$work/final.mkv" <<'PY'
import json,subprocess,sys
p=subprocess.check_output(['ffprobe','-v','error','-show_entries','stream=codec_type,codec_name','-show_entries','format=duration','-of','json',sys.argv[1]])
a=json.loads(p)
tracks=[s['codec_type'] for s in a['streams']]
duration=float(a['format']['duration'])
assert tracks.count('video')==1 and tracks.count('audio')==1,tracks
assert 6.0<duration<7.0,duration
print('Hive FFmpeg split/concat/remux proof-of-concept: PASSED',tracks,'duration',duration)
PY
