# SM-A300FU camera and native Samsung HDR

The image-processing stack is taken from **SM-A300FU A300FUXXU1CSG3, Android 6.0.1**.
`stock-firmware.json` records original paths and SHA-256 hashes for the unmodified
OEM binaries (and records the preview-stride compatibility fix below). It includes the HAL, daemon, ISP/statistics modules, AEC/AWB/AF,
IMX219/S5K5E3YX sensor and Chromatix libraries, SuperShot and JPEG dependencies.
Device-specific overrides select the matching daemon and JPEG libraries; other
msm8916 devices keep the common defaults. The unused `libmm-qcamera` test helper
is no longer a production package. The active vendor interface was already the
stock interface; this change does not substitute an msm8998 camera interface.

## Native processing path

Normal photos and video go through the original Samsung HAL/ISP and sensor
profiles. The wrapper sends the stock Samsung application handshake, command
1508. It does not add saturation, sharpening, denoise or colour transforms.

Rear `scene-mode=hdr` adapts standard Camera1 to the stock SDK protocol:

1. Stop preview when entering/leaving HDR; send command 1014 (enable/disable),
   set `hdr-mode=1/0`, then rebuild preview streams. Samsung's `setAEBracket`
   selects the native `-2,0,2` exposure series and NV21 output.
2. One `take_picture` delivers three NV21 buffers through the legacy compressed
   image callback. The HAL has already removed chroma-height padding. Copy the
   exact packed NV21 bytes, without JPEG encoding/decoding, RGB conversion,
   independent exposure changes, AWB changes or repeated preview settling.
3. Invoke the unmodified SuperShot `SS_Create/SS_Init/SS_Run/SS_Release`, with
   the stock ShotHDR defaults: registration enabled, uiMode 3, Qualcomm type 4,
   format 9 (NV21), three inputs and a separate output buffer. No external tuning
   INI is applied.
4. Encode once through the unmodified `libsecjpegboard` and its matching Qualcomm
   hardware JPEG stack. The OEM `libQjpeg` NV21 encoder is the software fallback,
   as in SecJpegEncoder. There is no AOSP/libjpeg fallback for the final image.
5. Add EXIF using the source port of the CSG3 HDR ExifManager/MakerNote path,
   without changing any JPEG image coding bytes. Read current HAL parameters
   after encoding, as stock does; keep the pre-capture snapshot private to apps.
   Return one JPEG/shutter to the app. Suppress internal raw/JPEG callbacks.

`SecJpegAbi` implements only the small original encoder base-class lifetime/job
ABI. Image conversion, padding, quantization and encoding stay in the stock
binary. The Android 6 interface library itself depends on obsolete Skia APIs and
is not loaded into Android 12.

The former sequential-JPEG HDR adapter has been removed. Its old tests show
functionality of that former adapter, not proof of stock processing parity.

## HDR EXIF parity

`StockExif.cpp` ports the precise `ShotHDR::setMakerNoteToEXIF` and
`ExifManager::GetResultJpeg(..., CameraParameters, false)` behaviour of CSG3.
It uses the current libexif serializer; the obsolete camera framework is not
loaded into mediaserver. The production build remains entirely source-built
apart from the existing OEM image-processing binaries.

* Samsung MakerNote: version 0100, device ID 0x12000, weather, city ID, model ID,
  sRGB, favourite flag and optional ISP AE debug (tag 0x60). Incoming EXIF
  MakerNote data is copied verbatim, as `ShotCommon::parseDebugInfo` does. Plain
  NV21 buffers carry none. Bounds checking prevents invalid callback payloads
  from overflowing the EXIF APP1 segment.
* Make is lowercase `samsung`; model and software follow `ro.product.model` and
  `ro.build.PDA`, with the original `samsung` fallback. No stock firmware version
  or camera telemetry is fabricated when a property/parameter is absent.
* Width/height in both IFDs, orientation, three local timestamps, white balance,
  flash, EXIF 2.2, exposure program, lens/aperture/35 mm fields and image unique
  ID follow the original parameter keys and tag types. HDR scene capture is 4;
  metering follows `rt-hdr`/spot/matrix/default rules.
* GPS uses the OEM conversion: truncate to 1e-7 degrees, then integer D/M/S;
  zero coordinates have S/W references. Stock HDR writes latitude/longitude
  only, not altitude, GPS time or processing method. Non-finite/out-of-range
  coordinates are rejected by the adapter.
* **Stock HDR passes false** to ExifManager: it removes ExposureTime, ISO and
  ShutterSpeed/Aperture/Brightness/ExposureBias APEX tags. It also does not
  generate a thumbnail. Their absence is deliberate stock behaviour, not an
  unfinished metadata port. Normal HAL JPEG metadata is not changed.

`prepare_exif_oracle.py` verifies firmware hashes and prepares the owner's
original `libseccameracore` for an explicit standalone test. The oracle maps
only diagnostic inputs, resolves the EXIF/JPEG calls against the original
`libexifa`/`libjpega`, and invokes ExifManager/MakerNote entry points without ELF
initializers or camera framework startup. CameraParameters uses the current
getters; the removed getInt64 getter is adapted to its original strtoll(base=0)
behaviour. Time is fixed only inside that test for deterministic comparisons.
No oracle library or diagnostic executable is a production package.

The source port and original code produced **byte-identical complete JPEGs in
18 synthetic cases**, covering optional fields, four rotations, metering,
context tags, positive/negative/zero/limit GPS and AE debug sizes 1..49999 bytes.
Callback MakerNote extraction and idempotent EXIF replacement also passed.
The main image bytes stay unchanged. Actual scene/image parity still requires
real capture/A-B testing, which was not performed at the owner's request.

To reproduce after extracting the four original libraries into STOCK_DIR:

```
m a3_stock_exif_oracle a3_stock_exif_test
python3 device/samsung/a3lte/camera/tests/prepare_exif_oracle.py STOCK_DIR OUT/exif-oracle
adb push OUT/exif-oracle.image OUT/exif-oracle.relocs OUT/exif-oracle.symbols OUT/exif-oracle.constants /data/local/tmp/
adb push STOCK_DIR/libexifa.so STOCK_DIR/libjpega.so /data/local/tmp/
adb push out/target/product/a3lte/system/vendor/bin/a3_stock_exif_oracle out/target/product/a3lte/system/vendor/bin/a3_stock_exif_test /data/local/tmp/
python3 device/samsung/a3lte/camera/tests/compare_stock_exif.py SYNTHETIC_JPEG OUTPUT_DIR
```

## Framework compatibility

The wrapper exposes module API 2.4 and Camera1 devices, forwarding the framework's
module/torch callbacks. Samsung's new flash manager requires callback registration
before the first camera open. The old wrapper omitted this, causing the initial
open to fail and the framework to cache incorrect camera/flash information.

The stock `getCapabilities/get_camera_info` metadata path crashes under the
current framework ABI. Camera1 only needs facing/orientation, so the wrapper
supplies the A300FU's stock back/90 and front/270 information, device API 1.0 and
resource cost 100. It does not invoke that incompatible metadata path. This is
an interface adaptation, not an ISP modification.

Callbacks, parameter storage and HDR state are per camera. Samsung's stream
allocator supplies a different memory callback cookie, so rear/front memory
trampolines must remain. Capture reservation/thread ownership are serialized.
Cancellation waits for processing, but never joins a callback already being
delivered: CameraClient may hold the lock that callback needs. Close joins the
remaining worker after framework messages are disabled. Callback reentry returns
EBUSY for another capture, rather than joining itself. Intermediate native
parameters are hidden from the client; user EV/AE/AWB settings are not modified.

The HDR controller keeps the client message mask separate from the internal
NV21/JPEG callback request. Disabling the public JPEG callback cannot interrupt
collection of the three native frames, and an unwanted result is not forwarded.
The forced callback is restored after success, failure or cancellation. Late
NV21 messages while HDR is selected are discarded instead of escaping as JPEGs.

HDR mode changes are transactional: command/parameter/preview-start failures
restore the old mode, parameters and preview. `recording-hint=true` leaves HDR;
`start_recording` also performs this transition if an app omitted the hint.
Camera1 HDR bracketing is a still-photo mode, not a recording stream. Release is
idempotent and per-camera parameter buffers are owned by each API call.

`persist.vendor.camera.samsung=false` disables the handshake and HDR;
`persist.vendor.camera.hdr=false` disables only HDR. Both normally default true.

## Resolutions and profiles

`DEVICE_MEDIA_PROFILES` selects `configs/media_profiles_V1_0.xml`. All stock
Android 5/6 camcorder profiles are retained, including **1080p/30 on both cameras**
and QCIF. Rates are 17 Mbps FHD, 12 Mbps HD, 3.449 Mbps 720x480 and 767 kbps QVGA.
CIF and VGA have additional standard recording/time-lapse profiles.

All native photo/video/preview sizes and actual EV/FPS ranges pass through. The
old wrapper's VGA-only preview list, unsupported 7500–30000 FPS range, incorrect
0.5 EV/-2..2 range and false video-snapshot capability are removed. No invented
4K/high-speed profiles are added. Camera2 groups sizes in its own menu; the full
native lists remain available through Camera1.

## Checks and limits

After lunch:

```
m camera.msm8916 a3_hdr_lifecycle_test a3_camera_wrapper_test a3_stock_hdr_probe a3_stock_jpeg_test a3_camera_blob_load_test
python3 device/samsung/a3lte/camera/tests/validate_media_profiles.py
```

Tests are explicit diagnostic targets, not production packages:

* `a3_hdr_lifecycle_test`: fake HAL with native three-buffer NV21 capture and the
  actual OEM fusion/encoder. Checks one capture call, repeated capture, busy
  rejection, parameters, EXIF orientation, cancellation, invalid/short frames,
  missing-frame timeout and recovery. Also tests callback reentry, cancellation
  during blocked callback delivery, internal/client message masks and two
  concurrent capture requests. Optional `width height` tests one size.
* `a3_camera_wrapper_test`: production wrapper with a fake module; tests both
  cameras' memory/callback isolation, HDR/video transitions, failure rollback at
  all three stages, late NV21 suppression and repeated release/close.
* `a3_stock_hdr_probe`: synthetic bracket through the actual SuperShot ABI.
* `a3_stock_jpeg_test [width height]`: synthetic NV21 through the OEM encoder;
  parse the output JPEG dimensions with libjpeg (parsing only).
* `a3_camera_blob_load_test`: load-only ELF compatibility; optional `--module`
  additionally queries the camera count. It does not call Camera.open.
* `CameraEncoderTest.java`: query the 36 camcorder/time-lapse profiles and encode
  synthetic video through Qualcomm AVC at all native video dimensions, checking
  MP4 dimensions and frame count. No sensor or microphone is opened.
* `CameraCaptureTest.java`: optional real sensor diagnostic with offscreen EGL.
  Do not run it without authorization for actual captures.

On the installed stock stack, the native NV21/SuperShot/OEM-JPEG test passed
all 15 rear picture sizes. The complete lifecycle suite passed, including
cancellation, missing/invalid frames and recovery. The Quram software fallback
also produced a valid 2448x3264 JPEG when the board factory rejected that
portrait dimension. The profile validator passed all 36 profiles; the separate
synthetic AVC test passed all 10 native video dimensions. These are synthetic
checks, not sensor capture checks.

The OEM processing binaries and their native call sequence are restored. This
is **not a claim of demonstrated bit-identical stock photographs**. Real normal,
HDR and video capture on this final stack and an illuminated A/B comparison with
stock have not been performed at the owner's request. The HDR EXIF source port
is checked separately against the original CSG3
ExifManager, as described above. Samsung Camera's separate beauty/panorama/other
shot-mode SDKs are not ported.
No system/framework source changes are used.

## Camera audit: kernel and SELinux

The A3 camera power helper is shared with GPIO-I2C EEPROMs. Their `eeprom_*`
pinctrl names do not describe a sensor power state. The helper skips acquisition
for those EEPROM nodes; the GPIO-I2C/sensor owner continues to control the shared
pins as on stock. Genuine sensor pinctrl failures still produce errors, and a
failed state lookup now releases its managed reference. Selecting EEPROM states
in this helper was rejected after an A/B boot test: the device-core reference
keeps their GPIO10/11 mux ownership after power-down and blocks the rear sensor.
No GPIO numbers, voltage values, clocks or device-tree contents are changed.

`sepolicy/file_contexts` labels the exact legacy `libmmcamera_faceproc.so` path
as vendor_file. Otherwise this non-Treble layout resolves it to system_file,
which makes the existing mm-qcamerad vendor_file execmod rule ineffective.
No broad permission or global SELinux-mode change is needed.

The final audit also opened/released each real Camera1 device three times,
without starting preview or capturing frames. All advertised photo/preview/video
sizes and FPS ranges were accepted; rear HDR and its recording-hint exit were
accepted by the real HAL. `CameraParameterTest.java` performs only these
parameter checks, with no torch, microphone or image acquisition. The final
kernel cold-booted with correct back/90/flash and front/270 information and no
camera pinctrl or execmod errors; the crash buffer remained empty. The AVC/MP4
synthetic test passed all ten dimensions after that boot. Synthetic MP4 files in
shell_data_file can generate read AVCs from metadata inspection services on this
permissive engineering build; no broader production policy is added for those
diagnostic files. Actual sensor image/video quality remains outside these tests.

## Preview stride compatibility

CSG3 `libmmcamera_interface.so::mm_stream_calc_offset_preview` aligns NV12/NV21
plane strides to 16 pixels. The installed Adreno allocator reports 32-pixel
alignment: a 720x540 preview is allocated with stride 736, but the original ISP
layout writes stride 720. This causes vertical corruption and green chroma at
4:3 photo preview sizes even though the separate full-resolution JPEG is valid.
528, 880 and 176-pixel previews have the same problem.

`fix_preview_alignment.py` changes four Thumb instruction immediates in the
verified CSG3 library, aligning both Y and UV widths to 32. The format switch,
height alignment, offsets, total-length calculation, snapshot/video functions,
ISP tuning and all advertised resolutions stay intact. The extraction script
reapplies this idempotent fix and refuses unknown donors. `stock-firmware.json`
keeps the original hash plus the installed hash and fix description.

Build `a3_preview_layout_test` and run it explicitly on the device to compare
all 16 rear/front preview dimensions in NV21 and YV12 with actual gralloc
allocations, without opening sensors. `--baseline` reports original mismatches
without failing. This diagnostic is not included in product images.
