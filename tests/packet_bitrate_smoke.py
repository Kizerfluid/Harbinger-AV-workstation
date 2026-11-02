#!/usr/bin/env python3
"""FFprobe compact packet bitrate smoke test for Harbinger File Information.

Creates one MKV containing one video stream and two audio streams, reads
compressed packet sizes/timestamps, and verifies believable nonzero bitrates.
No source media needed. Requires ffmpeg + ffprobe on PATH.
"""
import math
import subprocess
import tempfile
from collections import defaultdict
from pathlib import Path


def run(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.STDOUT)


def parse_packet_stats(text):
    stats = defaultdict(lambda: {"bytes": 0, "first": math.inf, "last": -math.inf})
    for line in text.splitlines():
        fields = dict(p.split("=", 1) for p in line.split("|") if "=" in p)
        try:
            ix, size = int(fields["stream_index"]), int(fields["size"])
            if size <= 0:
                continue
        except (KeyError, ValueError):
            continue
        s = stats[ix]
        s["bytes"] += size
        try:
            pts = float(fields.get("pts_time", "nan"))
            if not math.isfinite(pts):
                pts = float(fields.get("dts_time", "nan"))
            dur = float(fields.get("duration_time", "0"))
            if math.isfinite(pts):
                s["first"] = min(s["first"], pts)
                s["last"] = max(s["last"], pts + (dur if math.isfinite(dur) and 0 <= dur < 3600 else 0))
        except ValueError:
            pass
    return stats


def main():
    with tempfile.TemporaryDirectory(prefix="harbinger_bitrates_") as temp:
        outfile = Path(temp) / "test.mkv"
        run("ffmpeg", "-nostdin", "-v", "error", "-y",
            "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=24:duration=5",
            "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=5",
            "-f", "lavfi", "-i", "sine=frequency=660:sample_rate=48000:duration=5",
            "-map", "0:v:0", "-map", "1:a:0", "-map", "2:a:0",
            "-c:v", "mpeg4", "-q:v", "5", "-c:a", "libopus", "-b:a", "96k", str(outfile))
        records = run("ffprobe", "-v", "error", "-show_packets", "-show_entries",
                      "packet=stream_index,size,pts_time,dts_time,duration_time",
                      "-of", "compact=p=0:nk=0", str(outfile))
        stats = parse_packet_stats(records)
        assert set(stats) == {0, 1, 2}, f"Unexpected streams {stats.keys()}"
        for ix, stat in stats.items():
            span = stat["last"] - stat["first"]
            assert 4.8 < span < 5.3, (ix, span)
            kbps = stat["bytes"] * 8 / span / 1000
            assert 16 < kbps < 4000, (ix, kbps)
            print(f"stream #{ix}: {kbps:.1f} kbps average, {stat['bytes']} packet bytes, {span:.3f}s")
        print("Packet bitrate scan smoke test PASSED")


if __name__ == "__main__":
    main()
