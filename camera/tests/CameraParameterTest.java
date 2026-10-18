/* SPDX-License-Identifier: Apache-2.0 */
// Opens cameras and checks advertised parameters. Never starts preview, takes
// a picture, records video, enables the torch or requests microphone access.
import android.hardware.Camera;
import java.util.List;
@SuppressWarnings("deprecation")
public final class CameraParameterTest {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }
    public static void main(String[] args) throws Exception {
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
        check(Camera.getNumberOfCameras() == 2, "Camera count");
        for (int id = 0; id < 2; ++id) {
            for (int repeat = 0; repeat < 3; ++repeat) {
                Camera camera = Camera.open(id);
                try {
                    Camera.Parameters parameters = camera.getParameters();
                    if (repeat == 0) {
                        List<Camera.Size> photos = parameters.getSupportedPictureSizes();
                        for (Camera.Size size : photos) {
                            parameters.setPictureSize(size.width, size.height);
                            camera.setParameters(parameters);
                            Camera.Size actual = camera.getParameters().getPictureSize();
                            check(actual.width == size.width && actual.height == size.height,
                                    "Picture size changed camera=" + id);
                        }
                        int previews = parameters.getSupportedPreviewSizes().size();
                        for (Camera.Size size : parameters.getSupportedPreviewSizes()) {
                            parameters.setPreviewSize(size.width, size.height);
                            camera.setParameters(parameters);
                            Camera.Size actual = camera.getParameters().getPreviewSize();
                            check(actual.width == size.width && actual.height == size.height,
                                    "Preview size changed camera=" + id);
                        }
                        List<Camera.Size> videos = parameters.getSupportedVideoSizes();
                        for (Camera.Size size : videos) {
                            parameters.set("video-size", size.width + "x" + size.height);
                            camera.setParameters(parameters);
                            check((size.width + "x" + size.height).equals(
                                    camera.getParameters().get("video-size")), "Video size changed");
                        }
                        int fps = 0;
                        for (int[] range : parameters.getSupportedPreviewFpsRange()) {
                            parameters.setPreviewFpsRange(range[0], range[1]);
                            camera.setParameters(parameters);
                            ++fps;
                        }
                        if (id == 0) {
                            parameters = camera.getParameters();
                            check(parameters.getSupportedSceneModes().contains("hdr"), "Missing HDR");
                            parameters.setSceneMode("hdr");
                            camera.setParameters(parameters);
                            parameters = camera.getParameters();
                            check("hdr".equals(parameters.getSceneMode()), "HDR scene");
                            check("jpeg".equals(parameters.getPictureFormat() == 256 ? "jpeg" : "other"),
                                    "HDR must expose JPEG to Camera1");
                            parameters.setRecordingHint(true);
                            camera.setParameters(parameters);
                            check(!"hdr".equals(camera.getParameters().getSceneMode()),
                                    "Recording hint must exit HDR");
                        }
                        System.out.println("PASS camera=" + id + " photos=" + photos.size()
                                + " previews=" + previews + " videos=" + videos.size()
                                + " fpsRanges=" + fps + ", parameters only, no frames");
                    }
                } finally { camera.release(); }
                System.out.println("PASS open/release camera=" + id + " repeat=" + repeat);
            }
        }
        System.out.println("PASS real HAL parameters and repeated open/release without capture");
        System.exit(0);
    }
}
