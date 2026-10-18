# EVS on SM-A300FU

## Stock backend

The unmodified ARM32 library comes from SM-A300FU A300FUXXU1CSG3 Android 6.0.1
`/system/lib/libevs_float.so`. The vendor prebuilt is
`vendor/samsung/a3lte/proprietary/lib/libevs_float.so`, SHA-256 `3c599e83bca7ca132cb058341f821a421e53d0e283611522d4e4fe0ea97f3444`.
The application embeds this library and its device-owned JNI bridge. Android 6
IMS framework/services, modem firmware and DSP firmware are not installed.

`native/StockEvs.h` records the wrapper ABI recovered from the Samsung exports
sEVSCreateEnc/sEVSEncFrame/sEVSDeleteEnc and corresponding decoder functions.
State remains opaque and is allocated/freed by the stock library. JNI validates
integer handles, PCM/frame sizes, ToC and encoder configuration. Calls are
serialized because the old library contains shared arithmetic state.

The native codec uses 20 ms PCM frames. Production EVS sessions use 48000 Hz
mono PCM to support all negotiated bandwidths, while RTP always uses 16000 Hz
and a timestamp increment of 320. Lower PCM sample rates remain available to
synthetic diagnostics. The stock encoder automatically detects actual input
bandwidth within the configured maximum; a sparse test tone is not a reliable
measurement of the full encoded bandwidth.

## Supported modes and negotiation

- Primary: 5.9 kbit/s SC-VBR and 7.2, 8, 9.6, 13.2, 16.4, 24.4, 32, 48, 64,
  96, 128 kbit/s, limited to valid NB/WB/SWB/FB combinations.
- AMR-WB IO: 6.6, 8.85, 12.65, 14.25, 15.85, 18.25, 19.85, 23.05, 23.85
  kbit/s and negotiated mode-set/period/neighbor constraints.
- DTX, Primary SID, IO SID, NO_DATA, bad-frame/lost-frame concealment and codec
  comfort-noise synthesis during gaps. Consumed concealment timestamps advance
  the media clock so late RTP cannot insert the same loss twice.
- Channel-aware transmit mode at 13.2 kbit/s WB/SWB, offsets 2/3/5/7 and LO/HI.
  Channel-aware frames can be decoded normally. The playout path does not claim
  tested recovery of past losses from future partial-redundancy frames; no
  adaptive RF request algorithm or complete stock VoIP jitter buffer is ported.

`EvsSwbProfile` retains its original class name but now parses full mono EVS
constraints. It negotiates br/bw and their directional variants, preserves
immutable hf-only/dtx/evs-mode-switch values, mirrors send/receive constraints
into answers, and checks compatible rate/bandwidth sets. Unsupported/malformed
payloads do not become implicit AMR sessions. Payload IDs come only from the
active audio m-line and are tied to the selected fmtp.

`EvsSession` holds receive/transmit constraints per call and applies network CMR
requests within those constraints. CMR can change bitrate, bandwidth, Primary /
IO mode or channel-aware offset/level. Reserved or out-of-session requests are
ignored. No process-global codec configuration or hardcoded operator profile
is used. Initial offers allow the full locally verified primary range and all
IO modes; the network can select either initial mode or packet format.

## RTP and fallback

`EvsRtpPayload` implements default Compact / Header-Full and Header-Full-only
handling. Compact IO rotates d(0) behind d(K-1); its CMR is mapped independently
of the speech mode. Header-Full supports optional/required CMR, up to twelve
frames (240 ms), SID/NO_DATA, and size-collision zero padding. Parsing validates
RTP version, payload ID, CSRCs, extensions, RTP padding, ToCs, lengths and
negotiated receive modes before native decoding. Duplicate/reordered media is
not replayed. Standalone NO_DATA is not transmitted, and DTX gaps retain RTP
timestamp continuity and talkspurt markers.

The existing Android AMR MediaCodec backend is retained. AMR-WB and AMR bitrates
follow the accepted mode-set. Both bandwidth-efficient and single-frame
octet-aligned RTP are supported; CRC, robust sorting and interleaving are not
advertised. UPDATE/re-INVITE can renegotiate EVS parameters or move between EVS,
AMR-WB and AMR. The selected codec is stored in the call and changed media restarts
with the established-call state preserved. Generation checks prevent a delayed
restart from reviving a superseded or terminated media session. Audio quality
extras cover EVS NB/WB/SWB/FB and DTMF uses the RTP clock rather than PCM rate.

## Offline verification

Build A3Ims and libevs_float. Compile tests/EvsModesTest.java (optionally also the
legacy EvsCodecTest.java entry point) against Android 32 system SDK, convert
with D8, and run:

```
CLASSPATH=/data/local/tmp/evs-modes-test.dex app_process /system/bin EvsModesTest
```

The default uses the installed application's ClassLoader/native namespace. For
isolated verification before installation, pass the APK path and a directory
containing its extracted ARM32 JNI dependencies as two arguments. The test has
no AudioRecord, AudioTrack, socket, SMS, USSD or call operation.

Coverage includes all 88 valid mode/bandwidth/DTX cases, codec state transitions,
SID/NO_DATA, RF offsets/levels, all Compact/Header-Full configurations, independent
Compact IO bit-order/CMR fixtures, aggregated frames, header bounds/padding/truncation,
random malformed RTP, directional SDP, invalid parameters, constrained CMR, PCM
fragmentation, codec PLC, duplicate/loss timestamps, actual AMR encoder bitrates
and octet-aligned payloads, plus UPDATE/re-INVITE fallback selection. Spectral
checks measure upper-band energy for SWB and FB instead of demanding preservation
of one sinusoidal FFT bin. Synthetic results establish software behavior, not
operator acceptance or acoustic capture/output bandwidth. No live calls have
been used to validate this integration.

Protocol reference: [3GPP TS 26.445 Annex A (ETSI)](https://www.etsi.org/deliver/etsi_ts/126400_126499/126445/13.10.01_60/ts_126445v131001p.pdf).

After installing a previously tested APK, pass `--smoke` for a short native
namespace/loading and Primary SWB/FB/SC-VBR/AMR-WB IO encode/decode check instead
of repeating the full matrix.
