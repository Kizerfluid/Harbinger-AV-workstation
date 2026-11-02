# Harbinger v0.6.3 — cross-distribution Linux release

## Why AppImage?

An openSUSE-native `tar.gz` is **not** universal: it depends on openSUSE's
versions of Qt and glibc. Instead the recommended release is:

`Harbinger-v0.6.3-x86_64.AppImage`

AppImage packages the **Harbinger Qt6 GUI**, Qt shared libraries and plugins,
FFmpeg, FFprobe and the OpenSSL CLI in one downloadable file. NVIDIA/AMD/Intel
hardware drivers are still obtained from the user's operating system.

AppImage is cross-distribution, **not literally universal**: this particular
recipe supports Linux x86_64/glibc systems, not ARM64 or musl-only distributions.
The selected build baseline is **Debian 12 (glibc 2.36, Qt 6.4.2)**, so older
distributions may fail even if an AppImage is used. Host display drivers,
OpenGL/X11 libraries, audio/video hardware and Linux kernels still vary.
Always test on actual systems before claiming compatibility.

## Easiest path — GitHub Actions

1. Copy `scripts/package_linux_appimage.sh` and
   `.github/workflows/linux-appimage.yml` into your GitHub repository.
2. Commit/push these files alongside the latest v0.6.3 code.
3. On GitHub go to **Actions** → **Build portable Linux AppImage** →
   **Run workflow**.
4. Once it succeeds, download the `Harbinger-v0.6.3-x86_64-AppImage` artifact.
5. Extract the workflow artifact ZIP; it contains the AppImage and SHA-256
   checksum. Test the AppImage on several Linux distros.
6. Attach the tested `.AppImage` and `.sha256` files to GitHub Releases under
   tag `v0.6.3` (alongside the Windows ZIP). Do not publish them before testing.

The workflow builds inside Debian 12 rather than using Qt/glibc from openSUSE.
This is useful because glibc is not shipped inside AppImage: building on a
newer glibc generally excludes older distros.

### Running the result

```bash
chmod +x Harbinger-v0.6.3-x86_64.AppImage
./Harbinger-v0.6.3-x86_64.AppImage
```

If an older AppImage runtime complains about missing FUSE, a possible
alternative is:

```bash
./Harbinger-v0.6.3-x86_64.AppImage --appimage-extract-and-run
```

Modern AppImage runtimes may not require external FUSE. Neither command
requires root permissions. For a GUI, run from an X11/Wayland desktop session.

## Local packaging (optional)

If you already compiled Harbinger in `build-appimage/` using a sufficiently old
Linux baseline, have FFmpeg, FFprobe, OpenSSL and `curl` installed, run:

```bash
bash scripts/package_linux_appimage.sh
```

The script retrieves `linuxdeploy` and `linuxdeploy-plugin-qt` from their
upstream continuous release channels, then writes the AppImage and a checksum
to `release/`. The tool executables can instead be pre-provided in the
`HARBINGER_LINUXDEPLOY_TOOLS_DIR` environment variable. For reproducible release
engineering, pin reviewed tool builds and archive their checksums rather than
relying indefinitely on moving `continuous` release channels.

**Do not call an AppImage built directly against openSUSE Tumbleweed's newest
glibc a universal release**. It may not run on older Ubuntu, Debian or Fedora.
Build in the Debian 12 CI container, or another intentionally old baseline.

## Release checklist

- Full Qt GUI launches on Ubuntu/Mint, Fedora, Arch and openSUSE.
- Qt platform plugin (`libqxcb.so`) is present and X11/XWayland display works.
- Qt OpenSSL TLS plugin is present and Hive worker TLS pairing succeeds.
- `ffmpeg`/`ffprobe` from the bundle are visible to Harbinger. Verify HEVC,
  dual audio, subtitles and MKV final remux.
- Windows v0.6.3 and Linux v0.6.3 Hive clients use **protocol v5**; test a
  two-machine job with an approved TLS connection.
- GPU encoders tested with the host GPU driver. NVENC support also depends on
  the NVENC API version compiled into the bundled FFmpeg; it will differ from
  the user's distro-provided version. CPU fallback must work.
- Test Advanced frame-rate and bit-depth controls and the mux default-track
  edits introduced in v0.6.3.
- Record the minimum working distro versions rather than saying 'all Linux'.
- Keep third-party notices and make corresponding third-party source/license
  obligations available for bundled Qt, FFmpeg, OpenSSL and their dependencies.
  Review the full output library list and licenses before public distribution.
- If the AppImage fails to start, run it from a terminal and capture the Qt
  plugin error or missing GLIBC version; don't upgrade the user's system glibc.

## Notes

This script is a **release packaging recipe**, not a compiled or cross-distro
verified AppImage. The actual GitHub Actions build and GPU/Hive testing must
be performed before attaching a public release asset. ABI compatibility of
bundled libraries (including TLS plugins and FFmpeg) must be checked on the
targeted minimum supported distributions. Qt 6.4.2 is an older baseline;
review patched security updates and consider building a maintained Qt version
against an older glibc baseline before a wide production release.

OpenSUSE-native `scripts/package_opensuse_release.sh` can be kept for distro-
specific packages, but it should not be presented as the general Linux download.
