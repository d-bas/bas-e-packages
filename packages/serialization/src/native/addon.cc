#include <napi.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
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
const char kTypeBuffer[] = "Buffer";
const char kTypeArrayBuffer[] = "ArrayBuffer";
const char kTypeTypedArray[] = "TypedArray";
const char kTypeDataView[] = "DataView";
const char kTypeHole[] = "Hole";
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
using SeenStack = std::vector<Napi::Reference<Napi::Value>>;
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
bool SeenContains(const SeenStack &seen, const Napi::Value &value) {
  for (const auto &ref : seen) {
    if (value.StrictEquals(ref.Value())) {
      return true;
    }
  }
  return false;
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
                        SeenStack &seen) {
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
  if (SeenContains(seen, value)) {
    throw Napi::TypeError::New(env, "Circular reference detected");
  }
  SeenGuard guard(seen, value);
  if (value.IsArray()) {
    Napi::Array arr = value.As<Napi::Array>();
    uint32_t length = arr.Length();
    Napi::Array out = Napi::Array::New(env, length);
    for (uint32_t i = 0; i < length; i++) {
      if (arr.Has(i)) {
        out.Set(i, EncodeValue(env, arr.Get(i), seen));
      } else {
        out.Set(i, MakeWrapper(env, kTypeHole));
      }
    }
    return out;
  }
  if (value.IsArrayBuffer()) {
    Napi::ArrayBuffer buf = value.As<Napi::ArrayBuffer>();
    std::string b64 = Base64Encode(static_cast<uint8_t *>(buf.Data()),
                                   buf.ByteLength());
    return MakeWrapper(env, kTypeArrayBuffer, Napi::String::New(env, b64));
  }
  if (IsBufferInstance(env, value)) {
    Napi::Buffer<uint8_t> buf = value.As<Napi::Buffer<uint8_t>>();
    std::string b64 = Base64Encode(buf.Data(), buf.Length());
    return MakeWrapper(env, kTypeBuffer, Napi::String::New(env, b64));
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
    return wrapper;
  }
  if (IsInstanceOf(env, obj, "Date")) {
    Napi::Function toISOString = obj.Get("toISOString").As<Napi::Function>();
    Napi::Value iso = toISOString.Call(obj, {});
    return MakeWrapper(env, kTypeDate, iso);
  }
  if (IsInstanceOf(env, obj, "RegExp")) {
    Napi::Value source = obj.Get(kSourceKey);
    Napi::Value flags = obj.Get(kFlagsKey);
    Napi::Object payload = Napi::Object::New(env);
    payload.Set(kSourceKey, source);
    payload.Set(kFlagsKey, flags);
    return MakeWrapper(env, kTypeRegExp, payload);
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
      arr.Set(idx++, EncodeValue(env, v, seen));
    }
    return MakeWrapper(env, kTypeSet, arr);
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
               EncodeValue(env, entry.Get(static_cast<uint32_t>(0)), seen));
      pair.Set(static_cast<uint32_t>(1),
               EncodeValue(env, entry.Get(static_cast<uint32_t>(1)), seen));
      arr.Set(idx++, pair);
    }
    return MakeWrapper(env, kTypeMap, arr);
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
    out.Set(keyStr, EncodeValue(env, obj.Get(key), seen));
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
         t == kTypeMap || t == kTypeBuffer || t == kTypeArrayBuffer ||
         t == kTypeTypedArray || t == kTypeDataView;
}
Napi::Value DecodeValue(const Napi::Env &env, const Napi::Value &value,
                        const Ctors &ctors);
Napi::Value DecodeWrapper(const Napi::Env &env, const Napi::Object &obj,
                          const Ctors &ctors) {
  Napi::Value typeVal = obj.Get(kTypeKey);
  if (!typeVal.IsString()) return obj;
  std::string t = typeVal.As<Napi::String>().Utf8Value();
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
    return ctors.dateCtor.New({strVal});
  }
  if (t == kTypeRegExp) {
    Napi::Object payload = obj.Get(kValueKey).As<Napi::Object>();
    Napi::Value source = payload.Get(kSourceKey);
    Napi::Value flags = payload.Get(kFlagsKey);
    return ctors.regexpCtor.New({source, flags});
  }
  if (t == kTypeSet) {
    Napi::Array arr = obj.Get(kValueKey).As<Napi::Array>();
    Napi::Object setObj = ctors.setCtor.New({});
    Napi::Function addFn = setObj.Get("add").As<Napi::Function>();
    uint32_t length = arr.Length();
    for (uint32_t i = 0; i < length; i++) {
      Napi::Value decoded = DecodeValue(env, arr.Get(i), ctors);
      addFn.Call(setObj, {decoded});
    }
    return setObj;
  }
  if (t == kTypeMap) {
    Napi::Array arr = obj.Get(kValueKey).As<Napi::Array>();
    Napi::Object mapObj = ctors.mapCtor.New({});
    Napi::Function setFn = mapObj.Get("set").As<Napi::Function>();
    uint32_t length = arr.Length();
    for (uint32_t i = 0; i < length; i++) {
      Napi::Array entry = arr.Get(i).As<Napi::Array>();
      Napi::Value key = DecodeValue(env, entry.Get(static_cast<uint32_t>(0)), ctors);
      Napi::Value val = DecodeValue(env, entry.Get(static_cast<uint32_t>(1)), ctors);
      setFn.Call(mapObj, {key, val});
    }
    return mapObj;
  }
  if (t == kTypeBuffer) {
    std::string b64 = obj.Get(kValueKey).ToString().Utf8Value();
    std::vector<uint8_t> bytes = Base64Decode(b64);
    if (bytes.empty()) {
      return Napi::Buffer<uint8_t>::New(env, 0);
    }
    return Napi::Buffer<uint8_t>::Copy(env, bytes.data(), bytes.size());
  }
  if (t == kTypeArrayBuffer) {
    std::string b64 = obj.Get(kValueKey).ToString().Utf8Value();
    std::vector<uint8_t> bytes = Base64Decode(b64);
    Napi::ArrayBuffer buf = Napi::ArrayBuffer::New(env, bytes.size());
    if (!bytes.empty()) {
      std::memcpy(buf.Data(), bytes.data(), bytes.size());
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
    return ctor.New({buf, Napi::Number::New(env, 0), Napi::Number::New(env, length)});
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
    return ctor.New({buf, Napi::Number::New(env, 0), Napi::Number::New(env, length)});
  }
  return obj;
}
Napi::Value DecodeArray(const Napi::Env &env, const Napi::Array &arr,
                        const Ctors &ctors) {
  uint32_t length = arr.Length();
  Napi::Array out = Napi::Array::New(env, length);
  for (uint32_t i = 0; i < length; i++) {
    if (!arr.Has(i)) continue;
    Napi::Value item = arr.Get(i);
    if (IsWrapperType(env, item, kTypeHole)) {
      continue;
    }
    out.Set(i, DecodeValue(env, item, ctors));
  }
  return out;
}
Napi::Value DecodeObject(const Napi::Env &env, const Napi::Object &obj,
                         const Ctors &ctors) {
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
    out.Set(keyStr, DecodeValue(env, val, ctors));
  }
  return out;
}
Napi::Value DecodeValue(const Napi::Env &env, const Napi::Value &value,
                        const Ctors &ctors) {
  if (value.IsArray()) {
    return DecodeArray(env, value.As<Napi::Array>(), ctors);
  }
  if (value.IsObject()) {
    Napi::Object obj = value.As<Napi::Object>();
    if (obj.Has(kTypeKey)) {
      Napi::Value typeVal = obj.Get(kTypeKey);
      if (typeVal.IsString()) {
        std::string t = typeVal.As<Napi::String>().Utf8Value();
        if (IsKnownWrapperType(t)) {
          return DecodeWrapper(env, obj, ctors);
        }
      }
    }
    return DecodeObject(env, obj, ctors);
  }
  return value;
}
Napi::Value NativeStringify(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    throw Napi::TypeError::New(env, "Expected a value to stringify");
  }
  SeenStack seen;
  Napi::Value encoded = EncodeValue(env, info[0], seen);
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
  return DecodeValue(env, parsed, ctors);
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
