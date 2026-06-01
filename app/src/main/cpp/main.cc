#include "app.hh"
#include "audio.hh"
#include "transcribe.hh"
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

  bool permissionRequested = false;
};

static void onModelLoaded(WhisperApp* wa, whisper_context* ctx,
                          const std::string& modelName) {
  wa->whisperCtx = ctx;
  if (ctx) {
    wa->vk.ui.modelName = modelName;
    wa->vk.ui.state     = AppState::Idle;
  } else {
    wa->vk.ui.modelName = modelName;
    wa->vk.ui.errorMsg  = "FAILED TO LOAD MODEL";
    wa->vk.ui.state     = AppState::Error;
  }
  wa->vk.ui.dirty = true;
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

      // Check / request RECORD_AUDIO
      if (checkAudioPermission(state->activity)) {
        wa->vk.ui.state = AppState::LoadingModel;
      } else {
        wa->vk.ui.state = AppState::NeedPermission;
        if (!wa->permissionRequested) {
          requestAudioPermission(state->activity);
          wa->permissionRequested = true;
        }
      }
      wa->vk.ui.dirty = true;

      // Load model in background thread
      std::thread([wa, state]() {
        std::string name;
        whisper_context* ctx = loadModel(
            state->activity->internalDataPath,
            state->activity->assetManager,
            name);
        onModelLoaded(wa, ctx, name);
      }).detach();
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
        wa->vk.ui.state = (wa->whisperCtx) ? AppState::Idle : AppState::LoadingModel;
        wa->vk.ui.dirty = true;
      }
      break;

    default: break;
  }
}

static int32_t handleInput(struct android_app* state, AInputEvent* event) {
  if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
  int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
  if (action != AMOTION_EVENT_ACTION_DOWN) return 0;

  WhisperApp* wa = static_cast<WhisperApp*>(state->userData);
  if (!wa->vk.initialized) return 0;

  float x = AMotionEvent_getX(event, 0);
  float y = AMotionEvent_getY(event, 0);

  AppState before = wa->vk.ui.state;
  wa->vk.onTouch(x, y);
  AppState after  = wa->vk.ui.state;

  // IDLE -> RECORDING: start mic
  if (before == AppState::Idle && after == AppState::Recording) {
    if (!checkAudioPermission(state->activity)) {
      wa->vk.ui.state = AppState::NeedPermission;
      requestAudioPermission(state->activity);
      wa->vk.ui.dirty = true;
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
      wa->vk.ui.dirty = true;
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
  state->userData    = &wa;
  state->onAppCmd    = handleCommand;
  state->onInputEvent = handleInput;

  while (true) {
    int events;
    struct android_poll_source* source;
    ALooper_pollOnce(0, nullptr, &events, (void**)&source);
    if (source) source->process(state, source);

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
        wa.vk.ui.state = AppState::Idle;
      } else {
        wa.vk.ui.errorMsg = r.text;
        wa.vk.ui.state    = AppState::Error;
      }
      wa.vk.ui.dirty = true;
    }

    if (wa.vk.initialized && wa.vk.dirty) {
      wa.vk.drawFrame();
      wa.vk.dirty = false;
    }
  }
}
