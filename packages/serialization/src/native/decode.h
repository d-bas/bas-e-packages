#ifndef BAS_UTILS_SERIALIZATION_DECODE_H
#define BAS_UTILS_SERIALIZATION_DECODE_H

#include "serde_utils.h"

namespace bas_serde {

Napi::Value DecodeValue(const Napi::Env &env, const Napi::Value &value,
                        const Ctors &ctors, const Reviver &reviver,
                        DecodeContext &ctx, bool applyReviver);

}  // namespace bas_serde

#endif
