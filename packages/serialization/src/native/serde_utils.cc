#include "serde_utils.h"

#include <cmath>
#include <cstring>

namespace bas_serde {

constexpr const char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Pulls the last N-API error message for diagnostics.
std::string GetNapiErrorMessage(napi_env env) {
  const napi_extended_error_info *info = nullptr;
  napi_get_last_error_info(env, &info);
  if (info && info->error_message) {
    return info->error_message;
  }
  return "unknown napi error";
}

// Replacer callback used by stringify; stores replacement value in ReplaceState.
Napi::Value ReplaceCallback(const Napi::CallbackInfo &info) {
  auto *state = static_cast<ReplaceState *>(info.Data());
  state->replaced = true;
  state->value = info.Length() > 0 ? info[0] : info.Env().Undefined();
  return info.Env().Undefined();
}

// Detects if a value is in the current recursion stack.
bool SeenContains(const SeenStack &seen, const Napi::Value &value) {
  for (const auto &ref : seen) {
    if (value.StrictEquals(ref.Value())) {
      return true;
    }
  }
  return false;
}

// Finds a previously assigned id for circular reference support.
int FindSeenId(const SeenEntries &entries, const Napi::Value &value) {
  for (const auto &entry : entries) {
    if (value.StrictEquals(entry.ref.Value())) {
      return static_cast<int>(entry.id);
    }
  }
  return -1;
}

// Buffer should be detected via instanceof to avoid TypedArray/DataView conflicts.
bool IsBufferInstance(const Napi::Env &env, const Napi::Value &value) {
  if (!value.IsObject()) {
    return false;
  }
  Napi::Value ctorValue = env.Global().Get("Buffer");
  if (!ctorValue.IsFunction()) {
    return false;
  }
  return value.As<Napi::Object>().InstanceOf(ctorValue.As<Napi::Function>());
}

// Maps N-API typed array kinds to constructor names.
std::string TypedArrayName(napi_typedarray_type type) {
  switch (type) {
    case napi_int8_array:
      return "Int8Array";
    case napi_uint8_array:
      return "Uint8Array";
    case napi_uint8_clamped_array:
      return "Uint8ClampedArray";
    case napi_int16_array:
      return "Int16Array";
    case napi_uint16_array:
      return "Uint16Array";
    case napi_int32_array:
      return "Int32Array";
    case napi_uint32_array:
      return "Uint32Array";
    case napi_float32_array:
      return "Float32Array";
    case napi_float64_array:
      return "Float64Array";
    case napi_bigint64_array:
      return "BigInt64Array";
    case napi_biguint64_array:
      return "BigUint64Array";
    default:
      return "";
  }
}

// Maps N-API typed array kinds to element sizes.
size_t TypedArrayBytesPerElement(napi_typedarray_type type) {
  switch (type) {
    case napi_int8_array:
    case napi_uint8_array:
    case napi_uint8_clamped_array:
      return 1;
    case napi_int16_array:
    case napi_uint16_array:
      return 2;
    case napi_int32_array:
    case napi_uint32_array:
    case napi_float32_array:
      return 4;
    case napi_float64_array:
    case napi_bigint64_array:
    case napi_biguint64_array:
      return 8;
    default:
      return 0;
  }
}

// Creates a $$type wrapper.
Napi::Object MakeWrapper(Napi::Env env, const char *type) {
  Napi::Object obj = Napi::Object::New(env);
  obj.Set(kTypeKey, Napi::String::New(env, type));
  return obj;
}

// Creates a $$type wrapper with a value field.
Napi::Object MakeWrapper(Napi::Env env, const char *type, const Napi::Value &value) {
  Napi::Object obj = Napi::Object::New(env);
  obj.Set(kTypeKey, Napi::String::New(env, type));
  obj.Set(kValueKey, value);
  return obj;
}

// Creates a $$type wrapper with an id for circular reference support.
Napi::Object MakeWrapperWithId(Napi::Env env, const char *type, uint32_t id) {
  Napi::Object obj = Napi::Object::New(env);
  obj.Set(kTypeKey, Napi::String::New(env, type));
  obj.Set(kIdKey, Napi::Number::New(env, id));
  return obj;
}

// Creates a reference wrapper pointing at a previously seen id.
Napi::Object MakeReference(Napi::Env env, uint32_t id) {
  Napi::Object obj = Napi::Object::New(env);
  obj.Set(kTypeKey, Napi::String::New(env, kTypeReference));
  obj.Set(kIdKey, Napi::Number::New(env, id));
  return obj;
}

// Wraps a string property key (used for Error custom properties).
Napi::Object MakePropKeyString(Napi::Env env, const Napi::Value &value) {
  Napi::Object obj = Napi::Object::New(env);
  obj.Set(kTypeKey, Napi::String::New(env, kTypePropKeyString));
  obj.Set(kValueKey, value);
  return obj;
}

// Wraps a symbol key with global flag and key/description.
Napi::Object MakePropKeySymbol(Napi::Env env, bool isGlobal, const Napi::Value &keyOrDesc) {
  Napi::Object obj = Napi::Object::New(env);
  obj.Set(kTypeKey, Napi::String::New(env, kTypePropKeySymbol));
  obj.Set(kGlobalKey, Napi::Boolean::New(env, isGlobal));
  if (isGlobal) {
    obj.Set(kKeyKey, keyOrDesc);
  } else {
    obj.Set(kDescriptionKey, keyOrDesc);
  }
  return obj;
}

// Adds $$id to a wrapper if circular references are enabled.
void SetIdIfNeeded(Napi::Env env, Napi::Object &obj, bool hasId, uint32_t id) {
  if (hasId) {
    obj.Set(kIdKey, Napi::Number::New(env, id));
  }
}

// Resolves a reference id during parsing.
Napi::Value GetRefValue(DecodeContext &ctx, uint32_t id, const Napi::Env &env) {
  auto it = ctx.refs.find(id);
  if (it == ctx.refs.end()) {
    throw Napi::TypeError::New(env, "Unknown reference id");
  }
  return it->second.Value();
}

// Stores a decoded object by id for reference resolution.
void StoreRef(DecodeContext &ctx, uint32_t id, const Napi::Value &value) {
  ctx.refs.emplace(id, Napi::Persistent(value));
}

// Minimal Base64 encode for binary payloads.
std::string Base64Encode(const uint8_t *data, size_t len) {
  std::string out;
  out.reserve(((len + 2) / 3) * 4);

  size_t i = 0;
  while (i + 2 < len) {
    uint32_t triple = (static_cast<uint32_t>(data[i]) << 16) |
                      (static_cast<uint32_t>(data[i + 1]) << 8) |
                      static_cast<uint32_t>(data[i + 2]);
    out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
    out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
    out.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
    out.push_back(kBase64Alphabet[triple & 0x3F]);
    i += 3;
  }

  if (i < len) {
    uint32_t triple = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < len) {
      triple |= static_cast<uint32_t>(data[i + 1]) << 8;
    }

    out.push_back(kBase64Alphabet[(triple >> 18) & 0x3F]);
    out.push_back(kBase64Alphabet[(triple >> 12) & 0x3F]);
    if (i + 1 < len) {
      out.push_back(kBase64Alphabet[(triple >> 6) & 0x3F]);
      out.push_back('=');
    } else {
      out.push_back('=');
      out.push_back('=');
    }
  }

  return out;
}

int Base64Index(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// Minimal Base64 decode for binary payloads.
std::vector<uint8_t> Base64Decode(const std::string &input) {
  std::vector<uint8_t> out;
  size_t len = input.size();
  if (len == 0) return out;

  size_t i = 0;
  while (i < len) {
    int idx0 = Base64Index(input[i]);
    int idx1 = (i + 1 < len) ? Base64Index(input[i + 1]) : -1;
    if (idx0 < 0 || idx1 < 0) break;

    int idx2 = -1;
    int idx3 = -1;
    char c2 = (i + 2 < len) ? input[i + 2] : '=';
    char c3 = (i + 3 < len) ? input[i + 3] : '=';

    if (c2 != '=') idx2 = Base64Index(c2);
    if (c3 != '=') idx3 = Base64Index(c3);

    uint32_t triple = (static_cast<uint32_t>(idx0) << 18) |
                      (static_cast<uint32_t>(idx1) << 12);
    if (idx2 >= 0) triple |= static_cast<uint32_t>(idx2) << 6;
    if (idx3 >= 0) triple |= static_cast<uint32_t>(idx3);

    out.push_back(static_cast<uint8_t>((triple >> 16) & 0xFF));
    if (c2 != '=') out.push_back(static_cast<uint8_t>((triple >> 8) & 0xFF));
    if (c3 != '=') out.push_back(static_cast<uint8_t>(triple & 0xFF));

    i += 4;
  }

  return out;
}

// Checks if a value is a wrapper of a specific $$type.
bool IsWrapperType(const Napi::Env &env, const Napi::Value &value, const char *type) {
  if (!value.IsObject()) return false;
  Napi::Object obj = value.As<Napi::Object>();
  Napi::Value typeVal = obj.Get(kTypeKey);
  if (!typeVal.IsString()) return false;
  std::string t = typeVal.As<Napi::String>().Utf8Value();
  return t == type;
}

// Checks if $$type is one of the supported wrapper types.
bool IsKnownWrapperType(const std::string &t) {
  return t == kTypeUndefined || t == kTypeHole || t == kTypeNumber ||
         t == kTypeBigInt || t == kTypeDate || t == kTypeRegExp || t == kTypeSet ||
         t == kTypeMap || t == kTypeError || t == kTypeObject || t == kTypeArray ||
         t == kTypeReference || t == kTypePropKeyString || t == kTypePropKeySymbol ||
         t == kTypeBuffer || t == kTypeArrayBuffer || t == kTypeTypedArray ||
         t == kTypeDataView;
}

}  // namespace bas_serde
