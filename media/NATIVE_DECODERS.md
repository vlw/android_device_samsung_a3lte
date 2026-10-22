# Native AV1 / VP9 acceleration on SM-A300FU

Validated 2026-10-19 on the connected Android 12.1 engineering build. All source
changes are in device/vendor; no framework, external codec or kernel sources
were edited. Venus is loaded from the native firmware partition (see NATIVE_FIRMWARE_POLICY.md).

## Implementation

AV1 now uses dav1d 1.5.4 with ARM32 NEON, both 8/10-bit output, film grain,
up to four workers and two-frame parallel delay. The copied Codec2 adapter keeps
`c2.android.av1.decoder` and the existing graphics/metadata interface. VP9 keeps
platform libvpx and enables row multithreading (`VP9D_SET_ROW_MT`). Both adapters
size their workers from configured CPUs, not the transient online CPU count.
Core hotplug often leaves only one CPU online at component construction, which
previously resulted in a permanently undersized worker pool.

No filter skipping, CPU frequency floor, forced-online CPU setting or disabled
thermal protection is used. A temporary CPU minimum experiment was restored to
0. Kernel changes cannot provide AV1/VP9 hardware decoding to the existing
Venus block; the measured gain comes from userspace SIMD and parallelism.

Full source, upstream revisions, licenses, generated prebuilts and the explicit
regeneration command are in `vendor/samsung/a3lte/media/README.md`. The product
selects the replacements with the `a3_media.native_decoders` Soong option.
Disabling that option restores the platform source modules for a subsequent
ROM build. Regenerate the fixed prebuilts whenever the copied sources change.

## Measurements

Unpaced Surface decode, 450 frames per stream (15 seconds at nominal 30 fps).
The complete output is consumed and timestamps/frame count/EOS are checked.

| Decoder | Earlier platform baseline, fps | New backend, fps |
| --- | ---: | ---: |
| AV1 1280×720 | 16.8 | 57.5 |
| AV1 1920×1080 | 12.5 | 29.1 |
| VP9 1280×720 | 48.4 | 62.9 |
| VP9 1920×1080 | 25.4 | 35.9 |

These are sequential synthetic tests, not an isolated thermal-controlled A/B
benchmark. Startup hotplug/thread counts affect the old decoders: other AV1
720p baseline runs gave 24–26 fps and VP9 gave 31 fps. New results demonstrate
useful headroom, not a guarantee for arbitrary content. AV1 1080p still has
little margin for 30 fps and neither software decoder is guaranteed 1080p60.
Hardware AVC 1080p remained approximately 61 fps in the regression check.

## Correctness and deployment

The final dav1d suite passed eight checks: 10-bit SDR, film grain, ordinary and
film-grain flush/seek, complete 720p/1080p Surface streams, VP9 and hardware AVC
regressions. Film-grain/8-bit output matched the independent FFmpeg reference
exactly; 10-bit-to-8-bit output differed by mean 0.083/255 (rounding).

The final VP9 suite passed six checks: 10-bit profile 2, 8-bit and 10-bit
flush/seek, complete 720p/1080p Surface streams, and AV1 regression. 8-bit output
matched exactly; mean 10-bit conversion error was about 0.10/255. The separate
initial VP9 smoke also matched the 8-bit reference exactly.

Rebuilding both source producers and both preferred logical codec modules
succeeded, as did the complete `com.android.media.swcodec` package build;
output hashes match the checked-in prebuilts and installed phone
libraries. On this flattened-APEX build only media.swcodec was restarted to
load the replacements. No phone reboot was sent; boot ID remained
`231035ee-e1b3-4107-a4f8-e20212462d92`. The system filesystem was returned to RO.

Diagnostics: `out/video-diagnostics/2026-10-19/optimization/`, especially
`dav1d-validation.json`, `vp9-validation.json`, `final-rebuild.log` and rollback
copies of the original two libraries. Test API:

```
decode-flush NAME VIDEO REFERENCE_YUV FRAME_COUNT FPS
surface-decode NAME VIDEO FRAME_COUNT FPS
```

Still untested: DRM, HDR rendering/metadata end-to-end, adaptive resolution,
long sustained playback, concurrent clients and malformed input. The existing
8/10-bit interface does not add 12-bit AV1 support.

## YouTube / browser policy

No global AV1 ban or misleading capabilities/ranks were added: native
acceleration proved practical. Codec ranks choose implementations for a given
MIME type; they cannot tell a player to choose VP9 instead of AV1. These changes
benefit apps using Android MediaCodec. A browser using its own bundled software
decoder may bypass them. Actual YouTube stream selection/playback was not
observed, so no claim is made that YouTube now chooses VP9 or reaches these
rates on real content. AVC remains the highest-throughput tested hardware path.
