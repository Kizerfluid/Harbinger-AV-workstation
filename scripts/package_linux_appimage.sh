#!/usr/bin/env bash
# Harbinger v0.6.3: package the native Qt6 GUI as a portable x86_64 AppImage.
# Best run in the Debian 12 GitHub Actions container (see workflow) so the
# bundled libraries do not require openSUSE Tumbleweed's newer glibc.
# Does not change Harbinger source or the user's system libraries.
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION=0.6.3
ARCH_EXPECTED=x86_64
if [[ "$(uname -m)" != "$ARCH_EXPECTED" ]]; then
  echo "ERROR: This recipe currently builds only $ARCH_EXPECTED AppImages." >&2
  exit 1
fi

BUILD_DIR="${HARBINGER_APPIMAGE_BUILD_DIR:-$ROOT/build-appimage}"
OUT_DIR="${HARBINGER_APPIMAGE_OUTPUT_DIR:-$ROOT/release}"
WORK_DIR="$OUT_DIR/.appimage-work"
APPDIR="$WORK_DIR/AppDir"
TOOL_DIR="$WORK_DIR/tools"
mkdir -p "$OUT_DIR" "$TOOL_DIR"

# The application must be built with Qt from the intended deployment baseline.
BIN="$BUILD_DIR/Harbinger"
if [[ ! -x "$BIN" ]]; then
  echo "ERROR: Missing $BIN. First build using the instructions in docs/LINUX_APPIMAGE_RELEASE.md" >&2
  exit 1
fi
if [[ ! -f "$ROOT/assets/Harbinger.png" ]]; then
  echo "ERROR: Missing assets/Harbinger.png" >&2
  exit 1
fi

# Bundle FFmpeg, FFprobe and OpenSSL so users don't need to install these CLI
# programs; hardware drivers remain external system dependencies.
for cmd in ffmpeg ffprobe openssl; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "ERROR: '$cmd' is missing. Install it in the build environment." >&2
    exit 1
  fi
done
FFMPEG="$(command -v ffmpeg)"
FFPROBE="$(command -v ffprobe)"
OPENSSL="$(command -v openssl)"

get_tool() {
  local name="$1" url="$2"
  local target="$TOOL_DIR/$name"
  if [[ -n "${HARBINGER_LINUXDEPLOY_TOOLS_DIR:-}" && -x "$HARBINGER_LINUXDEPLOY_TOOLS_DIR/$name" ]]; then
    cp -f "$HARBINGER_LINUXDEPLOY_TOOLS_DIR/$name" "$target"
  elif [[ ! -x "$target" ]]; then
    echo "Downloading $name from its upstream linuxdeploy release..."
    curl --fail --location --retry 3 --silent --show-error "$url" --output "$target"
  fi
  chmod +x "$target"
}

get_tool linuxdeploy-x86_64.AppImage \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
get_tool linuxdeploy-plugin-qt-x86_64.AppImage \
  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage

rm -rf -- "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/licenses/Harbinger" \
         "$WORK_DIR/desktop"
install -m755 "$BIN" "$APPDIR/usr/bin/Harbinger"
install -m755 "$FFMPEG" "$APPDIR/usr/bin/ffmpeg"
install -m755 "$FFPROBE" "$APPDIR/usr/bin/ffprobe"
install -m755 "$OPENSSL" "$APPDIR/usr/bin/openssl"
for doc in LICENSE THIRD_PARTY_NOTICES.md; do
  [[ ! -f "$ROOT/$doc" ]] || cp "$ROOT/$doc" "$APPDIR/usr/share/licenses/Harbinger/"
done

cat > "$WORK_DIR/desktop/Harbinger.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Harbinger
Comment=Smart video transcoding, MKV muxing and distributed encoding
Exec=Harbinger
Icon=Harbinger
Terminal=false
Categories=AudioVideo;Video;
DESKTOP

# linuxdeploy-plugin-qt deploys Qt6 Widgets/Network/platform plugins. Qt TLS
# provider is loaded dynamically and may not be detected by ELF dependency
# analysis; ensure it is available within the bundle for Hive pairing.
QT_PLUGIN_ROOT=""
if command -v qmake6 >/dev/null 2>&1; then
  QT_PLUGIN_ROOT="$(qmake6 -query QT_INSTALL_PLUGINS || true)"
fi
if [[ -n "$QT_PLUGIN_ROOT" && -f "$QT_PLUGIN_ROOT/tls/libqopensslbackend.so" ]]; then
  mkdir -p "$APPDIR/usr/plugins/tls"
  cp -L "$QT_PLUGIN_ROOT/tls/libqopensslbackend.so" "$APPDIR/usr/plugins/tls/"
fi

export ARCH=x86_64
export VERSION="$VERSION"
# Lets the linuxdeploy AppImage (and its Qt plugin) run without FUSE on the
# build host, especially inside the GitHub Actions Debian container.
export APPIMAGE_EXTRACT_AND_RUN=1
# Qt plugin can be resolved next to linuxdeploy's own AppImage.
cd "$OUT_DIR"
"$TOOL_DIR/linuxdeploy-x86_64.AppImage" \
  --appdir "$APPDIR" \
  --executable "$BIN" \
  --executable "$FFMPEG" \
  --executable "$FFPROBE" \
  --executable "$OPENSSL" \
  --desktop-file "$WORK_DIR/desktop/Harbinger.desktop" \
  --icon-file "$ROOT/assets/Harbinger.png" \
  --plugin qt --output appimage

# Check critical packaging contents. Some paths vary between linuxdeploy
# versions; use name-based discovery rather than guessing the plugin prefix.
if ! find "$APPDIR" -type f -name 'libqxcb.so' -print -quit | grep -q .; then
  echo "ERROR: Qt's xcb platform plugin was not bundled." >&2
  exit 1
fi
if ! find "$APPDIR" -type f -name 'libqopensslbackend.so' -print -quit | grep -q .; then
  echo "ERROR: Qt's OpenSSL TLS plugin was not bundled (Hive would fail)." >&2
  exit 1
fi
for executable in Harbinger ffmpeg ffprobe openssl; do
  [[ -x "$APPDIR/usr/bin/$executable" ]] || { echo "ERROR: Missing $executable" >&2; exit 1; }
done

mapfile -t artifacts < <(find "$OUT_DIR" -maxdepth 1 -type f -name '*.AppImage' -newer "$APPDIR/usr/bin/Harbinger" -print)
if [[ ${#artifacts[@]} -ne 1 ]]; then
  echo "ERROR: Expected 1 newly created AppImage but found ${#artifacts[@]}; see $OUT_DIR." >&2
  exit 1
fi
NAME="Harbinger-v${VERSION}-x86_64.AppImage"
OUTPUT="$OUT_DIR/$NAME"
if [[ "${artifacts[0]}" != "$OUTPUT" ]]; then
  mv -f -- "${artifacts[0]}" "$OUTPUT"
fi
chmod +x "$OUTPUT"
(cd "$OUT_DIR" && sha256sum "$NAME" > "$NAME.sha256")
printf '\nCreated: %s\nChecksum: %s.sha256\n' "$OUTPUT" "$OUTPUT"
cat <<'NOTE'
IMPORTANT: This AppImage has NOT been certified on other distributions.
Test on at least Ubuntu/Mint, Fedora, Arch and openSUSE before declaring a
stable cross-distro release. See docs/LINUX_APPIMAGE_RELEASE.md.
NOTE
