# A3LTE vibration amplitude

The Samsung AIDL service was running and advertised amplitude control, but its
`setAmplitude(float)` still used the HIDL byte formula `(amplitude-1)*10000/254`.
AIDL supplies `(0,1]`. On the phone, 0.25 incorrectly wrote 10000, while 1.0 was
rejected and left the previous intensity unchanged. `perform()` also supplied
byte amplitudes, so fixing only the public setter would break effects.

The a3lte adapter reuses the existing Samsung implementation at build time. It
normalizes amplitude to the driver's 0..10000 scale, validates finite/range
inputs, propagates write errors and uses normalized LIGHT/MEDIUM/STRONG effects
(2500/5000/10000). Existing timed fallback effect durations and callbacks remain
inherited; this change does not add hardware-generated waveforms. Unsupported
external audio control capabilities are no longer advertised.

The service starts at 5000. The device framework overlay sets
`config_defaultVibrationAmplitude=128` instead of 255 for default-amplitude
one-shots/waveforms. Android's existing default intensity remains MEDIUM;
explicit maximum amplitudes and STRONG effects still reach 10000. Driver scale
is not a claim of linear perceived strength or electrical power.

User controls: Settings > Accessibility > Vibration, separately for touch,
notifications and ringing. The fixed HAL makes the levels effective.

Build `m android.hardware.vibrator-service.a3lte a3_vibrator_test` and the
framework resource RROs. The new module replaces the inherited Samsung service;
its own init/VINTF files and existing vibrator SELinux domain are used.

`a3_vibrator_test --baseline` reproduces the original conversion bug without
activating the motor; use it only with the old HAL. Without arguments it checks
real AIDL/sysfs amplitude values, invalid inputs, three 10 ms click strengths,
and a 40 ms timed activation, then leaves the motor off at intensity 5000.
The diagnostic is not in PRODUCT_PACKAGES.

On 2026-10-19 the patched binary was copied to the already installed Samsung
service path, and only vendor.vibrator-default was restarted. No phone reboot,
framework restart or user data reset was performed. Real Android Settings
levels 1/2/3 produced 2500/5000/10000 for CLICK, and the original level 2 was
restored. Diagnostics and the previous binary are under
`out/vibrator-diagnostics/2026-10-19/`.

The framework default amplitude is cached in system_server. Its new value is
part of the next ROM/resource-overlay build, not applied to the current running
framework. The new HAL and working strength selection are already active;
legacy default-amplitude one-shots on the current ROM still resolve to 255.
