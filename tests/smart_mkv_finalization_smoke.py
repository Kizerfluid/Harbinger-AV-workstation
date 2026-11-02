"""Harbinger Smart MKV finalisation: FFmpeg-only, multi-track round-trip smoke test.

Run: python tests/smart_mkv_finalization_smoke.py
Requires FFmpeg + FFprobe on PATH. No user media is required.
"""
import json
import shutil
import subprocess
import tempfile
from pathlib import Path


def run(*args):
    return subprocess.run([str(x) for x in args], check=True, capture_output=True, text=True)


def probe(path):
    return json.loads(run('ffprobe', '-v', 'error', '-show_streams',
                          '-show_chapters', '-show_format', '-of', 'json', path).stdout)


def main():
    if not shutil.which('ffmpeg') or not shutil.which('ffprobe'):
        print('SKIP: FFmpeg and FFprobe are required')
        return
    with tempfile.TemporaryDirectory(prefix='harbinger-mkv-finalize-') as d:
        temp = Path(d)
        subs = temp / 'dialogue.srt'
        subs.write_text('1\n00:00:00,200 --> 00:00:01,800\nHello world\n', encoding='utf-8')
        attach = temp / 'font.txt'
        attach.write_text('Harbinger test attachment (no outside fonts required).\n', encoding='utf-8')
        chapters = temp / 'chapters.ffmeta'
        chapters.write_text(';FFMETADATA1\ntitle=Two-language test\n'
                            '[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=1000\ntitle=Opening\n', encoding='utf-8')
        encoded = temp / 'episode.harbinger-part.mkv'
        final = temp / 'episode.harbinger-remux.mkv'
        # Construct the same kinds of tracks Smart MKV preserves: video, two
        # independently named audio tracks, subtitles, attachment and chapter.
        run('ffmpeg', '-hide_banner', '-loglevel', 'error', '-nostdin', '-y',
            '-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=24:duration=2',
            '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=2',
            '-f', 'lavfi', '-i', 'sine=frequency=880:sample_rate=48000:duration=2',
            '-i', subs, '-f', 'ffmetadata', '-i', chapters,
            '-map', '0:v:0', '-map', '1:a:0', '-map', '2:a:0', '-map', '3:s:0',
            '-map_chapters', '4', '-map_metadata', '4',
            '-c:v', 'mpeg4', '-q:v', '7', '-c:a', 'aac', '-b:a', '96k', '-c:s', 'srt',
            '-metadata:s:a:0', 'language=eng', '-metadata:s:a:0', 'title=English',
            '-metadata:s:a:1', 'language=jpn', '-metadata:s:a:1', 'title=Japanese',
            '-attach', attach, '-metadata:s:t:0', 'mimetype=text/plain',
            encoded)
        # Exact substantive remux arguments used by Engine's final stage.
        run('ffmpeg', '-hide_banner', '-nostdin', '-n', '-loglevel', 'warning',
            '-stats_period', '0.25', '-progress', 'pipe:1', '-i', encoded,
            '-map', '0', '-c', 'copy', '-map_metadata', '0', '-map_chapters', '0', final)
        source, result = probe(encoded), probe(final)
        a, b = source['streams'], result['streams']
        assert [x['codec_type'] for x in a] == [x['codec_type'] for x in b], (a, b)
        assert [x.get('codec_name') for x in a] == [x.get('codec_name') for x in b]
        assert len(a) == 5, [x['codec_type'] for x in a]
        assert len(source.get('chapters', [])) == len(result.get('chapters', [])) == 1
        for before, after in zip(a, b):
            if before['codec_type'] == 'audio':
                assert before['tags']['language'] == after['tags']['language']
                assert before['tags']['title'] == after['tags']['title']
            if before['codec_type'] == 'attachment':
                assert before['tags']['filename'] == after['tags']['filename']
                assert before['tags']['mimetype'] == after['tags']['mimetype']
        assert source['format']['tags']['title'] == result['format']['tags']['title']
        # Frame-level output equality verifies that the remux did not modify
        # the actual picture after decoding.
        def frames(path):
            return run('ffmpeg', '-v', 'error', '-i', path,
                       '-map', '0:v:0', '-f', 'framemd5', '-').stdout
        assert frames(encoded) == frames(final), 'video frame mismatch'
        print('PASS: video, both language tracks, subtitle, attachment, chapter, metadata and decoded frames preserved')


if __name__ == '__main__':
    main()
