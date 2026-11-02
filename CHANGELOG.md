# Changelog

Notable changes to Harbinger. Older release limitations are historical; the current behaviour is described in the [README](README.md) and [docs](docs/).

## v0.6.3 — Advanced video controls and MKV muxer stability

- Added Advanced Encoding output **Automatic / 8-bit / 10-bit** selection. 10-bit is offered for HEVC, AV1 and VP9 when the chosen encoder supports it; H.264/MPEG-4 remain 8-bit.
- Expanded frame-rate selection to original, **24000/1001**, 24, 25, **30000/1001**, 30, 50, **60000/1001**, 60, 120, and custom 1–240 fps.
- Added compatible software/hardware pixel-format handling for 10-bit output: software `yuv420p10le`, hardware P010 as appropriate for NVENC/QSV/AMF/VAAPI.
- Addressed a suspected intermittent MKV muxer GUI crash: editing default-track checkboxes and invalid audio delays now updates in place instead of destroying the edited Qt tree item within its event callback. **User-system regression still required.**
- **Hive protocol v5** carries Advanced frame-rate and bit-depth options; update every Windows/Linux node together.
- Retained Smart Encoding's distinct bit-depth planning, Hive Smart batches, final MKV remux and on-import bitrate measurements.

## v0.6.2 — Smart Encoding over Hive; openSUSE GUI build

- Added whole-file **Smart Encoding** distribution with source-specific video/audio planning and remote 10-bit intent.
- Hive protocol **v4** carries Smart bitrate budgets and final-remux options; worker capability filtering avoids unsupported codecs.
- Added openSUSE build script and desktop GUI setup. Linux remains a complete Qt GUI; optional allowlisted headless worker is available but not required.
- Remote Smart MKVs receive an FFmpeg-only lossless stream-copy remux before being returned to the host.
- **Subsequent real-world test:** a Windows RTX 3090 host and openSUSE GTX 1650 worker completed a 12-episode Smart Encoding batch, six episodes per machine. This validates the v0.6.2 Hive workflow on those two systems, not every network or codec combination.

## v0.6.1 — Smart MKV finalisation

- Added a lossless **FFmpeg-only final remux** before publishing Smart Encoding MKVs to address start-of-video corruption observed in a full multi-track MKV while copied video packets decoded cleanly.
- Added finalisation progress and safe handling of an encoded intermediate if remuxing fails.
- Real-world user test: final-remuxed anime episode played without its previous opening artifacts. The specific underlying original-muxer fault was not conclusively isolated.

## v0.6.0 — Smart Encoding and measured bitrates

- Added separate **Smart** and **Advanced** modes. Smart plans per-file encode settings from duration, resolution, audio tracks and optional approximate decimal-MB file-size budgets.
- Added Visual Quality / Balanced / Audio Quality priorities, Fast / Balanced / Slow settings, codec choices, source-aware downscale and supported 10-bit encoding.
- Retains compatible MKV audio, subtitles, font attachments, chapter and language metadata; rejects unsatisfiable size targets.
- Cleans stale inherited per-stream Matroska bitrate/statistics tags on transcoded streams without removing unrelated track metadata.
- Added asynchronous **compressed-packet bitrate measurement** in File Information when per-stream MKV bitrate data is absent or stale, labelled `(avg)`. Does not require MKVToolNix or modify files.
- Historical note: the first v0.6.0 Smart queue was local only; Hive support arrived in v0.6.2.

## v0.5.2 — Fine-grained audio bitrate controls

- Replaced the restricted audio bitrate selector with an adjustable slider and editable kbps value. Settings are codec-dependent and saved independently.

## v0.5.1 — Multitrack transcoding preservation

- Fixed the ordinary transcoder mapping only the first video/audio and dropping extra streams.
- MKV jobs now preserve compatible regular video/audio streams, subtitles, attachments/fonts, chapters and stream language/title/disposition metadata unless removed by user settings.
- Audio target bitrate applies **per encoded audio track**; copied video stays bit-for-bit copied.

## v0.5.0 — Batch MKV muxing

- Added season-wide mux templates: choose video/audio/subtitle source assignments once and reuse across corresponding episodes.
- Supports episode-ID pairing or explicitly selected sorted-order pairing, per-episode waveform synchronisation preview, and skipping uncertain matches.

## v0.4.9 — Dub-aware waveform synchronisation

- Waveform analysis compares stereo background **left-minus-right** plus full-mix **left-plus-right** signatures, reducing centre-dialogue interference between language dubs.
- For longer files, analyses three positions and requires at least two to agree before flagging an offset as reliable.
- Uncertain cases report the best observed correlation; differing edits, playback speeds or mixes may still need manual work.
- Analyses only temporary decoded samples; the final MKV copies compatible compressed audio without re-encoding.
- Includes the v0.4.8 fix preventing a closing Qt progress dialog from triggering cancellation.

## v0.4.8 — Automatic waveform audio alignment

- Added waveform-guided audio offset suggestions with confidence checks, manual approval, and non-destructive MKV stream copying.

## v0.4.7 — Per-track audio timing

- Added manual audio delay (ms), optional align-endings and clear-delay controls using FFmpeg input timestamp offsets.
- Duration differences do not prove sync; audio timing still needs listening verification.

## v0.4.6 — Languages and subtitle inspection

- File Information shows audio/subtitle languages, stream names, codecs and default/forced flags.

## v0.4.5 — Combined File Information

- Consolidated source video/audio details into File Information and simplified the encoding queue.

## v0.4.4 — Audio metadata

- Expanded per-audio-track codec, profile, bitrate, bit depth and channel-layout inspection without guessing missing values.

## v0.4.3 — Surround audio and Hive integration

- Added AC-3, E-AC-3, DTS Core, PCM and broader channel/bitrate controls; passthrough remains possible for compatible lossless and object-audio streams.
- Expanded early Hive audio option transport (historically protocol v3).

## v0.4.2 — Unified copy/transcode/remove controls

- Integrated **Passthrough** and **Remove** into video/audio codec dropdowns in place of redundant processing-mode settings.

## v0.4.0–v0.4.1 — Early Hive and stream operation controls

- Introduced opt-in TLS LAN workers, discovery, distributed whole-file queues, experimental single-video splitting and initial stream copy/remove options.

## v0.3 — Hardware acceleration

- Added NVIDIA/AMD/Intel encoding and decoding detection and optional CPU/GPU controls with FFmpeg capability probes.

## v0.2–v0.2.1 — Track muxing and naming

- Introduced multi-source MKV stream muxing, per-track selection and stream-copy output. Renamed the project **Harbinger**.
