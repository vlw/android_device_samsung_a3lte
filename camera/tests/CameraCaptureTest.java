/* SPDX-License-Identifier: Apache-2.0 */
import android.graphics.SurfaceTexture;
import android.hardware.Camera;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES20;
import android.os.Handler;
import android.os.HandlerThread;
import java.io.FileOutputStream;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Root app_process diagnostic. Captures only to the explicit supplied path. */
public final class CameraCaptureTest {
    static Camera camera;
    static SurfaceTexture texture;
    static int frames;
    static Throwable failure;
    static final CountDownLatch done = new CountDownLatch(1);

    static void save(String path, byte[] data) throws Exception {
        try (FileOutputStream out = new FileOutputStream(path)) { out.write(data); }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 5) throw new IllegalArgumentException("id scene width height prefix");
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
        HandlerThread thread = new HandlerThread("CameraCaptureTest");
        thread.start();
        Handler handler = new Handler(thread.getLooper());
        handler.post(() -> {
            try {
                EGLDisplay display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
                int[] version = new int[2];
                if (!EGL14.eglInitialize(display, version, 0, version, 1))
                    throw new AssertionError("EGL initialize");
                int[] attributes = {EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT,
                    EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
                    EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8,
                    EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_NONE};
                EGLConfig[] configs = new EGLConfig[1];
                int[] count = new int[1];
                EGL14.eglChooseConfig(display, attributes, 0, configs, 0, 1, count, 0);
                EGLContext context = EGL14.eglCreateContext(display, configs[0],
                    EGL14.EGL_NO_CONTEXT, new int[]{EGL14.EGL_CONTEXT_CLIENT_VERSION, 2,
                    EGL14.EGL_NONE}, 0);
                EGLSurface surface = EGL14.eglCreatePbufferSurface(display, configs[0],
                    new int[]{EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE}, 0);
                if (!EGL14.eglMakeCurrent(display, surface, surface, context))
                    throw new AssertionError("EGL make current");
                int[] name = new int[1];
                GLES20.glGenTextures(1, name, 0);
                texture = new SurfaceTexture(name[0]);
                texture.setOnFrameAvailableListener(t -> {
                    t.updateTexImage();
                    frames++;
                }, handler);
                camera = Camera.open(Integer.parseInt(args[0]));
                camera.setErrorCallback((error, c) -> {
                    failure = new AssertionError("Camera error " + error);
                    done.countDown();
                });
                Camera.Parameters parameters = camera.getParameters();
                save(args[4] + "-initial.txt", parameters.flatten().getBytes("UTF-8"));
                System.out.println("scenes=" + parameters.getSupportedSceneModes());
                System.out.println("preview-sizes=" + parameters.get("preview-size-values"));
                parameters.setPreviewSize(Integer.parseInt(args[2]), Integer.parseInt(args[3]));
                parameters.setSceneMode(args[1]);
                parameters.setFlashMode(Camera.Parameters.FLASH_MODE_OFF);
                parameters.setJpegQuality(96);
                if (parameters.getSupportedFocusModes().contains("continuous-picture"))
                    parameters.setFocusMode("continuous-picture");
                camera.setParameters(parameters);
                camera.setPreviewTexture(texture);
                camera.startPreview();
                handler.postDelayed(() -> {
                    try {
                        if (frames < 5) throw new AssertionError("Only " + frames + " preview frames");
                        save(args[4] + "-active.txt", camera.getParameters().flatten().getBytes("UTF-8"));
                        camera.takePicture(null, null, (data, c) -> {
                            try {
                                save(args[4] + ".jpg", data);
                                if (data.length < 4 || (data[0] & 255) != 255
                                        || (data[1] & 255) != 216)
                                    throw new AssertionError("Not a JPEG: " + data.length + " bytes");
                                System.out.println("JPEG=" + data.length + " preview-frames=" + frames);
                            } catch (Throwable e) { failure = e; }
                            done.countDown();
                        });
                    } catch (Throwable e) { failure = e; done.countDown(); }
                }, 2500);
            } catch (Throwable e) { failure = e; done.countDown(); }
        });
        if (!done.await(30, TimeUnit.SECONDS)) failure = new AssertionError("Capture timeout");
        CountDownLatch closed = new CountDownLatch(1);
        handler.post(() -> {
            if (camera != null) { camera.stopPreview(); camera.release(); }
            if (texture != null) texture.release();
            closed.countDown();
        });
        closed.await(5, TimeUnit.SECONDS);
        if (failure != null) { failure.printStackTrace(); System.exit(1); }
        System.out.println("Camera capture passed");
        System.exit(0);
    }
}
