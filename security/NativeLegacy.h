// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <hardware/keymaster0.h>
#include <keymaster/contexts/pure_soft_keymaster_context.h>
#include <keymaster/legacy_support/keymaster0_engine.h>

namespace a3tee {
keymaster0_device_t* openNativeLegacy();
// API adaptation only: RSA private operations are in TEE; authorization policy,
// AES/HMAC/EC and credential verification retain their software security level.
class NativeLegacyContext final : public keymaster::PureSoftKeymasterContext {
  public:
    explicit NativeLegacyContext(keymaster0_device_t* device);
    keymaster::KeyFactory* GetKeyFactory(keymaster_algorithm_t algorithm) const override;
    keymaster_error_t CreateKeyBlob(const keymaster::AuthorizationSet&, keymaster_key_origin_t,
        const keymaster::KeymasterKeyBlob&, keymaster::KeymasterKeyBlob*,
        keymaster::AuthorizationSet*, keymaster::AuthorizationSet*) const override;
    keymaster::CertificateChain GenerateAttestation(const keymaster::Key&,
        const keymaster::AuthorizationSet&, keymaster::UniquePtr<keymaster::Key>,
        const keymaster::KeymasterBlob&, keymaster_error_t* error) const override;
  private:
    keymaster::Keymaster0Engine engine_;
    std::unique_ptr<keymaster::KeyFactory> rsa_;
};
} // namespace a3tee
