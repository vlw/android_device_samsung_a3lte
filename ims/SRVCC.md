# Legacy Samsung SRVCC integration

This integration prepares mobile-originated and mobile-terminated IMS voice calls
for network-controlled PS-to-CS handover on SM-A300FU. It does not replace modem
firmware, force a RAT change, originate a replacement CS call, or modify Android
framework sources. A successful local build or call-config response alone is not
proof of an operator SRVCC handover.

## Call configuration

`LegacySamsungSrvcc` observes the active subscription and sends snapshots to the
UID-checked `a3ims-radio` socket on dialog/media state changes, with a polling
backup. Each SIP dialog retains its CP ID across order,
active/held and call-waiting changes. The bounded protocol carries direction,
state, multiparty and a dialable ASCII number; restricted/non-dialable identities
remain empty. Ended dialogs are removed, including the final empty table. Active
tables are periodically resent for RIL-process recovery; empty tables do not
periodically wake the modem.

The native bridge validates count, IDs, state, number length, number characters
and frame length before calling the vendor `IpcRxCallList` parser. The stock voice
LTE wire type 0x105 becomes internal `CallType=3` (IMS voice), not plain voice 1.
The parser constructs the vendor-owned CallList/CallInfo/CallDetails objects.

A dedicated Handler on the vendor EventScheduler assigns the parsed table to
CallManager's `mVoLteCallList`, then queues its normal internal request 1005/event
164 with `SrvccState(4)`. `DoSetVoLteCallList` handles IPC and, with
`CscFeature_RIL_SetVolteCalllistToQcCp`, Qualcomm synchronization. Release 9 uses
IPC preparation plus QMI voice-cache updates at completion; the separate QMI
SRVCC call-config request belongs to the release-10+ branch and is not used here. The CP
and AP tables are frozen while a handover is pending. Parser-created AP entries
have no Android CS index (-1, wire index 255); the adapter filters these metadata
entries from GET_CURRENT_CALLS responses so ImsPhone remains their sole owner.
Adopted CS entries retain their real modem indices and are forwarded normally.

There is no exported setter for the internal table. The narrow private ABI is
therefore explicitly restricted to the shipped RIL SHA-256:
`3689eee0f15aa3e5ff264ab220e5eeea07c767273c0c2a72d2a8bacd1ff0157c`.
The one CallManager member offset (172), Handler (16 bytes), Message (32 bytes)
and SrvccState (12 bytes) were checked against that blob. An instruction check
also verifies the table load in exported DoSetVoLteCallList. Unknown blobs disable
the SRVCC bridge; existing registration and ordinary RIL operations continue.
SecRil may still be null when RIL_Init returns on an asynchronous restart path.
Initialization therefore resolves the ABI first and attaches the Handler/CP
observer only after the SIM, radio state, SecRil and ImsManager are ready. A
metadata submission before readiness is rejected for the AP's normal retry.
No executable code or existing vendor vtable is patched. Only the new Handler
owns an adapter vtable; all vendor objects keep their stock constructors and
ownership. Table mutation and modem requests run on the original scheduler.

`CscFeature_RIL_SrvccRel` is an integer, now 9 rather than the invalid text TRUE.
This conservatively selects the legacy active-voice path. This port does not
claim release-10/11 alerting SRVCC, emergency SRVCC, conference continuity,
reverse SRVCC or an EVS codec carried into the CS domain. The modem/network
negotiate the target CS speech codec.

## Handover lifecycle

Android already receives the vendor STARTED/COMPLETED notifications and migrates
ImsPhoneConnection objects into GsmCdmaCallTracker. The app observes those real
subscription-scoped notifications through TelephonyCallback. The adapter also
observes the original CP indication and forwards failure/cancel, which this
legacy CallManager otherwise omits from its Android unsolicited path.

- STARTED freezes table updates and retains the SIP dialog/media.
- COMPLETED retires AP media generations and closes RTP sockets without sending
  BYE/CANCEL or callSessionTerminated. Releasing this app's communication audio
  mode leaves Telecom in charge of the CS call. Stale media workers cannot reset
  audio mode afterwards.
- FAILED/CANCELED retain IMS media. A genuine peer BYE deferred during transfer
  is then processed normally. A 10-second start timeout retains the session;
  timeout never fabricates successful handover. It also releases the native
  table freeze. A late real completion is accepted only for the original dialog
  identities, so a newer call is not retired.
- IMS-PDN loss during a call has a 12-second cleanup grace so bearer teardown
  cannot destroy the session before its SRVCC outcome reaches Android. Cleanup
  for an old network is ignored if a replacement network has arrived.
- A late BYE for a successfully transferred dialog is acknowledged without
  touching a later call or notifying Android that the CS call ended. Closing
  handed-over IMS sessions also releases their feature listeners; an unknown
  explicit Call-ID never falls back to an unrelated incoming listener.

## Validation

Host protocol/lifecycle checks (no network/SIM access):

```sh
javac --release 8 -d /tmp/a3-srvcc-tests \
  ims/src/me/phh/ims/SamsungSrvccCallList.java \
  ims/src/me/phh/ims/SrvccTransferState.java ims/tests/SrvccProtocolTest.java
java -cp /tmp/a3-srvcc-tests SrvccProtocolTest /tmp/a3-srvcc-frame.bin
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  ims/tests/srvcc_protocol_test.cpp -o /tmp/a3-srvcc-protocol-test
/tmp/a3-srvcc-protocol-test /tmp/a3-srvcc-frame.bin
```

`m A3Ims liba3ims_radio a3_srvcc_probe` builds the integration and an explicit
parser diagnostic. The probe is not in PRODUCT_PACKAGES and never invokes
RIL_Init or any modem operation. After pushing it to /data/local/tmp, run with
`LD_PRELOAD=/system/lib/libcutils.so LD_LIBRARY_PATH=/vendor/lib:/system/lib`.
It checks CSC enablement and all six IPC states against the actual stock parser,
including the internal voice type, CP ID, direction and number. The C++ host test
also covers the production native freeze logic, duplicate starts, bounded timeout
and late real failure/cancel. The Java lifecycle test covers late completion only
for the original dialog identities.

End-to-end acceptance requires a real operator-triggered LTE-to-UMTS/GSM handover
during an established call: STARTED then COMPLETED, a matching adopted CS call,
continued bidirectional speech, and subsequent hangup through the CS tracker.
Changing preferred network mode or merely losing LTE is not that test.

On 2026-10-20 the installed bridge survived reboot, reported CSC support=1,
QcCallList=1, release=9, and restored IMS registration with LTE voice available.
An authorized outgoing call connected with EVS-SWB, remained in LTE for the test,
and ended normally (337 uplink / 334 received RTP packets). CP IPC traces showed
call-table states dialing (3), alerting (4), active (1), then count=0 after hangup.
This verifies live table synchronization and the VoLTE regression, not an actual
network-triggered SRVCC transfer. No such transfer was observed in that test.

The final adapter was also checked across a separate RIL-daemon restart. Vendor
SecRil initialization is asynchronous on this path: symbol/ABI preparation happens
at RIL_Init, while handler attachment waits for the radio and CallManager to be
ready. An outgoing regression call after that restart attached the observer,
connected with EVS-SWB, and sent dialing/alerting/active/empty tables without a
native or Java crash. These checks do not exercise the operator's handover trigger
or establish CS audio continuity.
