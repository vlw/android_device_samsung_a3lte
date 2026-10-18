# Stock-function audit

Audited against both SM-A300FU `libtfa9895.so` versions identified in README,
and the Android 6 `audio.primary.msm8916.so` caller. The audit follows the
Samsung `tfa9895_*` entry points, their NXP runtime/transport callees and the
actual container descriptors. Matching profile traces alone does not establish
equivalence of the lifecycle, recovery or Android volume integration.

| OEM functionality | Native status / relevance |
| --- | --- |
| `speakeron`, `speakeroff`, `switchcase`, `iscoldboot`, `disablei2soutput` | Implemented by checked start/stop/profile and hardware-status paths. Warm startup restores I2S output; cold startup restores the patch/model/configuration. |
| Preset, EQ, DRC, volume step, attenuation | All 75 manual profile/step payloads match both stocks. Automatic Android volume selects the same tuning with full-volume hardware gain to avoid double attenuation; see README. Step changes reload tuning with digital mute. |
| Firmware/config/speaker model, DSP features and AGC gain insertion | Implemented for this chip and stock container, including DRC feature fallback. |
| Container device/profile registers and bitfields | Every descriptor used by this container is supported. Generic support for other chips, containers and file formats is deliberately rejected, rather than partially applied. |
| RPC ACK, RPC status, parameter transfer and signed 24-bit memory | Implemented with bounded waits and propagated errors. |
| Calibration completion and impedance readback | Normal startup waits for existing/automatic DSP calibration and checks stock's 4–9 ohm range. This does not require an Android diagnostic callback. |
| `tfaRunCheckEvents` / `ResolveIncident` | Cold/watchdog/lost-configuration recovery and protection handling implemented. Audit found a monitor deadlock after protection shutdown: CLKS disappears in powerdown. Fixed by probing with AMP/boost off, checking protection before clock readiness, and restoring the requested profile/step only with valid clocks and safe status. A persistent fault remains muted/off. |
| `tfaRunSetExternalTemp`, explicit `calibration`, `resetCalibrateState`, tCoef compensation helpers | Factory/replacement-speaker calibration path. Normal Samsung speaker startup does not call these. It can erase/rewrite MTP and override the calibration temperature; automatically invoking it would be inappropriate. Existing factory calibration and the stock speaker model are retained. |
| `tfaRunSelectPLLInput`, standalone channel/input/stereo selection and sample-rate SDK setters | Not invoked by this stock's normal startup. Its actual I2S settings come from chip initialization and container bitfields. The supplied profiles specify 48 kHz, matching this device's speaker backend. Other sample rates are converted upstream. No arbitrary clock/channel switching is needed. |
| `tfa98xx_dsp_write_tables` | Called by Android 6 stock startup but is literally `return 0` there; Android 5 does not call it. No missing table to port. |
| `after_patch_hook`, `after_calibration_hook` | No calls from the normal library path in these binaries. The hooks are not additional mandatory initialization operations. |
| State/model/config readback, register/memory dumps, version/error strings | Diagnostic/readback utilities, not required to drive sound. Runtime uses the status/impedance reads it needs and logs actionable errors. |
| DSP bypass, arbitrary direct DSP/register writes | Debug/tuning operations, not needed by ordinary Samsung playback. Bypass would remove the stock processing/protection path. |
| Multiple-device/stereo APIs | Generic SDK facilities. This container has one mono amplifier at I2C address 0x34. |
| `lxScribo`, `lxDummy`, serial/socket tools, CLI, file export, trace/timing counters | Host/debug transports and utilities. Replaced by native I2C and Android logging where applicable; no playback dependency. |

## Android integration boundary

Samsung's primary HAL consumes the private integer parameter `TFA_SETVOLUME`.
Its `NXP_set_Volume` chooses attenuation-only or preset writes, and its mode
selection also reads Samsung-specific call type/bandwidth/extra-volume state.
These are caller responsibilities, not autonomous functions inside the TFA
library.

The OEM producer was located in Android 6 `libaudiopolicymanagerdefault.so`,
`AudioPolicyManager::checkAndSetVolume`, at 0x164ce–0x164d4 and
0x1656c–0x16572: it sends the stream maximum minus its current index.
The current MSM8916 primary HAL has no such producer; its legacy amplifier
interface has route/mode and generic parameter callbacks. `A3TfaVolume` now
provides real stream-index snapshots from a device-owned system_ext component,
without framework edits. The native HAL scales the range and automatically
selects the DSP step. See README for gain ownership, mixed streams, manual
override, boot/restart handling and the distinction from Samsung's full policy.
Stock NB/WB speech tuning is identical; all explicit profiles are present.

## Verification of the recovery change

Portable tests model CLKS disappearing in powerdown even with an external clock
present. Cases cover a persistent and cleared thermal fault, latched/persistent
OCP, undervoltage with no clock, absent clocks after fault clearance, hardware
powerdown differing from cached state, watchdog/cold status, lost SBSL, and I2C
errors. Recovery retains the requested profile and volume step. No physical
overtemperature/short-circuit fault or phone call is induced on the device.
