/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "android.hardware.nfc@1.0-service.samsung"

#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

#include "Nfc.h"

using android::sp;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::nfc::V1_0::implementation::Nfc;

int main() {
    sp<Nfc> nfc = Nfc::create();
    if (nfc == nullptr) {
        ALOGE("Failed to load the NFC HAL module");
        return 1;
    }

    configureRpcThreadpool(1, true /*callerWillJoin*/);
    if (nfc->registerAsService() != android::OK) {
        ALOGE("Failed to register the NFC HAL service");
        return 1;
    }
    joinRpcThreadpool();
    return 1;
}
