// SPDX-License-Identifier: GPL-2.0
package me.phh.ims;

/** Only a real STARTED/COMPLETED sequence may release the AP media session. */
public final class SrvccTransferState {
    public enum Action { IGNORE, START, COMPLETE, RETAIN }
    private boolean pending;
    private boolean awaitingOutcome;
    public boolean isPending() { return pending; }
    public Action update(int state, boolean hasCalls) {
        if (state == 0 && !pending && hasCalls) {
            pending = true;
            awaitingOutcome = true;
            return Action.START;
        }
        if (state == 1 && awaitingOutcome) {
            pending = false;
            awaitingOutcome = false;
            return hasCalls ? Action.COMPLETE : Action.IGNORE;
        }
        if ((state == 2 || state == 3) && awaitingOutcome) {
            pending = false;
            awaitingOutcome = false;
            return Action.RETAIN;
        }
        return Action.IGNORE;
    }
    public void timeout() { pending = false; }
}
