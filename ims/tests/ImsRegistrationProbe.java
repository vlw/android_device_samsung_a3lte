/* SPDX-License-Identifier: Apache-2.0 */
// Read-only IMS status check. Never initiates calls, SMS or audio capture.
import android.telephony.ims.ImsMmTelManager;
import android.telephony.ims.RegistrationManager;
import android.telephony.ims.feature.MmTelFeature;
import android.telephony.ims.stub.ImsRegistrationImplBase;
import java.util.concurrent.*;
public final class ImsRegistrationProbe {
    static void waitVoice(ImsMmTelManager ims, boolean expected) throws Exception {
        long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(20);
        do {
            if (ims.isAvailable(MmTelFeature.MmTelCapabilities.CAPABILITY_TYPE_VOICE,
                    ImsRegistrationImplBase.REGISTRATION_TECH_LTE) == expected) return;
            Thread.sleep(250);
        } while (System.nanoTime() < end);
        throw new AssertionError("LTE voice capability did not become " + expected);
    }
    public static void main(String[] args) {
        try { run(args); }
        catch (Throwable error) { error.printStackTrace(); System.exit(1); }
    }
    static void run(String[] args) throws Exception {
        android.os.Looper.prepareMainLooper();
        Class<?> serviceManager = Class.forName("android.os.TelephonyServiceManager");
        Class.forName("android.telephony.TelephonyFrameworkInitializer")
                .getMethod("setTelephonyServiceManager", serviceManager)
                .invoke(null, serviceManager.getConstructor().newInstance());
        ImsMmTelManager ims = ImsMmTelManager.createForSubscriptionId(Integer.parseInt(args[0]));
        int[] state = {-1};
        boolean voice = false, sms = false;
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(40);
        do {
            CountDownLatch received = new CountDownLatch(1);
            ims.getRegistrationState(Runnable::run, value -> { state[0] = value; received.countDown(); });
            if (!received.await(10, TimeUnit.SECONDS)) throw new AssertionError("No IMS status");
            voice = ims.isAvailable(MmTelFeature.MmTelCapabilities.CAPABILITY_TYPE_VOICE,
                    ImsRegistrationImplBase.REGISTRATION_TECH_LTE);
            sms = ims.isAvailable(MmTelFeature.MmTelCapabilities.CAPABILITY_TYPE_SMS,
                    ImsRegistrationImplBase.REGISTRATION_TECH_LTE);
            if (state[0] == RegistrationManager.REGISTRATION_STATE_REGISTERED && voice) break;
            Thread.sleep(250);
        } while (System.nanoTime() < deadline);
        System.out.println("IMS registrationState="+state[0]+" LTE voice="+voice+" SMS="+sms);
        if (state[0] != RegistrationManager.REGISTRATION_STATE_REGISTERED || !voice)
            throw new AssertionError("IMS voice is not registered over LTE");
        System.out.println("PASS Android reports IMS registered and LTE voice available; no calls made");
        if (args.length > 1 && args[1].equals("toggle")) {
            boolean original = ims.isAdvancedCallingSettingEnabled();
            if (!original) throw new AssertionError("VoLTE was disabled before test");
            try {
                ims.setAdvancedCallingSettingEnabled(false);
                waitVoice(ims, false);
                System.out.println("PASS VoLTE off removes LTE voice capability");
            } finally {
                ims.setAdvancedCallingSettingEnabled(original);
            }
            waitVoice(ims, true);
            System.out.println("PASS original VoLTE setting and LTE voice capability restored");
        }
        System.exit(0);
    }
}
