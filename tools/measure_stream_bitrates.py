#!/usr/bin/env python3
"""Measure average bitrates of streams in a media file using FFprobe packet sizes.

This does not decode media or trust copied Matroska BPS tags. Requires ffprobe
on PATH. Approximate because it divides compressed packet payload by container
run time; stream-specific durations can differ and container overhead is omitted.

Windows usage: py tools\\measure_stream_bitrates.py "C:\\path\\file.mkv"
Linux usage: python3 tools/measure_stream_bitrates.py /path/file.mkv
"""
import argparse
import json
import subprocess
import sys
from collections import defaultdict
from pathlib import Path


def run_probe(args):
    return json.loads(subprocess.check_output(
        ["ffprobe", "-v", "error", *args, "-of", "json"], text=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("filename", help="Input media file")
    args = parser.parse_args()
    path = Path(args.filename)
    if not path.is_file():
        parser.error(f"File not found: {path}")
    try:
        metadata = run_probe(["-show_format", "-show_streams", str(path)])
        duration = float(metadata["format"].get("duration", "0"))
        if duration <= 0:
            raise ValueError("Missing/invalid container duration")
        stream_names = {
            int(s["index"]): f'{s.get("codec_type", "unknown")}: {s.get("codec_name", "unknown")}'
            for s in metadata.get("streams", [])
        }
        totals = defaultdict(int)
        cmd = ["ffprobe", "-v", "error", "-show_packets",
               "-show_entries", "packet=stream_index,size", "-of", "csv=p=0", str(path)]
        with subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              universal_newlines=True, bufsize=1) as proc:
            for line in proc.stdout:
                parts = line.strip().split(",")
                if len(parts) < 2:
                    continue
                try:
                    index, size = int(parts[0]), int(parts[1])
                    totals[index] += size
                except ValueError:
                    continue
            errors = proc.stderr.read()
            if proc.wait() != 0:
                raise RuntimeError(errors.strip() or "FFprobe packet scan failed")
        print(f"{path.name} — {duration:.3f} seconds")
        print("Payload-average rate over entire file (kbps; excludes container overhead):")
        for index, amount in sorted(totals.items()):
            print(f"  stream #{index}: {stream_names.get(index, 'unknown'):<25} {8 * amount / duration / 1000:,.1f} kbps")
        print("These are measured averages, not FFmpeg target-rate settings; variable bitrate is normal.")
    except (subprocess.CalledProcessError, FileNotFoundError, ValueError, RuntimeError) as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
