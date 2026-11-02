# Harbinger v0.6.3 — GitHub release packaging

**Windows:** Build Release x64 on Visual Studio 2022, then run:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\package_windows_release.ps1
```

Produces `release/Harbinger-v0.6.3-Windows-x64.zip` (plus checksum).
See the existing Windows packaging script for its FFmpeg and Qt requirements.
Test the extracted ZIP independently of Visual Studio.

**Linux (cross-distribution x86_64):** Use the new AppImage build:

1. Commit `.github/workflows/linux-appimage.yml` and `scripts/package_linux_appimage.sh`.
2. GitHub **Actions** → **Build portable Linux AppImage** → **Run workflow**.
3. Download the workflow artifact, extract the AppImage and SHA-256 checksum.
4. Test on Ubuntu/Mint, Fedora, Arch and openSUSE, including Hive and GPU paths.
5. Attach the tested AppImage, Windows ZIP and checksums to GitHub's `v0.6.3` release.

See [`docs/LINUX_APPIMAGE_RELEASE.md`](docs/LINUX_APPIMAGE_RELEASE.md) for
technical details, minimum glibc baseline, dependencies and limitations.

**Don't publish the previous `openSUSE-x86_64.tar.gz` as a universal Linux build.**
It does not bundle Qt and may require openSUSE-specific shared libraries.

Use **Pre-release** until platform compatibility, Qt TLS and the v0.6.3 mux
regressions have been tested. AppImage is portable across many desktop
Linux distributions, but not every architecture, libc or driver combination.
