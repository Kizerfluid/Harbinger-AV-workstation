# openSUSE — Harbinger desktop GUI and Hive worker

**Version v0.6.3 / Hive protocol v5.** Harbinger runs as a **full Qt6 graphical application on both Windows and Linux**. A terminal is used to build it, but the normal executable opens the GUI. Headless operation is strictly optional.

## 1. Install dependencies

On an openSUSE desktop with an active KDE/GNOME/X11/Wayland session, install the development tools, Qt, TLS and media utilities:

```bash
sudo zypper install gcc gcc-c++ cmake ninja qt6-base-devel qt6-network-tls openssl ffmpeg
```

Package names and codec availability may differ by openSUSE release/repository. Harbinger needs Qt **Core, Widgets, Network** (6.4+), a working TLS backend for Hive, and `ffmpeg`/`ffprobe` in its execution environment. The `openssl` CLI is needed for worker certificate generation.

Check codec support:

```bash
ffmpeg -hide_banner -encoders | grep -E 'libx265|hevc_nvenc|libopus|aac'
ffprobe -version | head -n 1
```

To verify that NVIDIA encoding actually works (not merely that the encoder is listed):

```bash
ffmpeg -hide_banner -f lavfi -i 'testsrc2=size=640x360:rate=5' \
  -frames:v 10 -c:v hevc_nvenc -f null -
```

If FFmpeg reports an **NVENC API / NVIDIA driver mismatch**, fix the driver or FFmpeg build compatibility before expecting GPU acceleration in Harbinger. An Emby installation may use its own separate FFmpeg build and therefore may not reflect the system FFmpeg's capabilities.

## 2. Build and launch the full GUI

Extract the Harbinger source, enter the folder containing `CMakeLists.txt`, then:

```bash
bash scripts/build_opensuse.sh
./build-opensuse/Harbinger
```

The build script configures and builds the **Harbinger GUI target**, without requiring optional C++ tests. To build and run the developer tests deliberately:

```bash
bash scripts/build_opensuse.sh --with-tests
```

Equivalent GUI-only commands:

```bash
cmake -S . -B build-opensuse -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-opensuse --target Harbinger --parallel 2
./build-opensuse/Harbinger
```

If Ninja says `no work to do`, the target is considered up to date. Launch the actual binary to check it. A GUI will not appear over plain SSH without an active graphical display.

## 3. Enable Hive in the Linux GUI

1. Open **Harbinger Hive** from the Linux desktop application.
2. Tick **Allow other Harbinger computers to request encoding work (requires your approval)**. The worker should display a ready status and a 16-character TLS code.
3. Enable **Allow GPU encoding on this worker** if the tested encoder is available; otherwise CPU operation is possible with appropriate codecs.
4. Leave Harbinger running. Windows will connect, verify the worker, then the Linux application will ask for approval.

Worker sharing resets to disabled after restarting Harbinger. The worker listens on **TCP 36840** and advertises on **UDP 36839**.

## 4. Configure openSUSE's firewall correctly

Identify the network interface's active zone:

```bash
sudo firewall-cmd --get-active-zones
```

**Important:** if your real LAN interface is in `home` and `public` is merely the default, opening a port in `public` does **not** allow traffic entering through the `home` interface.

For a quick temporary test on a trusted LAN using zone `home`:

```bash
sudo firewall-cmd --zone=home --add-port=36840/tcp
```

For a permanent rule restricted to your Windows host, substitute your Windows host's actual LAN IPv4 address:

```bash
sudo firewall-cmd --permanent --zone=home \
  --add-rich-rule='rule family="ipv4" source address="192.168.0.1/32" port port="36840" protocol="tcp" accept'
sudo firewall-cmd --reload
```

Do not copy `192.168.0.1` unless it really is your Windows PC. A broader `--add-port` rule already in the same zone will also allow other source addresses; remove it if you intend to enforce the more restrictive rule.

Verify the worker is listening:

```bash
ss -ltnp | grep 36840
```

From Windows PowerShell (substitute the server's IP):

```powershell
Test-NetConnection 192.168.0.1 -Port 36840
```

Require `TcpTestSucceeded : True` before troubleshooting TLS prompts. If there is no Linux `LISTEN` socket, fix worker startup first. Discovery does not require exposing TCP to the Internet; never configure router port forwarding for Hive.

## 5. Distribute a batch from Windows

1. Rebuild **both** Windows and Linux from **v0.6.3** (Hive v5 is not backward-compatible with v4).
2. Select Smart Encoding, choose codecs/settings and add episode files on Windows.
3. Open Windows **Harbinger Hive**, tick the Linux server or use **Worker IP address → Add IP**.
4. To use both machines, tick **Include this computer in encoding**. Do **not** tick experimental single-video splitting for Smart Encoding.
5. Click **Start Hive encoding**; verify the TLS code on the host and approve Windows on the Linux GUI.
6. Validate returned video, audio and subtitle tracks and playback. Whole-file work produces separate episodes on each machine, not sections of one episode.

A real v0.6.2 Windows RTX 3090 + openSUSE GTX 1650 Hive batch completed twelve episodes (six each). **v0.6.3 still needs its own end-to-end verification** after the protocol upgrade.

## 6. Optional headless worker

For a machine *without* a desktop session, the alternative CLI is:

```bash
./build-opensuse/Harbinger --hive-worker --allow-host WINDOWS_LAN_IPV4 --cpu-only
```

Replace `WINDOWS_LAN_IPV4` with a trusted host address. This mode is not required for normal Linux GUI use and has different worker approval behaviour.

## Troubleshooting

- **`CodecTests` compile failure:** use the normal GUI build (`BUILD_TESTING=OFF` / `--target Harbinger`) to isolate the application from optional test targets. Diagnose the test independently.
- **Hardware encoders unavailable:** compare `nvidia-smi`, `ffmpeg -encoders`, and a real ten-frame NVENC encode. NVIDIA driver/API mismatch can cause a probe failure despite a working GPU.
- **No code/approval popup:** confirm a Linux `LISTEN` socket, correct zone's TCP port, then Windows TCP accessibility. TLS or pinned certificates may explain subsequent failures.
- **No GUI:** launch the executable from a graphical desktop terminal and inspect Qt's plugin/TLS messages.
- **Protocol mismatch:** all machines must use v0.6.3 / v5.

See [HIVE.md](HIVE.md) for the complete Hive feature and security description.
