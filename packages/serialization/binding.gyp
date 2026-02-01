{
  "targets": [
    {
      "target_name": "bas_serde",
      "sources": ["src/native/addon.cc"],
      "cflags_cc": ["-std=c++17"],
      "include_dirs": ["<!@(node -p \"require('node-addon-api').include\")"],
      "dependencies": ["<!(node -p \"require('node-addon-api').gyp\")"],
      "defines": ["NAPI_CPP_EXCEPTIONS"]
    }
  ]
}
