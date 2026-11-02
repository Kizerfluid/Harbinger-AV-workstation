#!/usr/bin/env python3
"""FFmpeg/FFprobe smoke check for the fields displayed by File Information.
Does not substitute for compiling the Qt VideoInfoTests target.
"""
import json
import subprocess
import tempfile
from pathlib import Path


def run(*args):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=60)


def probe(p):
    return json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-show_streams", "-of", "json", str(p)], timeout=20))["streams"]

with tempfile.TemporaryDirectory(prefix="harbinger_file_info_") as tmp:
    root = Path(tmp)
    eight = root / "eight.mkv"
    ten = root / "ten.mkv"
    mixed = root / "mixed.mkv"
    run("ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30", "-t", "0.8", "-c:v", "libx264", "-threads", "2", "-pix_fmt", "yuv420p", "-y", str(eight))
    run("ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=30", "-t", "0.8", "-c:v", "ffv1", "-threads", "2", "-pix_fmt", "yuv420p10le", "-y", str(ten))
    run("ffmpeg", "-hide_banner", "-loglevel", "error", "-i", str(eight), "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000", "-t", "0.8", "-map", "0:v:0", "-map", "1:a:0", "-c:v", "copy", "-c:a", "aac", "-y", str(mixed))
    v8 = next(s for s in probe(eight) if s["codec_type"] == "video")
    v10 = next(s for s in probe(ten) if s["codec_type"] == "video")
    mix = probe(mixed)
    assert v8["codec_name"] == "h264" and v8["pix_fmt"] == "yuv420p"
    assert v10["pix_fmt"] == "yuv420p10le"
    assert v8["width"] == 320 and v8["height"] == 180
    assert v8["avg_frame_rate"] == "30/1"
    assert len([s for s in mix if s["codec_type"] in ("video", "audio")]) == 2
    assert any(s["codec_type"] == "audio" for s in mix)
    print("PASS: FFprobe provides expected video stream codec, profile/pixel format, dimensions and frame rate, plus audio metadata")
