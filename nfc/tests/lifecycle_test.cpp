// SPDX-License-Identifier: Apache-2.0
// Exercises the production wrapper with a fake module. No NFC hardware is used.
#include "Nfc.h"
#include <cstdio>
#include <cstdlib>
using namespace android;
using namespace android::hardware;
using namespace android::hardware::nfc::V1_0;
using namespace android::hardware::nfc::V1_0::implementation;
namespace {
int opens, closes, writes, openResult;
nfc_stack_callback_t* event;
nfc_stack_data_callback_t* data;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); std::abort(); } } while (0)
struct Callback : INfcClientCallback {
    int events = 0, packets = 0;
    Return<void> sendEvent(NfcEvent, NfcStatus) override { ++events; return Void(); }
    Return<void> sendData(const hidl_vec<uint8_t>&) override { ++packets; return Void(); }
};
int fakeOpen(const nfc_nci_device_t*, nfc_stack_callback_t* e, nfc_stack_data_callback_t* d) {
    ++opens; event = e; data = d; return openResult;
}
int fakeClose(const nfc_nci_device_t*) {
    ++closes; event(HAL_NFC_CLOSE_CPLT_EVT, HAL_NFC_STATUS_OK); return 0;
}
int fakeWrite(const nfc_nci_device_t*, uint16_t len, const uint8_t*) { ++writes; return len; }
}
int main() {
    nfc_nci_device_t device{};
    device.open = fakeOpen; device.close = fakeClose; device.write = fakeWrite;
    sp<Nfc> service = new Nfc(&device);
    sp<Callback> first = new Callback(), second = new Callback();
    CHECK(NfcStatus(service->open(first)) == NfcStatus::OK);
    CHECK(NfcStatus(service->open(second)) == NfcStatus::FAILED);
    CHECK(opens == 1);
    uint8_t resetRsp[] = {0x40, 0, 3, 0, 0x10, 1};
    data(sizeof(resetRsp), resetRsp);
    CHECK(first->events == 1 && first->packets == 0 && second->events == 0);
    event(HAL_NFC_OPEN_CPLT_EVT, HAL_NFC_STATUS_OK);
    CHECK(first->events == 1); // Real duplicate completion must be suppressed.
    hidl_vec<uint8_t> resetCmd;
    resetCmd = std::vector<uint8_t>{0x20, 0, 1, 0};
    CHECK(uint32_t(service->write(resetCmd)) == 4);
    CHECK(first->packets == 1 && writes == 0); // Cached reset response replayed.
    hidl_vec<uint8_t> empty;
    CHECK(uint32_t(service->write(empty)) == 0 && writes == 0);
    CHECK(NfcStatus(service->close()) == NfcStatus::OK && closes == 1);
    CHECK(NfcStatus(service->close()) == NfcStatus::FAILED && closes == 1);
    CHECK(NfcStatus(service->open(second)) == NfcStatus::OK && opens == 2);
    service->serviceDied(1, nullptr); // Stale death from the first owner.
    CHECK(closes == 1);
    service->serviceDied(2, nullptr);
    CHECK(closes == 2);
    CHECK(uint32_t(service->write(resetCmd)) == 0 && writes == 0);
    openResult = -1;
    CHECK(NfcStatus(service->open(first)) == NfcStatus::FAILED && opens == 3);
    openResult = 0;
    CHECK(NfcStatus(service->open(second)) == NfcStatus::OK && opens == 4);
    CHECK(NfcStatus(service->close()) == NfcStatus::OK && closes == 3);
    std::puts("PASS overlapping open, reset replay, duplicate completion, stale death, close, failed-open retry; no hardware");
}
