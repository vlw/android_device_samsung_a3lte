// SPDX-License-Identifier: GPL-2.0
#include "../native/srvcc_protocol.h"
#include "../native/srvcc_state.h"
#include <assert.h>
#include <stdio.h>
#include <vector>
int main(int argc, char** argv) {
    SrvccTableFreeze freeze;
    assert(!freeze.onCpState(0, false, 0) && !freeze.blocksUpdates(1));
    assert(!freeze.onCpState(0, true, 100));
    assert(freeze.blocksUpdates(101));
    assert(!freeze.onCpState(0, true, 1000)); // Duplicate START cannot extend the freeze.
    assert(!freeze.blocksUpdates(10100));
    assert(freeze.onCpState(2, true, 10200)); // Real failure survives a local timeout.
    assert(!freeze.onCpState(2, true, 10201));
    assert(!freeze.onCpState(0, true, 20000) && freeze.blocksUpdates(20001));
    assert(!freeze.onCpState(1, true, 20002) && !freeze.blocksUpdates(20003));
    assert(!freeze.onCpState(0, true, 30000));
    assert(freeze.onCpState(3, true, 30001) && !freeze.blocksUpdates(30002));
    assert(argc == 2);
    FILE* f = fopen(argv[1], "rb"); assert(f);
    uint8_t frame[1024]; size_t n = fread(frame,1,sizeof(frame),f); fclose(f);
    assert(n >= 8 && validateSrvccCalls(frame+8,n-8,frame[5]));
    for (size_t length = 0; length < n-8; ++length)
        assert(!validateSrvccCalls(frame+8,length,frame[5]));
    std::vector<uint8_t> data(frame+8,frame+n);
    data[2] = 0; assert(!validateSrvccCalls(data.data(),data.size(),2));
    data[2] = data[16]; assert(!validateSrvccCalls(data.data(),data.size(),2));
    data.assign(frame+8,frame+n); data[6] = 255;
    assert(!validateSrvccCalls(data.data(),data.size(),2));
    data.assign(frame+8,frame+n); data[8] = '\0';
    assert(!validateSrvccCalls(data.data(),data.size(),2));
    assert(validateSrvccCalls(nullptr,0,0));
    assert(!validateSrvccCalls(frame+8,n-8,9));
    puts("PASS Java-to-native frame, bounds, duplicate IDs, native freeze, timeout and real outcomes");
}
