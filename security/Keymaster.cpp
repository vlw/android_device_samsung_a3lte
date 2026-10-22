// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2026 The LineageOS Project
#include "Keymaster.h"
#include "KeyMaterial.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <new>

namespace a3tee {
namespace {
constexpr auto kBadReply = KM_ERROR_SECURE_HW_COMMUNICATION_FAILED;
struct Response : std::vector<uint8_t> { ~Response() { wipe(data(), size()); } };
struct Operation { keymaster_purpose_t purpose; uint32_t algorithm; };
struct Keymaster {
    keymaster1_device_t device{};
    std::unique_ptr<Transport> transport;
    std::mutex mutex;
    std::map<uint64_t, Operation> operations;
};
Keymaster* context(const keymaster1_device_t* dev) { return static_cast<Keymaster*>(dev->context); }
void freeBlob(keymaster_blob_t* blob) {
    wipe(const_cast<uint8_t*>(blob->data), blob->data_length);
    free(const_cast<uint8_t*>(blob->data));
    *blob = {};
}
keymaster_error_t send(Keymaster* km, Message& req, Response* res, InitialReply initial = {}) {
    if (!req.valid() || req.bytes().size() > kSharedSize-256) return KM_ERROR_INVALID_INPUT_LENGTH;
    if (!km->transport->transact(req, res, initial)) return kBadReply;
    auto status = Reply(*res).status();
    return status > 0 ? kBadReply : status;
}
bool validKey(const keymaster_key_blob_t* key) {
    return key && key->key_material && key->key_material_size && key->key_material_size < kSharedSize;
}
keymaster_error_t abortLocked(Keymaster* km, uint64_t operation) {
    Message req(12, 0x113);
    req.put64(4, operation);
    Response res;
    km->operations.erase(operation);
    return send(km, req, &res);
}
bool characteristics(const Reply& res, size_t field, size_t header,
                     keymaster_key_characteristics_t** out) {
    if (!out) return true;
    *out = nullptr;
    if (!res.range(field, 16) || res.word(field+4)%kParamSize || res.word(field+12)%kParamSize) return false;
    auto* c = static_cast<keymaster_key_characteristics_t*>(calloc(1, sizeof(keymaster_key_characteristics_t)));
    if (!c) return false;
    if (!res.params(res.word(field), res.word(field+4)/kParamSize, header, &c->hw_enforced) ||
        !res.params(res.word(field+8), res.word(field+12)/kParamSize, header, &c->sw_enforced)) {
        freeCharacteristics(c); return false;
    }
    *out = c;
    return true;
}
keymaster_error_t generated(const Reply& res, keymaster_key_blob_t* key,
                           keymaster_key_characteristics_t** out) {
    keymaster_blob_t blob{};
    if (!res.range(0, 32) || !res.blob(8, 32, &blob) || !blob.data_length) return kBadReply;
    if (!characteristics(res, 16, 32, out)) { freeBlob(&blob); return kBadReply; }
    *key = {blob.data, blob.data_length};
    return KM_ERROR_OK;
}
template <typename Enum>
keymaster_error_t supported(const keymaster1_device_t* dev, uint32_t command,
                            uint32_t algorithm, uint32_t purpose, Enum** values, size_t* count) {
    if (!dev || !values || !count) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    *values = nullptr; *count = 0;
    auto* km = context(dev);
    std::lock_guard<std::mutex> lock(km->mutex);
    size_t header = command == 0x101 ? 4 : (command < 0x105 ? 12 : 8);
    Message req(header, command);
    if (header >= 8) req.put(4, algorithm);
    if (header >= 12) req.put(8, purpose);
    Response bytes;
    auto error = send(km, req, &bytes);
    if (error) return error;
    Reply res(bytes);
    size_t size = res.word(4);
    if (!res.range(0, 8) || size%4 || size/4 > 128 || !res.range(8, size)) return kBadReply;
    if (!size) return KM_ERROR_OK;
    auto* out = static_cast<Enum*>(malloc(size));
    if (!out) return KM_ERROR_MEMORY_ALLOCATION_FAILED;
    for (size_t i = 0; i < size/4; ++i) out[i] = static_cast<Enum>(res.word(8+i*4));
    *values = out; *count = size/4;
    return KM_ERROR_OK;
}
keymaster_error_t algorithms(const keymaster1_device_t* d, keymaster_algorithm_t** v, size_t* n) {
    return supported(d, 0x101, 0, 0, v, n);
}
keymaster_error_t modes(const keymaster1_device_t* d, keymaster_algorithm_t a, keymaster_purpose_t p,
                       keymaster_block_mode_t** v, size_t* n) { return supported(d, 0x102, a, p, v, n); }
keymaster_error_t paddings(const keymaster1_device_t* d, keymaster_algorithm_t a, keymaster_purpose_t p,
                          keymaster_padding_t** v, size_t* n) { return supported(d, 0x103, a, p, v, n); }
keymaster_error_t digests(const keymaster1_device_t* d, keymaster_algorithm_t a, keymaster_purpose_t p,
                         keymaster_digest_t** v, size_t* n) { return supported(d, 0x104, a, p, v, n); }
keymaster_error_t imports(const keymaster1_device_t* d, keymaster_algorithm_t a,
                         keymaster_key_format_t** v, size_t* n) { return supported(d, 0x105, a, 0, v, n); }
keymaster_error_t exports(const keymaster1_device_t* d, keymaster_algorithm_t a,
                         keymaster_key_format_t** v, size_t* n) { return supported(d, 0x106, a, 0, v, n); }
keymaster_error_t entropy(const keymaster1_device_t*, const uint8_t*, size_t) {
    // CSG3 command 0x107 is an empty dispatcher branch. Never return a fake
    // successful entropy injection. Key generation uses the TA's own RNG.
    return KM_ERROR_UNIMPLEMENTED;
}
keymaster_error_t generate(const keymaster1_device_t* dev, const keymaster_key_param_set_t* params,
                           keymaster_key_blob_t* key, keymaster_key_characteristics_t** out) {
    if (key) *key = {};
    if (out) *out = nullptr;
    if (!dev || !params || !key) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    auto* km = context(dev);
    std::lock_guard<std::mutex> lock(km->mutex);
    Message req(12, 0x108);
    if (!req.params(4, params)) return KM_ERROR_INVALID_ARGUMENT;
    Response bytes;
    auto error = send(km, req, &bytes);
    return error ? error : generated(Reply(bytes), key, out);
}
keymaster_error_t getCharacteristics(const keymaster1_device_t* dev, const keymaster_key_blob_t* key,
                                    const keymaster_blob_t* client, const keymaster_blob_t* app,
                                    keymaster_key_characteristics_t** out) {
    if (out) *out = nullptr;
    if (!dev || !out) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    if (!validKey(key)) return KM_ERROR_INVALID_KEY_BLOB;
    auto* km = context(dev);
    std::lock_guard<std::mutex> lock(km->mutex);
    Message req(28, 0x109);
    req.blob(4, key->key_material, key->key_material_size);
    if (client) req.blob(12, client->data, client->data_length);
    if (app) req.blob(20, app->data, app->data_length);
    Response bytes;
    auto error = send(km, req, &bytes);
    if (error) return error;
    return characteristics(Reply(bytes), 8, 24, out) ? KM_ERROR_OK : kBadReply;
}
keymaster_error_t importKey(const keymaster1_device_t* dev, const keymaster_key_param_set_t* params,
                           keymaster_key_format_t format, const keymaster_blob_t* data,
                           keymaster_key_blob_t* key, keymaster_key_characteristics_t** out) {
    if (key) *key = {};
    if (out) *out = nullptr;
    if (!dev || !params || !data || !key) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    if (!data->data || !data->data_length || data->data_length > kSharedSize ||
        params->length > kMaxParams || (params->length && !params->params)) return KM_ERROR_INVALID_ARGUMENT;
    uint32_t algorithm = 0;
    for (size_t i = 0; i < params->length; ++i) {
        if (params->params[i].tag == KM_TAG_ALGORITHM) {
            if (algorithm) return KM_ERROR_INVALID_ARGUMENT;
            algorithm = params->params[i].enumerated;
        }
    }
    if (!algorithm) return KM_ERROR_UNSUPPORTED_ALGORITHM;
    auto* km = context(dev);
    std::lock_guard<std::mutex> lock(km->mutex);
    Message req(24, 0x10b);
    req.put(4, format);
    if (!req.params(8, params)) return KM_ERROR_INVALID_ARGUMENT;
    if (algorithm == KM_ALGORITHM_EC) {
        if (format != KM_KEY_FORMAT_PKCS8) return KM_ERROR_UNSUPPORTED_KEY_FORMAT;
        Message internal(28);
        if (!ecImport(*data, &internal)) return KM_ERROR_INVALID_ARGUMENT;
        req.blob(16, internal.bytes().data(), internal.bytes().size());
    } else req.blob(16, data->data, data->data_length);
    Response bytes;
    auto error = send(km, req, &bytes);
    return error ? error : generated(Reply(bytes), key, out);
}
keymaster_error_t exportKey(const keymaster1_device_t* dev, keymaster_key_format_t format,
                           const keymaster_key_blob_t* key, const keymaster_blob_t* client,
                           const keymaster_blob_t* app, keymaster_blob_t* out) {
    if (out) *out = {};
    if (!dev || !out) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    if (!validKey(key)) return KM_ERROR_INVALID_KEY_BLOB;
    if (format != KM_KEY_FORMAT_X509) return KM_ERROR_UNSUPPORTED_KEY_FORMAT;
    auto* km = context(dev);
    std::lock_guard<std::mutex> lock(km->mutex);
    Message req(32, 0x10c);
    req.blob(4, key->key_material, key->key_material_size);
    req.put(12, format);
    if (client) req.blob(16, client->data, client->data_length);
    if (app) req.blob(24, app->data, app->data_length);
    Response bytes;
    size_t capacity = responseCapacity(req.bytes().size());
    auto error = send(km, req, &bytes, {{12, 20}, {16, static_cast<uint32_t>(capacity-20)}});
    if (error) return error;
    return publicKey(Reply(bytes), out) ? KM_ERROR_OK : kBadReply;
}
keymaster_error_t deleteKey(const keymaster1_device_t* dev, const keymaster_key_blob_t* key) {
    if (!dev) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    if (!validKey(key)) return KM_ERROR_INVALID_KEY_BLOB;
    auto* km = context(dev);
    std::lock_guard<std::mutex> lock(km->mutex);
    Message req(12, 0x10d); req.blob(4, key->key_material, key->key_material_size);
    Response bytes; return send(km, req, &bytes);
}
keymaster_error_t deleteAll(const keymaster1_device_t* dev) {
    if (!dev) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    auto* km = context(dev);
    std::lock_guard<std::mutex> lock(km->mutex);
    // Never invoked as part of service startup or migration.
    Message req(4, 0x10e); Response bytes; return send(km, req, &bytes);
}
keymaster_error_t begin(const keymaster1_device_t* dev, keymaster_purpose_t purpose,
                        const keymaster_key_blob_t* key, const keymaster_key_param_set_t* params,
                        keymaster_key_param_set_t* out, keymaster_operation_handle_t* operation) {
    if (out) *out = {};
    if (operation) *operation = 0;
    if (!dev || !operation) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    if (!validKey(key)) return KM_ERROR_INVALID_KEY_BLOB;
    auto* km = context(dev);
    std::lock_guard<std::mutex> lock(km->mutex);
    if (km->operations.size() >= 32) return KM_ERROR_TOO_MANY_OPERATIONS;
    Message req(24, 0x10f);
    req.put(4, purpose);
    req.blob(8, key->key_material, key->key_material_size);
    if (!req.params(16, params)) return KM_ERROR_INVALID_ARGUMENT;
    Response bytes;
    auto error = send(km, req, &bytes);
    if (error) return error;
    Reply res(bytes);
    if (!res.range(0, 28)) return kBadReply;
    uint64_t handle = res.wide(16);
    if (!handle || km->operations.count(handle)) return kBadReply;
    keymaster_key_param_set_t output{};
    if (!res.params(res.word(8), res.word(12), 28, &output) || (output.length && !out)) {
        freeParams(&output); abortLocked(km, handle); return kBadReply;
    }
    km->operations.emplace(handle, Operation{purpose, res.word(24)});
    if (out) *out = output;
    *operation = handle;
    return KM_ERROR_OK;
}
keymaster_error_t abort(const keymaster1_device_t* dev, keymaster_operation_handle_t operation) {
    if (!dev) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    auto* km = context(dev);
    std::lock_guard<std::mutex> lock(km->mutex);
    if (!km->operations.count(operation)) return KM_ERROR_INVALID_OPERATION_HANDLE;
    return abortLocked(km, operation);
}
keymaster_error_t update(const keymaster1_device_t* dev, keymaster_operation_handle_t operation,
                         const keymaster_key_param_set_t* params, const keymaster_blob_t* input,
                         size_t* consumed, keymaster_key_param_set_t* outParams, keymaster_blob_t* out) {
    if (consumed) *consumed = 0;
    if (outParams) *outParams = {};
    if (out) *out = {};
    if (!dev || !consumed) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    auto* km = context(dev);
    std::lock_guard<std::mutex> lock(km->mutex);
    if (!km->operations.count(operation)) return KM_ERROR_INVALID_OPERATION_HANDLE;
    Message req(28, 0x111); req.put64(4, operation);
    size_t inputSize = input ? std::min(input->data_length, kChunkSize) : 0;
    if (!req.params(12, params) || (input && !req.blob(20, input->data, inputSize))) {
        abortLocked(km, operation); return KM_ERROR_INVALID_ARGUMENT;
    }
    Response bytes;
    size_t capacity = responseCapacity(req.bytes().size());
    auto error = send(km, req, &bytes, {{8, 20}, {12, static_cast<uint32_t>(capacity-20)}});
    if (error) { abortLocked(km, operation); return error; }
    Reply res(bytes);
    keymaster_blob_t output{};
    if (!res.range(0, 20) || res.word(16) > inputSize || !res.blob(8, 20, &output) ||
        (output.data_length && !out)) {
        freeBlob(&output); abortLocked(km, operation); return kBadReply;
    }
    *consumed = res.word(16);
    if (out) *out = output;
    return KM_ERROR_OK;
}
keymaster_error_t finish(const keymaster1_device_t* dev, keymaster_operation_handle_t operation,
                         const keymaster_key_param_set_t* params, const keymaster_blob_t* signature,
                         keymaster_key_param_set_t* outParams, keymaster_blob_t* out) {
    if (outParams) *outParams = {};
    if (out) *out = {};
    if (!dev) return KM_ERROR_UNEXPECTED_NULL_POINTER;
    auto* km = context(dev);
    std::lock_guard<std::mutex> lock(km->mutex);
    auto it = km->operations.find(operation);
    if (it == km->operations.end()) return KM_ERROR_INVALID_OPERATION_HANDLE;
    const Operation op = it->second;
    Message req(28, 0x112); req.put64(4, operation);
    if (!req.params(12, params)) { abortLocked(km, operation); return KM_ERROR_INVALID_ARGUMENT; }
    if (signature && signature->data_length) {
        if (op.algorithm == KM_ALGORITHM_EC && op.purpose == KM_PURPOSE_VERIFY) {
            Message internal(16);
            if (!signatureToWire(*signature, &internal)) {
                abortLocked(km, operation); return KM_ERROR_INVALID_ARGUMENT;
            }
            req.blob(20, internal.bytes().data(), internal.bytes().size());
        } else req.blob(20, signature->data, signature->data_length);
    }
    Response bytes;
    size_t capacity = responseCapacity(req.bytes().size());
    auto error = send(km, req, &bytes, {{8, 16}, {12, static_cast<uint32_t>(capacity-16)}});
    // A failed finish must not leave a live TA operation behind.
    if (error) { abortLocked(km, operation); return error; }
    km->operations.erase(operation);
    Reply res(bytes);
    keymaster_blob_t output{};
    if (!res.range(0, 16) || !res.blob(8, 16, &output) || (output.data_length && !out)) {
        freeBlob(&output); return kBadReply;
    }
    if (op.algorithm == KM_ALGORITHM_EC && op.purpose == KM_PURPOSE_SIGN) {
        keymaster_blob_t der{};
        bool ok = signatureFromWire(output, &der);
        freeBlob(&output);
        if (!ok) return kBadReply;
        output = der;
    }
    if (out) *out = output;
    return KM_ERROR_OK;
}
int close(hw_device_t* device) {
    auto* km = context(reinterpret_cast<keymaster1_device_t*>(device));
    // Secure-side operation state is released before the last QSEE handle.
    while (!km->operations.empty()) abortLocked(km, km->operations.begin()->first);
    delete km;
    return 0;
}
}  // namespace
keymaster1_device_t* makeKeymaster(const hw_module_t* module, std::unique_ptr<Transport> transport) {
    if (!transport) return nullptr;
    auto* km = new (std::nothrow) Keymaster;
    if (!km) return nullptr;
    km->transport = std::move(transport);
    auto& d = km->device;
    d.common.tag = HARDWARE_DEVICE_TAG;
    d.common.version = KEYMASTER_DEVICE_API_VERSION_1_0;
    d.common.module = const_cast<hw_module_t*>(module);
    d.common.close = close;
    d.context = km;
    d.flags = KEYMASTER_BLOBS_ARE_STANDALONE;
    d.get_supported_algorithms = algorithms;
    d.get_supported_block_modes = modes;
    d.get_supported_padding_modes = paddings;
    d.get_supported_digests = digests;
    d.get_supported_import_formats = imports;
    d.get_supported_export_formats = exports;
    d.add_rng_entropy = entropy;
    d.generate_key = generate;
    d.get_key_characteristics = getCharacteristics;
    d.import_key = importKey;
    d.export_key = exportKey;
    d.delete_key = deleteKey;
    d.delete_all_keys = deleteAll;
    d.begin = begin;
    d.update = update;
    d.finish = finish;
    d.abort = abort;
    return &d;
}
}  // namespace a3tee
