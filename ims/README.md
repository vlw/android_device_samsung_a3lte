# SM-A300FU IMS integration

This tree supplies an Android 12 ImsService/MmTel implementation, based on the
open-source Phh IMS Samsung fork. Source, license and local port details are
in UPSTREAM.md. It uses the existing Samsung RIL, USIM and modem; it does not
install the Android 6 Java framework or replace modem firmware.

## Integration

- device.mk inherits ims.mk and includes A3Ims and its framework, Telephony and
  CarrierConfig overlays. IMS permissions are installed in the system image.
- The framework advertises IMS/VoLTE support and Telephony selects me.phh.ims.
- Carrier ID 568 / MCC 257 enables VoLTE for the tested A1 Belarus SIM. Numeric
  carrier ID avoids AAPT converting an MNC attribute of 01 into integer 1.
- apns-custom.xml adds a separate IMS APN through CUSTOM_APNS_FILE, preserving
  the distribution's Internet and MMS APNs. Existing user APNs are not reset.
- The device-owned common libril translates non-persistent IMS profile IDs to
  legacy logical profile 2 and converts the Samsung SIM authentication wire
  format (Base64 <-> hex). Its pure C++ regression checks are in the common
  device tree under ril/tests.
- LegacySamsungPcscf reads the network-provided P-CSCF addresses published by
  the stock v6 data-call RIL, matching the active rmnet interface. It never
  supplies a hardcoded P-CSCF. Newer LinkProperties addresses take precedence.
- Registration waits for cellular service, handles delayed P-CSCF properties,
  performs USIM AKA and establishes transport IPsec before authenticated SIP
  registration. DNS fallback is bound to the IMS network.
- Voice capabilities follow registration and the Android per-technology voice
  setting. Unregistered/stale transports fall back to circuit-switched calling.
  Normal IMS does not advertise emergency MMTEL. VoWiFi, video calling, RCS and
  IMS UT are not enabled by this integration.

## Device verification

On SM-A300FU / Android 12 / A1 Belarus (25701), the network returned REGISTER
401, USIM AKA completed, IPsec was established and REGISTER returned 200 OK.
The Android ImsMmTelManager API reported registrationState=2 (REGISTERED),
LTE voice=true and SMS=true. Registration also recovered after an airplane-mode
radio cycle. Turning the Android VoLTE setting off removed LTE voice
availability; restoring the original setting restored it. No calls, SMS sends
or microphone captures were performed. Voice
media and actual SMS delivery therefore remain unverified.

Device diagnostics are explicit tools and are not included in production
packages: tests/LegacyPcscfTest.java checks synthetic address/interface cases;
tests/ImsRegistrationProbe.java checks Android registration and capabilities
without initiating communication. Compile with the Android 32 system SDK,
convert with D8 and run via app_process. LegacyPcscfTest additionally needs the
built A3Ims APK on CLASSPATH. ImsRegistrationProbe takes the active subscription
ID as its sole argument; do not assume that subscription IDs are SIM-slot IDs.

Passing `toggle` as a second ImsRegistrationProbe argument temporarily disables
and restores the existing VoLTE setting, checking the resulting voice capability.
The default invocation is read-only. The probe waits for registration to complete
before asserting availability.

## SMS and USSD audit

- SMS-SUBMIT gets an allocated TP-MR which is returned to Android for delivery
  report matching; retries preserve the reference. Android request completion is
  guarded against a socket exception racing the RP timeout.
- RP-DATA containing SMS-STATUS-REPORT is delivered through the status-report
  callback. An RP-ACK only completes submission and never fabricates a report.
- Inbound RP references are stored with the Android receive token; ACK/ERROR
  echoes the network RP reference, independently of TP-MR. Reports are also
  acknowledged. Each acknowledgement is a new SIP MESSAGE with a fresh CSeq.
- Transport shutdown fails pending submissions; missing transport requests CS
  fallback. Truncated RP packets are rejected instead of escaping the parser.
- Carrier overlay sets carrier_ussd_method_int=2 (CS only) and disables IMS UT.
  Android 12 ImsPhoneMmiCode then requests CS_FALLBACK, including requests from
  TelephonyManager.sendUssdRequest. No unsupported SIP USSD session is started.
  This requires operator CS coverage; it does not implement IMS USSD for LTE-only
  networks. Call forwarding/barring UT provisioning remains outside this port.
- tests/ImsSmsProtocolTest.java exercises synthetic RP/SIP frames, report routing,
  stored RP-reference acknowledgement, negative acknowledgement, fresh CSeq,
  malformed frames and Android adapter fallback. Run like LegacyPcscfTest with
  A3Ims APK on CLASSPATH. Its output writer is memory-only and it accesses no SIM.
  These tests cannot establish operator SMS acceptance or end-to-end delivery.

## VoWiFi boundary

The imported SIP stack contains IWLAN transport/policy code, but this product
currently lacks an enabled AP-assisted IWLAN/ePDG data path and a qualified
networks selection service. Merely adding the existing AOSP Iwlan package would
not supply transport selection, carrier entitlement/provisioning or validate
handover on this legacy radio HAL. VoWiFi therefore remains disabled for A1 BY;
a future compatible SIM is not by itself sufficient to make it work. Completing
this requires device-side IWLAN/QNS integration and a SIM/operator with WFC for
AKA, tunnel establishment, registration and handover tests. No WFC readiness is
claimed by this port.

## EVS

The stock Android 6 software backend supports the EVS Primary rate range and
NB/WB/SWB/FB bandwidths, all nine AMR-WB IO modes, DTX/SID, CMR and Compact /
Header-Full RTP. Per-session constraints follow SDP negotiation and renegotiation;
AMR-WB/AMR fallbacks retain Android MediaCodec and honor mode-set. See EVS.md
for the verified mode matrix, protocol behavior, build/provenance and limits.
There has been no live-call or operator-interoperability validation.
