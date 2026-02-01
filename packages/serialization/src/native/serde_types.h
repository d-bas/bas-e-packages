#ifndef BAS_UTILS_SERIALIZATION_SERDE_TYPES_H
#define BAS_UTILS_SERIALIZATION_SERDE_TYPES_H

#include <napi.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace bas_serde {

constexpr const char kTypeKey[] = "$$type";
constexpr const char kValueKey[] = "value";
constexpr const char kArrayTypeKey[] = "arrayType";
constexpr const char kByteOffsetKey[] = "byteOffset";
constexpr const char kLengthKey[] = "length";
constexpr const char kSourceKey[] = "source";
constexpr const char kFlagsKey[] = "flags";
constexpr const char kMessageKey[] = "message";
constexpr const char kNameKey[] = "name";
constexpr const char kStackKey[] = "stack";
constexpr const char kKeyKey[] = "key";
constexpr const char kDescriptionKey[] = "description";
constexpr const char kGlobalKey[] = "global";
constexpr const char kPropsKey[] = "props";
constexpr const char kIdKey[] = "$$id";

constexpr const char kTypeUndefined[] = "Undefined";
constexpr const char kTypeNumber[] = "Number";
constexpr const char kTypeBigInt[] = "BigInt";
constexpr const char kTypeDate[] = "Date";
constexpr const char kTypeRegExp[] = "RegExp";
constexpr const char kTypeSet[] = "Set";
constexpr const char kTypeMap[] = "Map";
constexpr const char kTypeError[] = "Error";
constexpr const char kTypeObject[] = "object";
constexpr const char kTypeArray[] = "array";
constexpr const char kTypeReference[] = "reference";
constexpr const char kTypePropKeyString[] = "PropKeyString";
constexpr const char kTypePropKeySymbol[] = "PropKeySymbol";
constexpr const char kTypeBuffer[] = "Buffer";
constexpr const char kTypeArrayBuffer[] = "ArrayBuffer";
constexpr const char kTypeTypedArray[] = "TypedArray";
constexpr const char kTypeDataView[] = "DataView";
constexpr const char kTypeHole[] = "Hole";

constexpr const char kNumNaN[] = "NaN";
constexpr const char kNumInf[] = "Infinity";
constexpr const char kNumNegInf[] = "-Infinity";

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

}  // namespace bas_serde

#endif
