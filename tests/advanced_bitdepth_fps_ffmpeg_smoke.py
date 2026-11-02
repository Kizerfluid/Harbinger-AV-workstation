#!/usr/bin/env python3
"""Validate FFmpeg's Advanced-mode 8-bit / 10-bit HEVC and exact fractional FPS.

Independent of the Qt GUI; requires FFmpeg/FFprobe and a libx265 build with
10-bit support. Harbinger argument generation is tested by CodecTests.cpp.
"""
import json
import shutil
import subprocess
import tempfile
from pathlib import Path


def run(cmd):
    return subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def smoke():
    for name in ("ffmpeg", "ffprobe"):
        if shutil.which(name) is None:
            raise RuntimeError(f"Missing {name} on PATH")
    with tempfile.TemporaryDirectory(prefix="harbinger-advanced-") as d:
        for depth, pix, profile in ((8, "yuv420p", "Main"), (10, "yuv420p10le", "Main 10")):
            out = Path(d) / f"hevc-{depth}bit.mkv"
            run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
                 "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=30",
                 "-t", "1", "-an", "-c:v", "libx265", "-threads", "1",
                 "-preset", "ultrafast", "-x265-params", "pools=none:frame-threads=1",
                 "-pix_fmt", pix, "-r", "24000/1001", str(out)])
            info = json.loads(run(["ffprobe", "-v", "error", "-select_streams", "v:0",
                                    "-show_entries", "stream=pix_fmt,profile,avg_frame_rate",
                                    "-of", "json", str(out)]).stdout)["streams"][0]
            assert info["pix_fmt"] == pix, info
            assert info["profile"] == profile, info
            assert info["avg_frame_rate"] == "24000/1001", info
            print(f"PASS: {depth}-bit {pix}, {profile}, 24000/1001 fps")


if __name__ == "__main__":
    smoke()
