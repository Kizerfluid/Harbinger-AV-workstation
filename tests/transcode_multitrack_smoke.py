"""FFmpeg integration smoke test for Harbinger's multi-track MKV transcoding.

This checks the FFmpeg syntax used by Codec::buildArgs. The Qt command builder
itself is separately covered by CodecTests.cpp and must be built with Qt.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

FFMPEG = shutil.which('ffmpeg')
FFPROBE = shutil.which('ffprobe')
if not FFMPEG or not FFPROBE:
    raise SystemExit('FFmpeg and FFprobe are required')

def run(*args):
    result = subprocess.run(args, capture_output=True, text=True, timeout=90)
    if result.returncode:
        raise RuntimeError(f'FAILED: {args}\n{result.stderr[-2800:]}')
    return result

def probe(path):
    return json.loads(run(FFPROBE, '-v', 'error', '-show_streams', '-show_chapters', '-of', 'json', str(path)).stdout)

with tempfile.TemporaryDirectory(prefix='harbinger-transcode-multitrack-') as tmp:
    root = Path(tmp)
    source = root / 'source.mkv'
    output = root / 'output.mkv'
    subs1 = root / 'en.srt'
    subs2 = root / 'ja.srt'
    subs1.write_text('1\n00:00:00,100 --> 00:00:01,100\nEnglish\n', encoding='utf-8')
    subs2.write_text('1\n00:00:00,100 --> 00:00:01,100\nJapanese\n', encoding='utf-8')
    metadata = root / 'chapters.txt'
    metadata.write_text(';FFMETADATA1\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=1600\ntitle=Opening\n', encoding='utf-8')
    # The file is an attachment used only to check that MKV attachments survive.
    font = root / 'sub-font.ttf'
    candidates = list(Path('/usr/share/fonts').rglob('*.ttf')) if Path('/usr/share/fonts').exists() else []
    if candidates:
        shutil.copyfile(candidates[0], font)
    else:
        font.write_bytes(b'placeholder font payload, no actual font parsing required')
    run(FFMPEG, '-hide_banner', '-loglevel', 'error', '-n',
        '-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=20',
        '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
        '-f', 'lavfi', '-i', 'sine=frequency=880:sample_rate=48000',
        '-i', str(subs1), '-i', str(subs2), '-f', 'ffmetadata', '-i', str(metadata),
        '-t', '1.6', '-map', '0:v:0', '-map', '1:a:0', '-map', '2:a:0', '-map', '3:s:0', '-map', '4:s:0',
        '-c:v', 'mpeg4', '-q:v', '4', '-c:a', 'aac', '-b:a', '192k', '-c:s', 'srt',
        '-metadata:s:a:0', 'language=eng', '-metadata:s:a:0', 'title=English',
        '-metadata:s:a:1', 'language=jpn', '-metadata:s:a:1', 'title=Japanese',
        '-metadata:s:s:0', 'language=eng', '-metadata:s:s:1', 'language=jpn',
        '-map_chapters', '5', '-attach', str(font), '-metadata:s:t:0', 'mimetype=application/x-truetype-font',
        '-shortest', str(source))
    before = probe(source)
    # Equivalent to the new MKV branch of Codec::buildArgs: all A/V streams,
    # subtitles, attachments, chapters. Audio target applies to both streams.
    run(FFMPEG, '-hide_banner', '-loglevel', 'error', '-n', '-i', str(source),
        '-map', '0:V?', '-c:v', 'copy',
        '-map', '0:a?', '-c:a', 'libopus', '-b:a', '128k',
        '-map', '0:s?', '-map', '0:t?', '-c:s', 'copy', '-c:t', 'copy',
        '-map_chapters', '0', '-map_metadata', '0', str(output))
    after = probe(output)
    def of_type(doc, kind):
        return [s for s in doc['streams'] if s['codec_type'] == kind]
    assert len(of_type(before, 'video')) == len(of_type(after, 'video')) == 1
    assert len(of_type(before, 'audio')) == len(of_type(after, 'audio')) == 2
    assert len(of_type(before, 'subtitle')) == len(of_type(after, 'subtitle')) == 2
    assert len(of_type(before, 'attachment')) == len(of_type(after, 'attachment')) == 1
    assert len(before['chapters']) == len(after['chapters']) == 1
    assert [s['codec_name'] for s in of_type(after,'audio')] == ['opus', 'opus']
    assert [s['codec_name'] for s in of_type(before,'video')] == [s['codec_name'] for s in of_type(after,'video')]
    assert [s['codec_name'] for s in of_type(before,'subtitle')] == [s['codec_name'] for s in of_type(after,'subtitle')]
    assert [s.get('tags', {}).get('language') for s in of_type(after,'audio')] == ['eng','jpn']
    assert [s.get('tags', {}).get('title') for s in of_type(after,'audio')] == ['English','Japanese']
    # Exact average Opus bitrate may deviate from the selected 128 kbps target
    # because Opus uses VBR; this test checks the explicit target command and streams.
    print('PASS: MKV kept 1 video, 2 Opus tracks, 2 subtitles, 1 font attachment, 1 chapter')
