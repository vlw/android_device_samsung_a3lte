// SPDX-License-Identifier: Apache-2.0
#include "Csg3Context.h"
#include <keymaster/key_blob_utils/integrity_assured_key_blob.h>
#include <keymaster/key_blob_utils/software_keyblobs.h>
#include <keymaster/key.h>

namespace a3tee {
using namespace keymaster;
Csg3Context::Csg3Context(keymaster1_device_t* device)
    : Keymaster1PassthroughContext(KmVersion::KEYMASTER_3,device),
      legacy_(KmVersion::KEYMASTER_4_1) {}
keymaster_error_t Csg3Context::SetSystemVersion(uint32_t version,uint32_t patch) {
    auto error=Keymaster1PassthroughContext::SetSystemVersion(version,patch);
    return error?error:legacy_.SetSystemVersion(version,patch);
}
keymaster_error_t Csg3Context::ParseKeyBlob(const KeymasterKeyBlob& blob,
    const AuthorizationSet& params,UniquePtr<Key>* key) const {
    KeymasterKeyBlob material;
    AuthorizationSet hw,sw;
    // This first parse only classifies the envelope. The legacy context below
    // independently verifies its HMAC and application binding before using it.
    auto error=DeserializeIntegrityAssuredBlob_NoHmacCheck(blob,&material,&hw,&sw);
    if(error==KM_ERROR_OK && hw.empty()) {
        // Auth-bound software keys need a separate migration: hardware GK HATs
        // do not authenticate old software SIDs. Never silently bypass policy.
        if(!sw.Contains(TAG_NO_AUTH_REQUIRED)) return KM_ERROR_KEY_USER_NOT_AUTHENTICATED;
        return legacy_.ParseKeyBlob(blob,params,key);
    }
    return Keymaster1PassthroughContext::ParseKeyBlob(blob,params,key);
}
} // namespace a3tee
