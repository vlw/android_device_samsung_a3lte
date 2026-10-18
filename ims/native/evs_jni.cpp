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
extern "C" JNIEXPORT jlong JNICALL Java_me_phh_sip_StockEvsCodec_nativeOpen(JNIEnv* env,jclass,jint sampleRate) {
    std::lock_guard<std::mutex> guard(lock);
    try {
        auto codec = std::make_unique<StockEvs>(sampleRate);
        const auto handle = nextHandle++;
        instances.emplace(handle, std::move(codec));
        return handle;
    } catch (const std::exception& e) { error(env,e.what()); return 0; }
}
extern "C" JNIEXPORT void JNICALL Java_me_phh_sip_StockEvsCodec_nativeClose(JNIEnv*,jclass,jlong id) {
    std::lock_guard<std::mutex> guard(lock); instances.erase(id);
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_me_phh_sip_StockEvsCodec_nativeEncode(JNIEnv* env,jclass,jlong id,jbyteArray input,jint bitrate,jint bandwidth,jboolean dtx,jint rfOffset,jboolean rfHigh) {
    std::lock_guard<std::mutex> guard(lock);
    if (!instances.count(id) || env->GetArrayLength(input)!=instances.at(id)->samples()*2) { error(env,"Invalid EVS PCM frame/handle"); return nullptr; }
    std::array<int16_t,960> pcm{}; std::array<uint8_t,512> bits{};
    const int inputBytes = instances.at(id)->samples()*2;
    int size = 0;
    env->GetByteArrayRegion(input,0,inputBytes,reinterpret_cast<jbyte*>(pcm.data()));
    try { size = instances.at(id)->encode(pcm.data(),bits.data(),bitrate,bandwidth,dtx,rfOffset,rfHigh); }
    catch (const std::exception& e) { error(env,e.what()); return nullptr; }
    auto result=env->NewByteArray(size);
    if(result) env->SetByteArrayRegion(result,0,size,reinterpret_cast<jbyte*>(bits.data()));
    return result;
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_me_phh_sip_StockEvsCodec_nativeDecode(JNIEnv* env,jclass,jlong id,jbyteArray input) {
    std::lock_guard<std::mutex> guard(lock);
    if (!instances.count(id) || env->GetArrayLength(input)<1 || env->GetArrayLength(input)>321) { error(env,"Invalid EVS speech frame/handle"); return nullptr; }
    std::array<uint8_t,512> bits{}; std::array<int16_t,960> pcm{};
    const int size = env->GetArrayLength(input);
    env->GetByteArrayRegion(input,0,size,reinterpret_cast<jbyte*>(bits.data()));
    const int speechBits = StockEvs::speechBits(bits[0]);
    if(speechBits<0 || size != 1+(speechBits+7)/8) { error(env,"Unsupported EVS mode"); return nullptr; }
    try { instances.at(id)->decode(bits.data(),pcm.data()); }
    catch (const std::exception& e) { error(env,e.what()); return nullptr; }
    const int outputBytes = instances.at(id)->samples()*2;
    auto result=env->NewByteArray(outputBytes);
    if(result) env->SetByteArrayRegion(result,0,outputBytes,reinterpret_cast<jbyte*>(pcm.data()));
    return result;
}
