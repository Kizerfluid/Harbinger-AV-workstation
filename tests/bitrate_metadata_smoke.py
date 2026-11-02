"""Regression: after transcoding, old Matroska BPS tags must not survive.

This is an FFmpeg command-equivalence test for Codec::buildArgs. The C++
argument builder is separately covered by CodecTests.cpp.
"""
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

ffmpeg = shutil.which('ffmpeg')
ffprobe = shutil.which('ffprobe')
if not ffmpeg or not ffprobe:
    raise SystemExit('FFmpeg and FFprobe required')


def run(*args):
    output = subprocess.run(args, capture_output=True, text=True, timeout=90)
    if output.returncode:
        raise RuntimeError(f'{args}: {output.stderr[-3000:]}')
    return output.stdout


def inspect(path):
    return json.loads(run(ffprobe, '-v', 'error', '-show_streams', '-of', 'json', str(path)))['streams']


def scrub(video, audio):
    keys = ['BPS', 'BPS-eng', 'NUMBER_OF_BYTES', 'NUMBER_OF_BYTES-eng',
            'NUMBER_OF_FRAMES', 'NUMBER_OF_FRAMES-eng', 'DURATION', 'DURATION-eng']
    args = []
    for key in keys:
        if video:
            args.extend(['-metadata:s:v', key + '='])
        if audio:
            args.extend(['-metadata:s:a', key + '='])
    return args


with tempfile.TemporaryDirectory() as temp:
    folder = Path(temp)
    source = folder / 'source.mkv'
    converted = folder / 'converted.mkv'
    audio_only = folder / 'audio-only.mkv'
    run(ffmpeg, '-hide_banner', '-v', 'error', '-y',
        '-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=24',
        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
        '-f', 'lavfi', '-i', 'sine=frequency=660:sample_rate=48000',
        '-t', '3', '-map', '0:v', '-map', '1:a', '-map', '2:a',
        '-c:v', 'mpeg4', '-c:a', 'aac', '-b:a', '192k',
        '-metadata:s:v:0', 'BPS=7580000',
        '-metadata:s:v:0', 'title=Video Title',
        '-metadata:s:a:0', 'BPS=192000',
        '-metadata:s:a:0', 'language=eng',
        '-metadata:s:a:1', 'BPS=191000',
        '-metadata:s:a:1', 'language=jpn', str(source))
    assert inspect(source)[0]['tags']['BPS'] == '7580000'
    run(ffmpeg, '-hide_banner', '-v', 'error', '-y', '-i', str(source),
        '-map', '0:V?', '-c:v', 'libx264', '-b:v', '220k',
        '-map', '0:a?', '-c:a', 'libopus', '-b:a', '64k',
        '-map_metadata', '0', *scrub(True, True), str(converted))
    streams = inspect(converted)
    assert [s['codec_name'] for s in streams] == ['h264', 'opus', 'opus']
    assert all('BPS' not in s.get('tags', {}) for s in streams)
    assert streams[0]['tags']['title'] == 'Video Title'
    assert [s['tags']['language'] for s in streams[1:]] == ['eng', 'jpn']
    run(ffmpeg, '-hide_banner', '-v', 'error', '-y', '-i', str(source),
        '-map', '0:V?', '-c:v', 'copy', '-map', '0:a?', '-c:a', 'libopus', '-b:a', '64k',
        '-map_metadata', '0', *scrub(False, True), str(audio_only))
    s = inspect(audio_only)
    assert s[0]['codec_name'] == 'mpeg4' and s[0]['tags']['BPS'] == '7580000'
    assert all('BPS' not in stream.get('tags', {}) for stream in s[1:])
    print('PASS: full and audio-only MKV transcoding scrub stale statistics but retain track metadata')
