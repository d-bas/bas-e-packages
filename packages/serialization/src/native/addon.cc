#include "decode.h"
#include "encode.h"
#include "serde_utils.h"

namespace bas_serde {

Napi::Value NativeStringify(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    throw Napi::TypeError::New(env, "Expected a value to stringify");
  }

  // Parse stringify options.
  Replacer replacer;
  EncodeContext ctx;

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
    if (options.Has("circularReferences")) {
      Napi::Value circularVal = options.Get("circularReferences");
      if (circularVal.IsBoolean()) {
        ctx.allowCircular = circularVal.ToBoolean().Value();
      }
    }
  }

  // Serialize to wrapper graph, then JSON.stringify.
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

  // Parse reviver options.
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

  // JSON.parse then decode wrappers.
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

  DecodeContext ctx;
  return DecodeValue(env, parsed, ctors, reviver, ctx, true);
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  exports.Set("stringify", Napi::Function::New(env, NativeStringify));
  exports.Set("parse", Napi::Function::New(env, NativeParse));
  return exports;
}

}  // namespace bas_serde

NODE_API_MODULE(bas_serde, bas_serde::Init)
