# Harbinger Hive — distributed encoding

**Current version: Harbinger v0.6.3 · Hive protocol v5.** Hive operates from the normal graphical application on **Windows and Linux**. The optional Linux headless worker is an alternative, not the default or required installation.

> **Verification:** Whole-file Smart Encoding was successfully used in v0.6.2 to transcode twelve anime episodes on Windows (RTX 3090) and openSUSE (GTX 1650), with six completed per machine. Protocol v5 and v0.6.3's new Advanced controls require another compatibility test on both rebuilt machines.

## How it works

A coordinator schedules independent encoding jobs onto itself and trusted workers on the local network. Each worker encodes **one job at a time** with its available compatible hardware/software encoder. Files and encoded results are transferred over TLS, with SHA-256 transfer checks; the coordinator saves the completed result locally.

- **TCP 36840:** worker TLS connection, transfer and job control.
- **UDP 36839:** automatic LAN discovery (optional if adding worker IP manually).
- **Hive v5:** incompatible with v4 and earlier workers; both peers must run v0.6.3.
- **Smart Encoding:** computes an individual target/codec plan per episode, processes whole files, and losslessly final-remuxes Smart MKVs at each worker.
- **Advanced Encoding:** passes through relevant codec, channel, bit-depth and exact fractional/custom frame-rate options, subject to the worker FFmpeg build.
- **Single-video segmentation:** experimental CPU-only mode for matching software encoder settings, not recommended for Smart batches.

## Graphical setup

1. Install/build Harbinger v0.6.3 on all machines. See the [openSUSE GUI instructions](OPENSUSE_HIVE.md) for Linux.
2. On the Linux worker, open **Harbinger Hive** and tick **Allow other Harbinger computers to request encoding work (requires your approval)**. Keep Harbinger running. The worker starts disabled by default at each application launch.
3. If you want GPU encoding, enable **Allow GPU encoding on this worker** and confirm its codec in **Hardware Capabilities**. Configure CPU threads only if necessary.
4. Permit incoming **TCP 36840** in the firewall zone assigned to the real LAN interface. Avoid opening the port to the public Internet.
5. On Windows, add files to the regular queue; choose Smart or Advanced Encoding settings. Open **Harbinger Hive**, select the worker by discovery or manually add its LAN IP, and optionally tick **Include this computer in encoding**.
6. Click **Start Hive encoding**. On first contact, verify the 16-character worker code on the Windows coordinator and approve the incoming request on the worker. Never approve an unknown host.
7. Monitor transfers, encoding, finalisation and returned outputs. Play and inspect the result, particularly audio languages, subtitles, video profile/bit depth and the first frames.

For an episode batch, **leave the experimental “Split ONE video into 60-second pieces” option unchecked**. It is a different feature with stricter requirements and validation limitations.

## Connection troubleshooting

On the Linux worker, with sharing **enabled**:

```bash
ss -ltnp | grep 36840
sudo firewall-cmd --get-active-zones
```

There should be a `LISTEN` socket. If there is none, investigate worker/TLS startup before changing firewall rules.

For example, if your physical interface is assigned to firewalld's **home** zone:

```bash
sudo firewall-cmd --zone=home --add-port=36840/tcp
```

This command is temporary. If connectivity improves, save the rule using `--permanent`, or preferably use a source-restricted rich rule as shown in the [openSUSE guide](OPENSUSE_HIVE.md).

On Windows (substitute the worker LAN IP):

```powershell
Test-NetConnection 192.168.0.1 -Port 36840
```

Look for `TcpTestSucceeded : True`. Discovery alone does **not** establish successful TLS connectivity. If TCP is accessible but no prompts appear, check Hive's host log and Qt TLS support on both machines.

### GPU detection

GPU support is determined by the worker's **actual local FFmpeg** and its installed driver, not by the GPU model alone. `hevc_nvenc` appearing in `ffmpeg -encoders` is insufficient if the NVENC runtime API is incompatible. Verify with a short hardware encode, and ensure `ffprobe`, `libopus` and other chosen codecs are present. See [openSUSE troubleshooting](OPENSUSE_HIVE.md).

## Security and limitations

- Use Hive on **trusted local networks only**; do not forward its port to the Internet. Discovery messages are not encrypted, while sessions and media transfers use TLS.
- The coordinator pins worker certificate identity after an out-of-band code check. The worker separately approves each incoming host connection. This is not mutual-TLS client-certificate authentication.
- Input/output files are staged in session-specific temporary directories; workers need enough disk space and do not share the host's filesystem.
- Network bandwidth, codec availability, GPU generation and stream compatibility affect throughput. A 50/50 episode split does not guarantee identical elapsed time or visual characteristics.
- Segmented single-video encoding remains experimental for unusual time bases, 10-bit/HDR, subtitle attachments and joins. Do not treat this as a general-purpose distributed single-video encoder.

The previous v0.4 preview documentation is preserved at [history/HIVE_EARLY_PREVIEW.md](history/HIVE_EARLY_PREVIEW.md) solely as a development record.
