{
  "targets": [
    {
      "target_name": "bas_serde",
      "sources": [
        "src/native/addon.cc",
        "src/native/encode.cc",
        "src/native/decode.cc",
        "src/native/serde_utils.cc"
      ],
      "cflags_cc": ["-std=c++17", "-fexceptions"],
      "xcode_settings": {
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
        "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
        "CLANG_CXX_LIBRARY": "libc++"
      },
      "include_dirs": ["<!@(node -p \"require('node-addon-api').include\")"],
      "dependencies": ["<!(node -p \"require('node-addon-api').gyp\")"],
      "defines": ["NAPI_CPP_EXCEPTIONS"]
    }
  ]
}
