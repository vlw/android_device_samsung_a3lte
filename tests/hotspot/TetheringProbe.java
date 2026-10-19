// SPDX-License-Identifier: Apache-2.0
// Optional app_process probe. Uses the real framework tethering service.
import android.app.ActivityThread;
import android.content.Context;
import android.net.TetheringManager;
import android.net.wifi.SoftApConfiguration;
import android.net.wifi.WifiManager;
import android.os.Looper;
import android.os.Parcel;
import android.os.Process;
import java.nio.file.attribute.PosixFilePermissions;
import java.io.File;
import java.nio.file.Files;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

public final class TetheringProbe {
    private static final File BACKUP =
            new File("/data/local/tmp/a3-hotspot-original.parcel");

    private static void restore(WifiManager wifi, TetheringManager tether) throws Exception {
        tether.stopTethering(TetheringManager.TETHERING_WIFI);
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(20);
        while (wifi.getWifiApState() != WifiManager.WIFI_AP_STATE_DISABLED) {
            if (System.nanoTime() > deadline) throw new IllegalStateException("AP stop timeout");
            Thread.sleep(200);
        }
        if (BACKUP.exists()) {
            byte[] bytes = Files.readAllBytes(BACKUP.toPath());
            Parcel parcel = Parcel.obtain();
            try {
                parcel.unmarshall(bytes, 0, bytes.length);
                parcel.setDataPosition(0);
                SoftApConfiguration original = SoftApConfiguration.CREATOR.createFromParcel(parcel);
                if (!wifi.setSoftApConfiguration(original)) {
                    throw new IllegalStateException("Cannot restore AP configuration");
                }
            } finally {
                parcel.recycle();
            }
            if (!BACKUP.delete()) throw new IllegalStateException("Cannot delete backup");
        }
        System.out.println("PASS AP stopped and original configuration restored");
    }

    public static void main(String[] args) throws Exception {
        if (Process.myUid() != 0) throw new IllegalStateException("Run as root");
        if (args.length != 1 && args.length != 3) {
            throw new IllegalArgumentException("stop | start SSID WPA2_PASSWORD");
        }
        Looper.prepareMainLooper();
        Context context = ActivityThread.systemMain().getSystemContext();
        WifiManager wifi = context.getSystemService(WifiManager.class);
        TetheringManager tether = context.getSystemService(TetheringManager.class);
        if (args[0].equals("stop") && args.length == 1) {
            restore(wifi, tether);
            System.exit(0);
        }
        if (!args[0].equals("start") || args.length != 3) {
            throw new IllegalArgumentException("stop | start SSID WPA2_PASSWORD");
        }
        if (wifi.getWifiApState() != WifiManager.WIFI_AP_STATE_DISABLED) {
            throw new IllegalStateException("An AP is already active; leave it untouched");
        }
        SoftApConfiguration original = wifi.getSoftApConfiguration();
        if (original == null || !BACKUP.createNewFile()) {
            throw new IllegalStateException("Missing configuration or outstanding backup");
        }
        Files.setPosixFilePermissions(BACKUP.toPath(), PosixFilePermissions.fromString("rw-------"));
        Parcel parcel = Parcel.obtain();
        try {
            original.writeToParcel(parcel, 0);
            Files.write(BACKUP.toPath(), parcel.marshall());
        } finally {
            parcel.recycle();
        }
        boolean started = false;
        try {
            SoftApConfiguration config = new SoftApConfiguration.Builder()
                    .setSsid(args[1]).setBand(SoftApConfiguration.BAND_2GHZ)
                    .setPassphrase(args[2], SoftApConfiguration.SECURITY_TYPE_WPA2_PSK).build();
            if (!wifi.setSoftApConfiguration(config)) {
                throw new IllegalStateException("Cannot configure test AP");
            }
            CountDownLatch done = new CountDownLatch(1);
            int[] result = {-1};
            tether.startTethering(new TetheringManager.TetheringRequest.Builder(
                    TetheringManager.TETHERING_WIFI).setShouldShowEntitlementUi(false).build(),
                    Runnable::run, new TetheringManager.StartTetheringCallback() {
                        @Override public void onTetheringStarted() { result[0] = 0; done.countDown(); }
                        @Override public void onTetheringFailed(int error) {
                            result[0] = error; done.countDown();
                        }
                    });
            if (!done.await(25, TimeUnit.SECONDS) || result[0] != 0) {
                throw new IllegalStateException("Tethering start result=" + result[0]);
            }
            started = true;
            System.out.println("PASS framework Wi-Fi tethering started (not just SoftAP)");
        } finally {
            if (!started) restore(wifi, tether);
        }
        System.exit(0);
    }
}
