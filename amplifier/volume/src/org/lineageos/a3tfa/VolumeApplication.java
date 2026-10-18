/* SPDX-License-Identifier: Apache-2.0 */
package org.lineageos.a3tfa;

import android.app.Application;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.media.AudioPlaybackConfiguration;
import android.media.AudioSystem;
import android.os.Handler;
import android.os.HandlerThread;
import android.util.Log;
import java.util.List;

/** Publishes real stream indices; never changes Android volumes or audio modes. */
public final class VolumeApplication extends Application {
    private static final String TAG = "A3TfaVolume";
    private AudioManager audio;
    private Handler handler;
    private String last = "";
    private final Runnable update = this::publish;

    @Override public void onCreate() {
        super.onCreate();
        HandlerThread thread = new HandlerThread(TAG);
        thread.start();
        handler = new Handler(thread.getLooper());
        audio = getSystemService(AudioManager.class);
        IntentFilter filter = new IntentFilter();
        filter.addAction(AudioManager.VOLUME_CHANGED_ACTION);
        filter.addAction(AudioManager.STREAM_MUTE_CHANGED_ACTION);
        filter.addAction(AudioManager.STREAM_DEVICES_CHANGED_ACTION);
        filter.addAction(AudioManager.RINGER_MODE_CHANGED_ACTION);
        filter.addAction(Intent.ACTION_USER_SWITCHED);
        registerReceiver(new BroadcastReceiver() {
            @Override public void onReceive(Context context, Intent intent) { refresh(); }
        }, filter, null, handler);
        audio.registerAudioPlaybackCallback(new AudioManager.AudioPlaybackCallback() {
            @Override public void onPlaybackConfigChanged(List<AudioPlaybackConfiguration> configs) {
                refresh();
            }
        }, handler);
        audio.registerAudioDeviceCallback(new AudioDeviceCallback() {
            @Override public void onAudioDevicesAdded(AudioDeviceInfo[] devices) { refresh(); }
            @Override public void onAudioDevicesRemoved(AudioDeviceInfo[] devices) { refresh(); }
        }, handler);
        audio.addOnModeChangedListener(handler::post, mode -> refresh());
        audio.setAudioServerStateCallback(handler::post, new AudioManager.AudioServerStateCallback() {
            @Override public void onAudioServerUp() {
                last = "";
                refresh();
            }
        });
        refresh();
    }

    void refresh() {
        handler.removeCallbacks(update);
        handler.post(update);
    }

    private boolean usesSpeaker(AudioPlaybackConfiguration config, int stream) {
        AudioDeviceInfo device = config.getAudioDeviceInfo();
        int routes = AudioSystem.getDevicesForStream(stream);
        if (device == null) return (routes & AudioSystem.DEVICE_OUT_SPEAKER) != 0;
        if (device.getType() == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER) return true;
        // A speaker/headphones combo may expose only the headphone port here.
        // Do not mistake an independently routed Bluetooth player for a combo.
        int port = AudioDeviceInfo.convertDeviceTypeToInternalDevice(device.getType());
        return (routes & AudioSystem.DEVICE_OUT_SPEAKER) != 0 && (routes & port) != 0;
    }

    private int selectMediaStream() {
        int selected = AudioManager.STREAM_MUSIC;
        float loudest = Float.NEGATIVE_INFINITY;
        for (AudioPlaybackConfiguration config : audio.getActivePlaybackConfigurations()) {
            if (!config.isActive()) continue;
            int stream = config.getAudioAttributes().getVolumeControlStream();
            if (stream < 0 || stream == AudioManager.STREAM_VOICE_CALL
                    || stream == AudioManager.STREAM_BLUETOOTH_SCO) continue;
            if (!usesSpeaker(config, stream) || audio.isStreamMute(stream)) continue;
            int index = audio.getStreamVolume(stream);
            int maximum = audio.getStreamMaxVolume(stream);
            if (maximum <= 0 || index <= 0) continue;
            // One mono DSP is shared by concurrent streams. Use the loudest
            // Android speaker gain, independent of app identity/per-track gain.
            float gain = AudioSystem.getStreamVolumeDB(stream, index, AudioSystem.DEVICE_OUT_SPEAKER);
            if (gain > loudest) {
                loudest = gain;
                selected = stream;
            }
        }
        return selected;
    }

    private String volume(int stream) {
        int maximum = audio.getStreamMaxVolume(stream);
        int index = audio.isStreamMute(stream) ? 0 : audio.getStreamVolume(stream);
        if (maximum <= 0 || maximum > 1000 || index < 0 || index > maximum)
            throw new IllegalStateException("Invalid stream volume range");
        return index + "," + maximum;
    }

    private void publish() {
        try {
            int stream = selectMediaStream();
            String parameters = "tfa_android_music=" + volume(stream)
                    + ";tfa_android_voice=" + volume(AudioManager.STREAM_VOICE_CALL);
            if (parameters.equals(last)) return;
            int rc = AudioSystem.setParameters(parameters);
            if (rc != 0) throw new IllegalStateException("Audio HAL status " + rc);
            last = parameters;
            Log.i(TAG, "stream=" + stream + " " + parameters);
        } catch (RuntimeException e) {
            Log.w(TAG, "Volume publication unavailable; retrying", e);
            handler.removeCallbacks(update);
            handler.postDelayed(update, 1000);
        }
    }
}
