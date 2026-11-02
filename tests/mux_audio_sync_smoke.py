#!/usr/bin/env python3
"""FFmpeg integration check for MKV audio delay without audio transcoding.

Run: python tests/mux_audio_sync_smoke.py
Requires ffmpeg and ffprobe on PATH; Qt and Harbinger not required.
"""
import json
import shutil
import subprocess
import tempfile
from pathlib import Path


def call(args, capture=False):
    p = subprocess.run(args, check=True, capture_output=capture, text=True)
    return p.stdout if capture else ''


def audio_hash(path, stream_index):
    lines = call(['ffmpeg', '-v', 'error', '-i', str(path), '-map', f'0:{stream_index}',
                  '-c', 'copy', '-f', 'streamhash', '-hash', 'SHA256', '-'], capture=True)
    return lines.strip().split('SHA256=')[-1]


def main():
    for exe in ('ffmpeg', 'ffprobe'):
        if not shutil.which(exe):
            raise SystemExit(f'Missing {exe} on PATH')
    with tempfile.TemporaryDirectory(prefix='harbinger-audio-delay-') as d:
        folder = Path(d)
        video = folder / 'video.mkv'
        a = folder / 'audio_long.mka'
        b = folder / 'audio_short.mka'
        dual = folder / 'both_audio.mkv'
        out = folder / 'combined.mkv'
        call(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
              'testsrc2=size=320x180:rate=10', '-t', '4', '-c:v', 'mpeg4', str(video)])
        call(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
              'sine=frequency=440:sample_rate=48000', '-t', '4', '-c:a', 'aac', str(a)])
        call(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i',
              'sine=frequency=880:sample_rate=48000', '-t', '2', '-c:a', 'aac', str(b)])
        call(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', str(a), '-i', str(b),
              '-map', '0:0', '-map', '1:0', '-c', 'copy', str(dual)])
        # This reproduces MuxPlan.cpp's duplicate-input approach:
        # 2 audio tracks from SAME source, independently offset.
        call(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y', '-i', str(video),
              '-i', str(dual), '-itsoffset', '2.000', '-i', str(dual),
              '-map', '0:0', '-map', '1:0', '-map', '2:1', '-c', 'copy',
              '-f', 'matroska', str(out)])
        info = json.loads(call(['ffprobe', '-v', 'error', '-show_entries',
                               'stream=index,codec_type,codec_name,start_time', '-of', 'json',
                               str(out)], capture=True))
        streams = info['streams']
        assert len(streams) == 3, streams
        assert [s['codec_type'] for s in streams] == ['video', 'audio', 'audio']
        assert abs(float(streams[1]['start_time'])) < .03, streams
        assert abs(float(streams[2]['start_time']) - 2) < .03, streams
        assert audio_hash(dual, 0) == audio_hash(out, 1), 'first audio was altered'
        assert audio_hash(dual, 1) == audio_hash(out, 2), 'delayed audio was altered'
        print('PASS: independent audio offset, copied codecs, unchanged encoded audio packets')


if __name__ == '__main__':
    main()
