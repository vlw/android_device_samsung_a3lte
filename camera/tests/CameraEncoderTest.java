/* SPDX-License-Identifier: Apache-2.0 */
import android.media.CamcorderProfile;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.MediaMetadataRetriever;
import android.media.MediaMuxer;
import java.nio.ByteBuffer;
import java.io.FileInputStream;
import java.util.Arrays;

/** Sensor-free check: query camcorder profiles and encode synthetic YUV frames. */
public final class CameraEncoderTest {
    static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    static void encode(int width, int height, int bitrate, String output) throws Exception {
        MediaFormat format = MediaFormat.createVideoFormat("video/avc", width, height);
        format.setInteger(MediaFormat.KEY_BIT_RATE, bitrate);
        format.setInteger(MediaFormat.KEY_FRAME_RATE, 30);
        format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1);
        format.setInteger(MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420SemiPlanar);
        MediaCodec encoder = MediaCodec.createByCodecName("OMX.qcom.video.encoder.avc");
        MediaMuxer muxer = null;
        boolean started = false, muxing = false;
        try {
            encoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            encoder.start(); started = true;
            muxer = new MediaMuxer(output, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);
            byte[] pixels = new byte[width * height * 3 / 2];
            Arrays.fill(pixels, 0, width * height, (byte) 80);
            Arrays.fill(pixels, width * height, pixels.length, (byte) 128);
            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            int inputFrames = 0, outputFrames = 0, track = -1;
            boolean inputDone = false, outputDone = false;
            long deadline = System.nanoTime() + 20_000_000_000L;
            while (!outputDone && System.nanoTime() < deadline) {
                if (!inputDone) {
                    int index = encoder.dequeueInputBuffer(10_000);
                    if (index >= 0) {
                        if (inputFrames == 16) {
                            encoder.queueInputBuffer(index, 0, 0, inputFrames * 1_000_000L / 30,
                                    MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            inputDone = true;
                        } else {
                            ByteBuffer buffer = encoder.getInputBuffer(index);
                            buffer.clear();
                            check(buffer.remaining() >= pixels.length, "Input buffer too small");
                            buffer.put(pixels);
                            encoder.queueInputBuffer(index, 0, pixels.length,
                                    inputFrames++ * 1_000_000L / 30, 0);
                        }
                    }
                }
                int index = encoder.dequeueOutputBuffer(info, 10_000);
                if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    MediaFormat actual = encoder.getOutputFormat();
                    check(actual.getInteger(MediaFormat.KEY_WIDTH) == width
                            && actual.getInteger(MediaFormat.KEY_HEIGHT) == height,
                            "Encoder changed dimensions: " + actual);
                    track = muxer.addTrack(actual); muxer.start(); muxing = true;
                } else if (index >= 0) {
                    if (info.size > 0 && (info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0) {
                        check(muxing, "Data before format");
                        ByteBuffer buffer = encoder.getOutputBuffer(index);
                        buffer.position(info.offset); buffer.limit(info.offset + info.size);
                        muxer.writeSampleData(track, buffer, info); outputFrames++;
                    }
                    outputDone = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
                    encoder.releaseOutputBuffer(index, false);
                }
            }
            check(outputDone && outputFrames == 16,
                    "Missing frames/EOS: " + outputFrames + " at " + width + "x" + height);
            muxer.stop(); muxing = false;
        } finally {
            if (muxer != null) {
                if (muxing) muxer.stop();
                muxer.release();
            }
            if (started) encoder.stop();
            encoder.release();
        }
        MediaMetadataRetriever metadata = new MediaMetadataRetriever();
        try {
            try (FileInputStream input = new FileInputStream(output)) {
                metadata.setDataSource(input.getFD());
            }
            check(Integer.toString(width).equals(metadata.extractMetadata(
                    MediaMetadataRetriever.METADATA_KEY_VIDEO_WIDTH)), "MP4 width");
            check(Integer.toString(height).equals(metadata.extractMetadata(
                    MediaMetadataRetriever.METADATA_KEY_VIDEO_HEIGHT)), "MP4 height");
        } finally { metadata.release(); }
        System.out.println("PASS AVC/MP4 " + width + "x" + height + " @30 " + bitrate + "bps, 16 frames");
    }
    public static void main(String[] args) throws Exception {
        if (args.length != 2) throw new IllegalArgumentException("output-directory sizes-comma-separated");
        android.os.Looper.prepareMainLooper();
        Class<?> activityThread = Class.forName("android.app.ActivityThread");
        Object activity = activityThread.getMethod("systemMain").invoke(null);
        android.content.Context system = (android.content.Context) activityThread
                .getMethod("getSystemContext").invoke(activity);
        android.content.Context shell = system.createPackageContext("com.android.shell", 0);
        Object application = android.app.Instrumentation.class
                .getMethod("newApplication", Class.class, android.content.Context.class)
                .invoke(null, android.app.Application.class, shell);
        java.lang.reflect.Field initial = activityThread.getDeclaredField("mInitialApplication");
        initial.setAccessible(true);
        initial.set(activity, application);
        int[] qualities = {0, 1, 2, 3, 4, 5, 6, 7, 9, 1000, 1001, 1002, 1003,
                1004, 1005, 1006, 1007, 1009};
        for (int camera = 0; camera <= 1; camera++) for (int quality : qualities) {
            check(CamcorderProfile.hasProfile(camera, quality),
                    "Missing profile camera=" + camera + " quality=" + quality);
            CamcorderProfile profile = CamcorderProfile.get(camera, quality);
            check(profile.videoFrameRate == 30, "Unexpected fps");
            if (quality == 1 || quality == 6 || quality == 1001 || quality == 1006)
                check(profile.videoFrameWidth == 1920 && profile.videoFrameHeight == 1080,
                        "Missing FHD camera=" + camera + " quality=" + quality);
            System.out.println("PROFILE camera=" + camera + " quality=" + quality + " "
                    + profile.videoFrameWidth + "x" + profile.videoFrameHeight + " "
                    + profile.videoBitRate + "bps");
        }
        for (String size : args[1].split(",")) {
            String[] pair = size.split("x");
            int width = Integer.parseInt(pair[0]), height = Integer.parseInt(pair[1]);
            int bitrate = Math.max(384000, (int) (17000000L * width * height / (1920 * 1080)));
            // Exact stock rates where a stock standard profile exists.
            for (int quality : new int[]{2, 3, 4, 5, 6, 7, 9}) {
                CamcorderProfile profile = CamcorderProfile.get(0, quality);
                if (profile.videoFrameWidth == width && profile.videoFrameHeight == height)
                    bitrate = profile.videoBitRate;
            }
            encode(width, height, bitrate, args[0] + "/encoder-" + size + ".mp4");
        }
        System.out.println("All profiles and sensor-free encoder checks passed");
    }
}
