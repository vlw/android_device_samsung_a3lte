# SM-A300FU NFC

S3FWRN5 uses the legacy Samsung module through the local HIDL wrapper.
The wrapper converts the initial CORE_RESET response into OPEN_CPLT and
replays that response for Android's reset command, including firmware checks
when NFC starts disabled.

The module owns one global transport. Overlapping opens must be rejected;
close and failed open release ownership. Client-death notifications carry an
owner generation so an old client cannot close a newer connection.

The board uses the stock 19.2 MHz PLL configuration. Android 12 expects the
nine-byte AOSP proprietary-protocol layout, without Samsung's extra CLT byte.
Clock configuration is applied by the firmware configuration path; changing
the config file alone may not update an already-programmed controller.
Keep FW_UPDATE_MODE=0 for normal operation.

`m a3_nfc_lifecycle_test` builds an optional regression test using a fake
legacy module, with no NFC hardware access. It covers overlapping opens,
reset replay, duplicate completion, stale client death, close, failed-open
retry and empty/closed writes. It is not included in PRODUCT_PACKAGES.
