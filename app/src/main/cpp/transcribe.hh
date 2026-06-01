#pragma once
#include <android/asset_manager.h>
#include <functional>
#include <string>
#include <vector>

struct whisper_context;

struct TranscribeResult {
  std::string text;
  bool ok = false;
};

// Load model: first checks internalDataPath/models/, then extracts from assets.
// Returns nullptr on failure. Caller owns the context (whisper_free it).
whisper_context* loadModel(const char* internalDataPath, AAssetManager* assetMgr,
                           std::string& outModelName);

// Starts transcription in a detached thread. Calls callback on the result
// from that thread — caller must synchronize (e.g. atomic flag + main loop).
void transcribeAsync(whisper_context* ctx,
                     std::vector<float> samples,
                     std::function<void(TranscribeResult)> callback);
