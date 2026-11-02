"""Standalone FFmpeg codec sanity tests; the GUI and command builder require Qt/MSVC."""
from pathlib import Path
import shutil
import subprocess
import tempfile
ffmpeg = shutil.which('ffmpeg')
ffprobe = shutil.which('ffprobe')
if not ffmpeg or not ffprobe:
    raise SystemExit('Requires ffmpeg and ffprobe on PATH')
with tempfile.TemporaryDirectory() as td:
    for codec, bitrate, suffix, expected in [
        ('libopus', 137, 'opus', 'opus'),
        ('ac3', 512, 'ac3', 'ac3'),
        ('libmp3lame', 160, 'mp3', 'mp3'),
    ]:
        output = Path(td) / f'target_{bitrate}.{suffix}'
        subprocess.run([ffmpeg, '-hide_banner', '-nostdin', '-loglevel', 'error',
            '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000:duration=1.5',
            '-c:a', codec, '-b:a', f'{bitrate}k', str(output)], check=True, timeout=30)
        info = subprocess.run([ffprobe, '-v', 'error', '-select_streams', 'a:0',
            '-show_entries', 'stream=codec_name,bit_rate', '-of', 'default=noprint_wrappers=1',
            str(output)], capture_output=True, check=True, text=True).stdout
        assert f'codec_name={expected}' in info, (codec, info)
        if codec != 'libopus':
            assert f'bit_rate={bitrate*1000}' in info, (codec, info)
        print(f'PASS: {codec} with {bitrate}k target')
print('Note: Opus is VBR, so actual measured output bitrate need not equal its target.')
