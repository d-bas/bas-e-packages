#ifndef BAS_UTILS_SERIALIZATION_ENCODE_H
#define BAS_UTILS_SERIALIZATION_ENCODE_H

#include "serde_utils.h"

namespace bas_serde {

Napi::Value EncodeValue(const Napi::Env &env, const Napi::Value &value,
                        EncodeContext &ctx, const Replacer &replacer,
                        bool applyReplacer);

}  // namespace bas_serde

#endif
