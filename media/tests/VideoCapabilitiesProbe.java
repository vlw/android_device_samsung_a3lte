/* SPDX-License-Identifier: Apache-2.0 */
/* Read-only public MediaCodecInfo query; no codec allocation or service changes. */
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.util.Range;
import org.json.JSONArray;
import org.json.JSONObject;

public class VideoCapabilitiesProbe {
    static JSONArray range(Range<? extends Number> r) throws Exception {
        return r == null ? null : new JSONArray().put(r.getLower()).put(r.getUpper());
    }
    public static void main(String[] args) throws Exception {
        JSONArray all = new JSONArray();
        int[][] sizes = {{176,144},{352,288},{640,360},{640,480},{512,512},{1280,720},{1920,1080},{1920,1088},{1080,1920},{1920,1920},{2048,1080},{3840,2160}};
        for (MediaCodecInfo info : new MediaCodecList(MediaCodecList.ALL_CODECS).getCodecInfos()) {
            if (info.isAlias()) continue;
            for (String mime : info.getSupportedTypes()) {
                if (!mime.startsWith("video/")) continue;
                MediaCodecInfo.CodecCapabilities c = info.getCapabilitiesForType(mime);
                MediaCodecInfo.VideoCapabilities v = c.getVideoCapabilities();
                JSONObject j = new JSONObject().put("name", info.getName()).put("mime", mime)
                    .put("encoder", info.isEncoder()).put("hardware", info.isHardwareAccelerated())
                    .put("width", range(v.getSupportedWidths())).put("height", range(v.getSupportedHeights()))
                    .put("bitrate", range(v.getBitrateRange())).put("frameRate", range(v.getSupportedFrameRates()))
                    .put("maxInstances", c.getMaxSupportedInstances());
                JSONArray profiles = new JSONArray();
                for (MediaCodecInfo.CodecProfileLevel p : c.profileLevels)
                    profiles.put(new JSONObject().put("profile", p.profile).put("level", p.level));
                j.put("profiles", profiles);
                JSONArray samples = new JSONArray();
                for (int[] s : sizes) {
                    JSONObject sample = new JSONObject().put("width",s[0]).put("height",s[1])
                        .put("supported",v.isSizeSupported(s[0],s[1]));
                    if (v.isSizeSupported(s[0],s[1])) {
                        sample.put("supportedRates",range(v.getSupportedFrameRatesFor(s[0],s[1])));
                        Range<Double> rate = v.getAchievableFrameRatesFor(s[0],s[1]);
                        sample.put("achievableRates",rate == null ? JSONObject.NULL : range(rate));
                    }
                    samples.put(sample);
                }
                j.put("sizes",samples);
                j.put("performancePoints",String.valueOf(v.getSupportedPerformancePoints()));
                all.put(j);
            }
        }
        System.out.println(all.toString(2));
    }
}
