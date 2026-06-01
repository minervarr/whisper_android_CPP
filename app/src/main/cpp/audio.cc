#include "audio.hh"
#include <android/log.h>

#define TAG "Audio"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,   TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,    TAG, __VA_ARGS__)

static constexpr int32_t kSampleRate    = 16000;
static constexpr int32_t kChannelCount  = 1;

aaudio_data_callback_result_t AudioCapture::callback(
    AAudioStream*, void* userData, void* audioData, int32_t numFrames) {
  auto* self = static_cast<AudioCapture*>(userData);
  const float* src = static_cast<const float*>(audioData);

  std::lock_guard<std::mutex> lock(self->mutex_);
  self->buffer_.insert(self->buffer_.end(), src, src + numFrames);

  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

bool AudioCapture::start() {
  if (stream_) return true;

  AAudioStreamBuilder* builder = nullptr;
  if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK) {
    LOGE("Failed to create stream builder");
    return false;
  }

  AAudioStreamBuilder_setDirection(builder,     AAUDIO_DIRECTION_INPUT);
#if __ANDROID_API__ >= 28
  AAudioStreamBuilder_setInputPreset(builder,   AAUDIO_INPUT_PRESET_VOICE_RECOGNITION);
#endif
  AAudioStreamBuilder_setSampleRate(builder,    kSampleRate);
  AAudioStreamBuilder_setChannelCount(builder,  kChannelCount);
  AAudioStreamBuilder_setFormat(builder,        AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setDataCallback(builder,  callback, this);
  AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);

  aaudio_result_t res = AAudioStreamBuilder_openStream(builder, &stream_);
  AAudioStreamBuilder_delete(builder);

  if (res != AAUDIO_OK) {
    LOGE("Failed to open stream: %s", AAudio_convertResultToText(res));
    stream_ = nullptr;
    return false;
  }

  res = AAudioStream_requestStart(stream_);
  if (res != AAUDIO_OK) {
    LOGE("Failed to start stream: %s", AAudio_convertResultToText(res));
    AAudioStream_close(stream_);
    stream_ = nullptr;
    return false;
  }

  LOGI("Recording started at %d Hz", kSampleRate);
  return true;
}

void AudioCapture::stop() {
  if (!stream_) return;
  AAudioStream_requestStop(stream_);
  AAudioStream_close(stream_);
  stream_ = nullptr;
  LOGI("Recording stopped");
}

std::vector<float> AudioCapture::takeBuffer() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<float> out;
  out.swap(buffer_);
  return out;
}
