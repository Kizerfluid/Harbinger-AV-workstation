# Third-party components

Harbinger's own source code is distributed under GNU GPL-3.0 (see `LICENSE`).

- **Qt 6**: used for the GUI, networking and core functionality. Qt modules
  have their own open-source licence terms (commonly LGPLv3/GPLv3); see
  https://www.qt.io/licensing/open-source-lgpl-obligations
- **FFmpeg / FFprobe**: invoked as separate external executables, not bundled
  with this source archive. FFmpeg's licence can be LGPL or GPL depending on
  how the binary was configured and distributed. See https://ffmpeg.org/legal.html
- **OpenSSL / NVIDIA / other system libraries and drivers**: used where available;
  each has its own licence and redistribution conditions. This source package
  does not include drivers, FFmpeg executables, or Qt library binaries.

If you distribute a *prebuilt* Harbinger package with these third-party binaries,
verify and comply with the precise terms for the versions included.
