# Multi-track transcoding and MKV stream preservation

Harbinger's normal encoding queue, Smart Encoding and dedicated MKV Track Muxer serve related but different purposes.

- **Smart Encoding:** plans a codec and approximate size per source, preserves compatible extra Matroska tracks and final-remuxes with FFmpeg.
- **Advanced Encoding:** offers manual codec, bitrate, frame-rate and bit-depth control. Stream copy is selected using **Passthrough** in the relevant codec dropdown.
- **MKV Track Muxer:** selects specific video, audio and subtitle tracks from *different source files*, with titles, languages, default/forced flags, manual audio delay and waveform matching.

For regular MKV transcodes, the v0.5.1 fix ensures the application maps **all compatible regular video and audio tracks**, and copies compatible subtitles, attachments/fonts and chapters unless a relevant stream was explicitly removed. The audio bitrate target is **per encoded audio stream**, not the aggregate of all audio tracks. Track-language/title flags are carried where the format supports them; some exotic streams and cover art may still be omitted.

**Stream copy is lossless for the copied compressed data**. If video is set to Passthrough, changing frame rate or bit depth cannot affect that copied video; use transcode mode to apply those settings. Certain subtitle formats, e.g. MP4 `mov_text`, may require conversion to a Matroska-compatible format.

## Historical v0.5.1 implementation note

The following section preserves the technical description supplied with the v0.5.1 correction. Its original patch-installation steps are historical and are **not** instructions for installing v0.6.3.

# Harbinger v0.5.1 — multitrack MKV transcoding correction

## What was wrong

The main transcoder previously used an explicit `-map` for only the first video and first audio stream, followed by `-sn -dn`. That dropped secondary audio tracks and subtitles even when the output was MKV. It also omitted Matroska attachments (including subtitle fonts).

## Updated MKV behaviour

- Video: copy or transcode **all regular video tracks** (or remove them when explicitly selected).
- Audio: copy or transcode **all audio tracks** (or remove them when explicitly selected). Selecting Opus 128 requests a **128 kbps target per audio track**, not a 128 kbps combined total.
- Subtitle tracks, font attachments and chapters are copied automatically for MKV outputs.
- Stream languages, titles and default/forced flags are retained by FFmpeg stream mapping; global metadata remains controlled by Preserve source metadata.
- Cover art classified as an attached-picture video stream and arbitrary data streams may still be omitted; this is not a promise to preserve unsupported stream types.
- Non-MKV container behaviour has not been expanded in this patch; for archival preservation, choose MKV.
- Source video stream is not re-encoded when Video codec is Passthrough.

## The 128 vs 192 kbps issue

`Codec.cpp` already emitted FFmpeg `-b:a 128k` when the effective GUI value was 128 and Opus was selected. The user-reported 192 kbps result could not be isolated from the screenshot alone. We now show the selected target in the on-screen settings summary and in the Activity Log when the job starts, so the actual value passed to FFmpeg is auditable. Opus is variable bitrate by default, so its average output bitrate need not be exactly the chosen target. A consistent 192 kbps average when the Activity Log says 128 kbps would require further investigation.

## Verification

The new `tests/transcode_multitrack_smoke.py` uses FFmpeg/FFprobe to create and transcode a source with 1 video, 2 different-language audio tracks, 2 subtitles, 1 font attachment and 1 chapter. All required streams survived, with both audio streams converted to Opus. This integration check verifies FFmpeg syntax matching the new command builder, not the Qt GUI itself.

`tests/CodecTests.cpp` adds command-builder assertions for `0:V?`, `0:a?`, `0:s?`, `0:t?`, `-map_chapters 0`, and `-b:a 128k`, and for preserving stereo on an unaffected track when another requires 7.1→5.1 downmix. These C++ tests still require a Qt-enabled compile, and the updated Qt GUI hasn't been built in this environment.

## Windows Visual Studio 2022

For an existing v0.5.0 project, extract the small upgrade patch onto the Harbinger project root, replacing `src/Codec.cpp`, `src/Engine.cpp`, `src/MainWindow.cpp`, and `tests/CodecTests.cpp`. The test script is added under `tests/`. **No CMake regeneration is required for the patch**, because no new C++ source files are added.

Build -> Rebuild Solution (Release / x64). Open a two-audio/multi-subtitle MKV, set container MKV, Video codec Passthrough, Audio codec Opus, Audio target bitrate 128 kbps. Enable Activity Log -> Show details. You should see `Audio encoding settings: libopus / 128 kbps target per audio stream` and the MKV stream mapping notice. Open the finished MKV in File Information and compare language tags, audio/subtitle counts and attachments with the original.

For the entire source distribution (not the incremental patch), regenerate CMake in the usual way before building. The full distribution includes the currently selected transparent cyan Harbinger icon.
