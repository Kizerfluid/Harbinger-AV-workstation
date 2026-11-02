# Validation and pre-release checklist

This page separates **real-world observations** from **historical source-package smoke tests** and tasks that still need checking. It does not claim that tests from old packages were rerun for v0.6.3.

## Confirmed through user testing

- **v0.6.1 Smart MKV finalisation:** a problematic full MKV showed opening corruption in VLC; a complete FFmpeg stream-copy remux fixed playback without re-encoding. A later Smart Encoding output with automatic finalisation played cleanly.
- **v0.6.2 cross-platform Hive:** Windows RTX 3090 + openSUSE GTX 1650 completed a 12-episode Smart Encoding batch (six episodes per computer) after correcting the Linux `home` firewalld zone rule and the server's NVIDIA/FFmpeg encoder compatibility.
- **v0.4.9 dub-aware synchronisation:** tested successfully on a 13-episode multi-source dubbing workflow; the older synthetic correlation checks below are separate evidence.

## v0.6.3 checks before publishing

- [ ] Build the full Qt6 application on Windows (MSVC) and openSUSE (GCC).
- [ ] Repeatedly change default tracks and invalid audio delays in the muxer; confirm no silent crash (possible Qt item lifecycle bug fix).
- [ ] Advanced mode: check HEVC Main/8-bit and Main 10/10-bit output using FFprobe; test CPU and NVENC where available.
- [ ] Verify `24000/1001` and other fractional FPS selections, as well as Keep original.
- [ ] Confirm Smart MKV automatic finalisation, multiple audio tracks, subtitles, attachments and chapters remain present.
- [ ] Rebuild **both** Hive nodes for protocol **v5** and run a short whole-file job in each direction used by the project.
- [ ] Run the project C++ tests where available and relevant FFmpeg smoke tests, recording compiler and tool versions.

## Historical v0.6.0–v0.6.2 validation records (as originally supplied)

The report below was created with earlier source packages; notes such as “not yet tested between physical machines” describe the state **at the time of writing**, not the later successful v0.6.2 user test.

## v0.6.2 Hive Smart / openSUSE validation status

- Source reviewed for Linux conditionals and QCoreApplication headless CLI routing.
- PASS: `python tests/hive_smart_worker_ffmpeg_smoke.py` — synthetic 10-bit HEVC, English/Japanese Opus tracks, SRT, stream-copy finalisation and identical decoded frame hashes.
- PASS: `python tests/smart_mkv_finalization_smoke.py` — two audio tracks, subtitle, font attachment, chapter, preserved decoded frames.
- PASS: `python tests/smart_encoding_ffmpeg_smoke.py`, `python tests/bitrate_metadata_smoke.py`, and `python tests/packet_bitrate_smoke.py`.
- PASS: `bash -n scripts/build_opensuse.sh` — shell syntax. Actual Qt C++ build not possible here.
- Hive protocol version raised to 4; regression test extended for Smart video bitrate/bit-depth round-trip and invalid bitrate rejection.
- Worker Smart job path adds second FFmpeg stream-copy MKV finalisation before publishing results; remote Smart metadata includes video bit depth and audio channel counts.
- **Not yet compiled** with Qt/MSVC or Qt/openSUSE: the available build container does not have Qt6 development libraries and its package repositories cannot be reached. Execute `bash scripts/build_opensuse.sh` for actual compiler/test results on openSUSE and rebuild the Windows patch in VS2022.
- **Not yet end-to-end tested** with a Windows host and openSUSE worker. Start with two small files, inspect stream/size/finalisation details, then scale to a batch.

# Harbinger v0.6.0 Smart Encoding — validation

## Checks run here

- PASS: `python tests/smart_encoding_ffmpeg_smoke.py` — target-bitrate H.264 video, two Opus audio tracks, SRT subtitle, an attached font, retained audio language metadata.
- PASS: `python tests/transcode_multitrack_smoke.py` — existing multitrack MKV preservation, 1 video + 2 Opus tracks + 2 subtitles + font + chapter.
- PASS: `python tests/fine_audio_bitrate_smoke.py` — 137k Opus, 512k AC-3 and 160k MP3 encoder arguments.
- PASS: software 10-bit HEVC smoke test via FFmpeg (`yuv420p10le` output detected by FFprobe).
- SOURCE REVIEW: Advanced mode remains on the same source-side configuration fields and normal Codec::buildArgs path; target-bitrate options are only enabled for Smart size-target jobs.

## Not yet verified

- C++ Qt tests, including `SmartEncodingTests`, could not be built/run; Qt development libraries were not available in this environment.
- Windows Qt GUI compilation and runtime, Smart/Advanced mode visibility, QSettings restore and control interactions require Visual Studio testing.
- GPU AV1/HEVC real-world rate-control on specific machines, target-size deviation at long durations, and colour/HDR fidelity need real-source validation.
- Smart output is single-pass target bitrate and **not exact-size guaranteed**; no two-pass or VMAF analysis in this release.
- At v0.6.0 release time Smart was local-only; v0.6.2 now adds experimental Hive whole-file Smart batch support (not end-to-end validated).

## Suggested Windows regression

1. Import a source MKV with two audio languages, ASS subtitles, embedded font and chapter. Smart Encoding > 350 MB > Balanced > H.265 + Opus > Balanced. Verify track counts, language tags and chapters after completion.
2. Repeat with Visual Quality and Audio Quality; verify planned audio/video bitrate allocations differ as intended.
3. Try an impossibly small size, unknown-duration file and passthrough video with a size target. Harbinger must reject these plans with a clear error rather than writing a truncated file.
4. Switch to Advanced Encoding; confirm the existing manually specified codecs, passthrough options and 1-kbps audio bitrate slider behave as before.
5. Use a 10-bit HEVC source; verify 10-bit output on a compatible software encode path and review HDR metadata separately.

### Smart bitrate metadata hotfix validation
- Reproduced original issue: transcode old-tagged H.264/AAC input to HEVC/Opus; output still contained original `BPS` values despite a much smaller actual output.
- Verified revised FFmpeg commands remove inherited `BPS` tags from re-encoded streams, retaining source `title` and `language` tags.
- Verified audio-only transcode retains copied video bitrate tags while clearing stale audio bitrate tags.
- Ran `tests/bitrate_metadata_smoke.py`: PASSED.
- Ran `tests/smart_encoding_ffmpeg_smoke.py`: PASSED.
- Ran `tools/measure_stream_bitrates.py` on synthetic result: displayed packet-byte averages for all three streams.
- C++ Qt/MSVC targets have not been compiled in this environment; run CodecTests, AudioInfoTests and VideoInfoTests after updating Windows.

### Bitrate measurement hotfix
- PASS: `python tests/packet_bitrate_smoke.py` generated video and two separate Opus streams, all independently measured from FFprobe packet sizes/timestamps.
- PASS: `python tests/bitrate_metadata_smoke.py` preserves stream metadata after stale-tag cleanup.
- PASS: `python tests/smart_encoding_ffmpeg_smoke.py` confirms Smart Encoding FFmpeg command smoke behaviour.
- NOT YET VERIFIED: MSVC/Qt GUI compilation and asynchronous file-selection lifecycle, since Qt dev libraries are unavailable here.


---

## Historical v0.4.9 dub-aware waveform validation (as originally supplied)

# Harbinger v0.4.9 — Validation

## Passed
- Compiled and ran `WaveMatchTests` using g++ C++17.
- Compiled and ran `DubWaveMatchTests`: synthetic alternative-language center dialogue with shared stereo effects, 1.6 s displacement. Detected **-1,600 ms** (correlation 0.989; distinctiveness 0.542).
- FFmpeg FLAC/AAC decode test on synthetic two-language stereo signals, with different dialogue and 1.6 s timing displacement. Correlation on stereo background gave **-1,600 ms** (score 0.976).
- `tests/mux_audio_sync_smoke.py` for lossless stream-copy offset and duplicate-source tracking.
- `tests/track_metadata_smoke.py` for source language, stream titles and subtitle disposition.
- `tests/ffmpeg_smoke.sh` for representative MP4, MP3, Ogg/Opus and FLAC workflows.

## Not verified here
- Qt 6 GUI / Windows MSVC build is not tested here because the Qt development kit is unavailable.
- `WaveSync.cpp` Qt/QProcess integration, cross-language real episode matching, cancel UI and the original output timing must be validated in the user's Visual Studio build.
- Different music mixes, center-only/mono background audio, edited scenes, and playback-speed drift may still prevent reliable matching. Do not auto-apply uncertain results.
