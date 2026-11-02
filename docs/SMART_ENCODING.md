# Smart Encoding

Harbinger **Smart Encoding** automatically plans an FFmpeg transcode for each queued media file. **Advanced Encoding** remains available for manually selecting bit depth, frame rate, codec and other settings. The two modes retain independent preferences.

## Typical workflow

1. Add source media to the main queue and allow FFprobe inspection to complete.
2. Select **Smart Encoding**.
3. Optionally enter a target **file size in decimal MB, per input file**. `0 MB` / no target uses quality-based encoding instead of a size budget.
4. Choose **Visual Quality**, **Balanced** or **Audio Quality**; **Fast**, **Balanced** or **Slow**; and video/audio codec preferences or Automatic/Passthrough where appropriate.
5. Review the per-source bitrate and resolution plan before encoding. For a network batch, open [Harbinger Hive](HIVE.md).

## Planning behaviour

- Reads duration, source size, video resolution/frame rate/bit depth and counts/layouts of audio and subtitle tracks.
- An approximate target size is converted into a total average-bitrate budget. Harbinger allocates separate audio targets for each audio stream and uses the remainder for video, reserving some overhead and optionally downscaling when necessary.
- Supports supported HEVC/AV1 10-bit output where the encoder allows it. Hardware-specific pixel formats may be required; selected HDR/Dolby Vision characteristics are **not** guaranteed to survive re-encoding.
- Produces an **MKV**, preserving compatible additional audio/subtitles, attached fonts, chapters, titles, languages and metadata instead of silently discarding tracks.
- Reports planned video/audio bitrate and actual size. Where source MKV stream bitrate is not reported reliably, File Information can asynchronously measure compressed packet bitrate, labelled `(avg)`, without editing the file.

**Size targets are approximate, not exact.** Single-pass video rate control, VBR audio, source complexity and Matroska overhead affect the final result. A Windows file property may display binary MiB and label it MB: **350 decimal MB ≈ 333.8 MiB**.

## Lossless final MKV remux

Since v0.6.1, Smart Encoding performs a second **FFmpeg-only stream-copy remux** after the encode. This was introduced after a full multi-track MKV exhibited corrupted opening playback but remuxing without re-encoding fixed it. The remux does **not** compress the video/audio a second time and does **not** require MKVToolNix. It needs some additional disk space and I/O. A failed remux preserves the encode intermediate for recovery rather than silently publishing it as complete.

## Smart Encoding over Hive

Since v0.6.2, the coordinator can send **whole-file** Smart jobs to trusted workers. Each source has its own resolved size plan; compatible workers encode and final-remux before returning the MKV. For **v0.6.3, Hive protocol v5** requires the same version on every Windows/Linux machine. The experimental split-one-video mode is **not** compatible with Smart mode.

## Safeguards and limitations

- Unachievable size budgets, unknown durations and incompatible passthrough/lossless arrangements may be rejected instead of silently removing tracks.
- Approximate bitrate planning is **per file**, not across the whole queue, and there is no exact-size two-pass guarantee.
- Not all hardware decoders/encoders support 10-bit or every format. Validate a small clip and source/output metadata first.
- Source files remain untouched; Harbinger uses temporary output naming and avoids overwriting completed files.

See [Hive](HIVE.md), [multi-track transcoding](TRANSCODING.md) and [validation](VALIDATION.md).
