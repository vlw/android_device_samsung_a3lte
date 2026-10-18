# Source provenance

The SIP/IMS implementation in src/me/phh is based on:

- https://github.com/phhusson/ims
- https://github.com/Samsung-Galaxy-A05/vendor_phhims
- Imported revision: 2bc8c93a4918f4b06c423406f27b4f3b29c44715 (main)
- License: GPL-2.0; see LICENSE and the SPDX notices in individual sources.

Only Java/Kotlin sources, application resources and the license were imported.
Upstream build keys, SDK jars, binaries and the unused RNNoise JNI helper are
not part of this device integration. AMR audio uses the platform codecs; EVS
uses the separately imported stock backend described below.

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

EVS: the device-owned StockEvs JNI wrapper and SipMediaCodec adapter retain
Android AMR MediaCodec while adding the SM-A300FU Android 6 software backend.
Per-session Primary/IO rate, bandwidth and DTX constraints follow directional
SDP negotiation; bounded CMR adaptation configures the stock wrapper. RTP
supports Compact/Header-Full payloads and aggregation, with codec CNG/PLC.
Renegotiated media propagates into call state while preserving established-call
state during worker restart. AMR fallback honors mode-set and supports
octet-aligned payloads. EvsModesTest covers the expanded mode/protocol matrix;
EvsCodecTest forwards to that suite. See EVS.md for validation and limitations.
The stock binary is unmodified in vendor/samsung/a3lte and retains its original
proprietary license.
