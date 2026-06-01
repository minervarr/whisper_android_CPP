#include "app.hh"
#include "audio.hh"
#include "transcribe.hh"
#include "whisper.h"
#include <android_native_app_glue.h>
#include <android/input.h>
#include <android/log.h>
#include <atomic>
#include <jni.h>
#include <mutex>
#include <string>
#include <thread>

#define TAG  "Main"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// ── JNI helpers ────────────────────────────────────────────────────────────

// Calls MainActivity.launchFilePicker() on the UI thread via JNI.
static void launchFilePicker(ANativeActivity* act) {
  JNIEnv* env = nullptr;
  act->vm->AttachCurrentThread(&env, nullptr);
  jclass    cls    = env->GetObjectClass(act->clazz);
  jmethodID method = env->GetMethodID(cls, "launchFilePicker", "()V");
  if (method) env->CallVoidMethod(act->clazz, method);
  act->vm->DetachCurrentThread();
}

static void clearSavedModelPath(ANativeActivity* act) {
  JNIEnv* env = nullptr;
  act->vm->AttachCurrentThread(&env, nullptr);
  jclass    cls    = env->GetObjectClass(act->clazz);
  jmethodID method = env->GetMethodID(cls, "clearSavedModelPath", "()V");
  if (method) env->CallVoidMethod(act->clazz, method);
  act->vm->DetachCurrentThread();
}

// Calls MainActivity.getSavedModelPath() and returns the result as std::string.
static std::string getSavedModelPath(ANativeActivity* act) {
  JNIEnv* env = nullptr;
  act->vm->AttachCurrentThread(&env, nullptr);
  jclass    cls    = env->GetObjectClass(act->clazz);
  jmethodID method = env->GetMethodID(cls, "getSavedModelPath", "()Ljava/lang/String;");
  std::string result;
  if (method) {
    jstring jstr = (jstring)env->CallObjectMethod(act->clazz, method);
    if (jstr) {
      const char* cstr = env->GetStringUTFChars(jstr, nullptr);
      result = cstr ? cstr : "";
      env->ReleaseStringUTFChars(jstr, cstr);
    }
  }
  act->vm->DetachCurrentThread();
  return result;
}

// Calls MainActivity.copyToClipboard(text) on the UI thread via JNI.
static void copyToClipboard(ANativeActivity* act, const std::string& text) {
  JNIEnv* env = nullptr;
  act->vm->AttachCurrentThread(&env, nullptr);
  jclass    cls    = env->GetObjectClass(act->clazz);
  jmethodID method = env->GetMethodID(cls, "copyToClipboard", "(Ljava/lang/String;)V");
  if (method) {
    jstring s = env->NewStringUTF(text.c_str());
    env->CallVoidMethod(act->clazz, method, s);
    env->DeleteLocalRef(s);
  }
  act->vm->DetachCurrentThread();
}

// ── Permission helpers ─────────────────────────────────────────────────────

static bool checkAudioPermission(ANativeActivity* act) {
  JNIEnv* env = nullptr;
  act->vm->AttachCurrentThread(&env, nullptr);

  jclass cls = env->GetObjectClass(act->clazz);
  jmethodID checkPerm = env->GetMethodID(cls, "checkSelfPermission",
                                         "(Ljava/lang/String;)I");
  jstring permStr = env->NewStringUTF("android.permission.RECORD_AUDIO");
  jint result = env->CallIntMethod(act->clazz, checkPerm, permStr);
  env->DeleteLocalRef(permStr);
  act->vm->DetachCurrentThread();
  return result == 0; // PackageManager.PERMISSION_GRANTED == 0
}

static void requestAudioPermission(ANativeActivity* act) {
  JNIEnv* env = nullptr;
  act->vm->AttachCurrentThread(&env, nullptr);

  jclass cls = env->GetObjectClass(act->clazz);
  jmethodID reqPerms = env->GetMethodID(cls, "requestPermissions",
                                        "([Ljava/lang/String;I)V");
  jclass strCls = env->FindClass("java/lang/String");
  jstring permStr = env->NewStringUTF("android.permission.RECORD_AUDIO");
  jobjectArray arr = env->NewObjectArray(1, strCls, permStr);
  env->CallVoidMethod(act->clazz, reqPerms, arr, 1);
  env->DeleteLocalRef(arr);
  env->DeleteLocalRef(permStr);
  act->vm->DetachCurrentThread();
}

// ── App-level state ────────────────────────────────────────────────────────

struct WhisperApp {
  App            vk;
  AudioCapture   audio;
  whisper_context* whisperCtx = nullptr;

  // Result delivery from transcription thread
  std::atomic<bool> resultReady{false};
  std::mutex        resultMutex;
  TranscribeResult  pendingResult;

  // Model-load result delivery from the background loader thread. The worker
  // only stores the result here; the main loop applies it to the UI. This keeps
  // all ui.* writes on the main thread (see Ui: "State written by main thread"),
  // so a window teardown/rebuild on return from the file picker can't clobber
  // the Idle transition and leave the UI stuck on "LOADING".
  std::atomic<bool> modelLoadDone{false};
  std::mutex        modelLoadMutex;
  whisper_context*  loadedCtx = nullptr;
  std::string       loadedName;

  // Model path delivered from Java (file picker or FileX intent)
  std::atomic<bool> modelPathReady{false};
  std::mutex        modelPathMutex;
  std::string       pendingModelPath;

  // Survives TERM_WINDOW so we can restore the UI without reloading.
  std::string       loadedModelName;

  bool permissionRequested = false;

  // Guards against spawning the model-loader thread more than once
  // (INIT_WINDOW and RESUME can both reach the load path).
  std::atomic<bool> modelLoadStarted{false};
};

// Global pointer so the JNI export can deliver the path.
static WhisperApp* g_app = nullptr;

// Called from Java MainActivity.nativeOnModelPicked()
extern "C" JNIEXPORT void JNICALL
Java_io_nava_whisper_MainActivity_nativeOnModelPicked(JNIEnv* env, jclass, jstring jpath) {
  if (!g_app) return;
  const char* cstr = env->GetStringUTFChars(jpath, nullptr);
  std::string path = cstr ? cstr : "";
  env->ReleaseStringUTFChars(jpath, cstr);
  {
    std::lock_guard<std::mutex> lock(g_app->modelPathMutex);
    g_app->pendingModelPath = std::move(path);
  }
  g_app->modelPathReady.store(true);
}

// Called from the background loader thread. Only stores the result + raises a
// flag; the main loop (android_main) applies it to the UI on the main thread.
static void onModelLoaded(WhisperApp* wa, whisper_context* ctx,
                          const std::string& modelName) {
  {
    std::lock_guard<std::mutex> lock(wa->modelLoadMutex);
    wa->loadedCtx  = ctx;
    wa->loadedName = modelName;
  }
  wa->modelLoadDone.store(true);
}

// Applies a delivered model-load result. Main thread only.
static void applyModelLoad(WhisperApp* wa, whisper_context* ctx,
                           const std::string& modelName) {
  wa->whisperCtx      = ctx;
  wa->loadedModelName = modelName;
  if (ctx) {
    wa->vk.ui.modelName = modelName;
    wa->vk.ui.state     = AppState::Idle;
  } else {
    wa->vk.ui.modelName = modelName;
    wa->vk.ui.errorMsg  = "FAILED TO LOAD MODEL";
    wa->vk.ui.state     = AppState::Error;
  }
  wa->vk.ui.dirty = true; wa->vk.ui.geomDirty = true;
}

// Spawns the background model-loader thread (saved path first, then bundled
// assets). Safe to call from multiple lifecycle events — only the first call
// after a fresh load actually starts a thread.
static void startModelLoad(struct android_app* state, WhisperApp* wa) {
  if (wa->whisperCtx) return;
  bool expected = false;
  if (!wa->modelLoadStarted.compare_exchange_strong(expected, true)) return;

  wa->vk.ui.state = AppState::LoadingModel;
  wa->vk.ui.dirty = true; wa->vk.ui.geomDirty = true;
  std::thread([wa, state]() {
    std::string savedPath = getSavedModelPath(state->activity);
    std::string name;
    whisper_context* ctx = nullptr;
    if (!savedPath.empty()) {
      ctx = loadModelFromPath(savedPath.c_str(), name);
      if (!ctx) clearSavedModelPath(state->activity); // bad path — don't retry
    }
    if (!ctx) {
      ctx = loadModel(state->activity->internalDataPath,
                      state->activity->assetManager, name);
    }
    onModelLoaded(wa, ctx, name);
  }).detach();
}

// ── android_native_app_glue callbacks ─────────────────────────────────────

// Derive system-bar insets from the glue's contentRect (full window minus the
// area the OS reserves for status/navigation bars) and push them to the UI.
static void pushContentRect(struct android_app* state, WhisperApp* wa) {
  if (!wa->vk.initialized || !state->window) return;
  int32_t w = ANativeWindow_getWidth(state->window);
  int32_t h = ANativeWindow_getHeight(state->window);
  const ARect& r = state->contentRect;
  uint32_t top    = r.top    > 0 ? (uint32_t)r.top                            : 0;
  uint32_t left   = r.left   > 0 ? (uint32_t)r.left                           : 0;
  uint32_t bottom = (r.bottom > 0 && r.bottom < h) ? (uint32_t)(h - r.bottom) : 0;
  uint32_t right  = (r.right  > 0 && r.right  < w) ? (uint32_t)(w - r.right)  : 0;
  wa->vk.setInsets(top, bottom, left, right);
}

static void handleCommand(struct android_app* state, int32_t cmd) {
  WhisperApp* wa = static_cast<WhisperApp*>(state->userData);

  switch (cmd) {
    case APP_CMD_INIT_WINDOW: {
      wa->vk.init(state->window, state->activity->assetManager);
      wa->vk.initialized = true;
      wa->vk.dirty       = true;
      pushContentRect(state, wa);

      bool hasPerm = checkAudioPermission(state->activity);
      if (!hasPerm) {
        wa->vk.ui.state = AppState::NeedPermission;
        if (!wa->permissionRequested) {
          requestAudioPermission(state->activity);
          wa->permissionRequested = true;
        }
        wa->vk.ui.dirty = true; wa->vk.ui.geomDirty = true;
        break;
      }

      // Model already in memory — restore UI instantly without reloading.
      if (wa->whisperCtx) {
        wa->vk.ui.modelName = wa->loadedModelName;
        wa->vk.ui.state     = AppState::Idle;
        wa->vk.ui.dirty     = true;
        break;
      }

      // First launch or model not yet loaded — load in background thread.
      startModelLoad(state, wa);
      break;
    }

    case APP_CMD_TERM_WINDOW:
      wa->vk.cleanup();
      new (&wa->vk) App();
      wa->vk.initialized = false;
      break;

    case APP_CMD_CONTENT_RECT_CHANGED:
      pushContentRect(state, wa);
      wa->vk.dirty = true;
      break;

    case APP_CMD_WINDOW_REDRAW_NEEDED:
    case APP_CMD_CONFIG_CHANGED:
      wa->vk.dirty = true;
      break;

    case APP_CMD_RESUME:
      // Re-check permission after returning from permission dialog
      if (wa->vk.initialized &&
          wa->vk.ui.state == AppState::NeedPermission &&
          checkAudioPermission(state->activity)) {
        if (wa->whisperCtx) {
          wa->vk.ui.state = AppState::Idle;
          wa->vk.ui.dirty = true; wa->vk.ui.geomDirty = true;
        } else {
          // Permission just granted — start the load now instead of spinning
          // on "LOADING" forever (the INIT_WINDOW load path was skipped while
          // permission was still denied).
          startModelLoad(state, wa);
        }
      }
      // If user cancelled the file picker, revert to Idle/Error.
      if (wa->vk.initialized && wa->vk.ui.state == AppState::PickingModel &&
          !wa->modelPathReady.load()) {
        wa->vk.ui.state = wa->whisperCtx ? AppState::Idle : AppState::Error;
        wa->vk.ui.dirty = true; wa->vk.ui.geomDirty = true;
      }
      break;

    default: break;
  }
}

static int32_t handleInput(struct android_app* state, AInputEvent* event) {
  if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;

  WhisperApp* wa = static_cast<WhisperApp*>(state->userData);
  if (!wa->vk.initialized) return 0;

  int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
  float x = AMotionEvent_getX(event, 0);
  float y = AMotionEvent_getY(event, 0);

  // Drag to scroll the transcription text area.
  if (action == AMOTION_EVENT_ACTION_MOVE) {
    wa->vk.ui.onDragMove(x, y);
    if (wa->vk.ui.dirty) wa->vk.dirty = true;
    return 1;
  }
  if (action != AMOTION_EVENT_ACTION_DOWN) return 0;

  wa->vk.ui.onDragStart(x, y);

  AppState before = wa->vk.ui.state;
  wa->vk.onTouch(x, y);
  AppState after  = wa->vk.ui.state;

  // COPY tapped — push the transcription to the system clipboard.
  if (wa->vk.ui.takeCopyRequest())
    copyToClipboard(state->activity, wa->vk.ui.transcription);

  // IDLE/ERROR -> PICKING MODEL: launch file picker
  if (after == AppState::PickingModel && before != AppState::PickingModel) {
    launchFilePicker(state->activity);
  }

  // IDLE -> RECORDING: start mic
  if (before == AppState::Idle && after == AppState::Recording) {
    if (!checkAudioPermission(state->activity)) {
      wa->vk.ui.state = AppState::NeedPermission;
      requestAudioPermission(state->activity);
      wa->vk.ui.dirty = true; wa->vk.ui.geomDirty = true;
    } else {
      wa->audio.start();
    }
  }

  // RECORDING -> PROCESSING: stop mic, launch transcription
  if (before == AppState::Recording && after == AppState::Processing) {
    wa->audio.stop();
    std::vector<float> samples = wa->audio.takeBuffer();

    if (samples.empty() || !wa->whisperCtx) {
      wa->vk.ui.state = AppState::Idle;
      wa->vk.ui.dirty = true; wa->vk.ui.geomDirty = true;
    } else {
      transcribeAsync(wa->whisperCtx, std::move(samples),
                      [wa](TranscribeResult r) {
                        std::lock_guard<std::mutex> lock(wa->resultMutex);
                        wa->pendingResult = std::move(r);
                        wa->resultReady.store(true);
                      });
    }
  }

  return 1;
}

// ── Entry point ────────────────────────────────────────────────────────────

void android_main(struct android_app* state) {
  WhisperApp wa;
  g_app = &wa;
  state->userData    = &wa;
  state->onAppCmd    = handleCommand;
  state->onInputEvent = handleInput;

  while (true) {
    int events;
    struct android_poll_source* source;
    // Block up to 16 ms when idle so transcription threads get full CPU.
    int pollTimeout = (wa.vk.initialized && wa.vk.dirty) ? 0 : 16;
    ALooper_pollOnce(pollTimeout, nullptr, &events, (void**)&source);
    if (source) source->process(state, source);

    // Reload model when a new path arrives from the file picker or FileX.
    if (wa.modelPathReady.load()) {
      wa.modelPathReady.store(false);
      std::string path;
      { std::lock_guard<std::mutex> lock(wa.modelPathMutex); path = wa.pendingModelPath; }
      if (!path.empty()) {
        wa.vk.ui.state = AppState::LoadingModel;
        wa.vk.ui.dirty = true; wa.vk.ui.geomDirty = true;
        // Free old context before loading new one.
        whisper_context* oldCtx = wa.whisperCtx;
        wa.whisperCtx = nullptr;
        std::thread([&wa, path, oldCtx]() {
          if (oldCtx) whisper_free(oldCtx);
          std::string name;
          whisper_context* ctx = loadModelFromPath(path.c_str(), name);
          onModelLoaded(&wa, ctx, name);
        }).detach();
      }
    }

    // Apply a model-load result delivered from the background loader thread.
    // Done on the main thread so it can't race with window teardown/rebuild.
    if (wa.modelLoadDone.load()) {
      wa.modelLoadDone.store(false);
      whisper_context* ctx;
      std::string name;
      {
        std::lock_guard<std::mutex> lock(wa.modelLoadMutex);
        ctx  = wa.loadedCtx;
        name = wa.loadedName;
      }
      applyModelLoad(&wa, ctx, name);
    }

    // Deliver transcription result from background thread
    if (wa.resultReady.load()) {
      wa.resultReady.store(false);
      TranscribeResult r;
      {
        std::lock_guard<std::mutex> lock(wa.resultMutex);
        r = std::move(wa.pendingResult);
      }
      if (r.ok) {
        if (!wa.vk.ui.transcription.empty()) wa.vk.ui.transcription += '\n';
        wa.vk.ui.transcription += r.text;
        wa.vk.ui.scrollTextToBottom();
        wa.vk.ui.state = AppState::Idle;
      } else {
        wa.vk.ui.errorMsg = r.text;
        wa.vk.ui.state    = AppState::Error;
      }
      wa.vk.ui.dirty = true; wa.vk.ui.geomDirty = true;
    }

    if (wa.vk.ui.dirty) wa.vk.dirty = true;

    if (wa.vk.initialized && wa.vk.dirty) {
      wa.vk.drawFrame();
      wa.vk.dirty = false;
    }
  }
}
