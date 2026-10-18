/* SPDX-License-Identifier: Apache-2.0 */
#include "A3Vibrator.h"
#include <android-base/logging.h>
#include <algorithm>
#include <cmath>
#include <fstream>

namespace aidl::android::hardware::vibrator {
A3Vibrator::A3Vibrator() {
    auto status = off();
    if (status.isOk()) status = setAmplitude(0.5f);
    if (!status.isOk()) LOG(ERROR) << "Cannot initialize a3lte vibrator";
}

ndk::ScopedAStatus A3Vibrator::getCapabilities(int32_t* capabilities) {
    auto status = Vibrator::getCapabilities(capabilities);
    // ss_vibrator has no external audio/haptic control interface.
    *capabilities &= ~(IVibrator::CAP_EXTERNAL_CONTROL | IVibrator::CAP_EXTERNAL_AMPLITUDE_CONTROL);
    return status;
}

ndk::ScopedAStatus A3Vibrator::setAmplitude(float amplitude) {
    // AIDL uses (0,1], unlike the old HIDL byte amplitude (1..255).
    if (!std::isfinite(amplitude) || amplitude <= 0 || amplitude > 1)
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    const int intensity = std::max(1L, std::lround(amplitude * 10000));
    std::ofstream node(VIBRATOR_INTENSITY_PATH);
    node << intensity << std::endl;
    if (!node) {
        LOG(ERROR) << "Cannot write vibrator intensity " << intensity;
        return ndk::ScopedAStatus::fromStatus(STATUS_UNKNOWN_ERROR);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus A3Vibrator::on(int32_t timeoutMs,
        const std::shared_ptr<IVibratorCallback>& callback) {
    if (timeoutMs <= 0) return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    return Vibrator::on(timeoutMs, callback);
}

ndk::ScopedAStatus A3Vibrator::perform(Effect effect, EffectStrength strength,
        const std::shared_ptr<IVibratorCallback>& callback, int32_t* duration) {
    // Retain Samsung's timed-output fallback effects, but pass normalized AIDL
    // amplitudes. Its original perform() passes byte amplitudes to setAmplitude.
    float amplitude;
    switch (strength) {
        case EffectStrength::LIGHT: amplitude = 0.25f; break;
        case EffectStrength::MEDIUM: amplitude = 0.5f; break;
        case EffectStrength::STRONG: amplitude = 1.0f; break;
        default: return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    switch (effect) {
        case Effect::CLICK:
        case Effect::HEAVY_CLICK: *duration = 10; break;
        case Effect::DOUBLE_CLICK: *duration = 15; break;
        case Effect::TICK:
        case Effect::TEXTURE_TICK:
        case Effect::THUD:
        case Effect::POP: *duration = 5; break;
        default:
            if (effect < Effect::RINGTONE_1 || effect > Effect::RINGTONE_15)
                return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
            *duration = 30000;
    }
    auto status = off();
    if (status.isOk()) status = setAmplitude(amplitude);
    if (status.isOk()) status = on(*duration, callback);
    return status;
}
}
