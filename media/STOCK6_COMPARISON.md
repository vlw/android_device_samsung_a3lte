> Historical record. As of 2026-10-22 all A3 variants use the native firmware
> partition. The FU Venus import and every global firmware override have been
> removed. See device/samsung/a3lte/media/NATIVE_FIRMWARE_POLICY.md.

# Android 6 stock video comparison

**Historical comparison:** the later requested permanent Venus 00155 update and
actual stock-vs-current H.264 throughput checks are documented in
[VENUS_UPDATE.md](VENUS_UPDATE.md). The initial restoration to 00113 described
below was subsequently superseded by that update.

2026-10-19, SM-A300FU. Donor: extracted system from A300FUXXU1CSG3 and
`CP_A300FUXXU1CQI1_REV00_CL2127430_QB7553660.tar.md5` / `NON-HLOS.bin`.
All phone tests used the existing Android 12.1 system. No reboot, firmware
partition flash, codec-service restart, or production-library replacement.

## Additional stock codecs

The active stock `media_codecs.xml` declares the same four Qualcomm hardware
encoder formats as our build: AVC, H.263, MPEG-4 Part 2 and VP8. It does not
provide a hardware HEVC encoder, VP9 encoder, or AV1 support.

Stock does contain additional legacy decoder entries:

| Stock component | Implementation / status in this audit |
| --- | --- |
| OMX.qcom.video.decoder.vc1 | Qualcomm vendor decoder, video/x-ms-wmv and video/wvc1 declared in stock XML; no stock-ROM playback test performed |
| OMX.SEC.vc1.sw.dec | Samsung software, libsomxvc1dsw.so + libsavsvc.so + libsavscmn.so |
| OMX.SEC.wmv7.dec | Samsung software, libsomxwmv7d.so + shared VC-1 engine dependencies |
| OMX.SEC.wmv8.dec | Samsung software, libsomxwmv8d.so + shared VC-1 engine dependencies |
| OMX.SEC.mp43.dec | Samsung software MS MPEG-4 v3, libsomxmp43d.so + shared engine dependencies |
| OMX.SEC.hevc.sw.dec | Samsung software, libsomxhevcdsw.so + libarac.so + libsxqk.so |

Samsung also supplies software AVC, MPEG-4, H.263 and VP8 alternatives. These
are additional implementations of formats already working in Android 12,
not additional hardware acceleration. Stock's Qualcomm
`OMX.qcom.video.decoder.hevcswvdec` entry is **commented out**, explicitly a
software decoder. Qualcomm DivX XML entries are also commented out. Their
presence as strings inside an OMX core library does not mean enabled playback.

Stock's Samsung HEVC entry follows the Google software include, with a donor
comment saying this ordering is due to a performance issue. There is no
evidence from this audit that the Samsung HEVC implementation is faster or
more correct than current C2 software HEVC. No stock decoder throughput claim
is made: only the current C2 decoder was tested with video frames.

## Actual loader and initialization checks

Unmodified stock libraries were placed in `/data/local/tmp/stock-video-libs`,
with their private engine dependencies. An isolated ARM32 NDK executable uses
`dlopen(RTLD_NOW)`; it does not replace or register anything in the OMX service.

- `libOmxVdec.so`: loads. This alone does not validate output, Android native
  buffers, Surface lifetime, or old Binder/MemoryHeap ABI assumptions.
- `libOmxVenc.so`: **does not load**. The missing Android symbol is
  `_ZN7android14MemoryHeapBase4initEiPviiPKc` (the old MemoryHeapBase::init ABI).
- Qualcomm `libOmxVdecHevc.so`: loads with `libHevcSwDecoder.so` and `libmmosal.so`;
  it is a software HEVC path, not a new hardware engine.
- Samsung HEVC, VC-1, WMV7, WMV8 and MP43 wrappers: load **and initialize**;
  input/output port queries and clean deinitialization succeed. No compressed
  media buffers were submitted and playback was not validated.

The Samsung OMX wrapper ABI expects version **1.0**, as verified from
`SEC_OMX_Check_SizeVersion` and `SEC_OMX_VideoDecodeComponentInit` in stock
`libsomxcmn.so`. Initializing it with AOSP's 1.1 header version is rejected with
`OMX_ErrorVersionMismatch`. An integration needs to account for this, not merely
copy files and add XML.

There is also a framework integration boundary for legacy formats:
`frameworks/av/media/libstagefright/omx/OMXUtils.cpp:GetComponentRole` has no
VC-1/WMV/DivX MIME mappings. `ACodec::queryCapabilities` rejects unknown roles
before inspecting the vendor component. This explains why the Qualcomm VC-1
and DivX registry/XML entries do not appear as usable MediaCodec components
in our current Android 12 runtime. Renaming a MIME to AVC would not be correct.
A separate adapter/plugin and appropriate container handling would be needed
for a proper device/vendor implementation; this comparison did not add one.

## Venus firmware: a real newer candidate, tested temporarily

Three distinct images were found:

| Location | Embedded version | Selection |
| --- | --- | --- |
| Current vendor/samsung/msm8916-common firmware | VIDEO.VE.1.8-00109 | Not the running image on this phone |
| Phone /firmware/image | VIDEO.VE.1.8-00113 | Running baseline, confirmed by driver debugfs |
| Android 6 donor NON-HLOS/image | VIDEO.VE.1.8-00155 | Temporarily tested, then removed from selection |

The kernel directly searches `/firmware/image` in `drivers/base/firmware_class.c`.
It succeeds before Android ueventd's vendor-directory fallback matters. Thus
comparing only `/vendor/firmware` would incorrectly identify 00109 as active.
The authoritative observed version was
`/sys/kernel/debug/msm_vidc/core0/info`.

For the temporary test, all six matched donor files (`venus.mdt`, `.b00` through
`.b04`) were copied to `/data/local/tmp/venus-stock6`. With the video core idle,
`/sys/module/firmware_class/parameters/path` was set to that directory. The
next normal codec session loaded **00155**, confirmed by debugfs. No firmware
partition was changed.

**15 checks passed on 00155:**

- AVC and VP8 1920×1080 Surface decode, 90 frames each, including timestamps.
- AVC, H.263, MPEG-4, VP8 and MPEG-2 reference decoding, with pixel comparisons.
- All four hardware encoders, 30 frames each, independently decoded by FFmpeg
  and compared to their original YUV input.

No speed improvement was established. On the short 1080p Surface tests,
00155 gave ~61.4 fps for AVC and ~61.5 for VP8. After restoring 00113, AVC gave
~66.6 fps. These are not controlled sustained benchmarks and the difference
is not evidence that 00155 is worse. The encoder outputs also remained within
the baseline error range. The vendor's firmware changelog is unavailable;
a higher revision alone is not proof of a specific bug fix.

The custom firmware path was cleared, an ordinary AVC session was rerun, and
debugfs again reported **00113**. The kernel boot ID stayed unchanged. Current
production selection is the original firmware. Merely replacing vendor's old
00109 files would not upgrade this phone's active image because the kernel
loads the firmware partition first.

Important diagnostic detail: writing zero bytes to the sysfs string parameter
does not clear it. The empty original value was restored by writing a single
NUL byte, followed by checking that reading it returned zero bytes, then
confirming the baseline firmware version after a new session.

## Reproducibility / evidence

`tests/StockCodecLoadProbe.c` supports:

```text
stock_codec_load_probe LIBRARY [LIBRARY ...]
stock_codec_load_probe --component OMX.SEC.NAME LIBRARY
```

The second form only initializes, queries ports and deinitializes the Samsung
component using its stock OMX 1.0 ABI. It is not a decode correctness test.
Build as ARM32 with an Android NDK compiler and the two include roots
`frameworks/native/headers/media_plugin` and
`frameworks/native/headers/media_plugin/media/openmax`, linking `-ldl`.

Evidence is in `out/video-diagnostics/2026-10-19/stock6/`:

- `stock6-active-video-xml.json`: parsed active entries, excluding XML comments.
- `omx-abi.json`, `stock-load.txt`, `stock-legacy-load.txt`, `init-*.txt`:
  dependency/symbol comparison and actual isolated load/initialization results.
- `stock6-venus-sha256.json`, `phone-venus-sha256.txt`: complete file hashes.
- `firmware-before.txt`, `firmware-during.txt`, `firmware-after.txt`:
  00113 → 00155 → 00113, measured in the driver.
- `venus155-results.json`: all 15 checks, plus encoded packets/host-decoded YUV.
- `path-restored.txt`: empty original firmware path restored.

Conclusion: the useful new finding is a compatible newer Venus image and
additional legacy software decoders. Neither a modern extra hardware codec
nor a proven performance/correctness benefit from replacing production
libraries was established. Production codec libraries, firmware selection,
device settings and system sources remain unchanged.
