# Historical Hive preview archive

> Original early Hive development notes. Version numbers and verification limitations below describe the state at their time of writing. For the current protocol v5 guide, see [Hive](../HIVE.md).

---

# Harbinger Hive v0.6.3 — Smart batch / openSUSE (protocol v5)

**Update all Windows and Linux nodes to v0.6.3.** Hive protocol v5 carries per-source resolved Smart Encoding video bitrate, source-depth intent, lossless MKV finalisation, and explicit Advanced frame rate / bit depth; older workers/hosts are intentionally incompatible. The headless Linux worker command is `./build-opensuse/Harbinger --hive-worker --allow-host <Windows-LAN-IP> --cpu-only`. It uses an explicit host-IP allowlist and prints a TLS certificate verification code for pairing. See **`OPENSUSE_HIVE.md`** for step-by-step setup and warnings.

Smart Hive mode distributes **whole media files**, not segments; each file has its own Smart size plan, and workers send back remux-finalised MKVs. Network transfers, CPU/GPU speed and audio codec availability affect actual throughput. Hive v0.6.2 completed a real Windows/openSUSE 12-episode Smart batch (six episodes per machine). The v0.6.3 protocol and Advanced options still require a fresh cross-platform check after both systems are rebuilt.

---

## Historical note: Harbinger v0.4.3 — Hive surround-audio compatibility

The **v0.4.3** Hive protocol was **v3**; the current **v0.6.3** protocol is **v5**, so update host and every worker before connecting. Batch jobs can carry AC-3, E-AC-3, DTS Core and PCM audio settings, bitrates up to 1536 kbps, and up to eight channels when the selected encoder supports them. A worker still needs the appropriate encoder in its local FFmpeg build. The experimental segmented single-video join path uses the same audio encoding builder as standalone jobs, including proper DTS experimental flags and 7.1→5.1 downmix. Distributed single-video encoding remains a developer preview and requires real multi-machine validation.

---

# Harbinger Hive — v0.4 developer preview

**Important status:** This is the first integrated C++/Qt source implementation of a LAN host/worker scheduler. It has **not** been compiled with Qt or tested between physical machines in the creation environment (the Qt 6 SDK is unavailable). FFmpeg-only encoding, segment concatenation, and existing muxing smoke tests passed. See `VALIDATION.md`. Treat Hive as a **development preview**, not a proven reliable distributed-transcoding release.

## Quick start (two computers)

1. Build and launch Harbinger v0.4 on **both** Windows/Linux PCs. Install Qt 6.4+ Core/Widgets/Network, FFmpeg, FFprobe and a Qt TLS backend. The **worker** also needs the `openssl` command-line program on PATH on first launch to generate its unique TLS certificate. Use current maintained versions of FFmpeg and OpenSSL.
2. On the worker, open **Harbinger Hive**, tick **Allow other Harbinger computers to request encoding work**. Windows Firewall or your Linux firewall may ask to allow inbound TCP 36840. Worker discovery sends UDP broadcast on port **36839**; discovery may fail on some networks/VLANs, so manual IP entry is provided.
3. Leave the worker's Harbinger program running. It will display a **16-character TLS verification code**. Configure worker GPU permission and FFmpeg thread limit if desired. Sharing is **off at every application launch** unless explicitly enabled in the current session.
4. On the **host**, add and probe video/audio files in the ordinary Harbinger encoding queue and select your desired output format/preset. Open **Harbinger Hive**. Choose which detected computers should participate; add a private LAN IPv4 address manually if discovery is blocked. Choose whether to **Include this computer**.
5. Click **Start Hive encoding**. On the first connection, the host asks you to type the **16-character verification code shown on the worker PC**. The host pins the full certificate SHA-256 fingerprint for subsequent connections to the same node ID. The **worker** then asks whether to approve that host's request. Only accept devices you recognise.
6. Work is dispatched to available approved workers, which stream the source input over TLS, use their own FFmpeg backend and compatible GPU (if enabled/verified), then return results. The host checks SHA-256 checksums before promoting a temporary output file. You can also run one local job simultaneously.
7. Jobs that fail or lose a worker are eligible for one retry, potentially on a different available node or the host. Do not count on distributed speed-up if the source files are enormous or the LAN is slow.

**Firewall:** Allow incoming TCP **36840** on sharing workers. UDP broadcasts to **36839** are used for discovery (not needed when a manual IP is used). Hive rejects worker connections from non-private/non-local IPv4 addresses, and is **not intended for Internet/WAN exposure**. This prototype uses IPv4 only.

## Two ways to distribute work

### A. Distributed batch queue (recommended)

- Multiple different media files can be processed concurrently across Windows/Linux hosts/workers.
- A worker chooses a verified hardware video encoder under Harbinger's usual **Automatic/GPU/CPU** policy. It may fall back to software when configured and supported. Workers can decline all GPU work with their **Allow GPU encoding** checkbox.
- The host also contributes its CPU/GPU when **Include this computer** is checked.
- Each worker accepts **one encoding job at a time**; as soon as it finishes, it receives another waiting job. Faster machines naturally process more files.
- For a particular source, the host transfers the **whole source file** once per worker session; if the same source is reused for several segments on the same worker, the staged file is reused. Uploaded/encoded files are kept only in a session-specific temporary directory and removed when the connection ends.

### B. Split one video (EXPERIMENTAL)

- Tick **Split ONE video into 60-second pieces** with **exactly one** probed video in the queue.
- Requires **H.264/libx264** or **HEVC/libx265** on the host and all participating workers; the worker must report the exact selected encoder. **All sections use CPU software encoding and the same FFmpeg quality/speed settings.** This strict restriction is deliberate: concatenating chunks encoded by different hardware implementations can cause broken output.
- Every connected worker receives sections as it becomes idle; the host can encode sections too. Work is not split equally by computer count, which means faster machines can take more sections.
- Sections are encoded as video-only MKV files. On completion the host checks their video codec, dimensions, frame rate, codec header hash and total duration. If checks fail, the final file is **not** produced. If checks pass, FFmpeg joins the sections using the concat demuxer and copies the original audio/subtitles/chapters into an MKV. Most ordinary files should work, but certain VFR, HDR/10-bit, edit-list, open-GOP, subtitle attachment, and unusual timeline sources may not.
- **This mode has passed only a synthetic FFmpeg proof-of-concept test.** Even matching codec headers do not guarantee seamless joins, timestamp continuity, exact frame accuracy, or unchanged color signaling across every input. Examine output for audio sync, missing/duplicated frames, and visible transitions before relying on it.
- The source file still has to be transferred to each participating worker. Sending a large 4K movie to several machines can cost far more time than the transcoding savings on a 1 Gbit LAN. This v0.4 preview does **not** yet benchmark your connection or automatically skip workers that are slower after network transfer.

### Worker control and limitations

- Worker sharing is **opt-in** and prompts on **every new approved host connection**. Discovery alone does not permit job execution.
- The host authenticates the worker's TLS certificate with a 16-character out-of-band SHA-256 comparison and persists the **entire** hash. If the certificate changes unexpectedly, the host refuses to connect. Worker requests still require human approval per connection; this is **not** mutual-TLS client-certificate authentication.
- Uploads and downloads are streamed in 48 KiB chunks instead of buffering whole media files in RAM, and checksums are verified on transfer completion.
- Worker input filenames are replaced with application-controlled staging paths. A small allowlist of codec IDs, container names, audio encoder names, and numeric ranges is checked before any FFmpeg process starts. FFmpeg is executed with argument arrays via `QProcess`, **never through a command shell**.
- Maximum input file size is 500 GiB, also constrained by available worker disk. A worker can run one job at a time. The **CPU threads** control sets FFmpeg's `-threads` option; it is **not** an operating-system CPU utilisation cap. GPU utilisation cannot be capped to an exact percentage by this preview.
- No local-network encryption is required for discovery broadcasts (only node names, IDs and service ports are advertised). Media and commands use TLS.
- Keep both systems and FFmpeg updated; the worker parses media provided by a paired host. Run it as a normal unprivileged user, never as Administrator/root.

## Known missing features / production checklist

- Build Qt app on Windows/Linux, run C++ tests, exercise real TLS pairing/discovery, and test multi-PC transfer/cancel/retry/compatibility and security. **None are confirmed here.**
- Full GUI integration/UX testing, multi-client long-running stress tests, long-file (>4 GiB) and disk-exhaustion handling.
- Per-worker measured network throughput and automated performance-vs-transfer break-even estimation; bandwidth caps; segment-size adaptation; latency-aware scheduling.
- General-purpose GPU-accelerated single-video split across mixed encoder vendors. Only batch mode supports heterogeneous GPU encoders safely in this preview.
- Persistent/reconnectable jobs, deduplicated media cache across sessions, remote shared-storage paths, resumable network transfer, worker multi-job concurrency, Windows/Linux release installers.
- More detailed GPU usage controls, complete time-base/codec-parameter/HDR and audio-sync validation for segmented movies, and independent verification of security protocol behavior.

## Code map

- `src/HiveProtocol.h`: wire framing, bounded JSON messages, options validation.
- `src/HiveWorker.h/.cpp`: TLS server, opt-in worker, LAN advertisement, temp staging, FFmpeg, return streaming, remote CPU fallback and worker limits.
- `src/HiveClient.h/.cpp`: TLS client, worker fingerprint pinning, upload/download, checksums, progress.
- `src/HiveWindow.h/.cpp`: host/worker GUI, LAN discovery, batching, local participation, segment scheduling, async validation and final remux.
- `tests/HiveProtocolTests.cpp`: deterministic framing, size and options validation (requires Qt SDK to run).
- `tests/hive_segment_smoke.sh`: executable FFmpeg-only split/recombine/audio-remux proof-of-concept (tested on Linux).

## Feedback requested from testers

Please test first with two machines and **short, non-sensitive sample clips**. Report OS, Qt version, FFmpeg version, available GPUs, whether OpenSSL certificate generation worked, network topology (Wi-Fi/ethernet, speed), and exact console or GUI errors. Validate outputs in a player and with `ffprobe` before putting production media through Hive.
