// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <keymaster/contexts/keymaster1_passthrough_context.h>
#include <keymaster/contexts/pure_soft_keymaster_context.h>

namespace a3tee {
// Preserve pre-existing software key blobs when Keystore2's requested-TEE
// UUID changes from its software fallback to the real hardware implementation.
// New key generation/import still goes through the KM1 hardware context.
class Csg3Context final : public keymaster::Keymaster1PassthroughContext {
  public:
    explicit Csg3Context(keymaster1_device_t* device);
    keymaster_error_t SetSystemVersion(uint32_t version, uint32_t patch) override;
    keymaster_error_t ParseKeyBlob(const keymaster::KeymasterKeyBlob& blob,
        const keymaster::AuthorizationSet& params,
        keymaster::UniquePtr<keymaster::Key>* key) const override;
  private:
    mutable keymaster::PureSoftKeymasterContext legacy_;
};
} // namespace a3tee
