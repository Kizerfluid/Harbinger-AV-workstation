#!/usr/bin/env bash
# Independent smoke test of representative FFmpeg arguments; no Qt or app binary is needed.
set -euo pipefail
command -v ffmpeg >/dev/null
command -v ffprobe >/dev/null
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=160x90:rate=25 \
    -f lavfi -i sine=frequency=440:sample_rate=48000 -t 1.2 -c:v mpeg4 -c:a pcm_s16le "$TMP/input.mkv"
ffmpeg -hide_banner -nostdin -n -loglevel warning -stats_period 0.25 -progress pipe:1 \
    -i "$TMP/input.mkv" -map 0:v:0 -map "0:a:0?" -sn -dn -c:v libx264 \
    -crf 23 -preset veryfast -pix_fmt yuv420p -c:a aac -b:a 192k \
    -map_metadata 0 -movflags +faststart "$TMP/test.harbinger-part.mp4" >/dev/null
mv "$TMP/test.harbinger-part.mp4" "$TMP/test.mp4"
ffprobe -v error -show_entries stream=codec_name -of compact "$TMP/test.mp4" | grep -q h264
ffprobe -v error -show_entries stream=codec_name -of compact "$TMP/test.mp4" | grep -q aac
ffmpeg -hide_banner -nostdin -n -loglevel error -i "$TMP/input.mkv" -map 0:a:0 -vn -sn -dn \
    -c:a libmp3lame -b:a 192k "$TMP/test.mp3"
ffmpeg -hide_banner -nostdin -n -loglevel error -i "$TMP/input.mkv" -map 0:a:0 -vn -sn -dn \
    -c:a libopus -b:a 128k "$TMP/test.ogg"
ffmpeg -hide_banner -nostdin -n -loglevel error -i "$TMP/input.mkv" -map 0:a:0 -vn -sn -dn \
    -c:a flac -compression_level 5 "$TMP/test.flac"
for f in "$TMP/test.mp4" "$TMP/test.mp3" "$TMP/test.ogg" "$TMP/test.flac"; do
    test -s "$f"
    ffprobe -v error "$f" >/dev/null
    echo "PASS: $(basename "$f")"
done
