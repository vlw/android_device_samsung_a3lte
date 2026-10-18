/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <Vibrator.h>

namespace aidl::android::hardware::vibrator {
// Reuse Samsung's AIDL boilerplate; adapt only the a3lte amplitude/effect path.
class A3Vibrator : public Vibrator {
public:
    A3Vibrator();
    ndk::ScopedAStatus getCapabilities(int32_t* capabilities) override;
    ndk::ScopedAStatus setAmplitude(float amplitude) override;
    ndk::ScopedAStatus on(int32_t timeoutMs, const std::shared_ptr<IVibratorCallback>& callback) override;
    ndk::ScopedAStatus perform(Effect effect, EffectStrength strength,
            const std::shared_ptr<IVibratorCallback>& callback, int32_t* duration) override;
};
}
