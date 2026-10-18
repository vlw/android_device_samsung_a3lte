/* SPDX-License-Identifier: Apache-2.0 */
import android.content.Context;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;

/** Run via root app_process. Exercises real Android volume changes with silence.
 * Restores the original media index and mute state even on assertion failure. */
public final class VolumeIntegrationTest {
    public static void main(String[] args) throws Exception {
        // Shell input key injection can be consumed by the foreground window.
        // Direct mode exercises the same AudioService volume changes headlessly.
        boolean direct = args.length > 0 && "--direct-volume".equals(args[0]);
        android.os.Looper.prepareMainLooper();
        Class<?> activityThread = Class.forName("android.app.ActivityThread");
        Object thread = activityThread.getMethod("systemMain").invoke(null);
        Context context = (Context) activityThread.getMethod("getSystemContext").invoke(thread);
        AudioManager manager = context.getSystemService(AudioManager.class);
        int stream = AudioManager.STREAM_MUSIC;
        int original = manager.getStreamVolume(stream);
        int originalVoice = manager.getStreamVolume(AudioManager.STREAM_VOICE_CALL);
        int originalMode = manager.getMode();
        boolean muted = manager.isStreamMute(stream);
        int maximum = manager.getStreamMaxVolume(stream);
        AudioTrack track = new AudioTrack(stream, 48000, AudioFormat.CHANNEL_OUT_STEREO,
                AudioFormat.ENCODING_PCM_16BIT, 48000 * 4, AudioTrack.MODE_STATIC);
        short[] silence = new short[48000 * 2];
        try {
            if (track.write(silence, 0, silence.length) != silence.length)
                throw new AssertionError("PCM write failed");
            if (track.setLoopPoints(0, 48000, -1) != AudioTrack.SUCCESS)
                throw new AssertionError("PCM loop failed");
            track.play();
            Thread.sleep(1000);
            for (int index : new int[]{1, maximum / 3, maximum * 2 / 3, maximum}) {
                manager.setStreamVolume(stream, index, 0);
                Thread.sleep(700);
                int actual = manager.getStreamVolume(stream);
                if (actual != index) throw new AssertionError("Index " + index + " != " + actual);
                System.out.println("volume=" + actual + "/" + maximum);
            }
            manager.setStreamVolume(stream, maximum / 3, 0);
            Thread.sleep(500);
            if (direct) manager.adjustStreamVolume(stream, AudioManager.ADJUST_RAISE, 0);
            else new ProcessBuilder("input", "keyevent", "KEYCODE_VOLUME_UP").start().waitFor();
            Thread.sleep(700);
            int up = manager.getStreamVolume(stream);
            System.out.println("button-up=" + up + "/" + maximum);
            if (up != maximum / 3 + 1) throw new AssertionError("Volume-up was not applied");
            if (direct) manager.adjustStreamVolume(stream, AudioManager.ADJUST_LOWER, 0);
            else new ProcessBuilder("input", "keyevent", "KEYCODE_VOLUME_DOWN").start().waitFor();
            Thread.sleep(700);
            int down = manager.getStreamVolume(stream);
            System.out.println("button-down=" + down + "/" + maximum);
            if (down != maximum / 3) throw new AssertionError("Volume-down was not applied");
            manager.adjustStreamVolume(stream, AudioManager.ADJUST_MUTE, 0);
            Thread.sleep(700);
            if (!manager.isStreamMute(stream)) throw new AssertionError("Mute failed");
            System.out.println("muted=true");
            manager.adjustStreamVolume(stream, AudioManager.ADJUST_UNMUTE, 0);
            Thread.sleep(700);
            manager.setStreamVolume(stream, 1, 0);
            Thread.sleep(700);
            AudioTrack alarm = new AudioTrack(AudioManager.STREAM_ALARM, 48000,
                    AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT,
                    48000 * 4, AudioTrack.MODE_STATIC);
            try {
                if (alarm.write(silence, 0, silence.length) != silence.length)
                    throw new AssertionError("Alarm PCM write failed");
                alarm.setLoopPoints(0, 48000, -1);
                alarm.play();
                Thread.sleep(1000);
                System.out.println("concurrent-alarm="
                        + manager.getStreamVolume(AudioManager.STREAM_ALARM) + "/"
                        + manager.getStreamMaxVolume(AudioManager.STREAM_ALARM));
            } finally {
                alarm.stop();
                alarm.release();
            }
            Thread.sleep(1000);
            System.out.println("returned-to-media=" + manager.getStreamVolume(stream));
            // Exercise speech DSP volume on the same silent media PCM. This
            // selects only TFA tuning: never changes Android mode or calls RIL.
            setParameters("tfa_profile=speechVoiceNB;tfa_vstep=auto");
            int voiceMinimum = manager.getStreamMinVolume(AudioManager.STREAM_VOICE_CALL);
            manager.setStreamVolume(AudioManager.STREAM_VOICE_CALL, voiceMinimum, 0);
            Thread.sleep(700);
            System.out.println("speech-profile-volume="
                    + manager.getStreamVolume(AudioManager.STREAM_VOICE_CALL) + "/"
                    + manager.getStreamMaxVolume(AudioManager.STREAM_VOICE_CALL));
            manager.setStreamVolume(AudioManager.STREAM_VOICE_CALL, originalVoice, 0);
            Thread.sleep(700);
            setParameters("tfa_profile=auto;tfa_vstep=auto");
            if (manager.getMode() != originalMode) throw new AssertionError("Audio mode changed");
        } finally {
            track.stop();
            track.release();
            manager.setStreamVolume(stream, original, 0);
            manager.adjustStreamVolume(stream, muted ? AudioManager.ADJUST_MUTE
                    : AudioManager.ADJUST_UNMUTE, 0);
            manager.setStreamVolume(AudioManager.STREAM_VOICE_CALL, originalVoice, 0);
            setParameters("tfa_profile=auto;tfa_vstep=auto");
            System.out.println("restored=" + manager.getStreamVolume(stream) + " muted="
                    + manager.isStreamMute(stream));
        }
        System.out.println(direct ? "Android volume integration passed (direct adjustments)"
                : "Android volume and button integration passed");
        System.exit(0);
    }

    private static void setParameters(String parameters) throws Exception {
        int result = (Integer) Class.forName("android.media.AudioSystem")
                .getMethod("setParameters", String.class).invoke(null, parameters);
        if (result != 0) throw new AssertionError("HAL status " + result);
    }
}
