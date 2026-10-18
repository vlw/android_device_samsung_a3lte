/* SPDX-License-Identifier: Apache-2.0 */
#include "A3Vibrator.h"
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
using aidl::android::hardware::vibrator::A3Vibrator;
int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    auto vibrator = ndk::SharedRefBase::make<A3Vibrator>();
    const std::string instance = std::string(A3Vibrator::descriptor) + "/default";
    CHECK(AServiceManager_addService(vibrator->asBinder().get(), instance.c_str()) == STATUS_OK);
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;
}
