# A3LTE TFA9895 amplifier

This is a native implementation of the SM-A300FU amplifier control path. It
does not load the proprietary Samsung/NXP library. The original device tuning,
DSP patch, speaker model, presets, EQ and DRC remain in `Tfa9895.cnt`, installed
as `/vendor/etc/Tfa9895.cnt` by the HAL's required module.

## Findings and implementation

The primary audio HAL uses the private MSM8916 sound-device enumeration. The
previous amplifier included SM8350 headers instead: voice speaker is 16 on
MSM8916 and 17 on SM8350; speaker/headphones is 11 versus 12. This prevented
correct recognition of actual routes. Keep the amplifier's headers aligned
with the primary HAL; these values are not Android audio-device bitmasks.

The former implementation also discarded the selected voice mode, used a
single captured music initialization sequence, ignored failed I2C transfers
and lacked DSP RPC completion/status checks. A short silent write did not
guarantee clocks throughout configuration. Resource ownership and shutdown were
incomplete.

The implementation now parses and validates the stock container, configures the
chip and DSP with checked transactions, selects music/voice/communication
profiles, handles overlapping speaker routes, and mutes during profile changes.
It borrows an existing clock when possible, otherwise owns a temporary silent
PCM and restores its mixer route. The monitor checks protection/reset status
while the requested route has clocks and retries configuration after DSP state
loss. Protection recovery probes a powered-down chip while muted: CLKS is not
valid in powerdown. This permits recovery after a cleared fault, including the
power cycle required by latched OCP, without creating a PCM from the monitor.
Persistent faults keep the amplifier disabled. Factory MTP calibration is not
explicitly erased or rewritten.

## Profiles and parameters

All 11 stock profiles are available: `hq`, `speechVoiceNB`,
`speechVoiceNBExtra`, `speechVoiceWB`, `speechVoiceWBExtra`, `speechVTNB`,
`speechVTWB`, `speechVoiceVOIP`, `speechVoiceVoLTE`, `speechVoiceVoLTEExtra`,
`speechVTVoLTE`. Music has 15 DSP volume steps; each speech profile has six.

The HAL accepts these vendor parameters through either parameter callback:

* `tfa_profile=auto` (default), or one of the names above.
* `tfa_vstep=auto` (default), or `0` through the selected profile's last step;
  zero is maximum. A numeric value holds a manual diagnostic override until
  `auto` is requested again.
* `extra_volume=true|false` (also `on|off` and `1|0`).

Automatic profile selection uses voice speaker routes / `AUDIO_MODE_IN_CALL` for the
normal or extra voice profile, `AUDIO_MODE_IN_COMMUNICATION` for VoIP, and music
otherwise. In manual-volume mode, profile changes reset the DSP step to zero.
The stock NB and WB profiles have identical tuning; the legacy amplifier
interface does not supply call codec bandwidth. Other profiles remain explicitly
selectable.

## Automatic Android volume

`A3TfaVolume`, a platform-signed persistent application built entirely from
`amplifier/volume`, publishes real Android stream indices through the existing
audio parameter interface. Its APK and required privileged-permission allowlist
are installed together under system_ext. There are no framework source changes,
PCM-amplitude estimates, per-track-volume hooks, periodic dumpsys polling, or
changes to the user's volume settings.

The component listens for volume/mute, playback, routing, mode and user changes,
publishes an initial snapshot at boot, and republishes after an audio-server
restart. Speaker playback uses the active non-call stream with the highest
Android speaker gain; when idle it caches media volume for the next start.
Independently routed headphone/Bluetooth playback is excluded. Voice/VoIP
profiles use a separately cached voice-call index. Routing and mode in the HAL
still determine whether the amplifier is enabled and which profile is selected.

Samsung's AudioPolicyManager sends `maxIndex - index` as `TFA_SETVOLUME`.
`tfa_volume.h` retains that mapping for the stock 15 music / 6 speech ranges and
rescales it for this ROM's configurable ranges (currently 25 media and 7 voice
indices). Music index zero remains Android mute, sharing the lowest DSP preset.

Volume changes within an already running profile update the DSP in place, without
digital mute, amplifier power cycling or the startup settling delay. Only changed
presets and EQ coefficients are sent; profile-wide DRC and unchanged filters are
retained. Repeated snapshots selecting the same step do not rewrite the DSP.
Cold start, route/profile changes and protection recovery retain their muted
configuration sequence; a failed live transfer still shuts down safely.

Automatic mode loads the selected stock preset/EQ/DRC but holds the chip volume
register at that profile's full-volume attenuation (-1 dB for music, 0 dB for
speech). Android already applies its stream gain; applying the step's additional
hardware attenuation would attenuate it twice and also change the gain of other
mixed streams. Thus the stock volume-dependent DSP tuning follows Android
volume, while the current ROM's software volume curve, mute and per-track gains
remain in control of overall loudness. This is an adaptation to Android 12, not
a claim of bit-identical acoustic loudness to Samsung's complete audio policy.
Manual numeric `tfa_vstep` retains the original complete OEM hardware attenuation.

The private snapshot parameters are `tfa_android_music=index,max` and
`tfa_android_voice=index,max`; invalid ranges are rejected. Publications do not
cancel a numeric manual override. Before the first valid snapshot the HAL keeps
its previous full-volume DSP default; snapshots are cached even while off.

This implements the operational amplifier path, not every factory diagnostic,
command-line or calibration utility exported by the proprietary SDK.
The remaining OEM functions and their relevance are audited in `STOCK_AUDIT.md`.

## Reverse-engineering provenance

The Android 5 and Android 6 stock `system/lib/libtfa9895.so` files were inspected
and their profile-writing paths executed in ARM emulation with hardware calls
replaced by trace collectors. Their generated preset/EQ/DRC/attenuation payloads
match for every one of the 75 profile/volume combinations. The native
implementation is compared against those results, not against itself.

SHA-256:

| Input | SHA-256 |
| --- | --- |
| Android 5 library | `763092a75441bfb681c90b4cb4f0000f9626e4f157ceafc47f770b568205c324` |
| Android 6 library | `8a6e9170fbb574345d08461b968778d8ff988b5ff623b15926b854ad2071c062` |
| Container, identical in both stocks | `81562ebfc4f7b74808c7563b80b4a64104d215c2b8b61d4ddacb9ba8e2167efd` |

`tests/stock_oracle.py STOCK.so Tfa9895.cnt OUT.json` regenerates the OEM
profile traces; it requires pyelftools, Capstone and Unicorn on the host and
never accesses physical hardware. The normalized expected hashes and provenance
are in `tests/stock_profiles.json`.

## Validation

Host: `python3 device/samsung/a3lte/amplifier/tests/run_tests.py` builds the
portable parser/protocol tests with AddressSanitizer and UndefinedBehaviorSanitizer.
It covers all container truncations, bad CRC/indices, I2C failures, RPC error and
timeout handling, protection faults, power loss, profile transitions, and all
75 OEM payload comparisons, plus 75 automatic-volume comparisons which differ
only in the deliberately retained full-volume hardware gain. It also checks
stock index mapping, range scaling, monotonicity and mode transitions. All 1,170
same-profile step transitions (both gain modes) are checked for equivalent final
preset/EQ/DRC/gain, no mute/power transitions, no settling sleeps, no DRC reload,
and no hardware-gain writes in automatic mode. A live RPC failure must shut down
and recover through full initialization.

After lunch, build `m audio_amplifier.msm8916 A3TfaVolume a3_tfa_hal_test a3_tfa_diag`.
The tests are explicit diagnostic targets, not production packages:

* `a3_tfa_hal_test` uses a mock TFA backend to check route numbers, mode changes,
  overlapping routes, parameters, errors, shutdown and reopen.
* `a3_tfa_diag /path/to/audio_amplifier.msm8916.so` accesses the actual chip,
  supplies digital silence, switches through all 11 profiles, then exercises
  speaker/voice-speaker overlap. Stop both `audioserver` and `vendor.audio-hal`
  first so that two implementations never access the amplifier simultaneously;
  restart both services afterward, even on test failure.
* `tests/AudioSmokeTest.java` plays a quiet tone through ordinary Android
  AudioTrack at 48 and 44.1 kHz, overlaps tracks, then stops and restarts playback.
  Compile against the public Android SDK and run its DEX using `app_process`.
* `tests/VolumeIntegrationTest.java`, run through root `app_process`, uses real
  AudioManager volume changes and volume-up/down input events during silent
  speaker playback. It checks mute, concurrent alarm/media playback, and speech
  DSP volume on the same silent media PCM without changing Android mode or
  placing a call. It restores the original media/voice indices and media mute
  state in `finally`. Bridge and native HAL logs show the index-to-DSP transitions.
  Use `--direct-volume` for AudioManager raise/lower adjustments when injected
  key events are consumed by the foreground window.

Automatic volume was verified on-device at music indices 1/8/16/25 of 25
(DSP steps 14/10/5/0), with volume buttons 8→9→8 (steps 10→9→10), mute,
alarm/media handoff, and speech indices 5→1→5 of 7 (steps 1→4→1).
Boot startup, process restart and audio-server restart republish were verified.

On the connected SM-A300FU, the host comparisons, HAL tests, actual DSP profile
switches and ordinary media test passed. The media test left PCM closed and
both audio-service PIDs unchanged. No actual phone call was placed or tested;
end-to-end call audio and long-term listening remain unverified.

The original device HAL was saved at
`/data/local/tmp/a3-tfa-original-amplifier.so` before replacement. Restore it to
`/vendor/lib/hw/audio_amplifier.msm8916.so` with mode 0644 and its original
SELinux context while both audio services are stopped, then restart them if
rollback is needed.
For a complete rollback of the automatic bridge, remove its APK directory
`/system_ext/priv-app/A3TfaVolume` and matching
`/system_ext/etc/permissions/privapp-permissions-a3-tfa.xml` together and reboot.
Keep the allowlist present whenever the privileged APK is installed.

The live-volume fix was installed and checked on 2026-10-19 without rebooting
(the kernel boot ID remained unchanged). Only the audio services were restarted
to reload the HAL. All profile/route hardware diagnostics and the direct-volume
Android integration test passed with digital silence; original indices and mute
state were restored. Injected Volume Up did not change the index in that session,
so key injection was not counted as passing. Audible continuity has not been
measured acoustically. Backup and results: `out/tfa-diagnostics/2026-10-19/live-volume/`.
