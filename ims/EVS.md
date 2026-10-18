# EVS Primary SWB on SM-A300FU

## Source and ABI

- Donor: SM-A300FU A300FUXXU1CSG3 Android 6.0.1, /system/lib/libevs_float.so.
- Unmodified ARM32 blob: vendor/samsung/a3lte/proprietary/lib/libevs_float.so.
- SHA-256: `3c599e83bca7ca132cb058341f821a421e53d0e283611522d4e4fe0ea97f3444`.
- The Samsung wrapper exports sEVSCreateEnc/sEVSEncFrame/sEVSDeleteEnc and the
  corresponding Dec functions. Native/StockEvs.h describes the small parameter
  structs recovered from those entry points. Encoder/decoder state is opaque and
  allocated/freed only by the stock library. Integer JNI handles are validated;
  codec operations are serialized because the legacy codec has shared globals.
- The legacy ARM __aeabi_uldivmod import resolves from runtime bionic libc,
  although absent from the build-time stub surface. The prebuilt explicitly
  allows this import; native loading and encode/decode are tested on the device.
- The original Android 6 IMS service/media framework is not installed. Modem
  firmware, DSP firmware, RIL and audio HAL are not changed for EVS.

## Supported profile

The local profile is EVS Primary 13.2 kbit/s SWB, mono, 20 ms, 32000 Hz PCM,
640 samples per frame. Its RTP clock is 16000 Hz, with timestamp increments of
320. Frame Type 4 has 264 speech bits; the stock storage frame is a ToC byte
followed by 33 speech bytes.

SDP offers EVS first, followed by existing AMR-WB and AMR fallbacks. Native
loading/encode/decode must pass a synthetic self-check before EVS is advertised.
The negotiated profile restricts br=13.2, bw=swb, hf-only=1, evs-mode-switch=0,
dtx=0, dtx-recv=0, cmr=-1, max-red=0, ptime/maxptime=20. Incoming broader primary
mode offers can be narrowed to this supported profile. Answers with unsupported
rates, bandwidth, DTX, mode switching or packet formats are not accepted as EVS.
A peer requiring CMR in every packet is not selected for this implementation.

Header-full RTP contains ToC + speech; cmr=-1 disables the CMR byte. This port
advertises no aggregation, redundancy, AMR-WB IO or variable-rate operation.
RTP parsing validates header bounds, extensions, padding and the single-frame
mode before passing speech to native code. Standalone EVS NO_DATA packets are
not sent during the existing pre-answer/microphone-settle pacer.

The PCM adapter retains incomplete encoder input until a full 1280-byte frame
is available. AMR still uses Android MediaCodec. Android call media extras report
AUDIO_QUALITY_EVS_SWB. DTMF duration/timestamp allocation uses RTP clock frequency,
independently of PCM sample rate. Initial answers, UPDATE, re-INVITE and hold SDP
preserve the local EVS constraints.

## Verification without calls

Build A3Ims and libevs_float. Compile tests/EvsCodecTest.java against Android 32
system SDK, convert with D8 and run with the installed A3Ims package available:

```
CLASSPATH=/data/local/tmp/evs-codec-test.dex app_process /system/bin EvsCodecTest
```

The test uses the installed app's ClassLoader/native namespace. It checks native
loading, SDP acceptance/rejection and AMR-WB fallback, 250 synthetic 20 ms frames
through PCM -> EVS -> RTP -> EVS -> PCM, energy at 10 kHz after decoding,
malformed/unsupported RTP rejection, fragmented PCM buffering, factory encoder/
decoder selection, Android AMR-NB/AMR-WB encoder operation and DTMF timestamps.
No microphone, speaker, socket or telephony request is opened. This confirms
software SWB processing, not operator interoperability or acoustic bandwidth of
the phone's live capture/output routes. No real calls have been made.

Protocol reference: [3GPP TS 26.445 Annex A (ETSI)](https://www.etsi.org/deliver/etsi_ts/126400_126499/126445/13.10.01_60/ts_126445v131001p.pdf).
