#include "transcribe.hh"
#include "whisper.h"
#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

#define TAG  "Transcribe"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)

static bool endsWith(const std::string& s, const char* suffix) {
  size_t sl = strlen(suffix);
  return s.size() >= sl && s.compare(s.size()-sl, sl, suffix) == 0;
}

// Scan dir for first .gguf or .bin file, return full path or "".
static std::string findModelInDir(const std::string& dir) {
  DIR* d = opendir(dir.c_str());
  if (!d) return "";
  struct dirent* ent;
  while ((ent = readdir(d)) != nullptr) {
    std::string name = ent->d_name;
    if (endsWith(name, ".gguf") || endsWith(name, ".bin")) {
      closedir(d);
      return dir + "/" + name;
    }
  }
  closedir(d);
  return "";
}

// Extract a single asset to destPath. Returns true on success.
static bool extractAsset(AAssetManager* mgr, const char* assetPath,
                         const char* destPath) {
  AAsset* asset = AAssetManager_open(mgr, assetPath, AASSET_MODE_STREAMING);
  if (!asset) { LOGE("Cannot open asset: %s", assetPath); return false; }

  FILE* f = fopen(destPath, "wb");
  if (!f) { AAsset_close(asset); LOGE("Cannot create: %s", destPath); return false; }

  const int BUF = 1 << 20; // 1 MB chunks
  std::vector<uint8_t> buf(BUF);
  int64_t n;
  while ((n = AAsset_read(asset, buf.data(), BUF)) > 0) {
    fwrite(buf.data(), 1, (size_t)n, f);
  }
  fclose(f);
  AAsset_close(asset);
  LOGI("Extracted %s", destPath);
  return true;
}

whisper_context* loadModel(const char* internalDataPath, AAssetManager* assetMgr,
                           std::string& outModelName) {
  std::string modelsDir = std::string(internalDataPath) + "/models";

  // Try internal storage first
  std::string modelPath = findModelInDir(modelsDir);

  if (modelPath.empty()) {
    // Try extracting from assets/models/
    LOGI("No model in %s, checking assets...", modelsDir.c_str());

    // Ensure dir exists
    mkdir(modelsDir.c_str(), 0755);

    AAssetDir* adir = AAssetManager_openDir(assetMgr, "models");
    if (adir) {
      const char* fname;
      while ((fname = AAssetDir_getNextFileName(adir)) != nullptr) {
        std::string n = fname;
        if (endsWith(n, ".gguf") || endsWith(n, ".bin")) {
          std::string assetPath = std::string("models/") + fname;
          std::string destPath  = modelsDir + "/" + fname;
          if (extractAsset(assetMgr, assetPath.c_str(), destPath.c_str())) {
            modelPath = destPath;
            break;
          }
        }
      }
      AAssetDir_close(adir);
    }
  }

  if (modelPath.empty()) {
    LOGE("No model found in internal storage or assets");
    outModelName = "NO MODEL";
    return nullptr;
  }

  // Extract just the filename for display
  size_t sep = modelPath.rfind('/');
  outModelName = (sep != std::string::npos) ? modelPath.substr(sep+1) : modelPath;

  LOGI("Loading model: %s", modelPath.c_str());
  whisper_context_params cparams = whisper_context_default_params();
  cparams.use_gpu = false;
  whisper_context* ctx = whisper_init_from_file_with_params(modelPath.c_str(), cparams);

  if (!ctx) {
    LOGE("whisper_init_from_file failed for: %s", modelPath.c_str());
    outModelName = "LOAD FAILED";
    return nullptr;
  }

  LOGI("Model loaded OK: %s", outModelName.c_str());
  return ctx;
}

void transcribeAsync(whisper_context* ctx,
                     std::vector<float> samples,
                     std::function<void(TranscribeResult)> callback) {
  std::thread([ctx, samples = std::move(samples), cb = std::move(callback)]() {
    TranscribeResult result;

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.language             = "auto";
    params.translate            = false;
    params.n_threads            = 4;
    params.print_progress       = false;
    params.print_realtime       = false;
    params.print_special        = false;
    params.print_timestamps     = false;
    params.single_segment       = false;
    params.suppress_blank       = true;

    int rc = whisper_full(ctx, params, samples.data(), (int)samples.size());
    if (rc != 0) {
      LOGE("whisper_full failed: %d", rc);
      result.ok   = false;
      result.text = "TRANSCRIPTION FAILED";
    } else {
      int n = whisper_full_n_segments(ctx);
      for (int i = 0; i < n; i++) {
        const char* seg = whisper_full_get_segment_text(ctx, i);
        if (seg) result.text += seg;
      }
      result.ok = true;
      LOGI("Transcription done: %d segments", n);
    }

    cb(result);
  }).detach();
}
