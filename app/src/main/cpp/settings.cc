#include "settings.hh"

#include <algorithm>
#include <thread>

#include "whisper.h"

int maxThreads() {
  static const int n = std::max(1u, std::thread::hardware_concurrency());
  return n;
}

void WhisperSettings::load(const archive::KeyValueStore& kv) {
  language        = kv.getString("language", language);
  translate       = kv.getBool("translate", translate);
  detect_language = kv.getBool("detect_language", detect_language);
  n_threads       = kv.getInt("n_threads", n_threads);
  use_beam_search = kv.getBool("use_beam_search", use_beam_search);
  beam_size       = kv.getInt("beam_size", beam_size);
  best_of         = kv.getInt("best_of", best_of);
  temperature     = kv.getFloat("temperature", temperature);
  temperature_inc = kv.getFloat("temperature_inc", temperature_inc);
  length_penalty  = kv.getFloat("length_penalty", length_penalty);
  entropy_thold   = kv.getFloat("entropy_thold", entropy_thold);
  logprob_thold   = kv.getFloat("logprob_thold", logprob_thold);
  no_speech_thold = kv.getFloat("no_speech_thold", no_speech_thold);
  n_max_text_ctx  = kv.getInt("n_max_text_ctx", n_max_text_ctx);
  max_len         = kv.getInt("max_len", max_len);
  max_tokens      = kv.getInt("max_tokens", max_tokens);
  audio_ctx       = kv.getInt("audio_ctx", audio_ctx);
  initial_prompt  = kv.getString("initial_prompt", initial_prompt);
  carry_initial_prompt = kv.getBool("carry_initial_prompt", carry_initial_prompt);
  suppress_blank  = kv.getBool("suppress_blank", suppress_blank);
  suppress_nst    = kv.getBool("suppress_nst", suppress_nst);
  single_segment  = kv.getBool("single_segment", single_segment);
  split_on_word   = kv.getBool("split_on_word", split_on_word);
  tdrz_enable     = kv.getBool("tdrz_enable", tdrz_enable);
  print_progress  = kv.getBool("print_progress", print_progress);
  print_realtime  = kv.getBool("print_realtime", print_realtime);
  n_threads       = std::clamp(n_threads, 1, maxThreads());
}

void WhisperSettings::save(archive::KeyValueStore& kv) const {
  kv.setString("language", language);
  kv.setBool("translate", translate);
  kv.setBool("detect_language", detect_language);
  kv.setInt("n_threads", n_threads);
  kv.setBool("use_beam_search", use_beam_search);
  kv.setInt("beam_size", beam_size);
  kv.setInt("best_of", best_of);
  kv.setFloat("temperature", temperature);
  kv.setFloat("temperature_inc", temperature_inc);
  kv.setFloat("length_penalty", length_penalty);
  kv.setFloat("entropy_thold", entropy_thold);
  kv.setFloat("logprob_thold", logprob_thold);
  kv.setFloat("no_speech_thold", no_speech_thold);
  kv.setInt("n_max_text_ctx", n_max_text_ctx);
  kv.setInt("max_len", max_len);
  kv.setInt("max_tokens", max_tokens);
  kv.setInt("audio_ctx", audio_ctx);
  kv.setString("initial_prompt", initial_prompt);
  kv.setBool("carry_initial_prompt", carry_initial_prompt);
  kv.setBool("suppress_blank", suppress_blank);
  kv.setBool("suppress_nst", suppress_nst);
  kv.setBool("single_segment", single_segment);
  kv.setBool("split_on_word", split_on_word);
  kv.setBool("tdrz_enable", tdrz_enable);
  kv.setBool("print_progress", print_progress);
  kv.setBool("print_realtime", print_realtime);
  kv.save();
}

void buildParams(const WhisperSettings& s, whisper_full_params& out) {
  const auto strategy = s.use_beam_search ? WHISPER_SAMPLING_BEAM_SEARCH
                                          : WHISPER_SAMPLING_GREEDY;
  out = whisper_full_default_params(strategy);

  out.language        = s.language.empty() ? "auto" : s.language.c_str();
  out.translate       = s.translate;
  out.detect_language = s.detect_language;
  out.n_threads       = s.n_threads > 0
                        ? s.n_threads
                        : (int)std::min(8u, std::max(1u, std::thread::hardware_concurrency()));

  out.print_realtime   = s.print_realtime;
  out.print_progress   = s.print_progress;
  out.print_special    = false;
  out.print_timestamps = false;

  out.suppress_blank = s.suppress_blank;
  out.suppress_nst   = s.suppress_nst;
  out.single_segment = s.single_segment;
  out.split_on_word  = s.split_on_word;

  out.temperature     = s.temperature;
  out.temperature_inc = s.temperature_inc;
  out.length_penalty  = s.length_penalty;
  out.entropy_thold   = s.entropy_thold;
  out.logprob_thold   = s.logprob_thold;
  out.no_speech_thold = s.no_speech_thold;

  if (s.n_max_text_ctx > 0) out.n_max_text_ctx = s.n_max_text_ctx;
  if (s.max_len        > 0) out.max_len        = s.max_len;
  if (s.max_tokens     > 0) out.max_tokens     = s.max_tokens;
  if (s.audio_ctx      > 0) out.audio_ctx      = s.audio_ctx;

  out.initial_prompt       = s.initial_prompt.empty() ? nullptr : s.initial_prompt.c_str();
  out.carry_initial_prompt = s.carry_initial_prompt;
  out.tdrz_enable          = s.tdrz_enable;

  if (s.use_beam_search) out.beam_search.beam_size = s.beam_size;
  out.greedy.best_of = s.best_of;
}

const std::vector<LangOption>& languageOptions() {
  static const std::vector<LangOption> table = [] {
    std::vector<LangOption> v;
    v.push_back({"auto", "Auto-detect"});
    int n = whisper_lang_max_id();
    for (int i = 0; i <= n; i++) {
      const char* code = whisper_lang_str(i);
      const char* full = whisper_lang_str_full(i);
      if (!code) continue;
      std::string label = full ? full : code;
      v.push_back({code, label});
    }
    return v;
  }();
  return table;
}

const std::vector<std::string>& languageLabels() {
  static const std::vector<std::string> labels = [] {
    std::vector<std::string> v;
    const auto& opts = languageOptions();
    v.reserve(opts.size());
    for (const auto& o : opts) v.push_back(o.label);
    return v;
  }();
  return labels;
}
