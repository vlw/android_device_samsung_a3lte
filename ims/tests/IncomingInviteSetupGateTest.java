// SPDX-License-Identifier: GPL-2.0
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import me.phh.sip.IncomingInviteSetupGate;

public final class IncomingInviteSetupGateTest {
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }

    public static void main(String[] args) throws Exception {
        IncomingInviteSetupGate gate = new IncomingInviteSetupGate();
        CountDownLatch start = new CountDownLatch(1);
        AtomicInteger sessions = new AtomicInteger(), duplicates = new AtomicInteger();
        AtomicReference<IncomingInviteSetupGate.Lease> owner = new AtomicReference<>();
        List<Thread> readers = new ArrayList<>();
        for (int i = 0; i < 32; i++) {
            Thread reader = new Thread(() -> {
                try { start.await(); } catch (InterruptedException e) { throw new AssertionError(e); }
                IncomingInviteSetupGate.Admission result = gate.begin(new String("same-call"));
                if (result.outcome == IncomingInviteSetupGate.Outcome.START) {
                    sessions.incrementAndGet();
                    owner.set(result.lease);
                } else if (result.outcome == IncomingInviteSetupGate.Outcome.DUPLICATE) {
                    duplicates.incrementAndGet();
                }
            });
            readers.add(reader);
            reader.start();
        }
        start.countDown();
        for (Thread reader : readers) reader.join();
        check(sessions.get() == 1 && duplicates.get() == 31, "retransmits created multiple dialogs");
        check(gate.begin("other-call").outcome == IncomingInviteSetupGate.Outcome.BUSY,
                "concurrent different initial dialogs admitted");
        gate.finish(owner.get());
        IncomingInviteSetupGate.Admission retry = gate.begin("same-call");
        check(retry.outcome == IncomingInviteSetupGate.Outcome.START, "failed setup cannot be retried");
        gate.finish(owner.get());
        check(gate.begin("same-call").outcome == IncomingInviteSetupGate.Outcome.DUPLICATE,
                "stale completion released a newer reservation");
        gate.finish(retry.lease);
        check(gate.begin("next-call").outcome == IncomingInviteSetupGate.Outcome.START,
                "next call blocked after cleanup");
        System.out.println("PASS parallel INVITE retransmits, competing calls, retry and stale-worker cleanup");
    }
}
