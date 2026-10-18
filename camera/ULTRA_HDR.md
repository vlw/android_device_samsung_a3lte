# Ultra HDR feasibility on SM-A300FU / LOS19

## Conclusion

Writing interoperable gain-map JPEGs is feasible on the current ARM32 Android
12.1 userspace without changing framework/system sources. A working capture
feature is **not implemented** yet. The remaining work is image reconstruction
and preservation through the camera application's save path, not the JPEG
container or a requirement for a 10-bit JPEG encoder.

## Verified codec and container prototype

Reference codec: https://github.com/google/libultrahdr
Revision: `47d9f00726adba87e3f04059a50a8b34492e057a` (v2.0.2).
Built on the host and also cross-compiled as an isolated ARM32 shared library
and CLI against this tree's bionic/libc++/libjpeg. All 49 required source files
compiled with C++17; optional GLES, HEIF and SIMD were disabled in the ARM probe.
No reference codec library was installed into the production image or HAL.
The device probe lives under `/data/local/tmp/a3-ultrahdr-probe/`.

A **synthetic** 816x612 monochrome gain map was packaged with an actual
3264x2448 stock HDR JPEG. Both host and phone recognised the file as Ultra HDR.
The host verified identical JPEG image-coding bytes and byte-identical EXIF,
and legacy JPEG decoding produced identical SDR pixels. The ARM-produced file
also decoded to identical SDR pixels. These tests demonstrate container and
codec compatibility, **not a captured scene's HDR reconstruction or quality**.

A separate synthetic full-resolution HDR-intent / compressed-SDR encode on the
phone, with gain-map scale 4, single channel and real-time preset, took 2.054 s
and peaked at 190560 KiB RSS. HDR decoding to RGBA F16 took 6.393 s and peaked
at 131908 KiB. These are single-run scalar-CPU measurements, not capture latency
or an optimised production pipeline. The upstream codec's luminance conventions
and metadata still have to be calibrated to actual camera inputs.

Private artifacts and logs:
`out/camera-diagnostics/2026-10-19/ultrahdr/`.
All files named `*test-only.jpg` and `arm32-generated-map-test-only.jpg` use
synthetic HDR data/maps; they must not be presented as captured Ultra HDR photos.

## Camera input and RAW

Rear native HDR already supplies three processed 8-bit NV21 images from one
`-2,0,2` exposure bracket. `StockHdr::fuse` feeds them to SuperShot and requests
an 8-bit NV21 output, followed by the stock JPEG encoder. The public path does
not return a linear HDR master, registration transforms or a recovery map.
Simply expanding the fused SDR pixels to 10/16 bits cannot recover that data.

Sensor traffic is Bayer: the stock sensor/ISP log reported a 3280x2458 Bayer
input. That is distinct from application-accessible RAW. The HAL advertises
Qualcomm/MIPI Bayer formats, but direct rear captures of `bayer-mipi-10rggb`
and `bayer-mipi-10bggr` returned zero data callbacks. The observed raw-channel
start failed in ISP crop/zoom setup (`VFE0 zoom error = -1`, followed by
`takePicture: cannot start raw channel`). Thus RAW delivery has not been
verified and cannot yet be a prerequisite for the implementation.

`a3_raw_capture_test CAMERA_ID PICTURE_FORMAT OUTPUT_PREFIX` is an explicit
sensor-opening diagnostic, excluded from product images. It configures a
single Bayer capture through the unwrapped HAL and saves callback bytes if
any arrive. Buffer delivery alone would still require packing, bit-depth,
Bayer pattern, stride, optical-black and calibration validation. This probe
was compiled using the generated Android compile/link rules into isolated
`/tmp` outputs; the first normal build attempt encountered an unrelated
concurrent duplicate recovery-policy module definition.

RAW is not required by the Ultra HDR container. A multi-exposure NV21-based
prototype is possible, but accurate inversion of ISP tone curves, actual
exposure ratios, alignment and ghost suppression must be established. A RAW
route would additionally require demosaic, black/white levels, lens shading,
white balance and colour calibration; RAW10 is not automatically an HDR master.

## Application save path is a real blocker

The installed `com.android.camera2` APK has SHA-256
`60d2052bbd632d7381c78c014f0de1d97c722bf84d240f1ce8781b800d2d2867`.
Both its DEX and the matching source show:

- `PhotoModule.JpegPictureCallback` forwards JPEG data to `MediaSaverImpl`.
- `Storage.addImage` decodes it using `BitmapFactory.decodeByteArray`.
- `Storage.writeBitmap` calls `ExifInterface.writeExif(Bitmap, OutputStream)`.
- That method recompresses the bitmap as JPEG quality 90.

This destroys the secondary image and gain-map association. A valid HAL JPEG
therefore does not imply a valid file in DCIM. Changing only EXIF metadata
handling is insufficient while the save path still recompresses pixels.

Respect the device/vendor/kernel-only constraint by supplying a device-owned
camera application build/fork with byte-preserving JPEG saving. Do not edit
`packages/apps/Camera2` or framework sources. Preserve auxiliary-image metadata
and MPF offsets, EXIF, orientation and all JPEG data. Crop/rotate/edit/share
paths must either handle both images or explicitly produce an SDR derivative.
A full app-save/reopen/export test is an acceptance requirement.

## Android version and display

Native framework `JPEG_R` capture integration starts in Android 14:
https://source.android.com/docs/core/camera/ultra-hdr
The format remains a backwards-compatible JPEG, so our Camera1 HAL can return
its bytes as JPEG on Android 12.1; this does not backport framework gain-map
rendering or make `JPEG_R` a supported Android 12 camera stream format.

The current phone display stack reports `mSupportedHdrTypes=[]`, and this
Android 12 bitmap/gallery path does not apply gain maps. SDR display on this
phone is expected. HDR presentation/brightness validation requires an external
gain-map-aware viewer with a suitable HDR display. File creation and display
support are separate requirements.

## Suggested implementation order

1. Preserve the stock SuperShot SDR image and stock JPEG/EXIF output as the
   primary image. Exif must be finalised **before** gain-map packaging so later
   insertions cannot invalidate MPF offsets.
2. Reconstruct a separate HDR intent from the rear bracket, aligned to that
   exact SDR output. Establish response curves, exposure ratios, clipping,
   motion handling and the luminance reference. Investigating SuperShot's
   internal pre-tone-map processing is another possible route, but its ABI
   and radiometric meaning have not been established.
3. Generate a calibrated, initially quarter-resolution gain map. Prefer a
   tiled/streaming or low-resolution HDR/gain-map stage plus the reference
   codec's compressed-base/compressed-map API to avoid full-resolution float
   buffer copies. Test that reduced map resolution preserves highlight detail.
4. Add an opt-in rear-photo HAL path and retain ordinary SDR fallback if
   reconstruction or packaging fails. No front HDR claim follows from this.
5. Use the device-owned byte-preserving camera app and validate saved/reopened
   files with the independent reference decoder, plus real bright/static,
   moving and low-light scenes on an HDR display.

At 3264x2448, three NV21 inputs require 34.29 MiB, one RGBA F16 buffer
60.96 MiB, and one RGB float32 buffer 91.44 MiB, before encoder, registration
and SuperShot scratch space. Memory use is a design constraint, not a claim
that a desktop-style all-float pipeline will fit safely in mediaserver.

The production camera libraries/configuration were left unchanged by this
investigation. After RAW probes the camera processes were restarted and
ordinary rear photo capture was rechecked successfully; no phone reboot.
