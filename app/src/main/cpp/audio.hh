#pragma once
#include <aaudio/AAudio.h>
#include <mutex>
#include <vector>

class AudioCapture {
 public:
  ~AudioCapture() { stop(); }

  bool start();
  void stop();

  // Thread-safe: returns all accumulated samples and clears the internal buffer.
  std::vector<float> takeBuffer();

  bool isRunning() const { return stream_ != nullptr; }

 private:
  static aaudio_data_callback_result_t callback(
      AAudioStream*, void* userData, void* audioData, int32_t numFrames);

  AAudioStream* stream_ = nullptr;
  std::vector<float> buffer_;
  std::mutex mutex_;
};
