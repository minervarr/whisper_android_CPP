#pragma once
#include <string>
#include <vector>

#include "archive.hh"

struct whisper_full_params;

// Full set of user-facing whisper parameters (mirrors whisper_destilado's
// cfg::Settings). Defaults match whisper.cpp's own defaults / sensible fast
// values. Persisted through archive::KeyValueStore; applied via buildParams().
struct WhisperSettings {
  // Language / translation
  std::string language        = "auto";
  bool        translate       = false;
  bool        detect_language = false;

  // Threads
  int         n_threads = 4;

  // Sampling strategy
  bool        use_beam_search = false;
  int         beam_size       = 5;
  int         best_of         = 1;   // 1 = ~5x faster than whisper's default 5

  // Decoder tunables
  float       temperature     = 0.0f;
  float       temperature_inc = 0.2f;
  float       length_penalty  = -1.0f;
  float       entropy_thold   = 2.4f;
  float       logprob_thold   = -1.0f;
  float       no_speech_thold = 0.6f;

  // Token / context limits (0 => leave whisper default)
  int         n_max_text_ctx = 16384;
  int         max_len        = 0;
  int         max_tokens     = 0;
  int         audio_ctx      = 0;

  // Prompt (free text — touch-only build can't edit yet; carry flag still works)
  std::string initial_prompt;
  bool        carry_initial_prompt = false;

  // Behaviour
  bool        suppress_blank = true;
  bool        suppress_nst   = true;
  bool        single_segment = false;
  bool        split_on_word  = false;
  bool        tdrz_enable    = false;

  // Debug
  bool        print_progress = false;
  bool        print_realtime = false;

  void load(const archive::KeyValueStore& kv);
  void save(archive::KeyValueStore& kv) const;
};

// Build whisper_full_params from settings (ports destilado build_params_from).
// `out` is filled; its string fields point into `s`, so keep `s` alive for the
// duration of the whisper_full() call.
void buildParams(const WhisperSettings& s, whisper_full_params& out);

// One language option: code ("auto", "en", "es", …) + a display label.
struct LangOption { std::string code; std::string label; };
const std::vector<LangOption>& languageOptions();  // built once from whisper
// Labels only (parallel to languageOptions()), cached for the dropdown list so
// it isn't rebuilt every frame while the picker is open/scrolling.
const std::vector<std::string>& languageLabels();

// Device CPU thread count (>=1), used as the Threads stepper's maximum.
int maxThreads();
