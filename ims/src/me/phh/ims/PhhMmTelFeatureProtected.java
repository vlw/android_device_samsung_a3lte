
package me.phh.ims;

import android.telephony.ims.feature.MmTelFeature;
import android.telephony.ims.feature.ImsFeature;
import android.telephony.ims.feature.CapabilityChangeRequest;
import android.telephony.Rlog;
import android.telephony.ims.stub.ImsRegistrationImplBase;

import java.util.List;

// intermediate class to MmTelFeature to extend
// changeEnabledCapabilities which cannot be done in kotlin
// see https://stackoverflow.com/questions/49284094/inheritance-from-java-class-with-a-public-method-accepting-a-protected-class-in/49287402#49287402
public class PhhMmTelFeatureProtected extends MmTelFeature {

    public interface CapabilityChangeCallback {
        void onChangeCapabilityConfigurationError(int capability, int radioTech, int reason);
    }

	private final static String TAG = "Phh MmTelFeatureProtected";
	private int slotId;
	private int capabilities;
	public PhhMmTelFeatureProtected(int slotId) {
		this.slotId = slotId;
		// Set what we want to support here.
		// Android will automatically remove capabilities if
		// something is missing, so setting too many should not
		// cause errors
		this.capabilities =
			MmTelCapabilities.CAPABILITY_TYPE_VOICE |
			MmTelCapabilities.CAPABILITY_TYPE_SMS;
	}

	@Override
	public void changeEnabledCapabilities(
	        android.telephony.ims.feature.CapabilityChangeRequest request,
	        android.telephony.ims.feature.ImsFeature.CapabilityCallbackProxy callback) {
	    onChangeEnabledCapabilities(request, new CapabilityChangeCallback() {
	        @Override
	        public void onChangeCapabilityConfigurationError(int capability, int radioTech, int reason) {
	            callback.onChangeCapabilityConfigurationError(capability, radioTech, reason);
	        }
	    });
	}

	protected void onChangeEnabledCapabilities(
	        android.telephony.ims.feature.CapabilityChangeRequest request,
	        CapabilityChangeCallback callback) {
	    for (android.telephony.ims.feature.CapabilityChangeRequest.CapabilityPair pair :
	            request.getCapabilitiesToEnable()) {
	        callback.onChangeCapabilityConfigurationError(
	                pair.getCapability(),
	                pair.getRadioTech(),
	                android.telephony.ims.feature.ImsFeature.CAPABILITY_SUCCESS);
	    }
	    for (android.telephony.ims.feature.CapabilityChangeRequest.CapabilityPair pair :
	            request.getCapabilitiesToDisable()) {
	        callback.onChangeCapabilityConfigurationError(
	                pair.getCapability(),
	                pair.getRadioTech(),
	                android.telephony.ims.feature.ImsFeature.CAPABILITY_SUCCESS);
	    }
	}


}
