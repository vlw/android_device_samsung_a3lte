> Historical record. As of 2026-10-22 all A3 variants use the native firmware
> partition. The FU Venus import and every global firmware override have been
> removed. See device/samsung/a3lte/media/NATIVE_FIRMWARE_POLICY.md.

# Venus 00155 deployment and YouTube-related decode checks

2026-10-19. This supersedes the earlier comparison's decision to leave 00113
selected: the user subsequently requested upgrading Venus regardless of speed.

## Implemented firmware selection

- Six unmodified, matched signed files from SM-A300FU Android 6 NON-HLOS,
  `VIDEO.VE.1.8-00155`, are now in
  `vendor/samsung/a3lte/proprietary/vendor/firmware/venus/`.
- `a3lte-vendor.mk` packages them under `/vendor/firmware/venus`.
  `proprietary-files.txt` records the new installed paths; donor provenance is
  in `vendor/samsung/a3lte/VENUS-PROVENANCE.md`.
- Device `media.mk` installs `init.a3lte.venus.rc`. Its `post-fs` action sets
  `/sys/module/firmware_class/parameters/path` to `/vendor/firmware/venus` before
  normal codec services start. This narrow directory contains only Venus files;
  other firmware filenames still fall back to the kernel's original paths.
- The old common vendor firmware remains untouched for other devices. No modem,
  bootloader, or firmware partition was flashed.

The seven copy targets (six files and the init rule) built successfully. They
were installed on the connected phone, permissions and labels restored, and
the system filesystem returned to read-only. The same kernel parameter was
set live with the video core idle, so no reboot or codec-service restart was
needed. The running driver's debugfs confirmed **VIDEO.VE.1.8-00155** from the
installed vendor directory. All six installed hashes match the stock donor.
The boot action is installed and syntax/build checked; a cold boot was not
performed at the user's request to avoid unnecessary restarts.

## Does this phone support 30/60 fps playback?

There is no single frame-rate ceiling across all formats. Runtime MediaCodec
identifies AVC/VP8 Qualcomm hardware components, but VP9/AV1 CPU components.
Venus cannot accelerate a CPU VP9/AV1 decoder. Advertising the format as
supported also does not guarantee real-time speed at every resolution.

Additional tests used **15-second synthetic testsrc2 clips**, consumed through
SurfaceTexture with output frame-count/timestamp checks and no real-time
pacing. Every frame was returned. These measure decoding capacity for these
streams, not end-to-end browser/YouTube playback, dropped display frames,
audio synchronization or sustained thermal behavior.

| Decoder | 1280×720 | 1920×1080 |
| --- | ---: | ---: |
| Qualcomm AVC | 133.4 fps, **900 frames of a 60 fps stream** | 61.4 fps, 450 frames of a 30 fps stream |
| Android software VP9 | 48.4 fps, 450 frames at source 30 fps | 25.4 fps, 450 frames at source 30 fps |
| Android software AV1 | 16.8 fps, 450 frames at source 30 fps | 12.5 fps, 450 frames at source 30 fps |

Thus the tested AVC path has room for 720p60 and 1080p30; it is incorrect to
infer a universal sub-30-fps limit from a software VP9/AV1 result. These tests
do not establish 1080p60 AVC support. VP9 720p30 has headroom in this test,
whereas VP9 1080p30 and AV1 720p30/1080p30 do not. Simpler or more complex
streams and foreground-player scheduling can change the results.

YouTube's actual selected codec was **not observed**. No YouTube package was
installed on the phone during this check; a browser may also use its own CPU
codec implementation rather than the tested C2 component. If the chosen
stream is VP9/AV1, the lack of hardware support is relevant, but these numbers
should not be presented as measured YouTube playback. The actual stream and
frame drops can be checked using YouTube's Stats for nerds:
https://support.google.com/youtube/answer/7519898

No global disabling of working VP9/AV1 decoders, forced false hardware
capabilities, browser changes, or third-party YouTube patches were installed.

## Stock H.264 decoder A/B comparison

To directly test the stock alternative, `tests/OmxDecodeProbe.cpp` loads an
explicit OMX core in an isolated process, uses ordinary input/output buffers,
handles initial output-port reconfiguration and decodes to EOS. Stock libraries
were loaded only under `/data/local/tmp/stock-video-libs`; the running system's
OMX service and its libraries were not replaced.

Both implementations used Venus 00155 and identical AVC files, twice each.

| Stream | Current CAF OMX, fps | Stock Android 6 OMX, fps |
| --- | --- | --- |
| 720p60, 900 frames | 133.29 / 134.09 | 133.83 / 133.64 |
| 1080p30, 450 frames | 61.12 / 61.12 | 61.24 / 61.01 |

All eight runs returned the exact expected frame count, ordered timestamps and
EOS. The first decoded frame from each run was deinterleaved from Venus NV12
using the returned stride/slice dimensions and compared against FFmpeg; every
comparison was **exact** (mean YUV error 0). This checks the first frame's pixel
layout, not every decoded stock frame. `/proc/PID/maps` confirmed that the
stock process really mapped the stock `libOmxCore.so` and `libOmxVdec.so`.

There is no measurable H.264 speed benefit in these tests. This does not prove
that the two library versions have identical behavior for every stream or
Surface/DRM scenario. The new native probe uses byte buffers; Android Surface
playback was validated with the current decoder, not the stock replacement.

For VP9, stock Android 6 contains Google's software implementation embedded
in `libstagefright_soft_vpxdec.so`; it is not an additional hardware decoder.
A direct load of that wrapper on Android 12 fails with missing
`android::SimpleSoftOMXComponent` vtable. No old VP9-engine throughput comparison
was performed, so no claim that the old implementation is intrinsically faster
or slower is made. Stock Android 6 supplies no AV1 decoder.

## Evidence and reproducing the isolated comparison

Artifacts: `out/video-diagnostics/2026-10-19/youtube/`:

- `venus155-installed-state.txt`, `installed-firmware-hashes.txt`,
  `venus155-installed-test.txt`: actual installed selection/version.
- `long-results.json`: all six Surface decode results.
- `omx-comparison.json`, `omx-*.txt`, `omx-*.first.bin`: all eight native runs.
- `native-loaded-maps-stock6.txt`: the actual donor library mappings.
- `stock-vp9-load.txt`: the old Google wrapper ABI failure.

Build `OmxDecodeProbe.cpp` as ARM32 using an Android NDK clang++ compiler,
`-std=c++17 -O2 -static-libstdc++`, include
`frameworks/native/headers/media_plugin/media/openmax`, link `-ldl -lmediandk`.
Arguments:

```text
omx_decode_probe CORE_LIBRARY AVC_MP4 EXPECTED_FRAMES OUTPUT_PREFIX
```

For the installed decoder, use `/vendor/lib/libOmxCore.so` and a library search
path of `/vendor/lib:/system/lib`. For a donor test, use the explicit donor
core path and put its isolated directory first in LD_LIBRARY_PATH. The probe
never installs libraries. Failed probe attempts are not codec regressions;
the initial probe was corrected to handle OMX output-port reconfiguration
before the recorded comparisons above.


## Model-specific secure firmware selection (2026-10-22)

The original runtime qualification above was on SM-A300FU. It cannot qualify
that signed image on SM-A300F or SM-A300H. On the connected SM-A300F with
A300FXXS1BRH1 bootloader, the unconditional FU override caused SCM
`0x2000201` to return `-2` at `init_image`, followed by `Invalid firmware
metadata`. Direct creation of OMX.qcom.video.decoder.avc failed; the media
service also cached a codec list with the hardware components omitted.

Selecting the phone's `/firmware/image` instead loaded
`VIDEO.VE.1.8-00130`; an AVC Surface decode returned all 60 frames. Restarting
only `media` refreshed the cached list and restored the Qualcomm components.
No firmware partition, TrustZone image, bootloader or kernel was flashed.
The failure therefore does not justify bypassing SCM authentication or
changing Venus clocks/reservations.

The post-fs default now selects `/firmware/image` for F/H and unknown variants.
Only an exact `ro.product.model=SM-A300FU` trigger selects the qualified vendor
00155 directory. libinit sets the model before post-fs. The FU path and its
vendor cmnlib symlinks are retained for the separately prepared TEE adapter.
The H fallback is conservative; runtime qualification on H is still required.
Installing this init rule prevents the wrong selection at the next boot;
restarting a media service is only needed to repair a cache populated during
an already failed boot. There is no unconditional media-service restart in init.
