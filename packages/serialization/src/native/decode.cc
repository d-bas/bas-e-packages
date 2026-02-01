#include "decode.h"

#include <cmath>
#include <cstring>

namespace bas_serde {

// Decodes a wrapped value based on $$type.
static Napi::Value DecodeWrapper(const Napi::Env &env, const Napi::Object &obj,
                                 const Ctors &ctors, const Reviver &reviver,
                                 DecodeContext &ctx, bool applyReviver);

// Decodes arrays while preserving holes.
static Napi::Value DecodeArray(const Napi::Env &env, const Napi::Array &arr,
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

// Decodes plain objects.
static Napi::Value DecodeObject(const Napi::Env &env, const Napi::Object &obj,
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

static Napi::Value DecodeWrapper(const Napi::Env &env, const Napi::Object &obj,
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

  // Reference support for circular graphs.
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
    if (hasId) StoreRef(ctx, refId, dateObj);
    return dateObj;
  }
  if (t == kTypeRegExp) {
    Napi::Object payload = obj.Get(kValueKey).As<Napi::Object>();
    Napi::Value source = payload.Get(kSourceKey);
    Napi::Value flags = payload.Get(kFlagsKey);
    Napi::Object reObj = ctors.regexpCtor.New({source, flags}).As<Napi::Object>();
    if (hasId) StoreRef(ctx, refId, reObj);
    return reObj;
  }
  if (t == kTypeObject) {
    Napi::Object payload = obj.Get(kValueKey).As<Napi::Object>();
    Napi::Object out = Napi::Object::New(env);
    if (hasId) StoreRef(ctx, refId, out);
    Napi::Array keys = payload.GetPropertyNames();
    uint32_t length = keys.Length();
    for (uint32_t i = 0; i < length; i++) {
      Napi::Value key = keys.Get(i);
      if (!key.IsString()) continue;
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
    if (hasId) StoreRef(ctx, refId, out);
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
  // Errors restore name/message/stack plus custom own properties.
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
    if (hasId) StoreRef(ctx, refId, errObj);
    if (nameVal.IsString()) errObj.Set(kNameKey, nameVal);
    if (stackVal.IsString()) errObj.Set(kStackKey, stackVal);

    Napi::Value propsVal = payload.Get(kPropsKey);
    if (propsVal.IsArray()) {
      Napi::Array props = propsVal.As<Napi::Array>();
      uint32_t length = props.Length();
      for (uint32_t i = 0; i < length; i++) {
        Napi::Value entryVal = props.Get(i);
        if (!entryVal.IsArray()) continue;
        Napi::Array pair = entryVal.As<Napi::Array>();
        if (pair.Length() < 2) continue;
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
    if (hasId) StoreRef(ctx, refId, setObj);
    Napi::Function addFn = setObj.Get("add").As<Napi::Function>();
    uint32_t length = arr.Length();
    for (uint32_t i = 0; i < length; i++) {
      Napi::Value decoded = DecodeValue(env, arr.Get(i), ctors, reviver, ctx, true);
      addFn.Call(setObj, {decoded});
    }
    return setObj;
  }
  if (t == kTypeMap) {
    Napi::Array arr = obj.Get(kValueKey).As<Napi::Array>();
    Napi::Object mapObj = ctors.mapCtor.New({});
    if (hasId) StoreRef(ctx, refId, mapObj);
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
    if (hasId) StoreRef(ctx, refId, buf);
    return buf;
  }
  if (t == kTypeArrayBuffer) {
    std::string b64 = obj.Get(kValueKey).ToString().Utf8Value();
    std::vector<uint8_t> bytes = Base64Decode(b64);
    Napi::ArrayBuffer buf = Napi::ArrayBuffer::New(env, bytes.size());
    if (!bytes.empty()) {
      std::memcpy(buf.Data(), bytes.data(), bytes.size());
    }
    if (hasId) StoreRef(ctx, refId, buf);
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
    if (hasId) StoreRef(ctx, refId, typed);
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
    if (hasId) StoreRef(ctx, refId, view);
    return view;
  }

  return obj;
}

// Entry point that optionally applies a reviver.
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

}  // namespace bas_serde
