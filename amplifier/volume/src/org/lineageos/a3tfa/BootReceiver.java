/* SPDX-License-Identifier: Apache-2.0 */
package org.lineageos.a3tfa;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

public final class BootReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        ((VolumeApplication) context.getApplicationContext()).refresh();
    }
}
