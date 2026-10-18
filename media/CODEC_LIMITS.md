# SM-A300FU codec capability policy and measured speed

The A3 product installs two vendor XML updates after the normal codec definitions:

* `media_codecs_performance.xml`: nine ordinary Qualcomm OMX video components.
* `media_codecs_performance_c2.xml`: thirteen Android Codec2 video components.

This is device-specific. The common Samsung configuration, system sources and
media APEX sources are unchanged. Codec2InfoBuilder reads the C2 update after
APEX definitions; the ordinary performance file is also read by the OMX builder.
The files use `update="true"` with existing component names, so they do not add
imaginary codecs or duplicate/rename native components.

## Capability bounds versus throughput

The ordinary video decoders and hardware encoders have a 60 fps policy ceiling;
software encoders have a 30 fps ceiling. Existing tighter profile/block limits
still apply: hardware AVC/VP8 keep 244800 16×16 blocks/s (1920×1088 at 30 fps,
1280×720 at up to 60 fps after the frame-rate ceiling). Supported resolution and
rate must be queried together, not inferred from the global maximum rate.

Full-size software AVC/HEVC/VP8/VP9/AV1 decoders and AVC/VP8 encoders are limited
to a Full HD allocation budget. The 1920×1920 dimension bounds plus 8160 16×16
blocks (32640 8×8 blocks for HEVC) allow both 1920×1080/1088 and portrait rotation,
while rejecting 1920×1920 and 4K. This is the device's advertised policy, not a
claim that the generic algorithms cannot decode larger pictures offline.

VP9 software encoding retains its existing 3600-block/720p area budget and gets
consistent 1280×1280 dimension bounds. HEVC software encoding retains its
512×512 maximum. Small H.263/MPEG-4 software profile bounds are preserved.
Software bitrate limits match the previous effective MediaCodec/native-profile
bounds. The hardware MPEG-4 entries are corrected from generic 1920×1088/20 Mbit/s
to D1/1620 blocks, 48600 blocks/s and 8 Mbit/s, within the advertised profiles.
The H.263 hardware decoder width is reconciled with its effective 720×480 bound.

Profiles/levels still come from native component queries. Limits intersect those
profiles; they do not enable High10 AVC, unsupported HEVC profiles, AV1 encoding,
secure decoding without DRM, or hardware AV1/VP9 acceleration.

For each ordinary component the XML also contains `measured-frame-rate-WxH`.
Applications see these through `VideoCapabilities.getAchievableFrameRatesFor()`;
`isSizeSupported()` / `areSizeAndRateSupported()` describe the functional policy.
Measured throughput can exceed a valid stream's nominal frame-rate ceiling:
unpaced decoding of a 30 fps stream at 100 fps demonstrates headroom, not
support for a stream signalled as 100 fps. Conversely a software format can be
functionally supported but too slow for real-time playback.

No sustained `performance-point-*` guarantees or media performance class are
invented from these short tests. Secure AVC/MPEG-2 keep their previous OEM
capabilities without fabricated performance samples. Concurrency limits and
untested bitrate/profile combinations are not claimed to have been validated.

## Evidence

`tests/codec-performance-samples.json` retains the codec, resolution, frame count,
measured fps and diagnostic source for each sample used. Both range endpoints are rounded down to integer fps, so a measured
29.13 fps does not advertise 30 fps. These are synthetic unpaced Surface tests, with
frame count, EOS and timestamps checked, not pixel-comparison rates. Pixel
comparison was verified separately and is too expensive to benchmark decoding.

AV1/VP9 decoder samples use the new dav1d/row-multithreaded implementations.
AV1/VP9/AVC long samples contain 450/900 frames. The remaining components were
measured repeatedly on the current Venus 00155 firmware. The phone uses its
normal governor/hotplug/thermal settings. Worker counts in unmodified codecs
can depend on CPUs online at startup; the observed variation remains in the
reported ranges. Content complexity, thermal state and competing workloads can
reduce real playback speed further. Players with private bundled decoders may
ignore MediaCodec capabilities and these settings.

A preliminary H.263 benchmark used a 15 fps asset with a 30 fps timestamp
expectation and failed the diagnostic's PTS check. It is excluded; the corrected
30 fps/180-frame H.263 stream was tested twice. This was a test setup error.

Raw logs and public-API before/after dumps are under
`out/video-diagnostics/2026-10-19/limits/`. The configuration does not guarantee
that every application consults achievable-rate estimates.


## Published measurements (fps)

| Component | Decode/encode | Size: fps |
| --- | --- | --- |
| `OMX.qcom.video.decoder.avc` | decode | 1280x720: 133; 1920x1080: 61 |
| `OMX.qcom.video.decoder.h263` | decode | 352x288: 247–255 |
| `OMX.qcom.video.decoder.mpeg2` | decode | 640x360: 179–195; 1280x720: 105; 1920x1080: 76–77 |
| `OMX.qcom.video.decoder.mpeg4` | decode | 640x480: 185–203 |
| `OMX.qcom.video.decoder.vp8` | decode | 1280x720: 85; 1920x1080: 61 |
| `c2.android.av1.decoder` | decode | 1280x720: 57; 1920x1080: 29 |
| `c2.android.avc.decoder` | decode | 1280x720: 40–47; 1920x1080: 22–24 |
| `c2.android.h263.decoder` | decode | 352x288: 253–256 |
| `c2.android.hevc.decoder` | decode | 1280x720: 57–64; 1920x1080: 16–38 |
| `c2.android.mpeg4.decoder` | decode | 176x144: 270–287 |
| `c2.android.vp8.decoder` | decode | 1280x720: 63–64; 1920x1080: 31–33 |
| `c2.android.vp9.decoder` | decode | 1280x720: 62; 1920x1080: 35 |
| `OMX.qcom.video.encoder.avc` | encode | 1280x720: 80–81; 1920x1080: 36 |
| `OMX.qcom.video.encoder.h263` | encode | 352x288: 194–214 |
| `OMX.qcom.video.encoder.mpeg4` | encode | 640x480: 99–100 |
| `OMX.qcom.video.encoder.vp8` | encode | 1280x720: 80; 1920x1080: 36 |
| `c2.android.avc.encoder` | encode | 1280x720: 12 |
| `c2.android.h263.encoder` | encode | 176x144: 241–249 |
| `c2.android.hevc.encoder` | encode | 512x512: 5–10 |
| `c2.android.mpeg4.encoder` | encode | 176x144: 207–220 |
| `c2.android.vp8.encoder` | encode | 640x360: 26 |
| `c2.android.vp9.encoder` | encode | 640x360: 11 |

## Final validation

The product copy targets built successfully and their hashes match the two XML
files installed on the phone. Public API verification passed for all 22 ordinary
components / 33 measured sizes, backed by 61 samples. Native profile/level lists
and both secure components' capabilities are unchanged. The complete offline
regression passed 51 checks across 12 ordinary decoders and 10 encoders, including
independent reference pixel comparisons and encode/decode round trips.

Only vendor.media.omx and media were restarted; media.swcodec and the kernel
remained running. The boot ID stayed
`231035ee-e1b3-4107-a4f8-e20212462d92`, and the system filesystem is read-only.

## Verification and refresh

Static check:

```
python3 device/samsung/a3lte/media/tests/check_codec_limits.py
```

`VideoCapabilitiesProbe.java` queries the public API for all 24 non-alias video
components, their profiles and limits, and achievable rates at the measured
sizes. Build with the Android SDK jar and D8, then run via app_process. Pass its
JSON output to the same checker with `--live path/to/after.json`. The checker
verifies all 22 ordinary components, policy ceilings, measured ranges, rejected
oversized pictures and agreement with the sample ledger.

A normal ROM build copies the two XML files into vendor/etc. For a live XML-only
update, preserve backups, install with mode 0644 and the existing vendor/etc
SELinux label, restore the system mount to read-only, then restart `vendor.media.omx` to refresh the OMX-store XML cache and
`media` (media.player/mediaserver) to invalidate its native codec-list cache. A phone reboot and media.swcodec restart are unnecessary. Already-running
apps may retain a Java MediaCodecList cache until their process is reopened.
