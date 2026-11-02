#!/usr/bin/env bash
set -euo pipefail
F="${FFMPEG:-ffmpeg}"
P="${FFPROBE:-ffprobe}"
D="$(mktemp -d)"
trap 'rm -rf "$D"' EXIT
# A video from one Matroska; separate audio tracks from two other Matroskas.
"$F" -hide_banner -loglevel error -y -f lavfi -i testsrc2=size=160x90:rate=12 -t 2 -an -c:v mpeg4 -q:v 5 "$D/Video A.mkv"
"$F" -hide_banner -loglevel error -y \
    -f lavfi -i sine=frequency=330:sample_rate=48000 \
    -f lavfi -i sine=frequency=440:sample_rate=48000 -t 2 -map 0:a:0 -map 1:a:0 -vn -c:a aac "$D/Audio B.mkv"
"$F" -hide_banner -loglevel error -y -f lavfi -i sine=frequency=550:sample_rate=48000 -t 2 -vn -c:a ac3 "$D/Audio C.mkv"
cat > "$D/Captions.srt" <<'SRT'
1
00:00:00,500 --> 00:00:01,500
Test subtitle from source D
SRT
"$F" -hide_banner -loglevel error -nostdin -n \
    -i "$D/Video A.mkv" -i "$D/Audio B.mkv" -i "$D/Audio C.mkv" -i "$D/Captions.srt" \
    -map 0:0 -map 1:1 -map 2:0 -map 3:0 -c copy \
    -metadata:s:0 language=und -metadata:s:1 language=eng -metadata:s:2 language=jpn -metadata:s:3 language=eng \
    -metadata:s:1 'title=English AAC' -metadata:s:2 'title=Japanese AC-3' \
    -disposition:0 default -disposition:1 default -disposition:2 0 -disposition:3 forced \
    -map_chapters -1 -f matroska "$D/Muxed.harbinger-part.mkv"
mv "$D/Muxed.harbinger-part.mkv" "$D/Muxed.mkv"
"$P" -v error -show_entries stream=index,codec_type,codec_name:stream_tags=language,title:stream_disposition=default,forced -of json "$D/Muxed.mkv" > "$D/results.json"
python - "$D/results.json" <<'PY'
import json, sys
streams=json.load(open(sys.argv[1]))['streams']
assert [s['codec_type'] for s in streams] == ['video','audio','audio','subtitle'], streams
assert [s['codec_name'] for s in streams] == ['mpeg4','aac','ac3','subrip'], streams
assert streams[1].get('tags',{}).get('language')=='eng', streams
assert streams[2].get('tags',{}).get('language')=='jpn', streams
assert streams[1]['disposition']['default']==1, streams
assert streams[2]['disposition']['default']==0, streams
assert streams[3]['disposition']['forced']==1, streams
print('PASS: 4 sources (including non-first audio stream) → one MKV, 1 video + 2 audio + 1 subtitle; codecs/metadata/dispositions correct.')
PY
# Test text-subtitle conversion from MP4's mov_text into Matroska SRT.
"$F" -hide_banner -loglevel error -y -i "$D/Video A.mkv" -i "$D/Captions.srt" \
    -map 0:0 -map 1:0 -c:v copy -c:s mov_text "$D/Timed Text.mp4"
"$F" -hide_banner -loglevel error -nostdin -n -i "$D/Timed Text.mp4" \
    -map 0:0 -map 0:1 -c copy -c:1 srt -f matroska "$D/Mp4SubToMkv.mkv"
CODEC=$("$P" -v error -select_streams s:0 -show_entries stream=codec_name -of default=nk=1:nw=1 "$D/Mp4SubToMkv.mkv")
[[ "$CODEC" == subrip ]] || { echo "FAIL: mov_text conversion: $CODEC" >&2; exit 1; }
echo 'PASS: mov_text subtitle converted to SRT without re-encoding video.'
