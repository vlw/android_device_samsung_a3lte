/* SPDX-License-Identifier: Apache-2.0 */
/* Offline MediaCodec validation. No camera, audio, network, or reboot. */
import android.graphics.Rect;
import android.graphics.SurfaceTexture;
import android.media.*;
import android.opengl.*;
import android.os.Handler;
import android.os.HandlerThread;
import android.view.Surface;
import java.io.*;
import java.nio.*;
import java.util.*;

public class VideoCodecProbe {
    static boolean surfaceMode;
    static boolean flushMode;
    static int requestedProfile;
    static int requestedBitrate;
    static class Gl implements AutoCloseable {
        EGLDisplay display;
        EGLContext context;
        EGLSurface eglSurface;
        Surface surface;
        SurfaceTexture texture;
        HandlerThread handler;
        final Object lock = new Object();
        boolean pending;
        Gl(Surface encoderSurface) throws Exception {
            display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
            int[] version = new int[2];
            if (!EGL14.eglInitialize(display, version, 0, version, 1))
                throw new Exception("EGL initialize");
            int[] attrs = {EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8, EGL14.EGL_BLUE_SIZE, 8,
                EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT, EGL14.EGL_SURFACE_TYPE,
                encoderSurface == null ? EGL14.EGL_PBUFFER_BIT : EGL14.EGL_WINDOW_BIT, 0x3142, 1,
                EGL14.EGL_NONE};
            EGLConfig[] configs = new EGLConfig[1];
            int[] count = new int[1];
            if (!EGL14.eglChooseConfig(display, attrs, 0, configs, 0, 1, count, 0) || count[0] == 0)
                throw new Exception("No recordable EGL config");
            context = EGL14.eglCreateContext(display, configs[0], EGL14.EGL_NO_CONTEXT,
                new int[] {EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE}, 0);
            surface = encoderSurface;
            eglSurface = encoderSurface == null ? EGL14.eglCreatePbufferSurface(display, configs[0],
                             new int[] {EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE}, 0)
                                                : EGL14.eglCreateWindowSurface(display, configs[0],
                                                    encoderSurface, new int[] {EGL14.EGL_NONE}, 0);
            if (!EGL14.eglMakeCurrent(display, eglSurface, eglSurface, context))
                throw new Exception("EGL make current");
            if (encoderSurface == null) {
                int[] id = new int[1];
                GLES20.glGenTextures(1, id, 0);
                texture = new SurfaceTexture(id[0]);
                surface = new Surface(texture);
                handler = new HandlerThread("video-frame-callback");
                handler.start();
                texture.setOnFrameAvailableListener(t->{synchronized(lock){pending=true;lock.notifyAll();
            }
        },new Handler(handler.getLooper()));
    }
}
void render(int frame, int w, int h, int fps) throws Exception {
    GLES20.glViewport(0, 0, w, h);
    GLES20.glClearColor((frame % 3) / 2f, 0.25f, 0.75f, 1f);
    GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
    if (!EGLExt.eglPresentationTimeANDROID(display, eglSurface, frame * 1000000000L / fps)
        || !EGL14.eglSwapBuffers(display, eglSurface))
        throw new Exception("EGL encoder swap");
}
void awaitFrame(long pts) throws Exception {
    synchronized (lock) {
        long end = System.nanoTime() + 5_000_000_000L;
        while (!pending) {
            long remain = end - System.nanoTime();
            if (remain <= 0)
                throw new Exception("Surface frame timeout");
            lock.wait(Math.max(1, remain / 1000000));
        }
        pending = false;
    }
    texture.updateTexImage();
    if (Math.abs(texture.getTimestamp() / 1000 - pts) > 1)
        throw new Exception("Surface timestamp mismatch " + texture.getTimestamp() + " vs " + pts);
}
public void close() {
    if (surface != null)
        surface.release();
    if (texture != null)
        texture.release();
    if (handler != null)
        handler.quitSafely();
    EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT);
    EGL14.eglDestroySurface(display, eglSurface);
    EGL14.eglDestroyContext(display, context);
    EGL14.eglTerminate(display);
}
}
static final long TIMEOUT_US = 10000;
static class Packet {
    byte[] data;
    long pts;
    int flags;
    Packet(byte[] d, long p, int f) {
        data = d;
        pts = p;
        flags = f;
    }
}
static class Pixels {
    long error, samples;
    int images;
    void compare(Image image, byte[] ref, int w, int h, int frame) throws Exception {
        if (image == null)
            throw new Exception("Output Image unavailable: pixel correctness unverified");
        try {
            Rect r = image.getCropRect();
            if (r.width() != w || r.height() != h)
                throw new Exception("Wrong crop " + r + " expected " + w + "x" + h);
            Image.Plane[] planes = image.getPlanes();
            int base = frame * w * h * 3 / 2;
            for (int p = 0; p < 3; p++) {
                int scale = p == 0 ? 1 : 2, pw = w / scale, ph = h / scale;
                int offset = base + (p == 0 ? 0 : w * h + (p - 1) * w * h / 4);
                ByteBuffer b = planes[p].getBuffer();
                int origin = b.position();
                for (int y = 0; y < ph; y++)
                    for (int x = 0; x < pw; x++) {
                        int index = origin + (y + r.top / scale) * planes[p].getRowStride()
                            + (x + r.left / scale) * planes[p].getPixelStride();
                        error += Math.abs((b.get(index) & 255) - (ref[offset + y * pw + x] & 255));
                        samples++;
                    }
            }
            images++;
        } finally {
            image.close();
        }
    }
    double mae() {
        return samples == 0 ? Double.NaN : (double) error / samples;
    }
}
static byte[] read(String path) throws Exception {
    try (FileInputStream f = new FileInputStream(path);
         ByteArrayOutputStream o = new ByteArrayOutputStream()) {
        byte[] b = new byte[65536];
        int n;
        while ((n = f.read(b)) > 0) o.write(b, 0, n);
        return o.toByteArray();
    }
}
static void fill(Image image, byte[] raw, int w, int h, int frame) throws Exception {
    if (image == null)
        throw new Exception("Input Image unavailable");
    try {
        Image.Plane[] planes = image.getPlanes();
        int base = frame * w * h * 3 / 2;
        for (int p = 0; p < 3; p++) {
            int pw = p == 0 ? w : w / 2, ph = p == 0 ? h : h / 2;
            int off = base + (p == 0 ? 0 : w * h + (p - 1) * w * h / 4);
            ByteBuffer b = planes[p].getBuffer();
            int origin = b.position();
            for (int y = 0; y < ph; y++)
                for (int x = 0; x < pw; x++)
                    b.put(origin + y * planes[p].getRowStride() + x * planes[p].getPixelStride(),
                        raw[off + y * pw + x]);
        }
    } finally {
        image.close();
    }
}
static void inventory() {
    for (MediaCodecInfo i : new MediaCodecList(MediaCodecList.ALL_CODECS).getCodecInfos())
        if (!i.isAlias())
            for (String mime : i.getSupportedTypes())
                if (mime.startsWith("video/")) {
                    MediaCodecInfo.CodecCapabilities c = i.getCapabilitiesForType(mime);
                    System.out.println("CODEC\t" + i.getName() + "\t" + mime + "\t"
                        + (i.isEncoder() ? "ENC" : "DEC") + "\thw=" + i.isHardwareAccelerated()
                        + "\tsecure=" + c.isFeatureRequired("secure-playback") + "\t"
                        + c.getVideoCapabilities().getSupportedWidths() + "x"
                        + c.getVideoCapabilities().getSupportedHeights());
                }
}
static void decode(String name, String input, String reference, int expected, int fps)
    throws Exception {
    decode(name, input, reference, expected, fps, null);
}
static void decode(String name, String input, String reference, int expected, int fps,
    String packetMime) throws Exception {
    MediaExtractor ex = new MediaExtractor();
    MediaCodec codec = null;
    Gl gl = null;
    ArrayList<Packet> packetInput = new ArrayList<>();
    int packetIndex = 0;
    try {
        MediaFormat format = null;
        if (packetMime == null) {
            ex.setDataSource(input);
            int track = -1;
            for (int i = 0; i < ex.getTrackCount(); i++)
                if (ex.getTrackFormat(i).getString(MediaFormat.KEY_MIME).startsWith("video/")) {
                    track = i;
                    format = ex.getTrackFormat(i);
                    break;
                }
            if (track < 0)
                throw new Exception("No video track");
            ex.selectTrack(track);
        } else {
            try (DataInputStream in = new DataInputStream(new FileInputStream(input))) {
                int width = in.readInt(), height = in.readInt();
                in.readInt();
                format = MediaFormat.createVideoFormat(packetMime, width, height);
                while (in.available() > 0) {
                    long pts = in.readLong();
                    int flags = in.readInt(), size = in.readInt();
                    if (size < 0 || size > 16777216)
                        throw new Exception("Invalid packet size");
                    byte[] data = new byte[size];
                    in.readFully(data);
                    packetInput.add(new Packet(data, pts, flags));
                }
            }
        }
        int w = format.getInteger(MediaFormat.KEY_WIDTH),
            h = format.getInteger(MediaFormat.KEY_HEIGHT);
        boolean benchmark = reference.equals("-");
        byte[] ref = benchmark ? null : read(reference);
        if (!benchmark && ref.length != expected * w * h * 3 / 2)
            throw new Exception("Bad reference size " + ref.length);
        codec = MediaCodec.createByCodecName(name);
        format.setInteger(MediaFormat.KEY_COLOR_FORMAT,
            MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible);
        if (surfaceMode)
            gl = new Gl(null);
        codec.configure(format, gl == null ? null : gl.surface, null, 0);
        codec.start();
        Pixels pixels = new Pixels();
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        boolean inEos = false, outEos = false;
        boolean didFlush = false;
        int fed = 0, got = 0;
        long prev = -1;
        BitSet seen = new BitSet(expected);
        long start = System.nanoTime(), deadline = start + 60_000_000_000L;
        while (!outEos && System.nanoTime() < deadline) {
            if (!inEos) {
                int i = codec.dequeueInputBuffer(TIMEOUT_US);
                if (i >= 0) {
                    ByteBuffer b = codec.getInputBuffer(i);
                    b.clear();
                    int n = packetMime == null ? ex.readSampleData(b, 0)
                                               : (packetIndex < packetInput.size()
                                                       ? packetInput.get(packetIndex).data.length
                                                       : -1);
                    if (n < 0) {
                        codec.queueInputBuffer(i, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                        inEos = true;
                    } else if (packetMime != null) {
                        Packet p = packetInput.get(packetIndex++);
                        b.put(p.data);
                        codec.queueInputBuffer(
                            i, 0, n, p.pts, p.flags & ~MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                        fed++;
                    } else {
                        codec.queueInputBuffer(i, 0, n, ex.getSampleTime(), ex.getSampleFlags());
                        ex.advance();
                        fed++;
                    }
                }
            }
            int i = codec.dequeueOutputBuffer(info, TIMEOUT_US);
            if (i == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED)
                System.out.println("FORMAT\t" + name + "\t" + codec.getOutputFormat());
            if (i >= 0) {
                if (info.size > 0 && (info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0) {
                    if (info.presentationTimeUs < prev)
                        throw new Exception("Nonmonotonic PTS");
                    prev = info.presentationTimeUs;
                    int frame = (int) Math.round(info.presentationTimeUs * fps / 1000000.0);
                    if (frame < 0 || frame >= expected)
                        throw new Exception("Out of range PTS " + info.presentationTimeUs);
                    if (seen.get(frame))
                        throw new Exception("Duplicate output PTS " + info.presentationTimeUs);
                    seen.set(frame);
                    if (!benchmark && !surfaceMode)
                        pixels.compare(codec.getOutputImage(i), ref, w, h, frame);
                    got++;
                }
                outEos = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
                boolean render = surfaceMode && info.size > 0;
                codec.releaseOutputBuffer(i, render);
                if (render)
                    gl.awaitFrame(info.presentationTimeUs);
            }
            if (flushMode && !didFlush && got >= 10 && !outEos) {
                codec.flush();
                ex.seekTo(0, MediaExtractor.SEEK_TO_PREVIOUS_SYNC);
                inEos = false; outEos = false; got = 0; prev = -1;
                seen.clear(); didFlush = true;
            }
        }
        double seconds = (System.nanoTime() - start) / 1e9;
        if (!outEos || got != expected || (!benchmark && pixels.mae() > 3.0))
            throw new Exception("Decode mismatch fed=" + fed + " got=" + got
                + " expected=" + expected + " eos=" + outEos + " mae=" + pixels.mae());
        System.out.printf(Locale.US, "PASS\tDEC\t%s\t%s\t%dx%d\tframes=%d\tfps=%.2f\tmae=%.4f%n",
            name, new File(input).getName(), w, h, got, got / seconds, pixels.mae());
    } finally {
        if (codec != null) {
            try {
                codec.stop();
            } finally {
                codec.release();
            }
        }
        ex.release();
        if (gl != null)
            gl.close();
    }
}
static void encode(String name, String mime, String rawPath, int w, int h, int count, int fps,
    String output) throws Exception {
    MediaCodec codec = null;
    Gl gl = null;
    byte[] raw = surfaceMode ? null : read(rawPath);
    ArrayList<Packet> packets = new ArrayList<>();
    try {
        codec = MediaCodec.createByCodecName(name);
        MediaFormat f = MediaFormat.createVideoFormat(mime, w, h);
        f.setInteger(MediaFormat.KEY_COLOR_FORMAT,
            surfaceMode ? MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface
                        : MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible);
        int bitrate =
            mime.equals("video/3gpp") ? 128000 : mime.equals("video/mp4v-es") ? 64000 : 1000000;
        if (requestedBitrate > 0)
            bitrate = requestedBitrate;
        if (requestedProfile != 0) {
            f.setInteger(MediaFormat.KEY_PROFILE, requestedProfile);
            // Legacy OMX requires a level together with an explicitly requested AVC profile.
            f.setInteger(MediaFormat.KEY_LEVEL, MediaCodecInfo.CodecProfileLevel.AVCLevel3);
        }
        f.setInteger(MediaFormat.KEY_BIT_RATE, bitrate);
        f.setInteger(MediaFormat.KEY_FRAME_RATE, fps);
        f.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1);
        codec.configure(f, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
        if (surfaceMode)
            gl = new Gl(codec.createInputSurface());
        codec.start();
        int fed = 0, got = 0;
        boolean inEos = false, outEos = false;
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        MediaFormat outFormat = null;
        long start = System.nanoTime(), deadline = start + 90_000_000_000L;
        while (!outEos && System.nanoTime() < deadline) {
            if (!inEos && surfaceMode) {
                if (fed == count) {
                    codec.signalEndOfInputStream();
                    inEos = true;
                } else
                    gl.render(fed++, w, h, fps);
            }
            if (!inEos && !surfaceMode) {
                int i = codec.dequeueInputBuffer(TIMEOUT_US);
                if (i >= 0) {
                    if (fed == count) {
                        codec.queueInputBuffer(
                            i, 0, 0, fed * 1000000L / fps, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                        inEos = true;
                    } else {
                        fill(codec.getInputImage(i), raw, w, h, fed);
                        codec.queueInputBuffer(i, 0, w * h * 3 / 2, fed * 1000000L / fps, 0);
                        fed++;
                    }
                }
            }
            int i = codec.dequeueOutputBuffer(info, TIMEOUT_US);
            if (i == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                outFormat = codec.getOutputFormat();
                System.out.println("FORMAT\t" + name + "\t" + outFormat);
            }
            if (i >= 0) {
                if (info.size > 0) {
                    ByteBuffer b = codec.getOutputBuffer(i);
                    b.position(info.offset);
                    b.limit(info.offset + info.size);
                    byte[] d = new byte[info.size];
                    b.get(d);
                    packets.add(new Packet(d, info.presentationTimeUs, info.flags));
                    if ((info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0)
                        got++;
                }
                outEos = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
                codec.releaseOutputBuffer(i, false);
            }
        }
        double seconds = (System.nanoTime() - start) / 1e9;
        if (!outEos || got != count || outFormat == null)
            throw new Exception("Encoder frames=" + got + " expected=" + count + " eos=" + outEos);
        // Preserve codec-specific data independently: VPx config is often absent.
        try (DataOutputStream out = new DataOutputStream(new FileOutputStream(output))) {
            out.writeInt(w);
            out.writeInt(h);
            out.writeInt(fps);
            for (int i = 0; outFormat.containsKey("csd-" + i); i++) {
                ByteBuffer b = outFormat.getByteBuffer("csd-" + i).duplicate();
                byte[] d = new byte[b.remaining()];
                b.get(d);
                out.writeLong(0);
                out.writeInt(MediaCodec.BUFFER_FLAG_CODEC_CONFIG);
                out.writeInt(d.length);
                out.write(d);
            }
            for (Packet p : packets) {
                out.writeLong(p.pts);
                out.writeInt(p.flags);
                out.writeInt(p.data.length);
                out.write(p.data);
            }
        }
        System.out.printf(Locale.US, "PASS\tENC\t%s\t%s\t%dx%d\tframes=%d\tfps=%.2f\tfile=%s%n",
            name, mime, w, h, got, got / seconds, output);
    } finally {
        if (gl != null)
            gl.close();
        if (codec != null) {
            try {
                codec.stop();
            } finally {
                codec.release();
            }
        }
    }
}
public static void main(String[] args) {
    try {
        if (args[0].equals("surface-encode") && args.length >= 9)
            requestedProfile = Integer.parseInt(args[8]);
        if (args[0].equals("surface-encode") && args.length >= 10)
            requestedBitrate = Integer.parseInt(args[9]);
        switch (args[0]) {
            case "list":
                inventory();
                break;
            case "surface-decode":
                surfaceMode = true;
                decode(args[1], args[2], "-", Integer.parseInt(args[3]), Integer.parseInt(args[4]));
                break;
            case "decode-flush": flushMode = true;
                case "decode":
                decode(args[1], args[2], args[3], Integer.parseInt(args[4]),
                    Integer.parseInt(args[5]));
                break;
            case "surface-encode":
                surfaceMode = true;
                encode(args[1], args[2], "-", Integer.parseInt(args[3]), Integer.parseInt(args[4]),
                    Integer.parseInt(args[5]), Integer.parseInt(args[6]), args[7]);
                break;
            case "decode-packets":
                decode(args[1], args[3], args[4], Integer.parseInt(args[5]),
                    Integer.parseInt(args[6]), args[2]);
                break;
            case "encode":
                encode(args[1], args[2], args[3], Integer.parseInt(args[4]),
                    Integer.parseInt(args[5]), Integer.parseInt(args[6]), Integer.parseInt(args[7]),
                    args[8]);
                break;
            default:
                throw new Exception("Unknown command");
        }
    } catch (Throwable e) {
        System.out.println("FAIL\t" + Arrays.toString(args) + "\t" + e);
        e.printStackTrace(System.out);
        System.exit(1);
    }
}
}
