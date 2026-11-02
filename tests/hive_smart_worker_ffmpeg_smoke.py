"""Exercise the FFmpeg paths used by a remote Hive v4 Smart worker.

This is not a live Hive network test. The real Qt worker/protocol and host GUI
must be compiled and exercised separately on Windows/openSUSE.
"""
import json
import shutil
import subprocess
import tempfile
from pathlib import Path


def run(*args):
    r = subprocess.run([str(x) for x in args], capture_output=True, text=True, timeout=90)
    if r.returncode:
        raise RuntimeError(f"Failed ({r.returncode}): {args}\n{r.stderr[-2500:]}")
    return r.stdout


def probe(path):
    return json.loads(run('ffprobe', '-v', 'error', '-show_streams', '-of', 'json', path))['streams']


def video_md5(path):
    return run('ffmpeg', '-hide_banner', '-v', 'error', '-i', path,
               '-map', '0:v:0', '-f', 'framemd5', '-')


def main():
    if not shutil.which('ffmpeg') or not shutil.which('ffprobe'):
        print('SKIP: FFmpeg / FFprobe not installed')
        return
    encoders=run('ffmpeg','-hide_banner','-encoders')
    if 'libx265' not in encoders or 'libopus' not in encoders:
        print('SKIP: libx265 and libopus encoders required')
        return
    with tempfile.TemporaryDirectory(prefix='hive-smart-worker-') as d:
        root=Path(d)
        subs=root/'en.srt';subs.write_text('1\n00:00:00,200 --> 00:00:01,000\nHi!\n')
        source=root/'source.mkv'
        encoded=root/'result.part.mkv'
        final=root/'result.remux.mkv'
        run('ffmpeg','-hide_banner','-loglevel','error','-nostdin','-y',
            '-f','lavfi','-i','testsrc2=size=160x90:rate=24:duration=1.5,format=yuv420p10le',
            '-f','lavfi','-i','sine=frequency=440:sample_rate=48000:duration=1.5',
            '-f','lavfi','-i','sine=frequency=660:sample_rate=48000:duration=1.5',
            '-i',subs,'-map','0:v:0','-map','1:a:0','-map','2:a:0','-map','3:s:0',
            '-c:v','libx265','-preset','ultrafast','-x265-params','pools=none:frame-threads=1',
            '-pix_fmt','yuv420p10le','-c:a','aac','-b:a','64k','-c:s','copy',
            '-metadata:s:a:0','language=eng','-metadata:s:a:1','language=jpn',source)
        assert any(s['codec_type']=='video' and s.get('pix_fmt')=='yuv420p10le' for s in probe(source))
        # Resolved Smart options: 10-bit HEVC, target video kbps, both audio tracks.
        run('ffmpeg','-hide_banner','-loglevel','error','-nostdin','-n','-i',source,
            '-map','0:V?','-c:v','libx265','-b:v','400k','-preset','ultrafast',
            '-x265-params','pools=none:frame-threads=1','-pix_fmt','yuv420p10le',
            '-map','0:a?','-c:a','libopus','-b:a','64k',
            '-map','0:s?','-map','0:t?','-c:s','copy','-c:t','copy',
            '-map_chapters','0','-map_metadata','0',encoded)
        # Identical lossless remux args as the new HiveWorkerSession::finished phase.
        run('ffmpeg','-hide_banner','-nostdin','-n','-loglevel','error','-i',encoded,
            '-map','0','-c','copy','-map_metadata','0','-map_chapters','0',final)
        before=probe(encoded);after=probe(final)
        assert [s['codec_type'] for s in before]==[s['codec_type'] for s in after]
        assert len(after)==4
        assert [s.get('tags',{}).get('language') for s in after if s['codec_type']=='audio']==['eng','jpn']
        assert any(s.get('codec_name')=='hevc' and s.get('pix_fmt')=='yuv420p10le' for s in after)
        assert all(s.get('codec_name')=='opus' for s in after if s['codec_type']=='audio')
        assert video_md5(encoded)==video_md5(final)
        print('PASS: 10-bit HEVC + dual Opus + SRT passed lossless worker remux with frames intact')


if __name__ == '__main__':
    main()
