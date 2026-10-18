# Offline video codec audit — SM-A300FU, Android 12.1

Audit date: 2026-10-19. The initial codec audit added diagnostics only. A later
user-requested firmware update now selects Venus 00155; see
[VENUS_UPDATE.md](VENUS_UPDATE.md) for installation and the longer AVC/VP9/AV1
and stock-vs-current decoder measurements. Codec libraries, XML capabilities,
ranks and system sources were unchanged at that stage. Subsequent native AV1/VP9
acceleration is documented in [NATIVE_DECODERS.md](NATIVE_DECODERS.md); it
supersedes the software decoder throughput below.

Device capability policy and measured rates for every ordinary video component
are now configured; see [CODEC_LIMITS.md](CODEC_LIMITS.md).

## Results

Android exposes 24 non-alias video components: 12 ordinary decoders, 10 encoders,
and 2 secure decoders. All 22 ordinary components successfully processed complete
synthetic streams. Secure AVC/MPEG-2 require a DRM session and were not exercised.
This is coverage of the tested profiles/streams, not every allowed profile,
resolution, bitrate, concurrent-session count, or malformed input.

| Format | Decode | Encode | Tested details |
| --- | --- | --- | --- |
| H.264/AVC | Qualcomm + Android software | Qualcomm + Android software | Baseline; Main/High encoder requests with explicit level, independently verified SPS profiles; High/B-frame decoding; hardware Surface encode/decode up to 1920×1080 |
| H.265/HEVC | Android software | Android software | Main 8-bit; Surface decode 720p/1080p; software encoder up to its advertised 512×512 limit |
| VP8 | Qualcomm + Android software | Qualcomm + Android software | 8-bit; hardware Surface encode/decode at 720p/1080p |
| VP9 | Android software | Android software | Profile 0; Profile 2 10-bit decode checked against FFmpeg and through Surface; software Surface encode 640×360 |
| AV1 | Android software | Not exposed | Main 8-bit; Surface decode 720p/1080p |
| MPEG-4 Part 2 | Qualcomm + Android software | Qualcomm + Android software | Independent reference decode, encoder round trips; Qualcomm Surface encode 640×480 |
| H.263 | Qualcomm + Android software | Qualcomm + Android software | Independent reference decode, encoder round trips; Qualcomm Surface encode 352×288 |
| MPEG-2 | Qualcomm | Not exposed | Reference stream with B-frames, 640×360 |

VC-1/DivX entries in the legacy vendor XML are not exposed in the runtime
MediaCodecList; this audit does not claim those formats work. HEVC Main10, AVC
High10, AV1 10-bit/HDR, 4K, DRM, seeking/adaptive-resolution transitions, long
thermal runs, and multi-client contention were not validated. No camera,
microphone, audio playback, calls, or network video were used.

### Throughput, not just successful output

Short runs of 90 synthetic frames, without real-time pacing. Surface output was
consumed with SurfaceTexture.updateTexImage(), with per-frame timestamp checks.
These rates include framework/Surface delivery; the content is synthetic and
cannot predict sustained playback of arbitrary content.

| Decoder, Surface path | 1280×720 | 1920×1080 |
| --- | ---: | ---: |
| Qualcomm H.264 | 92.8 fps | 66.5 fps |
| Qualcomm VP8 | 94.6 fps | 61.3 fps |
| Software HEVC | 53.0 fps | 37.2 fps |
| Software VP9 | 47.3 fps | 24.8 fps |
| Software AV1 | 26.5 fps | 16.1 fps |

The Qualcomm AVC/VP8 figures were taken while some small round-trip tests were
finishing; software figures were obtained in a separate sequential repeat with
a stable kernel boot ID. Do not interpret these as isolated codec benchmarks.
CPU video is functional but can be too slow for 30 fps. In particular, AV1 is
already below 30 fps at 720p in this test, and VP9 is below 30 fps at 1080p.

Qualcomm Surface encoding completed 60 frames at ~36 fps at 1080p for both AVC
and VP8. Software Surface encoding was substantially slower: AVC 720p ~12 fps,
HEVC 512×512 ~11 fps, VP8 640×360 ~27 fps, VP9 640×360 ~13 fps. Encoder input
for these tests deliberately changes a solid RGB color every frame. These
figures are compatibility checks and illustrative rates, not camera-quality
benchmarks.

### Correctness checks

- Independently generated FFmpeg streams → each matching Android decoder.
  Every Y, U, V sample is compared to FFmpeg output at its presentation timestamp.
  Frame count, crop geometry, EOS and output ordering are checked.
- Synthetic YUV420 → each Android encoder → independent FFmpeg decoder. All
  30 frames must survive; lossy image error is checked against the source.
- Those encoded packets → every matching Android decoder, compared against
  the independently decoded images. The initial runs had mean sample errors
  below 0.2/255 (mostly exact matches; small MPEG/H.263 IDCT rounding differences).
- EGL RGBA Surface → Qualcomm and software encoders → FFmpeg RGB output. Known
  changing colors detect channel swaps, stale frames and basic conversion errors.
- Surface decode also checks frame callbacks and timestamps, so successful
  ByteBuffer output is not the only exercised path.

One host-test setup issue was corrected: raw MPEG-4 needs FFmpeg
`-fps_mode passthrough` to preserve every decoded frame.

A separate API compatibility limitation was found: an explicit AVC `KEY_PROFILE`
without `KEY_LEVEL` fails with `0x80001001` / `INVALID_OPERATION`. This rejection
happens in the shared `ACodec::setupAVCEncoderParameters` code, before the vendor
profile is configured. The public MediaFormat docs describe profile/level as
hints; consequently this is relevant to apps that set only a profile, even
though it is not a broken Main/High hardware encoder. Supplying both makes
Main/High work and produces the requested SPS profile. No framework source was
modified and this profile-only compatibility limitation remains. The diagnostic
explicitly supplies Level 3 for its 640×480 Main/High tests.

Lossy VP8 at 1 Mbit/s has numeric color error on abrupt whole-frame color
transitions (mean RGB error 5.22/255, maximum 18.25/255 at 720p); this alone is not
evidence of a channel/layout defect. A repeat at 8 Mbit/s reduced mean RGB
error to 1.43/255 and maximum to 8.25/255, with all 60 frames intact.

Two USB interruptions occurred during the audit. The user confirmed manually
rebooting during the first; the second coincided with other work and its origin
was not established. The interrupted HEVC Surface case subsequently passed,
along with VP9/AV1 Surface tests, without a boot ID change. No test sends reboot,
service-restart, remount, or persistent-setting commands.

## Reproduction

Host requirements: `adb`, JDK, `ffmpeg` with libx264/libx265/libvpx/libaom, Python 3
and numpy. Keep the phone connected; use a separate output directory. No ROM
build or flashing is required. The script stops if USB disappears or the kernel
boot ID changes rather than treating later connection failures as codec errors.

From the Android source root:

```sh
python3 device/samsung/a3lte/media/tests/run_video_codecs.py \
  --serial RF8H10GGY0A --out /tmp/a3-video-audit
```

Add `--performance` for the 720p/1080p Surface decoder runs. Base runs cover all
ordinary components at modest resolutions, pixel comparisons and encode/decode
round trips. Encoder Surface/profile/10-bit cases above were additional targeted
runs using the same Java tool. Artifacts from this audit, including interrupted
attempts and later successful repeats, are in `out/video-diagnostics/2026-10-19/`.
The `reproducible/` subdirectory contains the final base-script run:
51 successful checks, 12 distinct decoders and 10 distinct encoders. See also
`summary.json` for coverage and the remaining API limitation.

`VideoCodecProbe` commands (arguments after the class name):

```text
list
decode NAME VIDEO REFERENCE_YUV FRAME_COUNT FPS
decode NAME VIDEO - FRAME_COUNT FPS                  # throughput; no pixel comparison
decode-flush NAME VIDEO REFERENCE_YUV FRAME_COUNT FPS
decode-packets NAME MIME PACKETS REFERENCE_YUV FRAME_COUNT FPS
encode NAME MIME INPUT_YUV WIDTH HEIGHT FRAME_COUNT FPS OUTPUT_PACKETS
surface-decode NAME VIDEO FRAME_COUNT FPS
surface-encode NAME MIME WIDTH HEIGHT FRAME_COUNT FPS OUTPUT_PACKETS [AVC_PROFILE [BITRATE]]
```

The optional AVC profile uses Level 3 and is intended for the 640×480 profile
checks, not a general profile/level selection policy. Omit it or use zero to
keep the default profile. Packet files are diagnostic, not playable containers:
three big-endian int32 values (width, height, fps), then records of big-endian
int64 PTS-us, int32 MediaCodec flags, int32 length and compressed payload. Codec
configuration records may occur more than once. The host script handles them
and constructs elementary streams/IVF for independent decode.

Uploaded diagnostics live only under `/data/local/tmp`; they can be removed
after collecting results. The original audit was read-only; see the native
decoder and Venus documents for subsequent production changes.

## Android 6 stock comparison

See [STOCK6_COMPARISON.md](STOCK6_COMPARISON.md) for additional legacy decoders,
actual stock-library ABI checks, and a temporary Venus 00155 test with verified
restoration to the phone’s original 00113.
