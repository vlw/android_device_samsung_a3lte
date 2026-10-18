// SPDX-License-Identifier: GPL-2.0
#include "StockEvs.h"
#include <jni.h>
#include <array>
#include <memory>
#include <mutex>
#include <unordered_map>
// Integer handles prevent stale Java handles from becoming native pointers.
static std::mutex lock;
static int64_t nextHandle = 1;
static std::unordered_map<int64_t, std::unique_ptr<StockEvs>> instances;
static void error(JNIEnv* env, const char* text) {
    env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), text);
}
extern "C" JNIEXPORT jlong JNICALL Java_me_phh_sip_StockEvsCodec_nativeOpen(JNIEnv* env,jclass) {
    std::lock_guard<std::mutex> guard(lock);
    try {
        auto codec = std::make_unique<StockEvs>();
        const auto handle = nextHandle++;
        instances.emplace(handle, std::move(codec));
        return handle;
    } catch (const std::exception& e) { error(env,e.what()); return 0; }
}
extern "C" JNIEXPORT void JNICALL Java_me_phh_sip_StockEvsCodec_nativeClose(JNIEnv*,jclass,jlong id) {
    std::lock_guard<std::mutex> guard(lock); instances.erase(id);
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_me_phh_sip_StockEvsCodec_nativeEncode(JNIEnv* env,jclass,jlong id,jbyteArray input) {
    std::lock_guard<std::mutex> guard(lock);
    if (env->GetArrayLength(input)!=1280 || !instances.count(id)) { error(env,"Invalid EVS PCM frame/handle"); return nullptr; }
    std::array<int16_t,640> pcm{}; std::array<uint8_t,512> bits{};
    env->GetByteArrayRegion(input,0,1280,reinterpret_cast<jbyte*>(pcm.data()));
    try { instances.at(id)->encode(pcm.data(),bits.data()); }
    catch (const std::exception& e) { error(env,e.what()); return nullptr; }
    auto result=env->NewByteArray(34);
    if(result) env->SetByteArrayRegion(result,0,34,reinterpret_cast<jbyte*>(bits.data()));
    return result;
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_me_phh_sip_StockEvsCodec_nativeDecode(JNIEnv* env,jclass,jlong id,jbyteArray input) {
    std::lock_guard<std::mutex> guard(lock);
    if (env->GetArrayLength(input)!=34 || !instances.count(id)) { error(env,"Invalid EVS speech frame/handle"); return nullptr; }
    std::array<uint8_t,34> bits{}; std::array<int16_t,640> pcm{};
    env->GetByteArrayRegion(input,0,34,reinterpret_cast<jbyte*>(bits.data()));
    if(bits[0]!=4) { error(env,"Unsupported EVS mode"); return nullptr; }
    try { instances.at(id)->decode(bits.data()+1,pcm.data()); }
    catch (const std::exception& e) { error(env,e.what()); return nullptr; }
    auto result=env->NewByteArray(1280);
    if(result) env->SetByteArrayRegion(result,0,1280,reinterpret_cast<jbyte*>(pcm.data()));
    return result;
}
