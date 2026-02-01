#ifndef BAS_UTILS_SERIALIZATION_SERDE_UTILS_H
#define BAS_UTILS_SERIALIZATION_SERDE_UTILS_H

#include "serde_types.h"

namespace bas_serde {

std::string GetNapiErrorMessage(napi_env env);

Napi::Value ReplaceCallback(const Napi::CallbackInfo &info);

bool SeenContains(const SeenStack &seen, const Napi::Value &value);
int FindSeenId(const SeenEntries &entries, const Napi::Value &value);

bool IsBufferInstance(const Napi::Env &env, const Napi::Value &value);

std::string TypedArrayName(napi_typedarray_type type);
size_t TypedArrayBytesPerElement(napi_typedarray_type type);

Napi::Object MakeWrapper(Napi::Env env, const char *type);
Napi::Object MakeWrapper(Napi::Env env, const char *type, const Napi::Value &value);
Napi::Object MakeWrapperWithId(Napi::Env env, const char *type, uint32_t id);
Napi::Object MakeReference(Napi::Env env, uint32_t id);
Napi::Object MakePropKeyString(Napi::Env env, const Napi::Value &value);
Napi::Object MakePropKeySymbol(Napi::Env env, bool isGlobal, const Napi::Value &keyOrDesc);
void SetIdIfNeeded(Napi::Env env, Napi::Object &obj, bool hasId, uint32_t id);

Napi::Value GetRefValue(DecodeContext &ctx, uint32_t id, const Napi::Env &env);
void StoreRef(DecodeContext &ctx, uint32_t id, const Napi::Value &value);

std::string Base64Encode(const uint8_t *data, size_t len);
std::vector<uint8_t> Base64Decode(const std::string &input);

bool IsWrapperType(const Napi::Env &env, const Napi::Value &value, const char *type);
bool IsKnownWrapperType(const std::string &t);

}  // namespace bas_serde

#endif
