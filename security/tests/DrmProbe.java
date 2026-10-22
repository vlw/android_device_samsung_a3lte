// SPDX-License-Identifier: Apache-2.0
// Queries capability and opens/closes sessions only. No network, licenses,
// provisioning responses, device identifiers or keybox access.
import android.media.MediaDrm;
import java.util.UUID;
public class DrmProbe {
    public static void main(String[] args) throws Exception {
        android.os.Looper.prepareMainLooper();
        Class.forName("android.app.ActivityThread").getMethod("systemMain").invoke(null);
        UUID widevine=UUID.fromString("edef8ba9-79d6-4ace-a3c8-27dcd51d21ed");
        System.out.println("supported="+MediaDrm.isCryptoSchemeSupported(widevine));
        try (MediaDrm drm=new MediaDrm(widevine)) {
            for(String property:new String[]{"vendor","version","description","securityLevel","systemId","algorithms"}) {
                try {System.out.println(property+"="+drm.getPropertyString(property));}
                catch(Exception e){System.out.println(property+"="+e.getClass().getSimpleName());}
            }
            System.out.println("maxApiSecurityLevel="+MediaDrm.getMaxSecurityLevel());
            for(int level:new int[]{MediaDrm.SECURITY_LEVEL_HW_SECURE_ALL,MediaDrm.SECURITY_LEVEL_SW_SECURE_CRYPTO}) {
                try {
                    byte[] session=drm.openSession(level);
                    System.out.println("session requested="+level+" actual="+drm.getSecurityLevel(session));
                    drm.closeSession(session);
                } catch(Exception e) {System.out.println("session requested="+level+" error="+e.getClass().getSimpleName()+": "+e.getMessage());}
            }
        }
    }
}
