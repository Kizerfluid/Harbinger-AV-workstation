# Harbinger

**Harbinger** is an open-source, cross-platform desktop application for video transcoding, multi-track MKV muxing, audio synchronisation and distributed encoding. Built with **C++17 and Qt6**, it uses **FFmpeg/FFprobe** behind a graphical interface for **Windows and Linux**.

**Latest public release: v0.6.3** · **Licence: GPL-3.0** · **Harbinger Hive protocol: v5**

[**Download the latest release**](../../releases/latest) · [Changelog](CHANGELOG.md) · [Build from source](#building-from-source) · [Documentation](#documentation)

## Download

You **do not need to compile Harbinger** to try the published builds.

| Platform | Download | Package |
| --- | --- | --- |
| **Windows 10/11 (x64)** | [**Download Windows ZIP**](../../releases/download/0.6.3/Harbinger-v0.6.3-Windows-x64.zip) | Portable ZIP containing `Harbinger.exe` and deployed Qt runtime files |
| **Linux (x86-64)** | [**Download Linux AppImage**](../../releases/download/0.6.3/Harbinger-v0.6.3-x86_64.AppImage) | Single-file graphical application with packaged Qt and FFmpeg tools |

**Checksums:** [Windows SHA-256](../../releases/download/0.6.3/Harbinger-v0.6.3-Windows-x64.zip.sha256) · [Linux SHA-256](../../releases/download/0.6.3/Harbinger-v0.6.3-x86_64.AppImage.sha256). The release page also contains GitHub-generated source archives.

### Windows installation

1. Download the Windows ZIP and extract **the entire archive** to a directory of your choice.
2. Run `Harbinger.exe` from the extracted directory.
3. If FFmpeg and FFprobe are not already provided or configured, install compatible Windows executables and put them on your `PATH`, beside Harbinger, or select their paths in the application settings.

The Windows package deploys Qt's application libraries; it is not a traditional installer. GPU acceleration requires a suitable driver and FFmpeg hardware-encoder support.

### Linux installation

Download the x86-64 AppImage, then open a terminal in its download directory:

```bash
chmod +x Harbinger-v0.6.3-x86_64.AppImage
./Harbinger-v0.6.3-x86_64.AppImage
```

The AppImage is intended for modern **x86-64, glibc-based Linux distributions** such as openSUSE, Ubuntu/Mint, Fedora and Arch. It is built from a Debian 12 baseline (glibc 2.36); compatibility with every distribution and desktop environment has **not** been verified. It does not cover ARM64. GPU drivers are supplied by the host system and hardware-accelerated encoding depends on their compatibility with the packaged FFmpeg build.

For packaging details and troubleshooting, see [Linux AppImage releases](docs/LINUX_APPIMAGE_RELEASE.md). For a native openSUSE source build, see [openSUSE build and Hive guide](docs/OPENSUSE_HIVE.md).

> **Release status:** v0.6.3 is an early public release. Smart Encoding and Hive have undergone real-world testing, but some features (notably experimental single-video splitting) remain under development. The v0.6.3 muxer stability fix and new Advanced Encoding settings should be tested with your own media before relying on them for archival work. Keep original files until output has been checked.

## Features

- **Smart Encoding:** optional approximate target file size *per input*, source-aware video/audio bitrate allocation, quality and speed preferences, codec selection, compatible MKV track preservation, and a lossless FFmpeg-only final remux.
- **Advanced Encoding:** codec and quality settings, CPU/GPU encoding and decoding preferences, optional resizing, explicit **8-bit or 10-bit** output on supported codecs, and frame-rate choices including **23.976, 29.97 and 59.94 fps** or custom rates.
- **Multi-track MKV muxer:** combine video, multiple audio languages and subtitles from different sources without re-encoding compatible streams; edit titles, language tags, default/forced track flags and audio delays, with chapter selection where supported.
- **Dub-aware audio synchronisation:** compare waveform signatures to suggest offsets for alternative-language dubs, with manual adjustment and batch muxing across matched episodes.
- **Harbinger Hive:** share encoding work between trusted Windows and Linux computers over a TLS-protected local network, with user approval and per-worker hardware capabilities. Smart Encoding distributes whole files across workers.
- **Hardware acceleration:** detect and test supported NVIDIA NVENC, AMD/Intel and software encoding/decoding paths through the installed or packaged FFmpeg build.
- **Media inspection:** see video, audio and subtitle stream metadata; measure approximate average video/audio bitrates from FFprobe packets when stream metadata is missing or outdated.

## Getting started

For a normal encode, add one or more media files to the queue, choose **Smart Encoding** for automatic bitrate planning or **Advanced Encoding** for manual control, select the output settings and start encoding.

For combining tracks from different files, open the **MKV Track Muxer**, import the sources, select the video/audio/subtitle tracks and adjust their metadata or timing as needed. Compatible streams are copied rather than re-encoded.

### Using Harbinger Hive

1. Run **the same Harbinger version** on each Windows or Linux computer. v0.6.3 uses **Hive protocol v5** and is not compatible with v4 workers.
2. On the worker machine, open **Harbinger Hive** and enable **Allow other Harbinger computers to request encoding work**.
3. Permit inbound **TCP 36840** on the worker's actual local firewall zone. **UDP 36839** is used for automatic discovery; you can also add a worker by IP address.
4. On the coordinator, queue files and open Hive. Select the worker and optionally enable **Include this computer in encoding** so both machines contribute.
5. Verify the worker's TLS pairing code and approve the connection on the worker. For Smart Encoding batches, leave the experimental single-video segment splitting disabled.

**Keep Hive on a trusted LAN; do not expose its worker port to the public Internet.** The v0.6.2 Hive implementation successfully processed a real 12-episode Smart Encoding batch across a Windows RTX 3090 PC and an openSUSE GTX 1650 server (six episodes on each). This demonstrates a working cross-platform batch workflow; the v0.6.3 protocol changes still need separate regression testing.

See [Hive networking, setup and limitations](docs/HIVE.md) for additional details.

## Building from source

Prebuilt releases are available above. The following requirements are for developers compiling Harbinger themselves.

### Requirements

- A **C++17** compiler (Visual Studio 2022/MSVC, GCC or Clang) and **CMake 3.21+**.
- **Qt 6.4+** development libraries with Core, Widgets and Network, plus a functioning Qt TLS backend for Hive.
- **FFmpeg** and **FFprobe** executables. The required codecs must be available in the chosen FFmpeg build.
- The `openssl` command-line tool for certificate generation when running a Hive worker.
- Compatible GPU drivers only if hardware acceleration is wanted; CPU encoding can be used instead.

### Windows — Visual Studio 2022 and vcpkg

Install the build tools and Qt6 through vcpkg, then configure from a Visual Studio 2022 developer terminal:

```bat
cmake -S . -B build-vcpkg -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="%USERPROFILE%\tools\vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build-vcpkg --config Release --target Harbinger
```

Adjust the vcpkg path if yours is different. For redistribution, use Qt's `windeployqt` and comply with the applicable third-party licences.

### Linux — native GUI build

Install the corresponding Qt6, compiler, CMake, FFmpeg and TLS development packages for your distribution. On openSUSE, use:

```bash
bash scripts/build_opensuse.sh
./build-opensuse/Harbinger
```

This opens the **full graphical application**; the optional command-line Hive worker is not required. See [openSUSE desktop build instructions](docs/OPENSUSE_HIVE.md) and [Linux AppImage release tooling](docs/LINUX_APPIMAGE_RELEASE.md).

## Documentation

- [Changelog](CHANGELOG.md) — version and feature history.
- [Harbinger Hive](docs/HIVE.md) — networking, pairing, batch distribution and current limitations.
- [Linux AppImage release guide](docs/LINUX_APPIMAGE_RELEASE.md) — portable build, packaging and compatibility.
- [openSUSE GUI and Hive setup](docs/OPENSUSE_HIVE.md) — native Linux build and worker configuration.
- [Smart Encoding](docs/SMART_ENCODING.md) — file-size estimates, quality settings and bitrate allocation.
- [Transcoding and MKV stream preservation](docs/TRANSCODING.md).
- [Validation and regression tests](docs/VALIDATION.md).
- [Third-party notices](THIRD_PARTY_NOTICES.md).

Historical development notes are in [docs/history](docs/history/); use the current guides above for installation and usage.

## Limitations

Target file sizes are **approximate**, not an exact two-pass guarantee. GPU support depends on the device, driver, FFmpeg build and chosen codec/pixel format. Unusual streams, HDR metadata and subtitle attachments may require manual validation. Waveform-based synchronisation suggests a fixed offset; it cannot repair time drift or different edits. Hive's **single-video segmentation** remains experimental.

## Licence

Harbinger's original frontend code is licensed under the **[GNU General Public License v3.0](LICENSE)**. Qt, FFmpeg, OpenSSL and other third-party components have their own licences; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The source repository does not contain proprietary GPU drivers. Release bundles and system builds may include different third-party binaries.
