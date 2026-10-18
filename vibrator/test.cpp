/* SPDX-License-Identifier: Apache-2.0 */
#include <aidl/android/hardware/vibrator/IVibrator.h>
#include <android/binder_manager.h>
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <unistd.h>
using namespace aidl::android::hardware::vibrator;
static int intensity() {
    std::ifstream file("/sys/class/timed_output/vibrator/intensity");
    std::string label;
    int value = -1;
    file >> label >> value;
    assert(file && label == "intensity:");
    return value;
}
int main(int argc, char**) {
    auto vibrator = IVibrator::fromBinder(ndk::SpAIBinder(
            AServiceManager_checkService("android.hardware.vibrator.IVibrator/default")));
    assert(vibrator);
    int32_t caps;
    assert(vibrator->getCapabilities(&caps).isOk());
    assert(caps & IVibrator::CAP_AMPLITUDE_CONTROL);
    if (argc > 1) {
        // Reproduce the old service's unit mismatch without activating the motor.
        assert(vibrator->setAmplitude(0.25f).isOk());
        assert(intensity() == 10000);
        assert(!vibrator->setAmplitude(1.0f).isOk());
        std::cout << "CONFIRMED old HAL: 0.25 -> 10000; 1.0 rejected\n";
        return 0;
    }
    for (float value : {0.25f, 0.5f, 1.0f}) {
        assert(vibrator->setAmplitude(value).isOk());
        assert(intensity() == std::lround(value * 10000));
        std::cout << value << " -> " << intensity() << '\n';
    }
    for (float invalid : {0.f, -1.f, 1.1f, std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity()}) {
        assert(!vibrator->setAmplitude(invalid).isOk());
        assert(intensity() == 10000);
    }
    assert(!vibrator->on(-1, nullptr).isOk());
    assert(!vibrator->on(0, nullptr).isOk());
    for (auto strength : {EffectStrength::LIGHT, EffectStrength::MEDIUM, EffectStrength::STRONG}) {
        int32_t duration = 0;
        assert(vibrator->perform(Effect::CLICK, strength, nullptr, &duration).isOk());
        assert(duration == 10);
        int expected = strength == EffectStrength::LIGHT ? 2500 :
                       strength == EffectStrength::MEDIUM ? 5000 : 10000;
        assert(intensity() == expected);
        usleep(25000);
    }
    assert(vibrator->setAmplitude(0.5f).isOk());
    assert(vibrator->on(40, nullptr).isOk());
    usleep(60000);
    assert(vibrator->off().isOk());
    assert(intensity() == 5000);
    std::cout << "PASS amplitude validation, effect strengths, timed on/off; left at 5000\n";
}
