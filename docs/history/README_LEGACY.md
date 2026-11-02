# Historical README archive

> This is the original extended README supplied with the v0.6.3 project. It contains older release notes, obsolete patch instructions, and relative links that may no longer resolve after the documentation cleanup. For current guidance see [the root README](../../README.md).

---

> **Current v0.6.3 (Hive Smart / openSUSE):** See [`OPENSUSE_HIVE.md`](OPENSUSE_HIVE.md) for the native openSUSE **desktop GUI** build, graphical Hive worker setup and Windows Smart batch instructions (optional headless operation is a separate mode). Hive protocol is **v5** on both machines. Earlier version notes below are historical, not current limitations.

> Latest: v0.5.2 adds a slider and editable kbps field for audio bitrate targets. See AUDIO_BITRATE_UPGRADE.txt.

> Latest: v0.5.1 fixes silent loss of additional MKV tracks during main-window transcoding. See TRANSCODING_FIX.md.

# Harbinger v0.4.7 — Optional MKV Audio Delay / End Alignment

**New in the MKV Track Muxer:** Each selected audio stream has its own **Delay (ms)** column. Double-click and enter `1500` to start that track **1.5 seconds later**; `-250` shifts it earlier relative to the other tracks. `0` preserves original timing. Video and subtitle tracks are unchanged. **Audio remains in stream-copy mode**, including DTS-HD / TrueHD / Atmos / DTS:X content that the selected output supports; this is a timestamp shift, not silence insertion or audio processing.

**Align audio endings** is an explicit, opt-in convenience button: it uses the selected video as a duration/timeline reference, or the longest selected audio stream if no video was selected. For each audio track ending earlier than the reference, Harbinger proposes a delay equal to the estimated difference in end timestamps. Existing source `start_time` is taken into account. The operation displays a confirmation warning before replacing manually entered audio delays. **Clear delays** resets all selected audio delays to zero. Subtitle timestamps aren't changed.

**Important:** Difference in runtime alone does **not** reveal true audio-to-video sync. Different intros, outros, edits, silence, frame rates, and source-specific cuts may all cause unequal durations. Aligning the endings can be wrong even when the durations differ. Use a known dialogue/event as a reference and fine-tune individual delays manually; this feature does **not** perform waveform matching or drift correction. A constant delay cannot correct audio whose speed differs from the video. When FFprobe lacks stream-specific duration, Harbinger warns that the calculation depends on a container-level estimate.

**How it works:** FFmpeg `-itsoffset` is applied to the selected audio input *before* `-i`. A separate input instance is used for tracks that need different offsets, even when those tracks originate from the same MKV. `-c copy` is retained for compatible streams, avoiding quality loss. If FFmpeg needs to convert MP4 `mov_text` subtitles to SRT, that pre-existing exception remains in place.

### Update your existing Visual Studio project

1. Back up your Harbinger source directory and close Harbinger.
2. Extract the **v0.4.7 Audio Sync Upgrade Patch** into your existing `Harbinger` project folder, replacing matching `src/` files. `CMakeLists.txt` changes only the displayed version; no new C++ module is added, so a regular Visual Studio Build should suffice.
3. Select **Release / x64** in Visual Studio 2022 and **Build → Build Solution**.
4. Open **Mux / combine MKV tracks**, import files with different audio durations, and select the tracks. Double-click the **Delay (ms)** cell to enter a delay, or use **Align audio endings**. Confirm timing by playing the resulting MKV.
5. Optional smoke test from a shell with FFmpeg on PATH: `python tests/mux_audio_sync_smoke.py`. C++ tests: `ctest --test-dir build-vcpkg -C Release --output-on-failure`.

**Validation:** Actual FFmpeg testing passed with video plus two audio tracks taken from the same source at different offsets; the delayed track's encoded packet SHA-256 matched the input. Existing FFmpeg encoding, source metadata and muxing smoke tests passed. The modified C++/Qt GUI and its new `MuxTests` assertions have **not** been compiled in this environment; please rebuild in Visual Studio.

---

# Harbinger v0.4.6 — Audio languages and subtitles in File Information

**New:** The File Information table now lists video, **all audio tracks** and **all subtitle tracks**, including language and track names from the source metadata. Typical rows show *Audio #1 — English Stereo*, *Audio #2 — Japanese Commentary*, *Subtitle #3 — English — Signs and Songs*, or *Subtitle #4 — Japanese — Full subtitles*. The table also shows the subtitle format and Default/Forced flags.

The six headings are **Track | Language / name | Codec / profile | Bitrate | Bit depth | Details**. Track numbering is the original FFprobe index. No language or name is guessed if the source file does not provide it. Hover over a row to view the complete reported metadata. Subtitles display a dash for bitrate and bit depth because those fields are not meaningful to this inspector.

**Upgrade from v0.4.5:** Close Harbinger. Back up your project and replace `src/MainWindow.cpp` using the small upgrade patch. In Visual Studio 2022, select **Build → Build Solution** (Release/x64). This patch adds no new C++ source modules or dependencies and does not require CMake regeneration. The complete source ZIP includes a version bump in CMake.

**Note:** This is an information display. It does not change which subtitle tracks the ordinary encoder includes in output; for multi-source track selection use the **MKV Track Muxer**.

**Validation:** FFprobe multi-language/subtitle metadata and FFmpeg encoding/MKV mux smoke tests passed. The modified Qt GUI still needs building and visual inspection on Windows.

---

# Previous update — Harbinger v0.4.5 Combined File Information panel

**New in v0.4.5:** The encoding queue now has just **File, Duration, Status and Progress** columns. Source video and audio codec details are no longer duplicated in the queue. Click a queued item to inspect all of its **video and audio tracks** in a single **File Information** table immediately beneath the queue.

The File Information columns are **Type, Stream, Codec / profile, Bitrate, Bit depth, Resolution / channels**. Video rows include resolution and average FPS; audio rows include the channel layout/count. Hover over a row for its full available details, including pixel format on video and original track name/language on audio. Video codec names include H.264/AVC, HEVC, AV1, VP9 and others; profiles appear when FFprobe reports them.

**Important:** Video bitrate comes from a stream's own `bit_rate` or recognised stream-level Matroska bitrate tag, **never the container's overall bitrate**. Video bit depth is obtained from reported raw depth or a recognised pixel format (such as `yuv420p` or `yuv420p10le`), and is shown as **Not reported** otherwise. Audio bit depth continues to follow the conservative v0.4.4 rules. Stream numbers are FFprobe's original absolute indexes, which matters with mixed or multiple tracks.

No changes to encoding, passthrough, MKV muxing, Hive networking or stored preferences. This is a source-level UI/metadata update, not a built Windows executable. See `VALIDATION.md` for test status.

## Upgrade your existing Visual Studio 2022 project

1. Close Harbinger and Visual Studio's active debug session. Back up the existing `src` directory.
2. Extract `Harbinger_v0.4.5_File_Information_Upgrade_Patch.zip` directly into your existing `C:\Users\ash_5\Desktop\Harbinger` folder, replacing matching files.
3. Regenerate the existing Visual Studio solution from **Developer Command Prompt for VS 2022** because CMake now includes `VideoInfo.cpp` and `VideoInfoTests.cpp`:

```cmd
cd /d "C:\Users\ash_5\Desktop\Harbinger"
cmake -S . -B build-vcpkg -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="%USERPROFILE%\tools\vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
```

4. Open `build-vcpkg\Harbinger.sln` in Visual Studio. Choose **Release / x64** and **Build > Rebuild Solution**.
5. Run `Harbinger.exe`, add a video file, and click its queue row. Confirm the File Information table displays video and audio tracks.
6. Optional automated tests: `ctest --test-dir build-vcpkg -C Release --output-on-failure`.

The Qt DLL deployment from your previous build generally remains in the `build-vcpkg\Release` directory; if the executable is moved elsewhere, use `windeployqt` as before.

---

# Harbinger v0.4.4 — Source Audio Inspector

**New in 0.4.4:** Select a file in the main **Encoding Queue** to view **all of its source audio tracks** in the **Source Audio Tracks** table, with five columns: track index/language, codec and profile, per-track bitrate, source bit depth and channel layout/count. Hover over an audio entry to see its full details and original track title. The queue's Audio column also shows the first track's codec/profile and indicates when more audio streams exist.

In **MKV Track Muxer**, imported audio tracks show their codec/profile and a compact summary of channels, bitrate and bit depth in **Details**. Hover over any audio track, including in the final selected-track list, to see all properties. This is a **display-only** change: it does not change passthrough, audio encoding or track selection.

**Metadata accuracy:** Values come from FFprobe's **stream-level** data, never the file's aggregate bitrate. DTS-HD Master Audio and other profiles are identified when FFprobe exposes the profile; DTS:X/Atmos extensions are not assumed from ordinary DTS/TrueHD/E-AC-3 labels. If a value is not available (common for compressed audio bit depth or Matroska stream bitrate), Harbinger displays **Not reported**. Sample format reported by a decoder does **not** necessarily describe encoded source precision, so it is not used as a bit-depth substitute. If a channel count is known but the layout is missing, Harbinger reports the count without assuming 6 channels means 5.1.

**Building after upgrading:** The patch adds `src/AudioInfo.cpp` and a new `AudioInfoTests` CMake target. Reconfigure your Visual Studio CMake build before rebuilding. See `PATCH_INSTRUCTIONS.txt` for the exact command. Hive protocol remains **v3** and workers running v0.4.3 need no update solely for this display feature.

**Validation:** FFmpeg/FFprobe metadata smoke checks, existing surround audio and passthrough smoke tests and MKV muxing smoke tests passed. Qt 6 development libraries are not installed in the package-building environment, so the new Qt GUI and `AudioInfoTests` remain to be compiled and run in Visual Studio 2022. No compiled Windows executable is included.

---

# Harbinger v0.4.3 — Surround audio & expanded formats

## Surround audio options (v0.4.3)

In **Format & codecs**, the **Audio codec** dropdown now includes Dolby Digital (AC-3), Dolby Digital Plus (E-AC-3), DTS Core (DCA; may require an experimental FFmpeg encoder), and PCM 16-bit, 24-bit or 32-bit floating-point. Only codecs advertised by your local `ffmpeg -encoders` appear. Dolby TrueHD / DTS-HD / Atmos **encoding is not implemented**; where compatible with the destination container, their original streams can be preserved through Passthrough or the multi-source MKV Muxer.

**Channels** offers Original, Mono, Stereo, **5.1** and **7.1**. The UI disables 7.1 when the chosen encoder only supports up to 5.1 (AC-3, E-AC-3 and DTS Core). MP3 encoding is limited to stereo. Selecting **Original** preserves source channels when supported; if a 7.1 source is transcoded to an encoder with a 5.1 limit, Harbinger explicitly downmixes to 5.1. Passthrough leaves channel count and audio packets untouched. No upmixing or artificial surround reconstruction is promised.

Audio bitrates are chosen according to the codec: AC-3 supports e.g. 448/640 kbps, E-AC-3 768/1024 kbps, and DTS Core 1536 kbps. Lossless codecs do not expose a bitrate target. WAV is a container, not a codec: choose `.wav` with PCM 16-bit, PCM 24-bit or PCM float 32-bit. Harbinger also supports standalone `.ac3`, `.eac3` and `.dts` audio output and corresponding codecs inside MKV (with suitable FFmpeg support); E-AC-3 is also available in MP4/MOV. DTS-HD, TrueHD and Atmos cannot be created by the included encoders.

**Hive:** full-file distribution supports the new audio options, and the single-video join path shares the same audio encoder-argument builder as local jobs. **Hive protocol is now v3**; upgrade all Harbinger nodes together. A remote worker must actually have the requested encoder installed, or its job will fail. No new GPU audio acceleration is implied (these are software audio encoders).

**Important:** This source patch has not been compiled or GUI-tested with Qt 6 in the authoring environment; it was checked against the existing C++ source and with FFmpeg smoke tests. Use Visual Studio 2022 Release x64 to rebuild, then test the codec/channel dropdowns and local/Hive jobs on your systems.

---

## Video and audio passthrough (v0.4.2 — unified codec dropdowns)

In **Output Settings → Format & codecs**, select one choice in each dropdown:

- **Video codec:** H.264, HEVC, AV1, VP9 etc. (transcode); **Passthrough · copy original video**; or **Remove · no video**.
- **Audio codec:** AAC, MP3, Opus, FLAC etc. (transcode); **Passthrough · copy original audio**; or **Remove · no audio**.

The redundant Video/Audio processing-mode dropdowns from v0.4.1 have been removed. Selecting an actual codec automatically means **Transcode**, while selecting **Passthrough** or **Remove** determines the stream operation. The current selections appear in the output summary. FFmpeg still uses `-c:v copy` / `-c:a copy` when requested; stream-copy never invokes GPU decoding/encoding or alters video quality, audio bitrate, resolution or frame rate. Harbinger disables only options that cannot affect the chosen stream, **not the codec dropdown itself**.

Quick presets select codecs explicitly. An audio-only container requires no video and prevents deleting its only audio stream. Existing v0.4.1 preferences are migrated, retaining prior Copy/Remove choices, and obsolete action keys are cleared on save. Switching temporarily to audio-only output preserves the prior video choice for when you switch back. That v0.4.2 UI change did not alter the protocol at the time; v0.4.3 now increases it to **v3** to support surround audio options.

Examples: H.264 video passthrough + AAC audio transcode; H.265 video transcode (NVENC or CPU) + original multichannel audio passthrough; remux both audio and video without transcoding; extract the first audio stream to an audio-only container when compatible.

**Limits:** The standard encoding queue processes the **first detected video and audio stream** from each file; use **MKV Track Muxer** for selecting multiple audio/subtitle streams from different sources. Stream-copy requires a destination container compatible with the source codec (MKV is the most flexible). FFmpeg reports an error if an unsupported codec/container combination is selected; Harbinger does not silently transcode. This feature does not change chapter/subtitle handling in the standard encoding queue (it continues to omit subtitles). The dedicated MKV muxer remains the tool for carrying subtitles and other tracks.

**Hive:** Full-file jobs honour the independent copy/transcode/remove choices, including remote workers. Experimental single-video splitting requires *video transcoding* and processes audio once when joining segments, respecting the selected audio action. Hive protocol is **v3** in Harbinger v0.4.3. **Upgrade all Hive computers to the same version** before running distributed jobs.

# Harbinger 0.4.3 — Qt 6 / C++17 FFmpeg encoder + MKV muxer + GPU acceleration + Hive

Harbinger is an **early, source-distributed** Windows/Linux desktop application for batch audio/video encoding **and multi-source MKV stream muxing**. It launches external `ffmpeg` and `ffprobe` executables via Qt `QProcess` without invoking a command shell.

> **Build status:** The codebase is supplied as an initial implementation. In the environment used to prepare it, FFmpeg was available but Qt 6 development libraries were unavailable, so the C++ GUI (including Hive) could not be compiled or manually launched here. The included tests and smoke-test instructions are provided for local verification. This is **not** a verified release binary.

## NEW in v0.4: Harbinger Hive (distributed transcoding developer preview)

**[Read the complete Harbinger Hive guide](HIVE.md)** — setup, firewall ports, TLS approval/pairing, two-machine workflow, distributed GPU-aware batch encoding, experimental single-video segmentation and limitations.

- Workers opt in to advertising/accepting LAN work. UDP **36839** discovery, private-LAN TCP **36840** TLS service. Manual IP connection is available.
- A joining host verifies the worker's TLS certificate using the verification code shown on the worker, then the worker explicitly approves the connecting host.
- Process multiple files concurrently across host + workers, using each node's CPU and its verified GPU encoding hardware when permitted.
- Stream input/output with bounded chunks and SHA-256 integrity checks; cancel/retry work, inspect status and tune worker GPU permission/FFmpeg thread cap.
- **Experimental:** divide one H.264/HEVC target video into 60-second jobs encoded with matching **CPU** libx264/libx265 encoders, validate each section and remux original audio/subtitle streams into a final MKV. Not yet validated end-to-end across physical computers; not a production-safe video-splitter.
- Windows worker certificate generation currently needs the **OpenSSL CLI** executable on PATH. Qt Network and a working TLS backend are also required. Sharing is disabled by default after app restart.

## Features

- Qt 6 Widgets GUI with dark theme, drag-and-drop file addition, multi-file batch queue.
- **Dedicated MKV Track Muxer:** import multiple MKV, MP4, audio or subtitle files; inspect **all** video, audio and subtitle streams and tick exactly which ones to include.
- Combine **one video**, **any number of audio tracks**, and **any number of subtitles** into one new Matroska `.mkv`, even when each stream originates from a different file.
- Rearrange the final track order, rename tracks, edit language codes, choose default/forced flags, and optionally copy chapters from one selected source.
- Video and audio streams are **copied without re-encoding** (`-c copy`); most subtitle tracks are also copied. MP4 `mov_text` is converted to SRT if selected. This is fast and does not reduce video/audio quality.
- A separate mux progress indicator, cancellation, logging, automatic filename suggestions and overwrite-safe temporary output files.
- Asynchronous FFprobe media inspection: duration, codecs, resolution and audio presence.
- Detects FFmpeg encoders at launch. The video codec menu groups implementations by output format (H.264, H.265, AV1, VP9) instead of exposing confusing duplicate CPU/GPU entries.
- **NEW: Hardware Capabilities** page under **Tools**: one matrix per enumerated NVIDIA, AMD or Intel adapter, with separate video hardware **decode** and **encode** status per codec. Three states: **Test passed**, **Reported / unverified**, **Not available**.
- **NEW: independent acceleration preferences** for video encoding and decoding: Automatic, CPU, or GPU. Automatic favours verified GPU encoding in balanced/fast modes, and software in high-quality/slow modes when available.
- **NEW: GPU selection** for NVIDIA NVENC adapters and Linux VAAPI render nodes; explicit per-adapter selection is not reliable for Windows AMF/QSV and therefore uses FFmpeg's default adapter.
- **NEW: per-file overrides**: select exactly one item in the encoding queue and edit its decode/encode/GPU preferences; the batch uses those settings for that item.
- **NEW: CPU retry switch**: on a hardware operation failure, FFmpeg can retry CPU decoding and (if a corresponding software encoder is available) CPU encoding of the **same codec family**. Failure and retry attempts are shown in the log.
- Video: H.264/x264, HEVC/x265, AV1/SVT-AV1 or libaom, VP9, MPEG-4 Part 2; runtime-tested NVIDIA NVENC, Windows Intel QSV and AMD AMF, or Linux Intel/AMD VAAPI when usable.
- Audio: AAC, MP3/LAME, Opus, Vorbis, FLAC, ALAC, AC-3, PCM 16/24-bit.
- Output: MP4, MKV, WebM, MOV, MP3, M4A, OGG, FLAC and WAV.
- Beginner-friendly quality choices, encode-speed settings, optional resize and frame rate conversion, audio bitrate / sample rate / channels, metadata toggle.
- Sequential queue, per-file/overall progress (when duration known), error logs, cancellation.
- Collision-safe output naming: e.g. `clip_encoded.mp4`, `clip_encoded_1.mp4`. FFmpeg writes a **partial file** and promotes it only on successful encoding. Original inputs are never modified.
- Remembers GUI options and executable locations via `QSettings`. If you previously used StreamForge 0.2, Harbinger imports your existing preferences once, without modifying the old settings.

### Known scope limitations

This is a first functional source version, not a production replacement for HandBrake, Shutter Encoder or similar mature applications. In particular:

- No trim editor, preview player, custom filters, two-pass bitrate targeting, HDR/10-bit controls, batch job persistence, parallel encoding or pause/resume.
- **The batch encoder** still uses the first video and first audio track and omits subtitles. For mixing streams from multiple sources, use the new **MKV Track Muxer** instead.
- The muxer selects **whole tracks**, not portions of a timeline, and does not yet provide per-track time offset, automatic synchronisation, trimming, track extraction, or video merging/concatenation.
- Only one video stream can be chosen in this initial mux UI; audio and subtitle stream counts are not limited by the app. Output is Matroska only.
- MKV attachments (such as embedded fonts used by some ASS subtitle tracks) are **not transferred** by the current muxer. Those subtitles can be missing their intended font styling. Some rare codecs may be unsupported by FFmpeg's Matroska muxer.
- Subtitle bitstreams are copied where possible; MP4 timed-text (`mov_text`) is converted to SRT, which may change subtitle styling. Bitmap subtitles are not OCR-converted to text. Sources are not automatically time-aligned.
- A hardware backend being **listed by FFmpeg does not establish usable GPU support**. Each hardware encoder is exercised with a tiny FFmpeg test, and hardware decoding is tested with a tiny synthetic source where a software sample encoder is available.
- GPU capability probes are **best-effort**: a short 8-bit test does not prove that high bit-depth, HDR, all profiles, frame sizes or future FFmpeg driver combinations work. Some working hardware capabilities may remain *unverified* if a test cannot run.
- Individual GPU selection currently works for NVENC ordinals and Linux VAAPI render nodes. Windows QSV/AMF default-adapter selection does not guarantee control over a specific physical GPU.
- Hardware decode requests and GPU encode/filter combinations may introduce memory transfers and can occasionally be slower than CPU-only processing. Automatic CPU fallback can retry jobs, but the retries re-encode the file from the beginning.
- Hardware support is primarily for video. Audio codecs use the normal FFmpeg audio encoders.
- Setting resolution may upscale smaller sources; it targets the specified output height.
- Some codec/container combinations may still be rejected by specific FFmpeg builds or require more constrained settings. The GUI intentionally restricts options to common combinations, not every FFmpeg capability.
- The source does not bundle FFmpeg, Qt runtimes or installer files. A release build needs platform packaging and hands-on QA.
- English language UI only; no accessibility audit yet.

## Requirements

- C++17-compatible compiler: MSVC 2022+, GCC or Clang.
- CMake 3.21+.
- Qt 6.4+ development kit, **Widgets** and **Core** and **Network**.
- FFmpeg and FFprobe on PATH, next to the app executable, or manually configured under **Tools**.
- Current Windows 10/11 or a modern Linux desktop with the Qt runtime available.

### Windows — build

1. Install **Qt 6** with the MSVC 2022 64-bit kit using the Qt Online Installer.
2. Install **Visual Studio 2022** with *Desktop development with C++* and CMake.
3. Obtain `ffmpeg.exe` and `ffprobe.exe` from a trusted FFmpeg distributor, or build FFmpeg yourself. Put them on `PATH`, or beside `Harbinger.exe`.
4. For Harbinger Hive worker mode, additionally install an `openssl.exe` CLI on PATH and provide the Qt TLS backend. Open a *Developer PowerShell for VS 2022*, then:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Update the Qt path to match the actual Qt installation. The executable is typically `build/Release/Harbinger.exe`.

To prepare a portable folder, copy the release EXE to a folder and run Qt's `windeployqt` for the matching Qt build; also supply FFmpeg and FFprobe separately as permitted by their licences. Example:

```powershell
windeployqt --release "build/Release/Harbinger.exe"
```

Run `windeployqt` on your final distribution directory if preferred, rather than inside your build tree.

### Linux — Debian / Ubuntu / Mint

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev ffmpeg openssl
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
bash tests/mux_smoke.sh
./build/Harbinger
```

### Linux — Fedora

```bash
sudo dnf install gcc-c++ cmake qt6-qtbase-devel ffmpeg openssl
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/Harbinger
```

Depending on the Fedora repositories and enabled codecs, you may need an FFmpeg package with the encoder libraries you want. The application detects actual installed encoders.

### Linux — Arch

```bash
sudo pacman -S --needed base-devel cmake qt6-base ffmpeg openssl
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
./build/Harbinger
```

### Linux — openSUSE / headless Hive worker

**See `OPENSUSE_HIVE.md` for the v0.6.3 openSUSE build, headless `--hive-worker` mode, TLS pairing, firewall and Windows/Smart setup.** It includes a `scripts/build_opensuse.sh` helper.

## Using Harbinger

1. Launch the application. The top right should show `FFmpeg connected` and the detected encoder count.
2. Choose **Add files** or drag media onto the queue.
3. Select a container, a video encoder (when applicable), and an audio encoder.
4. Start with **Balanced** quality and speed. Choose output directory.
5. Click **Start encoding**. Use **Show details** to inspect FFmpeg errors.
6. Completed outputs are saved with `_encoded` in their filenames. Existing outputs and input files are protected against overwriting.

If FFmpeg is not detected, select exact executable paths from **Tools** and try again. For media files stuck in `Inspecting…`, check your FFprobe executable path and re-add the file after correcting it.

## Hardware acceleration and codec capability detection

1. Open **Tools → Hardware Capabilities**. Harbinger scans GPU adapters and performs small FFmpeg encoding and decoding probes without touching your media. Click **Rescan hardware** after changing drivers or the FFmpeg executable.
2. Choose an adapter from the dropdown. The **Hardware decoding** and **Hardware encoding** columns are independent and show *Test passed*, *Reported / unverified*, or *Not available*. Hover a cell to see the tested FFmpeg backend and any recent probe error.
3. In **Hardware acceleration** in the main window, choose separate **Encoding** and **Decoding** modes. `Automatic` is recommended. GPU mode requires a *Test passed* path for the requested codec; it does not silently guess that a build-listed encoder will function.
4. Optionally choose a GPU when your installed backend supports stable selection. NVIDIA GPUs are selected with FFmpeg's NVENC `-gpu` index (ordinals can differ from other tools on some setups); Linux VAAPI uses its `/dev/dri/renderD*` render node. Windows Intel Quick Sync and AMD AMF select the FFmpeg default adapter.
5. Leave **Retry on CPU if hardware fails** checked if robustness matters more than an immediate failure. A hardware failure is logged, and a retry uses the same output codec family; it does not replace HEVC output with H.264, for example. Disable this if you prefer a hard failure.
6. To override a single queued file, select one queue row and adjust **Per-file acceleration override**. The global defaults remain unchanged.

**Important:** Re-muxing compatible MKV streams via **MKV Track Muxer** does not decode or re-encode them and does not require hardware acceleration. The hardware settings affect the **batch encoder**, not the muxer's pure stream-copy path.

**Platform identification:** NVIDIA enumeration uses `nvidia-smi`; Linux Intel/AMD identification uses `/sys/class/drm` and `/dev/dri`; Windows Intel/AMD identification uses `Win32_VideoController` via PowerShell. A driver/device may be missing if those discovery mechanisms are absent or blocked. Linux VAAPI generally requires access to `/dev/dri/renderD*`.

## Muxing video and audio from different MKV files

1. Open **Mux / combine MKV tracks** from the main window or **Tools → MKV Track Muxer**.
2. Add several source files at once, or drag them onto **Source files**. Each file expands to show its individually detected video, audio and subtitle tracks, with stream index, codec, dimensions or channel count, and language.
3. The first imported file's first video and audio streams are ticked automatically as a convenient starting point. Untick anything you don't want, and tick tracks from other sources. Selecting a new video automatically replaces the existing one.
4. On the right, reorder tracks with **Up / Down**. Double-click a **Language** or **Track name** cell to edit it. Tick **Default** on one audio stream (and optionally one subtitle stream); **Forced** is useful for forced-subtitle tracks.
5. Optionally choose which source to copy chapters from. The default is to omit chapters rather than incorrectly copying chapters from another source.
6. Confirm the new `.mkv` destination and click **Mux selected tracks**. FFmpeg copies compatible streams into a new MKV. It never modifies the input files or overwrites an existing output file.
7. After completion, inspect the generated file's streams in your video player or with `ffprobe`.

**Example:** Import `Movie.mkv`, `EnglishAudio.mkv`, `JapaneseAudio.mkv` and `Subtitles.srt`. Tick video only from `Movie.mkv`, an audio stream from each audio file, and the subtitle track from the `.srt`. Set `eng` and `jpn` as the audio languages, mark English as default, and mux to `Movie_muxed.mkv`. The result contains 1 video, 2 audio, and 1 subtitle stream, copied without video/audio re-encoding.

**Important:** Combining video and audio from different sources does not guarantee they are synchronised; they must already have compatible timestamps/start points. If the audio is early or late, a future offset-adjustment feature is needed. The muxer's output is an MKV container, not a new video render. Codec compatibility still depends on FFmpeg and the target player.

## Design / source map

- `HIVE.md` — Harbinger Hive setup, safety constraints and worker/host guide
- `src/HiveProtocol.h` — LAN wire protocol and validation
- `src/HiveWorker.*` — TLS worker, temp staging and FFmpeg execution
- `src/HiveClient.*` — authenticated TLS host client, transfers and checksums
- `src/HiveWindow.*` — Hive control panel, discovery and scheduling
- `src/main.cpp` — main Qt application and styles
- `src/MainWindow.*` — queue, settings, user interactions, persistence
- `src/Probe.*` — asynchronous FFprobe wrapper and metadata JSON parsing
- `src/Engine.*` — sequential encoding scheduler, progress and cancellation
- `src/Codec.*` — supported containers, video codec families and FFmpeg option builder
- `src/Hardware.*` — GPU discovery, per-codec FFmpeg probes, backend selection and device options
- `src/HardwareDialog.*` — GPU codec capability matrix UI
- `tests/HardwareTests.cpp` — deterministic hardware policy and argument-construction tests
- `src/Types.h` — data structures and option types (including all probed tracks)
- `src/MuxPlan.*` — testable multi-source mapping, metadata/disposition selection and validation
- `src/MuxEngine.*` — FFmpeg mux process, progress, cancellation and safe output promotion
- `src/MuxWindow.*` — drag/drop source inspection and track selection GUI
- `tests/MuxTests.cpp` — deterministic mux plan and argument construction assertions (requires Qt Core)
- `tests/mux_smoke.sh` — creates four synthetic sources and validates multi-track MKV + timed-text conversion
- `tests/CodecTests.cpp` — deterministic codec/argument assertions (requires Qt Core)
- `tests/ffmpeg_smoke.sh` — independent native FFmpeg smoke tests (Linux/macOS shell)

## Safety / security notes

- Only local file paths are accepted through the UI. No URL download feature is implemented.
- All FFmpeg arguments are passed as **discrete QProcess arguments**, so shell characters in file paths are not evaluated by a shell.
- FFmpeg uses `-n` to refuse overwrites. Encodes use unique `.harbinger-part` names, which are renamed only after a successful exit.
- On failure or cancellation, the partial file is removed. If promotion/renaming fails, the temporary file is intentionally retained and its location shown in the log.
- Carefully consider untrusted input: FFmpeg is a complex multimedia parser. Keep it updated; do not run the application with elevated privileges.

## Licensing / redistribution

This repository contains original frontend source, not a bundled FFmpeg binary or Qt binaries. Verify licences and redistribution obligations for **Qt (typically LGPL/GPL/commercial depending on usage), FFmpeg (LGPL/GPL depending on build) and any optional codec libraries**. Patent licensing may also be relevant for commercial distribution. Bundling arbitrary FFmpeg builds without checking their enabled libraries and licences is not advised.

## Next engineering milestones

Native Qt builds, multi-computer Hive networking tests and **real GPU tests on NVIDIA/AMD/Intel** Windows and Linux hosts; QSV/AMF explicit adapter selection research; manual GUI testing; multi-source timestamp alignment and audio/subtitle offsets; optional MKV font/attachment handling; track preview; stream copy/encode per-track controls; installer/AppImage/Flatpak; broader hardware encoding; automated CI; licensing review.

## Waveform-guided MKV audio synchronization (v0.4.8)

Open **Mux / combine MKV tracks**, import the sources and select two or more audio tracks. Click **Auto-sync by waveform**, choose the reference track whose timing should stay unchanged, and approve the analysis. Harbinger extracts short, 8-kHz mono excerpts through the configured FFmpeg executable. A waveform-derived loudness envelope is compared against candidate excerpts within a bounded search radius, producing proposed positive or negative millisecond delays. Files longer than about 90 seconds are analysed at two points; inconsistent results are rejected rather than applying an unsafe single offset.

**No automatic changes**: the review dialog shows scores and delays. Only high-confidence suggestions are eligible, and the user must explicitly accept them. Other tracks and their existing manual delays are unchanged. Analyse only where two recordings contain substantially the same music/sound-effects or the same original audio. Different dub performances, sound mixes, intros, edits, silence, frame-rate conversion, and audio drift can prevent confident matching. Manual listening remains necessary. A delay alone cannot fix time-stretch or different cuts.

The analysis temporarily **decodes** short samples in memory but the final MKV still uses FFmpeg stream copy with per-audio-track input timestamp offsets; original compressed audio quality is unchanged. The existing **Align audio endings** option is separate and only compares durations.

`WaveMatchTests` tests the standalone C++ correlation engine. Hardware/Qt GUI integration must still be compiled and exercised on Windows with Visual Studio (or on Linux with Qt 6 installed).

## Dub-aware soundtrack sync (v0.4.9)

The MKV muxer's Auto-sync by waveform feature first tries the stereo difference (L−R) signal to reduce centrally mixed alternate-language dialogue, then a normal full mix. It checks up to three points and requires agreement at two points for a long episode before proposing an offset.

This is a heuristic. Files with only mono audio, different effects/music mixing, different edits or playback speed may not reliably sync. Inspect the proposed offset and verify sync at the opening, middle and ending before publishing the MKV. Selecting Passthrough continues to copy original compressed audio; analysis does not alter the output bitstream.

## Batch MKV Muxing / season templates (v0.5.0)

You can now configure a **track template once** and apply it to multiple episodes. Open **Mux / combine MKV tracks**, import an example episode from every source release and select exactly the tracks you want, e.g.:

- release A → video track
- release B → English audio track
- release C → Japanese audio and selected subtitle tracks

Then click **Batch / whole season…** next to the output track list. For each source group, select the row and choose **Use folder for selected source** or **Choose files for selected source**. A folder selection scans media files directly inside that folder (not nested folders). The example template files do not have to be included in the batch selection, but the release's stream layout must be compatible.

The default matching policy uses episode identifiers (e.g., `S01E03`, `Episode 03`, `Show - 03`) and identifies missing/duplicate episode numbers. If a release doesn't use recognisable episode markers, you can explicitly select **Pair by sorted order**. This mode requires identical file counts and **must be reviewed carefully**, since release filenames may sort differently. The preview displays the exact source filenames for every episode; no processing starts until you approve the batch.

Check **Analyse waveform sync separately for every episode** and select the timing reference audio (usually the original Japanese track). Click **Analyse episodes & preview**. Harbinger probes all referenced streams, verifies codec and language matches, and independently computes dubbed-audio offsets for each episode. Weak or inconsistent waveform matches are marked **SKIP**. Harbinger does not silently mux uncertain results; run those episodes through the individual muxer. Previewed offsets and matches appear in the batch table. You can uncheck waveform matching to simply copy streams with no new offsets. **Sample episode's manual delays are intentionally not reused across the season** because each episode may have different edits and timing.

Choose a separate output directory, then click **Start batch muxing**. Harbinger generates `Episode 01.mkv`, `Episode 02.mkv`, etc. and processes validated episodes sequentially. It never overwrites existing final outputs, and each FFmpeg job uses the existing temporary-file safety mechanism. You can cancel the current mux; completed outputs are retained. A failed job is reported and subsequent valid jobs continue.

**Known limits:** This does not time-stretch, fix drift, or alter scene edits; single-offset waveform sync is a heuristic, especially for alternate mixes. It does not automatically include attached fonts needed by ASS subtitles (same limitation as the normal muxer). It does not detect a wrong episode if the explicitly chosen sorted order is incorrect, so inspect the preview. Tracks are mapped conservatively by codec, language and stream index; changed or ambiguous layouts require manual intervention. No Qt Windows/Linux GUI build of this new batch feature has been verified yet.

## v0.6.0 Smart Encoding

Choose **Smart Encoding** in the Output Settings panel to let Harbinger calculate codec settings per queued source file from duration, frame rate, resolution, bit depth, number of audio tracks, and their metadata. Set an optional approximate file-size target in MB, choose video/audio priority, choose Fast/Balanced/Slow and optional preferred codecs. The other mode, **Advanced Encoding**, keeps the original manual GUI.

Smart Encoding currently produces MKV output, preserves compatible extra tracks, rejects impossible size budgets and displays a per-file preview. Target sizes use **single-pass approximate** rate control; exact file size is not guaranteed. See [SMART_ENCODING.md](SMART_ENCODING.md) for details and limitations. Hive presently uses Advanced Encoding only.

**Smart Encoding bitrate metadata hotfix:** When transcoding to MKV, Harbinger now clears inherited per-track statistic tags on re-encoded streams so FFprobe cannot misleadingly show source BPS values for new output. Existing files with unverified tags display `(tag)` in File Information. Optional `tools/measure_stream_bitrates.py` computes actual average per-stream packet rates.


### Measured average bitrates for MKV files
When selecting an MKV in the queue, File Information may initially say `Measuring…` for video/audio bitrates that FFprobe does not report. Harbinger scans the compressed packet sizes and timestamps in the background; once complete, the UI shows per-stream averages labelled `(avg)`. These are measured averages, not guaranteed encoder targets. The scan reads the file without decoding or altering it. Large files may require a few seconds or longer.


### v0.6.3 Advanced video settings
Advanced Encoding offers frame rate (original, fractional NTSC/film, whole-number, custom 1–240 fps)
and output bit depth (Automatic, 8-bit, 10-bit). 10-bit requires a supported HEVC, AV1, or VP9
encoder. Hardware encoding capabilities vary by GPU and FFmpeg build; check hardware capability
detection and try a short encode before running a full batch. Changing bit depth or frame rate
requires video transcoding; passthrough retains the original pixels and frame rate.

### Hive compatibility
Both Linux and Windows must use v0.6.3 (Hive protocol v5); v0.6.2 workers do not accept
the updated protocol. The normal Linux build remains a full Qt GUI; headless operation is optional.
