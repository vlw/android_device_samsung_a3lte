# Source provenance

The SIP/IMS implementation in src/me/phh is based on:

- https://github.com/phhusson/ims
- https://github.com/Samsung-Galaxy-A05/vendor_phhims
- Imported revision: 2bc8c93a4918f4b06c423406f27b4f3b29c44715 (main)
- License: GPL-2.0; see LICENSE and the SPDX notices in individual sources.

Only Java/Kotlin sources, application resources and the license were imported.
Upstream build keys, SDK jars, binaries and the unused RNNoise JNI helper are
not part of this device integration. Audio uses the platform AMR codecs.

Local adaptations include Android 12 slot/subscription and incoming-session
APIs, legacy ServiceState readiness, the bundled Kotlin version, IMS-network
DNS routing, stock v6 RIL P-CSCF properties, registration/voice-toggle
capability reporting, service cleanup/synchronization, and removal of AKA
key/digest debug logging. Integration and
carrier overlays are specific to this device. Carrier availability is not
proof of successful IMS registration or tested calls.

SMS follow-up: fix RP vs TP reference correlation, incoming status-report
routing/acknowledgement, remove synthetic reports on unmatched RP-ACK, allocate
TP-MR for Android delivery tracking, bound adapter completion to once, fail
pending SMS on teardown, reject malformed RP frames and return CS fallback on
missing transport. Device CarrierConfig keeps unsupported USSD/UT on CS.

EVS follow-up: device-owned StockEvs JNI wrapper and SipMediaCodec adapter keep
the Android AMR MediaCodec path while adding the SM-A300FU Android 6 software
EVS backend. The local profile is fixed Primary 13.2 SWB, 20 ms, header-full,
DTX/CMR disabled. SDP selection, RTP packing, renegotiation constraints and
Android audio quality reporting are extended; DTMF uses RTP clock frequency.
See EVS.md. The stock codec binary is unmodified in vendor/samsung/a3lte.
