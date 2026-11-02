#!/usr/bin/env python3
"""FFmpeg regression tests for Harbinger surround audio, DTS/Dolby and WAV.
Run: python tests/surround_smoke.py   (requires ffmpeg and ffprobe on PATH)
These test the FFmpeg command paths, not the Qt GUI implementation.
"""
import json
import shutil
import subprocess
import tempfile
from pathlib import Path


def run(*args):
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(f"Command failed: {args}\n{result.stderr[-1500:]}")
    return result.stdout.strip()


def stream(path):
    obj = json.loads(run("ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=codec_name,channels,channel_layout", "-of", "json", str(path)))
    return obj["streams"][0]


def stream_hash(path):
    return run("ffmpeg", "-v", "error", "-nostdin", "-i", str(path), "-map", "0:a:0", "-c:a", "copy", "-f", "streamhash", "-hash", "SHA256", "-")


def main():
    if not shutil.which("ffmpeg") or not shutil.which("ffprobe"):
        raise SystemExit("Install FFmpeg and FFprobe first")
    with tempfile.TemporaryDirectory(prefix="harbinger_surround_") as d:
        t = Path(d)
        src = t / "src_7.1.mkv"
        run("ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "anullsrc=channel_layout=7.1:sample_rate=48000", "-t", "0.8", "-c:a", "flac", str(src))
        assert stream(src)["channels"] == 8
        outputs = [
            ("dolby_5.1.ac3", "ac3", 6, ["-c:a", "ac3", "-ac", "6", "-b:a", "640k"]),
            ("dolby_plus_5.1.eac3", "eac3", 6, ["-c:a", "eac3", "-ac", "6", "-b:a", "768k"]),
            ("dts_5.1.dts", "dts", 6, ["-c:a", "dca", "-strict", "-2", "-ac", "6", "-b:a", "1536k"]),
            ("wav_7.1.wav", "pcm_s24le", 8, ["-c:a", "pcm_s24le", "-ac", "8"]),
            ("wav_float_7.1.wav", "pcm_f32le", 8, ["-c:a", "pcm_f32le", "-ac", "8"]),
            ("flac_7.1.mkv", "flac", 8, ["-c:a", "flac", "-ac", "8"]),
        ]
        for name, codec, count, opts in outputs:
            path = t / name
            run("ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(src), "-map", "0:a:0", *opts, str(path))
            info = stream(path)
            assert info["channels"] == count, (name, info)
            assert info["codec_name"] == codec, (name, info)
            print("PASS", name, f"{codec} {count} channels")
        dest = t / "passthrough.mkv"
        run("ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(src), "-map", "0:a:0", "-c:a", "copy", str(dest))
        assert stream(dest)["channels"] == 8
        assert stream_hash(src) == stream_hash(dest), "Stream-copy altered compressed audio"
        print("PASS multichannel passthrough SHA-256 stream hash")
    print("All surround audio FFmpeg smoke tests passed")

if __name__ == "__main__":
    main()
