// SPDX-License-Identifier: GPL-2.0
package me.phh.sip;

/** Reserves the initial dialog slot before its asynchronous setup publishes a Call. */
public final class IncomingInviteSetupGate {
    public enum Outcome { START, DUPLICATE, BUSY }

    public static final class Lease {
        private final String callId;
        private Lease(String callId) { this.callId = callId; }
    }

    public static final class Admission {
        public final Outcome outcome;
        public final Lease lease;
        private Admission(Outcome outcome, Lease lease) {
            this.outcome = outcome;
            this.lease = lease;
        }
    }

    private Lease owner;

    public synchronized Admission begin(String callId) {
        if (owner != null) {
            return new Admission(owner.callId.equals(callId) ? Outcome.DUPLICATE : Outcome.BUSY, null);
        }
        owner = new Lease(callId);
        return new Admission(Outcome.START, owner);
    }

    public synchronized void finish(Lease lease) {
        if (lease != null && owner == lease) owner = null;
    }
}
