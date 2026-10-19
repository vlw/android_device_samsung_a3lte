// SPDX-License-Identifier: GPL-2.0
package me.phh.ims;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Stock IPC 4.1 voice table, with stable CP IDs for the lifetime of a SIP dialog. */
public final class SamsungSrvccCallList {
    public static final class Call {
        public final String key, number;
        public final boolean outgoing, multiparty;
        public final int state; // IPC: active=1, held=2, dialing=3, alerting=4, incoming=5, waiting=6.
        public Call(String key, String number, boolean outgoing, int state, boolean multiparty) {
            this.key = key; this.number = number; this.outgoing = outgoing;
            this.state = state; this.multiparty = multiparty;
        }
    }
    private final Map<String, Integer> ids = new HashMap<>();

    public synchronized byte[] encode(List<Call> calls) {
        if (calls.size() > 8) throw new IllegalArgumentException("Too many SRVCC calls");
        Set<String> live = new HashSet<>();
        for (Call call : calls) {
            if (call.key == null || call.key.isEmpty() || !live.add(call.key) ||
                    call.state < 1 || call.state > 6) throw new IllegalArgumentException("Invalid call");
        }
        ids.keySet().retainAll(live);
        ByteArrayOutputStream body = new ByteArrayOutputStream();
        for (Call call : calls) {
            Integer id = ids.get(call.key);
            if (id == null) {
                id = 1;
                while (ids.containsValue(id)) id++;
                ids.put(call.key, id);
            }
            String number = call.number == null ? "" : call.number;
            if (!number.matches("[+0-9*#]{0,82}")) number = "";
            byte[] digits = number.getBytes(StandardCharsets.US_ASCII);
            body.write(5); body.write(1); // IPC_CALL_TYPE_VOICE_LTE = 0x105.
            body.write(id); body.write(call.outgoing ? 1 : 2);
            body.write(call.state); body.write(call.multiparty ? 1 : 0);
            body.write(digits.length); body.write(33);
            body.write(digits, 0, digits.length);
        }
        byte[] bytes = body.toByteArray();
        ByteArrayOutputStream frame = new ByteArrayOutputStream();
        frame.write(65); frame.write(51); frame.write(73); frame.write(77);
        frame.write(2); frame.write(calls.size()); frame.write(bytes.length & 255); frame.write(bytes.length >> 8);
        frame.write(bytes, 0, bytes.length);
        return frame.toByteArray();
    }
}
