# Harbinger

**Harbinger** is a cross-platform C++17/Qt6 desktop application for video transcoding, multi-track MKV muxing and LAN-distributed encoding. It uses external **FFmpeg** and **FFprobe** executables, with a full graphical interface on **Windows and Linux**.

**Current source version: v0.6.3** · **Licence: GPL-3.0** · **Hive protocol: v5**

> **Development status:** Harbinger is a work-in-progress source project, not a prebuilt installer. Hive Smart Encoding was tested successfully on Windows + openSUSE in v0.6.2 (12 episodes, six per machine). The v0.6.3 changes—including its MKV muxer crash fix and Hive v5 compatibility—still require regression testing before being treated as fully verified.

## Features

- **Smart Encoding:** optional approximate per-file target size, quality priority, speed, codec choice, source-aware bitrate allocation, preservation of compatible MKV tracks, and an FFmpeg-only lossless final remux.
- **Advanced Encoding:** per-codec quality/speed options, GPU/CPU encode and decode preferences, optional resizing, explicit **8-bit/10-bit** output (encoder-dependent), and frame-rate control including **23.976/29.97/59.94 fps** and custom rates.
- **MKV track muxer:** combine video, multiple audio languages, subtitles and optional chapters from different sources without re-encoding supported streams; edit track titles, languages, default/forced flags and audio delays.
- **Dub-aware audio synchronisation:** compare waveform signatures to suggest time offsets for alternative-language soundtracks, with manual review. A separate batch muxer can apply a source-track template across a season.
- **Harbinger Hive:** distribute independent encoding jobs between trusted Windows and Linux machines over an encrypted LAN connection, with opt-in worker approval and hardware-aware routing.
- **Hardware detection:** probes FFmpeg's supported NVIDIA NVENC, AMD/Intel and software paths; actual capabilities depend on the local driver and FFmpeg build.
- **Media inspection:** view video/audio/subtitle metadata and estimated average per-stream bitrates; for MKV files, missing bitrate values can be measured from compressed packets on import.

## Requirements

- C++17 compiler and CMake 3.21 or newer.
- Qt 6.4 or newer with **Core**, **Widgets** and **Network**, plus a working Qt TLS backend for Hive.
- Installed `ffmpeg` and `ffprobe` available to Harbinger; the worker also needs the relevant encoders.
- `openssl` command-line utility for worker certificate creation where required.
- GPU drivers only if using hardware acceleration. CPU encoding works without a GPU.

### Windows — Visual Studio 2022 + vcpkg

The project uses a Qt6 CMake build. An example command when Qt6 is supplied via vcpkg:

```bat
cmake -S . -B build-vcpkg -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="%USERPROFILE%\tools\vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
```

Build the `Harbinger` target in **Release / x64**. Ensure the vcpkg installation contains `qtbase` and its required dependencies. When deploying a Windows build to another directory or PC, use Qt's `windeployqt` and include all applicable third-party licence notices.

### Linux — openSUSE desktop GUI

Install the required packages using the distro's package manager, then run from the source directory:

```bash
bash scripts/build_opensuse.sh
./build-opensuse/Harbinger
```

This launches the **full graphical interface**, not headless mode. See [openSUSE setup and Hive guide](docs/OPENSUSE_HIVE.md) for dependencies, firewall settings and troubleshooting.

## Using Harbinger Hive

1. Install the **same Harbinger v0.6.3 build** on the Windows coordinator and Linux worker. Hive v5 is incompatible with older v4 workers.
2. On the worker GUI, open **Harbinger Hive** and enable **Allow other Harbinger computers to request encoding work**. Approve only recognised hosts.
3. Allow incoming **TCP 36840** in the worker's actual firewall zone; **UDP 36839** is used for automatic discovery. Do not expose Hive to the public Internet.
4. Queue files on the coordinator, open Hive, select the worker, and optionally keep **Include this computer in encoding** enabled to use both machines.
5. Verify the worker's TLS code during first pairing and approve the connection at the worker. For Smart Encoding, use **whole-file batch mode**, not experimental single-video splitting.

Full details: [Hive guide](docs/HIVE.md) · [Smart Encoding guide](docs/SMART_ENCODING.md).

## Documentation

- [Changelog](CHANGELOG.md) — feature history and v0.6.3 notes.
- [Hive networking and limitations](docs/HIVE.md).
- [openSUSE desktop GUI and worker setup](docs/OPENSUSE_HIVE.md).
- [Smart Encoding and approximate size targets](docs/SMART_ENCODING.md).
- [Multi-track transcoding and stream preservation](docs/TRANSCODING.md).
- [Validation and regression checklist](docs/VALIDATION.md).
- [Third-party notices](THIRD_PARTY_NOTICES.md).

Previous long-form README and Hive prototype documentation are retained under [docs/history](docs/history/) for historical reference, not as up-to-date build instructions.

## Limitations

This is not an exact-size two-pass encoder or a full video editor. GPU support varies by GPU, FFmpeg build, driver and selected pixel format. Some unusual streams, HDR metadata and subtitle attachments may require manual verification; audio waveform matching cannot correct time drift or different edits. Hive's single-video segmentation is experimental. Always verify important outputs before deleting source files.

## Licence

Harbinger's original frontend source is under **GNU GPL-3.0**; see [LICENSE](LICENSE). Qt, FFmpeg, OpenSSL and other dependencies have their own terms; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). This source repository does not itself bundle FFmpeg or graphics drivers.
