#include "encode.h"

namespace bas_serde {

// Tracks the current recursion stack to detect cycles when circular refs are disabled.
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

Napi::Value EncodeValue(const Napi::Env &env, const Napi::Value &value,
                        EncodeContext &ctx, const Replacer &replacer,
                        bool applyReplacer) {
  // Apply replacer before serialization if enabled.
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

  // Primitives and special numbers.
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
        return MakeWrapper(env, kTypeNumber, Napi::String::New(env, kNumNaN));
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
  // Unsupported types.
  if (value.IsFunction() || value.IsSymbol()) {
    throw Napi::TypeError::New(env, "Unsupported value type");
  }
  if (!value.IsObject()) {
    throw Napi::TypeError::New(env, "Unsupported value type");
  }

  Napi::Object obj = value.As<Napi::Object>();
  uint32_t currentId = 0;
  bool hasId = false;

  // Circular reference handling.
  if (ctx.allowCircular) {
    int seenId = FindSeenId(ctx.entries, value);
    if (seenId >= 0) {
      return MakeReference(env, static_cast<uint32_t>(seenId));
    }
    currentId = ctx.nextId++;
    hasId = true;
    ctx.entries.push_back({Napi::Persistent(value), currentId});
  } else if (SeenContains(ctx.stack, value)) {
    throw Napi::TypeError::New(env, "Circular reference detected");
  }

  SeenGuard guard(ctx.stack, value);

  // Arrays (preserve holes).
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

  // Buffers and binary types.
  if (value.IsArrayBuffer()) {
    Napi::ArrayBuffer buf = value.As<Napi::ArrayBuffer>();
    std::string b64 = Base64Encode(static_cast<uint8_t *>(buf.Data()),
                                   buf.ByteLength());
    Napi::Object wrapper = MakeWrapper(env, kTypeArrayBuffer, Napi::String::New(env, b64));
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

  // DataView and TypedArray handling via N-API.
  bool isDataView = false;
  napi_status dataViewStatus = napi_is_dataview(env, value, &isDataView);
  if (dataViewStatus != napi_ok) {
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
  napi_status typedArrayStatus = napi_is_typedarray(env, value, &isTypedArray);
  if (typedArrayStatus != napi_ok) {
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
        napi_get_typedarray_info(env, value, &type, &length, &data, &arraybuffer,
                                 &byteOffset);
    if (status != napi_ok) {
      std::string message = GetNapiErrorMessage(env);
      throw Napi::TypeError::New(env, "napi_get_typedarray_info failed: " + message);
    }
    std::string typeName = TypedArrayName(type);
    size_t bytesPerElement = TypedArrayBytesPerElement(type);
    if (typeName.empty() || bytesPerElement == 0) {
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

  // Built-in complex types.
  if (obj.InstanceOf(env.Global().Get("Date").As<Napi::Function>())) {
    Napi::Function toISOString = obj.Get("toISOString").As<Napi::Function>();
    Napi::Value iso = toISOString.Call(obj, {});
    Napi::Object wrapper = MakeWrapper(env, kTypeDate, iso);
    SetIdIfNeeded(env, wrapper, hasId, currentId);
    return wrapper;
  }

  if (obj.InstanceOf(env.Global().Get("RegExp").As<Napi::Function>())) {
    Napi::Value source = obj.Get(kSourceKey);
    Napi::Value flags = obj.Get(kFlagsKey);
    Napi::Object payload = Napi::Object::New(env);
    payload.Set(kSourceKey, source);
    payload.Set(kFlagsKey, flags);
    Napi::Object wrapper = MakeWrapper(env, kTypeRegExp, payload);
    SetIdIfNeeded(env, wrapper, hasId, currentId);
    return wrapper;
  }

  // Errors (own properties + symbols).
  if (obj.InstanceOf(env.Global().Get("Error").As<Napi::Function>())) {
    Napi::Object payload = Napi::Object::New(env);
    Napi::Value name = obj.Get(kNameKey);
    Napi::Value message = obj.Get(kMessageKey);
    Napi::Value stack = obj.Get(kStackKey);
    payload.Set(kNameKey, name.IsUndefined() ? env.Undefined() : name.ToString());
    payload.Set(kMessageKey,
                message.IsUndefined() ? env.Undefined() : message.ToString());
    payload.Set(kStackKey, stack.IsUndefined() ? env.Undefined() : stack.ToString());

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
      pair.Set(static_cast<uint32_t>(1), EncodeValue(env, obj.Get(key), ctx, replacer, true));
      props.Set(idx++, pair);
    }

    Napi::Object objectCtor = env.Global().Get("Object").As<Napi::Object>();
    Napi::Function getOwnPropertySymbols =
        objectCtor.Get("getOwnPropertySymbols").As<Napi::Function>();
    Napi::Array symbols =
        getOwnPropertySymbols.Call(objectCtor, {obj}).As<Napi::Array>();
    uint32_t symLength = symbols.Length();
    Napi::Object symbolCtor = env.Global().Get("Symbol").As<Napi::Object>();
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
      pair.Set(static_cast<uint32_t>(1), EncodeValue(env, obj.Get(sym), ctx, replacer, true));
      props.Set(idx++, pair);
    }

    payload.Set(kPropsKey, props);
    Napi::Object wrapper = MakeWrapper(env, kTypeError, payload);
    SetIdIfNeeded(env, wrapper, hasId, currentId);
    return wrapper;
  }

  // Collections.
  if (obj.InstanceOf(env.Global().Get("Set").As<Napi::Function>())) {
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

  if (obj.InstanceOf(env.Global().Get("Map").As<Napi::Function>())) {
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
               EncodeValue(env, entry.Get(static_cast<uint32_t>(0)), ctx, replacer, true));
      pair.Set(static_cast<uint32_t>(1),
               EncodeValue(env, entry.Get(static_cast<uint32_t>(1)), ctx, replacer, true));
      arr.Set(idx++, pair);
    }
    Napi::Object wrapper = MakeWrapper(env, kTypeMap, arr);
    SetIdIfNeeded(env, wrapper, hasId, currentId);
    return wrapper;
  }

  // Plain objects.
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

}  // namespace bas_serde
