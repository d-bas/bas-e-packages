#include <napi.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
namespace {
const char kTypeKey[] = "$$type";
const char kValueKey[] = "value";
const char kArrayTypeKey[] = "arrayType";
const char kByteOffsetKey[] = "byteOffset";
const char kLengthKey[] = "length";
const char kSourceKey[] = "source";
const char kFlagsKey[] = "flags";
const char kTypeUndefined[] = "Undefined";
const char kTypeNumber[] = "Number";
const char kTypeBigInt[] = "BigInt";
const char kTypeDate[] = "Date";
const char kTypeRegExp[] = "RegExp";
const char kTypeSet[] = "Set";
const char kTypeMap[] = "Map";
const char kTypeError[] = "Error";
const char kTypeObject[] = "object";
const char kTypeArray[] = "array";
const char kTypeReference[] = "reference";
const char kTypePropKeyString[] = "PropKeyString";
const char kTypePropKeySymbol[] = "PropKeySymbol";
const char kTypeBuffer[] = "Buffer";
const char kTypeArrayBuffer[] = "ArrayBuffer";
const char kTypeTypedArray[] = "TypedArray";
const char kTypeDataView[] = "DataView";
const char kTypeHole[] = "Hole";
const char kMessageKey[] = "message";
const char kNameKey[] = "name";
const char kStackKey[] = "stack";
const char kKeyKey[] = "key";
const char kDescriptionKey[] = "description";
const char kGlobalKey[] = "global";
const char kPropsKey[] = "props";
const char kIdKey[] = "$$id";
const char kNumNaN[] = "NaN";
const char kNumInf[] = "Infinity";
const char kNumNegInf[] = "-Infinity";
const char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
struct Ctors {
  Napi::Function mapCtor;
  Napi::Function setCtor;
  Napi::Function dateCtor;
  Napi::Function regexpCtor;
  Napi::Function bigintCtor;
};
struct Replacer {
  bool enabled = false;
  Napi::Function fn;
};
struct ReplaceState {
  bool replaced = false;
  napi_value value = nullptr;
};
struct Reviver {
  bool enabled = false;
  Napi::Function fn;
};
struct SeenEntry {
  Napi::Reference<Napi::Value> ref;
  uint32_t id;
};
using SeenStack = std::vector<Napi::Reference<Napi::Value>>;
using SeenEntries = std::vector<SeenEntry>;
struct EncodeContext {
  SeenStack stack;
  SeenEntries entries;
  bool allowCircular = false;
  uint32_t nextId = 1;
};
struct DecodeContext {
  std::unordered_map<uint32_t, Napi::Reference<Napi::Value>> refs;
};
struct SeenGuard {
  SeenStack &seen;
  bool active;
  SeenGuard(SeenStack &stack, const Napi::Value &value) : seen(stack), active(true) {
    seen.emplace_back(Napi::Persistent(value));
  }
  ~SeenGuard() {
    if (active) {
      seen.pop_back();
    }
  }
  SeenGuard(const SeenGuard &) = delete;
  SeenGuard &operator=(const SeenGuard &) = delete;
};
Napi::Object MakeWrapper(Napi::Env env, const char *type) {
  Napi::Object obj = Napi::Object::New(env);
  obj.Set(kTypeKey, Napi::String::New(env, type));
  return obj;
}
Napi::Object MakeWrapper(Napi::Env env, const char *type, const Napi::Value &value) {
  Napi::Object obj = Napi::Object::New(env);
  obj.Set(kTypeKey, Napi::String::New(env, type));
  obj.Set(kValueKey, value);
  return obj;
}
Napi::Object MakeWrapperWithId(Napi::Env env, const char *type, uint32_t id) {
  Napi::Object obj = Napi::Object::New(env);
  obj.Set(kTypeKey, Napi::String::New(env, type));
  obj.Set(kIdKey, Napi::Number::New(env, id));
  return obj;
}
Napi::Object MakeReference(Napi::Env env, uint32_t id) {
  Napi::Object obj = Napi::Object::New(env);
  obj.Set(kTypeKey, Napi::String::New(env, kTypeReference));
  obj.Set(kIdKey, Napi::Number::New(env, id));
  return obj;
}
void SetIdIfNeeded(Napi::Env env, Napi::Object &obj, bool hasId, uint32_t id) {
  if (hasId) {
    obj.Set(kIdKey, Napi::Number::New(env, id));
  }
}
Napi::Object MakePropKeyString(Napi::Env env, const Napi::Value &value) {
  Napi::Object obj = Napi::Object::New(env);
  obj.Set(kTypeKey, Napi::String::New(env, kTypePropKeyString));
  obj.Set(kValueKey, value);
  return obj;
}
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
bool IsInstanceOf(const Napi::Env &env, const Napi::Object &obj,
                  const char *ctorName) {
  Napi::Value ctorValue = env.Global().Get(ctorName);
  if (!ctorValue.IsFunction()) return false;
  return obj.InstanceOf(ctorValue.As<Napi::Function>());
}
std::string GetNapiErrorMessage(napi_env env) {
  const napi_extended_error_info *info = nullptr;
  napi_get_last_error_info(env, &info);
  if (info && info->error_message) {
    return info->error_message;
  }
  return "unknown napi error";
}
Napi::Value ReplaceCallback(const Napi::CallbackInfo &info) {
  auto *state = static_cast<ReplaceState *>(info.Data());
  state->replaced = true;
  state->value = info.Length() > 0 ? info[0] : info.Env().Undefined();
  return info.Env().Undefined();
}
bool SeenContains(const SeenStack &seen, const Napi::Value &value) {
  for (const auto &ref : seen) {
    if (value.StrictEquals(ref.Value())) {
      return true;
    }
  }
  return false;
}
int FindSeenId(const SeenEntries &entries, const Napi::Value &value) {
  for (const auto &entry : entries) {
    if (value.StrictEquals(entry.ref.Value())) {
      return static_cast<int>(entry.id);
    }
  }
  return -1;
}
Napi::Value GetRefValue(DecodeContext &ctx, uint32_t id, const Napi::Env &env) {
  auto it = ctx.refs.find(id);
  if (it == ctx.refs.end()) {
    throw Napi::TypeError::New(env, "Unknown reference id");
  }
  return it->second.Value();
}
void StoreRef(DecodeContext &ctx, uint32_t id, const Napi::Value &value) {
  ctx.refs.emplace(id, Napi::Persistent(value));
}
bool IsBufferInstance(const Napi::Env &env, const Napi::Value &value) {
  if (!value.IsObject()) {
    return false;
  }
  return IsInstanceOf(env, value.As<Napi::Object>(), "Buffer");
}
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
Napi::Value EncodeValue(const Napi::Env &env, const Napi::Value &value,
                        EncodeContext &ctx, const Replacer &replacer,
                        bool applyReplacer) {
  if (applyReplacer && replacer.enabled) {
    ReplaceState state;
    Napi::Function cb =
        Napi::Function::New(env, ReplaceCallback, "replace", &state);
    replacer.fn.Call(env.Global(), {value, cb});
    if (state.replaced) {
      Napi::Value nextValue = Napi::Value(env, state.value);
      return EncodeValue(env, nextValue, ctx, replacer, false);
    }
  }
  if (value.IsUndefined()) {
    return MakeWrapper(env, kTypeUndefined);
  }
  if (value.IsNull()) {
    return env.Null();
  }
  if (value.IsBoolean() || value.IsString()) {
    return value;
  }
  if (value.IsNumber()) {
    double num = value.As<Napi::Number>().DoubleValue();
    if (!std::isfinite(num)) {
      if (std::isnan(num)) {
        return MakeWrapper(env, kTypeNumber,
                           Napi::String::New(env, kNumNaN));
      }
      return MakeWrapper(env, kTypeNumber,
                         Napi::String::New(env, num > 0 ? kNumInf : kNumNegInf));
    }
    return value;
  }
  if (value.IsBigInt()) {
    std::string text = value.ToString().Utf8Value();
    return MakeWrapper(env, kTypeBigInt, Napi::String::New(env, text));
  }
  if (value.IsFunction() || value.IsSymbol()) {
    throw Napi::TypeError::New(env, "Unsupported value type");
  }
  if (!value.IsObject()) {
    throw Napi::TypeError::New(env, "Unsupported value type");
  }
  Napi::Object obj = value.As<Napi::Object>();
  uint32_t currentId = 0;
  bool hasId = false;
  if (ctx.allowCircular) {
    int seenId = FindSeenId(ctx.entries, value);
    if (seenId >= 0) {
      return MakeReference(env, static_cast<uint32_t>(seenId));
    }
    currentId = ctx.nextId++;
    hasId = true;
    ctx.entries.push_back({Napi::Persistent(value), currentId});
  } else {
    if (SeenContains(ctx.stack, value)) {
      throw Napi::TypeError::New(env, "Circular reference detected");
    }
  }
  SeenGuard guard(ctx.stack, value);
  if (value.IsArray()) {
    Napi::Array arr = value.As<Napi::Array>();
    uint32_t length = arr.Length();
    Napi::Array out = Napi::Array::New(env, length);
    for (uint32_t i = 0; i < length; i++) {
      if (arr.Has(i)) {
        out.Set(i, EncodeValue(env, arr.Get(i), ctx, replacer, true));
      } else {
        out.Set(i, MakeWrapper(env, kTypeHole));
      }
    }
    if (ctx.allowCircular && hasId) {
      Napi::Object wrapper = MakeWrapperWithId(env, kTypeArray, currentId);
      wrapper.Set(kValueKey, out);
      return wrapper;
    }
    return out;
  }
  if (value.IsArrayBuffer()) {
    Napi::ArrayBuffer buf = value.As<Napi::ArrayBuffer>();
    std::string b64 = Base64Encode(static_cast<uint8_t *>(buf.Data()),
                                   buf.ByteLength());
    Napi::Object wrapper =
        MakeWrapper(env, kTypeArrayBuffer, Napi::String::New(env, b64));
    SetIdIfNeeded(env, wrapper, hasId, currentId);
    return wrapper;
  }
  if (IsBufferInstance(env, value)) {
    Napi::Buffer<uint8_t> buf = value.As<Napi::Buffer<uint8_t>>();
    std::string b64 = Base64Encode(buf.Data(), buf.Length());
    Napi::Object wrapper = MakeWrapper(env, kTypeBuffer, Napi::String::New(env, b64));
    SetIdIfNeeded(env, wrapper, hasId, currentId);
    return wrapper;
  }
  bool isDataView = false;
  napi_status isDataViewStatus = napi_is_dataview(env, value, &isDataView);
  if (isDataViewStatus != napi_ok) {
    std::string message = GetNapiErrorMessage(env);
    throw Napi::TypeError::New(env, "napi_is_dataview failed: " + message);
  }
  if (isDataView) {
    size_t byteLength;
    void *data;
    napi_value arraybuffer;
    size_t byteOffset;
    napi_status status =
        napi_get_dataview_info(env, value, &byteLength, &data, &arraybuffer,
                               &byteOffset);
    if (status != napi_ok) {
      std::string message = GetNapiErrorMessage(env);
      throw Napi::TypeError::New(env, "napi_get_dataview_info failed: " + message);
    }
    std::string b64 = Base64Encode(static_cast<uint8_t *>(data), byteLength);
    Napi::Object wrapper = MakeWrapper(env, kTypeDataView);
    wrapper.Set(kValueKey, Napi::String::New(env, b64));
    wrapper.Set(kByteOffsetKey, Napi::Number::New(env, 0));
    wrapper.Set(kLengthKey, Napi::Number::New(env, byteLength));
    SetIdIfNeeded(env, wrapper, hasId, currentId);
    return wrapper;
  }
  bool isTypedArray = false;
  napi_status isTypedArrayStatus = napi_is_typedarray(env, value, &isTypedArray);
  if (isTypedArrayStatus != napi_ok) {
    std::string message = GetNapiErrorMessage(env);
    throw Napi::TypeError::New(env, "napi_is_typedarray failed: " + message);
  }
  if (isTypedArray) {
    napi_typedarray_type type;
    size_t length;
    void *data;
    napi_value arraybuffer;
    size_t byteOffset;
    napi_status status =
        napi_get_typedarray_info(env, value, &type, &length, &data,
                                 &arraybuffer, &byteOffset);
    if (status != napi_ok) {
      std::string message = GetNapiErrorMessage(env);
      throw Napi::TypeError::New(env, "napi_get_typedarray_info failed: " + message);
    }
    std::string typeName = TypedArrayName(type);
    size_t bytesPerElement = TypedArrayBytesPerElement(type);
    if (typeName.empty()) {
      throw Napi::TypeError::New(env, "Unsupported typed array");
    }
    if (bytesPerElement == 0) {
      throw Napi::TypeError::New(env, "Unsupported typed array");
    }
    size_t byteLength = length * bytesPerElement;
    std::string b64 = Base64Encode(static_cast<uint8_t *>(data), byteLength);
    Napi::Object wrapper = MakeWrapper(env, kTypeTypedArray);
    wrapper.Set(kArrayTypeKey, Napi::String::New(env, typeName));
    wrapper.Set(kValueKey, Napi::String::New(env, b64));
    wrapper.Set(kByteOffsetKey, Napi::Number::New(env, 0));
    wrapper.Set(kLengthKey, Napi::Number::New(env, length));
    SetIdIfNeeded(env, wrapper, hasId, currentId);
    return wrapper;
  }
  if (IsInstanceOf(env, obj, "Date")) {
    Napi::Function toISOString = obj.Get("toISOString").As<Napi::Function>();
    Napi::Value iso = toISOString.Call(obj, {});
    Napi::Object wrapper = MakeWrapper(env, kTypeDate, iso);
    SetIdIfNeeded(env, wrapper, hasId, currentId);
    return wrapper;
  }
  if (IsInstanceOf(env, obj, "RegExp")) {
    Napi::Value source = obj.Get(kSourceKey);
    Napi::Value flags = obj.Get(kFlagsKey);
    Napi::Object payload = Napi::Object::New(env);
    payload.Set(kSourceKey, source);
    payload.Set(kFlagsKey, flags);
    Napi::Object wrapper = MakeWrapper(env, kTypeRegExp, payload);
    SetIdIfNeeded(env, wrapper, hasId, currentId);
    return wrapper;
  }
  if (IsInstanceOf(env, obj, "Error")) {
    Napi::Object payload = Napi::Object::New(env);
    Napi::Value name = obj.Get(kNameKey);
    Napi::Value message = obj.Get(kMessageKey);
    Napi::Value stack = obj.Get(kStackKey);
    payload.Set(kNameKey, name.IsUndefined() ? env.Undefined() : name.ToString());
    payload.Set(kMessageKey,
                message.IsUndefined() ? env.Undefined() : message.ToString());
    payload.Set(kStackKey,
                stack.IsUndefined() ? env.Undefined() : stack.ToString());
    Napi::Array props = Napi::Array::New(env);
    uint32_t idx = 0;
    Napi::Array keys = obj.GetPropertyNames();
    uint32_t length = keys.Length();
    for (uint32_t i = 0; i < length; i++) {
      Napi::Value key = keys.Get(i);
      if (!key.IsString()) {
        continue;
      }
      Napi::Array pair = Napi::Array::New(env, 2);
      pair.Set(static_cast<uint32_t>(0), MakePropKeyString(env, key));
      pair.Set(static_cast<uint32_t>(1),
               EncodeValue(env, obj.Get(key), ctx, replacer, true));
      props.Set(idx++, pair);
    }
    Napi::Object global = env.Global();
    Napi::Object objectCtor = global.Get("Object").As<Napi::Object>();
    Napi::Function getOwnPropertySymbols =
        objectCtor.Get("getOwnPropertySymbols").As<Napi::Function>();
    Napi::Array symbols =
        getOwnPropertySymbols.Call(objectCtor, {obj}).As<Napi::Array>();
    uint32_t symLength = symbols.Length();
    Napi::Object symbolCtor = global.Get("Symbol").As<Napi::Object>();
    Napi::Function keyForFn = symbolCtor.Get("keyFor").As<Napi::Function>();
    for (uint32_t i = 0; i < symLength; i++) {
      Napi::Value sym = symbols.Get(i);
      if (!sym.IsSymbol()) {
        continue;
      }
      Napi::Value keyFor = keyForFn.Call(symbolCtor, {sym});
      bool isGlobal = !keyFor.IsUndefined() && !keyFor.IsNull();
      Napi::Value descVal = env.Undefined();
      if (!isGlobal) {
        Napi::Object symObj = sym.ToObject();
        descVal = symObj.Get(kDescriptionKey);
      }
      Napi::Array pair = Napi::Array::New(env, 2);
      pair.Set(static_cast<uint32_t>(0),
               MakePropKeySymbol(env, isGlobal, isGlobal ? keyFor : descVal));
      pair.Set(static_cast<uint32_t>(1),
               EncodeValue(env, obj.Get(sym), ctx, replacer, true));
      props.Set(idx++, pair);
    }
    payload.Set(kPropsKey, props);
    Napi::Object wrapper = MakeWrapper(env, kTypeError, payload);
    SetIdIfNeeded(env, wrapper, hasId, currentId);
    return wrapper;
  }
  if (IsInstanceOf(env, obj, "Set")) {
    Napi::Function valuesFn = obj.Get("values").As<Napi::Function>();
    Napi::Object iterator = valuesFn.Call(obj, {}).As<Napi::Object>();
    Napi::Function nextFn = iterator.Get("next").As<Napi::Function>();
    Napi::Array arr = Napi::Array::New(env);
    uint32_t idx = 0;
    while (true) {
      Napi::Object next = nextFn.Call(iterator, {}).As<Napi::Object>();
      bool done = next.Get("done").ToBoolean().Value();
      if (done) break;
      Napi::Value v = next.Get("value");
      arr.Set(idx++, EncodeValue(env, v, ctx, replacer, true));
    }
    Napi::Object wrapper = MakeWrapper(env, kTypeSet, arr);
    SetIdIfNeeded(env, wrapper, hasId, currentId);
    return wrapper;
  }
  if (IsInstanceOf(env, obj, "Map")) {
    Napi::Function entriesFn = obj.Get("entries").As<Napi::Function>();
    Napi::Object iterator = entriesFn.Call(obj, {}).As<Napi::Object>();
    Napi::Function nextFn = iterator.Get("next").As<Napi::Function>();
    Napi::Array arr = Napi::Array::New(env);
    uint32_t idx = 0;
    while (true) {
      Napi::Object next = nextFn.Call(iterator, {}).As<Napi::Object>();
      bool done = next.Get("done").ToBoolean().Value();
      if (done) break;
      Napi::Array entry = next.Get("value").As<Napi::Array>();
      Napi::Array pair = Napi::Array::New(env, 2);
      pair.Set(static_cast<uint32_t>(0),
               EncodeValue(env, entry.Get(static_cast<uint32_t>(0)), ctx,
                          replacer, true));
      pair.Set(static_cast<uint32_t>(1),
               EncodeValue(env, entry.Get(static_cast<uint32_t>(1)), ctx,
                          replacer, true));
      arr.Set(idx++, pair);
    }
    Napi::Object wrapper = MakeWrapper(env, kTypeMap, arr);
    SetIdIfNeeded(env, wrapper, hasId, currentId);
    return wrapper;
  }
  Napi::Array keys = obj.GetPropertyNames();
  uint32_t length = keys.Length();
  Napi::Object out = Napi::Object::New(env);
  for (uint32_t i = 0; i < length; i++) {
    Napi::Value key = keys.Get(i);
    if (!key.IsString()) {
      throw Napi::TypeError::New(env, "Only string keys are supported");
    }
    std::string keyStr = key.As<Napi::String>().Utf8Value();
    out.Set(keyStr, EncodeValue(env, obj.Get(key), ctx, replacer, true));
  }
  if (ctx.allowCircular && hasId) {
    Napi::Object wrapper = MakeWrapperWithId(env, kTypeObject, currentId);
    wrapper.Set(kValueKey, out);
    return wrapper;
  }
  return out;
}
bool IsWrapperType(const Napi::Env &env, const Napi::Value &value,
                   const char *type) {
  if (!value.IsObject()) return false;
  Napi::Object obj = value.As<Napi::Object>();
  Napi::Value typeVal = obj.Get(kTypeKey);
  if (!typeVal.IsString()) return false;
  std::string t = typeVal.As<Napi::String>().Utf8Value();
  return t == type;
}
bool IsKnownWrapperType(const std::string &t) {
  return t == kTypeUndefined || t == kTypeHole || t == kTypeNumber ||
         t == kTypeBigInt || t == kTypeDate || t == kTypeRegExp || t == kTypeSet ||
         t == kTypeMap || t == kTypeError || t == kTypeObject || t == kTypeArray ||
         t == kTypeReference || t == kTypePropKeyString || t == kTypePropKeySymbol ||
         t == kTypeBuffer || t == kTypeArrayBuffer || t == kTypeTypedArray ||
         t == kTypeDataView;
}
Napi::Value DecodeValue(const Napi::Env &env, const Napi::Value &value,
                        const Ctors &ctors, const Reviver &reviver,
                        DecodeContext &ctx, bool applyReviver);
Napi::Value DecodeWrapper(const Napi::Env &env, const Napi::Object &obj,
                          const Ctors &ctors, const Reviver &reviver,
                          DecodeContext &ctx, bool applyReviver) {
  Napi::Value typeVal = obj.Get(kTypeKey);
  if (!typeVal.IsString()) return obj;
  std::string t = typeVal.As<Napi::String>().Utf8Value();
  uint32_t refId = 0;
  bool hasId = false;
  if (obj.Has(kIdKey)) {
    Napi::Value idVal = obj.Get(kIdKey);
    if (idVal.IsNumber()) {
      refId = idVal.ToNumber().Uint32Value();
      hasId = true;
    }
  }
  if (t == kTypeReference) {
    return GetRefValue(ctx, refId, env);
  }
  if (t == kTypeUndefined) {
    return env.Undefined();
  }
  if (t == kTypeHole) {
    return obj;
  }
  if (t == kTypeNumber) {
    std::string repr = obj.Get(kValueKey).ToString().Utf8Value();
    if (repr == kNumNaN) return Napi::Number::New(env, std::nan(""));
    if (repr == kNumInf) return Napi::Number::New(env, INFINITY);
    if (repr == kNumNegInf) return Napi::Number::New(env, -INFINITY);
    return Napi::Number::New(env, std::stod(repr));
  }
  if (t == kTypeBigInt) {
    Napi::Value strVal = obj.Get(kValueKey);
    return ctors.bigintCtor.Call(env.Global(), {strVal});
  }
  if (t == kTypeDate) {
    Napi::Value strVal = obj.Get(kValueKey);
    Napi::Object dateObj = ctors.dateCtor.New({strVal}).As<Napi::Object>();
    if (hasId) {
      StoreRef(ctx, refId, dateObj);
    }
    return dateObj;
  }
  if (t == kTypeRegExp) {
    Napi::Object payload = obj.Get(kValueKey).As<Napi::Object>();
    Napi::Value source = payload.Get(kSourceKey);
    Napi::Value flags = payload.Get(kFlagsKey);
    Napi::Object reObj = ctors.regexpCtor.New({source, flags}).As<Napi::Object>();
    if (hasId) {
      StoreRef(ctx, refId, reObj);
    }
    return reObj;
  }
  if (t == kTypeObject) {
    Napi::Object payload = obj.Get(kValueKey).As<Napi::Object>();
    Napi::Object out = Napi::Object::New(env);
    if (hasId) {
      StoreRef(ctx, refId, out);
    }
    Napi::Array keys = payload.GetPropertyNames();
    uint32_t length = keys.Length();
    for (uint32_t i = 0; i < length; i++) {
      Napi::Value key = keys.Get(i);
      if (!key.IsString()) {
        continue;
      }
      std::string keyStr = key.As<Napi::String>().Utf8Value();
      Napi::Value val = payload.Get(key);
      out.Set(keyStr, DecodeValue(env, val, ctors, reviver, ctx, true));
    }
    return out;
  }
  if (t == kTypeArray) {
    Napi::Array payload = obj.Get(kValueKey).As<Napi::Array>();
    uint32_t length = payload.Length();
    Napi::Array out = Napi::Array::New(env, length);
    if (hasId) {
      StoreRef(ctx, refId, out);
    }
    for (uint32_t i = 0; i < length; i++) {
      if (!payload.Has(i)) continue;
      Napi::Value item = payload.Get(i);
      if (IsWrapperType(env, item, kTypeHole)) {
        continue;
      }
      out.Set(i, DecodeValue(env, item, ctors, reviver, ctx, true));
    }
    return out;
  }
  if (t == kTypePropKeyString) {
    Napi::Value value = obj.Get(kValueKey);
    return value.IsUndefined() ? env.Undefined() : value.ToString();
  }
  if (t == kTypePropKeySymbol) {
    Napi::Value globalVal = obj.Get(kGlobalKey);
    bool isGlobal = globalVal.IsBoolean() && globalVal.ToBoolean().Value();
    Napi::Object symbolCtor = env.Global().Get("Symbol").As<Napi::Object>();
    if (isGlobal) {
      Napi::Value keyVal = obj.Get(kKeyKey);
      Napi::Function keyForFn = symbolCtor.Get("for").As<Napi::Function>();
      return keyForFn.Call(symbolCtor, {keyVal});
    }
    Napi::Value descVal = obj.Get(kDescriptionKey);
    Napi::Function symbolFn = symbolCtor.As<Napi::Function>();
    return symbolFn.Call(env.Global(), {descVal});
  }
  if (t == kTypeError) {
    Napi::Object payload = obj.Get(kValueKey).As<Napi::Object>();
    Napi::Value nameVal = payload.Get(kNameKey);
    Napi::Value messageVal = payload.Get(kMessageKey);
    Napi::Value stackVal = payload.Get(kStackKey);

    Napi::Function ctor = env.Global().Get("Error").As<Napi::Function>();
    if (nameVal.IsString()) {
      std::string name = nameVal.As<Napi::String>().Utf8Value();
      Napi::Value candidate = env.Global().Get(name);
      if (candidate.IsFunction()) {
        ctor = candidate.As<Napi::Function>();
      }
    }

    Napi::Value msgArg = messageVal.IsUndefined() ? env.Undefined() : messageVal;
    Napi::Object errObj = ctor.New({msgArg}).As<Napi::Object>();
    if (hasId) {
      StoreRef(ctx, refId, errObj);
    }
    if (nameVal.IsString()) {
      errObj.Set(kNameKey, nameVal);
    }
    if (stackVal.IsString()) {
      errObj.Set(kStackKey, stackVal);
    }
    Napi::Value propsVal = payload.Get(kPropsKey);
    if (propsVal.IsArray()) {
      Napi::Array props = propsVal.As<Napi::Array>();
      uint32_t length = props.Length();
      for (uint32_t i = 0; i < length; i++) {
        Napi::Value entryVal = props.Get(i);
        if (!entryVal.IsArray()) {
          continue;
        }
        Napi::Array pair = entryVal.As<Napi::Array>();
        if (pair.Length() < 2) {
          continue;
        }
        Napi::Value keyVal = DecodeValue(env, pair.Get(static_cast<uint32_t>(0)),
                                         ctors, reviver, ctx, true);
        Napi::Value val = DecodeValue(env, pair.Get(static_cast<uint32_t>(1)),
                                      ctors, reviver, ctx, true);
        if (keyVal.IsString() || keyVal.IsSymbol()) {
          errObj.Set(keyVal, val);
        }
      }
    }
    return errObj;
  }
  if (t == kTypeSet) {
    Napi::Array arr = obj.Get(kValueKey).As<Napi::Array>();
    Napi::Object setObj = ctors.setCtor.New({});
    if (hasId) {
      StoreRef(ctx, refId, setObj);
    }
    Napi::Function addFn = setObj.Get("add").As<Napi::Function>();
    uint32_t length = arr.Length();
    for (uint32_t i = 0; i < length; i++) {
      Napi::Value decoded =
          DecodeValue(env, arr.Get(i), ctors, reviver, ctx, true);
      addFn.Call(setObj, {decoded});
    }
    return setObj;
  }
  if (t == kTypeMap) {
    Napi::Array arr = obj.Get(kValueKey).As<Napi::Array>();
    Napi::Object mapObj = ctors.mapCtor.New({});
    if (hasId) {
      StoreRef(ctx, refId, mapObj);
    }
    Napi::Function setFn = mapObj.Get("set").As<Napi::Function>();
    uint32_t length = arr.Length();
    for (uint32_t i = 0; i < length; i++) {
      Napi::Array entry = arr.Get(i).As<Napi::Array>();
      Napi::Value key = DecodeValue(env, entry.Get(static_cast<uint32_t>(0)),
                                    ctors, reviver, ctx, true);
      Napi::Value val = DecodeValue(env, entry.Get(static_cast<uint32_t>(1)),
                                    ctors, reviver, ctx, true);
      setFn.Call(mapObj, {key, val});
    }
    return mapObj;
  }
  if (t == kTypeBuffer) {
    std::string b64 = obj.Get(kValueKey).ToString().Utf8Value();
    std::vector<uint8_t> bytes = Base64Decode(b64);
    Napi::Buffer<uint8_t> buf =
        bytes.empty() ? Napi::Buffer<uint8_t>::New(env, 0)
                      : Napi::Buffer<uint8_t>::Copy(env, bytes.data(), bytes.size());
    if (hasId) {
      StoreRef(ctx, refId, buf);
    }
    return buf;
  }
  if (t == kTypeArrayBuffer) {
    std::string b64 = obj.Get(kValueKey).ToString().Utf8Value();
    std::vector<uint8_t> bytes = Base64Decode(b64);
    Napi::ArrayBuffer buf = Napi::ArrayBuffer::New(env, bytes.size());
    if (!bytes.empty()) {
      std::memcpy(buf.Data(), bytes.data(), bytes.size());
    }
    if (hasId) {
      StoreRef(ctx, refId, buf);
    }
    return buf;
  }
  if (t == kTypeTypedArray) {
    std::string typeName = obj.Get(kArrayTypeKey).ToString().Utf8Value();
    std::string b64 = obj.Get(kValueKey).ToString().Utf8Value();
    uint32_t length = obj.Get(kLengthKey).ToNumber().Uint32Value();
    std::vector<uint8_t> bytes = Base64Decode(b64);
    Napi::ArrayBuffer buf = Napi::ArrayBuffer::New(env, bytes.size());
    if (!bytes.empty()) {
      std::memcpy(buf.Data(), bytes.data(), bytes.size());
    }
    Napi::Value ctorVal = env.Global().Get(typeName);
    if (!ctorVal.IsFunction()) {
      throw Napi::TypeError::New(env, "Unknown typed array constructor");
    }
    Napi::Function ctor = ctorVal.As<Napi::Function>();
    Napi::Object typed =
        ctor.New({buf, Napi::Number::New(env, 0), Napi::Number::New(env, length)});
    if (hasId) {
      StoreRef(ctx, refId, typed);
    }
    return typed;
  }
  if (t == kTypeDataView) {
    std::string b64 = obj.Get(kValueKey).ToString().Utf8Value();
    uint32_t length = obj.Get(kLengthKey).ToNumber().Uint32Value();
    std::vector<uint8_t> bytes = Base64Decode(b64);
    Napi::ArrayBuffer buf = Napi::ArrayBuffer::New(env, bytes.size());
    if (!bytes.empty()) {
      std::memcpy(buf.Data(), bytes.data(), bytes.size());
    }
    Napi::Value ctorVal = env.Global().Get("DataView");
    if (!ctorVal.IsFunction()) {
      throw Napi::TypeError::New(env, "DataView constructor not found");
    }
    Napi::Function ctor = ctorVal.As<Napi::Function>();
    Napi::Object view =
        ctor.New({buf, Napi::Number::New(env, 0), Napi::Number::New(env, length)});
    if (hasId) {
      StoreRef(ctx, refId, view);
    }
    return view;
  }
  return obj;
}
Napi::Value DecodeArray(const Napi::Env &env, const Napi::Array &arr,
                        const Ctors &ctors, const Reviver &reviver,
                        DecodeContext &ctx, bool applyReviver) {
  uint32_t length = arr.Length();
  Napi::Array out = Napi::Array::New(env, length);
  for (uint32_t i = 0; i < length; i++) {
    if (!arr.Has(i)) continue;
    Napi::Value item = arr.Get(i);
    if (IsWrapperType(env, item, kTypeHole)) {
      continue;
    }
    out.Set(i, DecodeValue(env, item, ctors, reviver, ctx, true));
  }
  return out;
}
Napi::Value DecodeObject(const Napi::Env &env, const Napi::Object &obj,
                         const Ctors &ctors, const Reviver &reviver,
                         DecodeContext &ctx, bool applyReviver) {
  Napi::Array keys = obj.GetPropertyNames();
  uint32_t length = keys.Length();
  Napi::Object out = Napi::Object::New(env);
  for (uint32_t i = 0; i < length; i++) {
    Napi::Value key = keys.Get(i);
    if (!key.IsString()) {
      throw Napi::TypeError::New(env, "Only string keys are supported");
    }
    std::string keyStr = key.As<Napi::String>().Utf8Value();
    Napi::Value val = obj.Get(key);
    out.Set(keyStr, DecodeValue(env, val, ctors, reviver, ctx, true));
  }
  return out;
}
Napi::Value DecodeValue(const Napi::Env &env, const Napi::Value &value,
                        const Ctors &ctors, const Reviver &reviver,
                        DecodeContext &ctx, bool applyReviver) {
  if (applyReviver && reviver.enabled) {
    Napi::Value nextValue = reviver.fn.Call(env.Global(), {value});
    return DecodeValue(env, nextValue, ctors, reviver, ctx, false);
  }
  if (value.IsArray()) {
    return DecodeArray(env, value.As<Napi::Array>(), ctors, reviver, ctx, true);
  }
  if (value.IsObject()) {
    Napi::Object obj = value.As<Napi::Object>();
    if (obj.Has(kTypeKey)) {
      Napi::Value typeVal = obj.Get(kTypeKey);
      if (typeVal.IsString()) {
        std::string t = typeVal.As<Napi::String>().Utf8Value();
        if (IsKnownWrapperType(t)) {
          return DecodeWrapper(env, obj, ctors, reviver, ctx, true);
        }
      }
    }
    return DecodeObject(env, obj, ctors, reviver, ctx, true);
  }
  return value;
}
Napi::Value NativeStringify(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    throw Napi::TypeError::New(env, "Expected a value to stringify");
  }
  Replacer replacer;
  if (info.Length() >= 2 && info[1].IsObject()) {
    Napi::Object options = info[1].As<Napi::Object>();
    if (options.Has("replacer")) {
      Napi::Value replVal = options.Get("replacer");
      if (!replVal.IsUndefined() && !replVal.IsNull()) {
        if (!replVal.IsFunction()) {
          throw Napi::TypeError::New(env, "replacer must be a function");
        }
        replacer.enabled = true;
        replacer.fn = replVal.As<Napi::Function>();
      }
    }
  }
  EncodeContext ctx;
  ctx.allowCircular = false;
  if (info.Length() >= 2 && info[1].IsObject()) {
    Napi::Object options = info[1].As<Napi::Object>();
    if (options.Has("circularReferences")) {
      Napi::Value circularVal = options.Get("circularReferences");
      if (circularVal.IsBoolean()) {
        ctx.allowCircular = circularVal.ToBoolean().Value();
      }
    }
  }
  Napi::Value encoded = EncodeValue(env, info[0], ctx, replacer, true);
  Napi::Object json = env.Global().Get("JSON").As<Napi::Object>();
  Napi::Function stringify = json.Get("stringify").As<Napi::Function>();
  return stringify.Call(json, {encoded});
}
Napi::Value NativeParse(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsString()) {
    throw Napi::TypeError::New(env, "Expected a JSON string to parse");
  }
  Napi::Object json = env.Global().Get("JSON").As<Napi::Object>();
  Napi::Function parse = json.Get("parse").As<Napi::Function>();
  Napi::Value parsed = parse.Call(json, {info[0]});
  Ctors ctors{
      env.Global().Get("Map").As<Napi::Function>(),
      env.Global().Get("Set").As<Napi::Function>(),
      env.Global().Get("Date").As<Napi::Function>(),
      env.Global().Get("RegExp").As<Napi::Function>(),
      env.Global().Get("BigInt").As<Napi::Function>(),
  };
  Reviver reviver;
  if (info.Length() >= 2 && info[1].IsObject()) {
    Napi::Object options = info[1].As<Napi::Object>();
    if (options.Has("reviver")) {
      Napi::Value revVal = options.Get("reviver");
      if (!revVal.IsUndefined() && !revVal.IsNull()) {
        if (!revVal.IsFunction()) {
          throw Napi::TypeError::New(env, "reviver must be a function");
        }
        reviver.enabled = true;
        reviver.fn = revVal.As<Napi::Function>();
      }
    }
  }
  DecodeContext ctx;
  return DecodeValue(env, parsed, ctors, reviver, ctx, true);
}
Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set("stringify", Napi::Function::New(env, NativeStringify));
  exports.Set("parse", Napi::Function::New(env, NativeParse));
  exports.Set("debugType", Napi::Function::New(env, [](const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    if (info.Length() < 1) {
      throw Napi::TypeError::New(env, "Expected a value to debug");
    }
    Napi::Value value = info[0];
    Napi::Object out = Napi::Object::New(env);

    bool isDataView = false;
    napi_status dataViewStatus = napi_is_dataview(env, value, &isDataView);
    out.Set("isDataViewStatus", Napi::Number::New(env, dataViewStatus));
    out.Set("isDataView", Napi::Boolean::New(env, isDataView));
    if (dataViewStatus != napi_ok) {
      out.Set("isDataViewError", Napi::String::New(env, GetNapiErrorMessage(env)));
    }

    bool isTypedArray = false;
    napi_status typedArrayStatus = napi_is_typedarray(env, value, &isTypedArray);
    out.Set("isTypedArrayStatus", Napi::Number::New(env, typedArrayStatus));
    out.Set("isTypedArray", Napi::Boolean::New(env, isTypedArray));
    if (typedArrayStatus != napi_ok) {
      out.Set("isTypedArrayError", Napi::String::New(env, GetNapiErrorMessage(env)));
    }

    bool isBuffer = false;
    napi_status bufferStatus = napi_is_buffer(env, value, &isBuffer);
    out.Set("isBufferStatus", Napi::Number::New(env, bufferStatus));
    out.Set("isBuffer", Napi::Boolean::New(env, isBuffer));
    if (bufferStatus != napi_ok) {
      out.Set("isBufferError", Napi::String::New(env, GetNapiErrorMessage(env)));
    }
    bool isBufferInstance = false;
    if (value.IsObject()) {
      isBufferInstance = IsInstanceOf(env, value.As<Napi::Object>(), "Buffer");
    }
    out.Set("isBufferInstance", Napi::Boolean::New(env, isBufferInstance));

    out.Set("isArrayBuffer", Napi::Boolean::New(env, value.IsArrayBuffer()));
    out.Set("isTypedArrayNapi", Napi::Boolean::New(env, value.IsTypedArray()));
    out.Set("isDataViewNapi", Napi::Boolean::New(env, value.IsDataView()));

    Napi::Object global = env.Global();
    Napi::Object obj = global.Get("Object").As<Napi::Object>();
    Napi::Object proto = obj.Get("prototype").As<Napi::Object>();
    Napi::Function toStringFn = proto.Get("toString").As<Napi::Function>();
    Napi::Value tag = toStringFn.Call(value, {});
    out.Set("objectTag", tag);
    return out;
  }));
  return exports;
}
}
NODE_API_MODULE(bas_serde, Init)
