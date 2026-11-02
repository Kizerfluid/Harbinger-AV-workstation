#!/usr/bin/env python3
"""Generate a 24-bit FLAC/PCM file and validate metadata fields supplied by FFprobe.

This tests FFprobe's output contract; AudioInfoTests.cpp tests Harbinger's parser.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile


def run(*args):
    subprocess.run(args, check=True, capture_output=True, text=True)


def probe(path):
    output = subprocess.check_output(['ffprobe', '-v', 'error', '-show_streams', '-of', 'json', str(path)], text=True)
    return next(s for s in json.loads(output)['streams'] if s['codec_type'] == 'audio')


with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    wav = p / 'source_51.wav'
    flac = p / 'source_51.flac'
    run('ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi',
        '-i', 'anullsrc=r=48000:cl=5.1', '-t', '0.1', '-c:a', 'pcm_s24le', str(wav))
    run('ffmpeg', '-nostdin', '-hide_banner', '-loglevel', 'error', '-i', str(wav), '-c:a', 'flac', str(flac))
    w = probe(wav)
    f = probe(flac)
    assert w['codec_name'] == 'pcm_s24le' and w['channels'] == 6, w
    assert f['codec_name'] == 'flac' and f['channels'] == 6, f
    assert int(f.get('bits_per_raw_sample', 0)) == 24, f
    print('FFprobe audio metadata smoke PASSED:', w['codec_name'], f['codec_name'], '6 channels, 24-bit')
