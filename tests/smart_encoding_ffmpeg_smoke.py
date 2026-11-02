"""Integration check of the bitrate-controlled FFmpeg mapping used by Smart Encoding.
The Qt planner is separately covered by SmartEncodingTests.cpp. A successful
FFmpeg command does not certify target-size accuracy on all encoders.
"""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ffmpeg = shutil.which('ffmpeg')
ffprobe = shutil.which('ffprobe')
if not ffmpeg or not ffprobe:
    raise SystemExit('FFmpeg/FFprobe required')

def run(*args):
    r = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=100)
    if r.returncode:
        raise RuntimeError(f'FFmpeg failed: {args}\n{r.stderr[-2500:]}')
    return r

def inspect(path):
    return json.loads(run(ffprobe, '-v', 'error', '-show_streams', '-show_chapters',
                          '-of', 'json', str(path)).stdout)

with tempfile.TemporaryDirectory(prefix='harbinger-smart-') as folder:
    root = Path(folder)
    subs = root / 'english.srt'
    subs.write_text('1\n00:00:00,100 --> 00:00:01,000\nHello\n', encoding='utf-8')
    attach = root / 'font.ttf'
    attach.write_bytes(b'temporary test attachment, not an actual font')
    source = root / 'source.mkv'
    output = root / 'smart_output.mkv'
    run(ffmpeg, '-hide_banner', '-loglevel', 'error', '-nostdin',
        '-f', 'lavfi', '-i', 'testsrc2=size=640x360:rate=24',
        '-f', 'lavfi', '-i', 'sine=frequency=380:sample_rate=48000',
        '-f', 'lavfi', '-i', 'sine=frequency=580:sample_rate=48000',
        '-i', str(subs), '-t', '3', '-map', '0:v:0', '-map', '1:a:0',
        '-map', '2:a:0', '-map', '3:s:0', '-c:v', 'mpeg4', '-q:v', '4',
        '-c:a', 'aac', '-b:a', '128k', '-c:s', 'srt',
        '-metadata:s:a:0', 'language=eng', '-metadata:s:a:1', 'language=jpn',
        '-attach', str(attach), '-metadata:s:t:0', 'mimetype=application/x-truetype-font',
        str(source))
    run(ffmpeg, '-hide_banner', '-loglevel', 'error', '-nostdin', '-n',
        '-i', str(source), '-map', '0:V?', '-c:v', 'libx264',
        '-b:v', '420k', '-preset', 'veryfast', '-pix_fmt', 'yuv420p',
        '-map', '0:a?', '-c:a', 'libopus', '-b:a', '96k',
        '-map', '0:s?', '-map', '0:t?', '-c:s', 'copy', '-c:t', 'copy',
        '-map_chapters', '0', '-map_metadata', '0', str(output))
    media = inspect(output)
    count = lambda kind: len([s for s in media['streams'] if s['codec_type'] == kind])
    assert count('video') == 1 and count('audio') == 2, media['streams']
    assert count('subtitle') == 1 and count('attachment') == 1, media['streams']
    assert all(s['codec_name'] == 'opus' for s in media['streams'] if s['codec_type'] == 'audio')
    assert [s.get('tags', {}).get('language') for s in media['streams'] if s['codec_type'] == 'audio'] == ['eng', 'jpn']
    assert output.stat().st_size > 0
    print(f'PASS: smart ABR video + 2 Opus tracks + subtitle + font; output {output.stat().st_size / 1e6:.2f} MB')
