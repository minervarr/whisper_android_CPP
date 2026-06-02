#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "canvas.hh"  // Rect
#include "settings.hh"
#include "textarea.hh"
#include "widgets.hh"

struct Font;

enum class AppState { LoadingModel, NeedPermission, Idle, Recording, Processing, Error, PickingModel, Settings };

class Ui {
 public:
  void init(uint32_t screenW, uint32_t screenH);
  // System-bar insets in pixels (status bar, navigation/gesture bar, notches).
  void setInsets(uint32_t top, uint32_t bottom, uint32_t left, uint32_t right);
  void onTouch(float px, float py);
  // Drag within the text area to scroll. dy is the pointer delta (finger up =>
  // negative dy => reveal later text).
  void onDragStart(float px, float py);
  void onDragMove(float px, float py);
  void onDragEnd(float px, float py, bool isUp);
  bool dragInText() const { return dragInText_; }

  // ── Settings interface ─────────────────────────────────────────────────────
  // Host binds the live settings object and reads takeSettingsSave() to persist.
  void bindSettings(WhisperSettings* s) { settings_ = s; }
  bool takeSettingsSave() { bool r = settingsSaveReq_; settingsSaveReq_ = false; return r; }
  // True when the user just tapped COPY and there is text to copy. The host
  // reads this (and the transcription) to push to the system clipboard, then
  // clears it via takeCopyRequest().
  bool takeCopyRequest();
  // Pin the text view to the bottom (call after appending a new result).
  void scrollTextToBottom() { pinBottom_ = true; dirty = true; geomDirty = true; }

  // ── History interface (persistence handled by the host) ────────────────────
  // The host seeds these from the on-disk store and reads back the request
  // flags each frame to perform file I/O (Ui itself does no I/O).
  void setHistory(std::vector<std::string> entries, bool enabled) {
    historyEntries_ = std::move(entries); historyEnabled_ = enabled;
    dirty = true; geomDirty = true;
  }
  bool historyEnabled() const { return historyEnabled_; }
  void setTranscription(const std::string& t) {  // restore on launch
    transcription = t; scrollTextToBottom();
  }
  // Returns non-empty text the host should append to history (set when the user
  // hits CLEAR while archiving is enabled), else empty.
  std::string takeArchiveText() { std::string t = std::move(archiveText_); archiveText_.clear(); return t; }
  bool takeHistoryToggle()   { bool r = histToggleReq_;   histToggleReq_   = false; return r; }
  bool takeHistoryClearAll() { bool r = histClearAllReq_; histClearAllReq_ = false; return r; }
  // True when transcription was changed by a user action and current.txt should
  // be re-persisted.
  bool takePersistCurrent()  { bool r = persistCurrentReq_; persistCurrentReq_ = false; return r; }

  // Pass a loaded Font to use OTF rendering; nullptr falls back to stroke glyphs.
  // When msdf + quadsOut are provided, text is emitted as MSDF glyph quads into
  // quadsOut instead of Bézier curves.
  void rebuildCurves(std::vector<float>& out, const Font* font = nullptr,
                     const MsdfFont* msdf = nullptr,
                     std::vector<float>* quadsOut = nullptr);

  // ── MSDF GPU-scroll interface (read by the host after rebuildCurves) ────────
  // Glyph quads are laid out [chrome | transcription]. Chrome is drawn at a zero
  // offset over the whole screen; transcription is drawn with a scroll offset
  // and clipped to the text band, so scrolling never rebuilds geometry.
  uint32_t chromeVertCount() const { return textVertStart_; }
  float    textScrollPx()    const { return scrollOffset_; }
  void     textBand(int32_t& x, int32_t& y, uint32_t& w, uint32_t& h) const {
    x = (int32_t)bandX_; y = (int32_t)bandY_; w = (uint32_t)bandW_; h = (uint32_t)bandH_;
  }

  bool dirty = true;
  bool geomDirty = true;   // geometry (quads/curves) needs rebuild, not just redraw
  bool quadsDirty = true;  // only MSDF quads need rebuild, bypassing expensive curve compute

  // State written by main thread
  AppState    state       = AppState::LoadingModel;
  std::string modelName;          // shown in status bar
  std::string transcription;      // latest result (full text)
  std::string errorMsg;           // shown in error state

  // Called from main thread when record button is pressed/released
  // Returns true if the app should start recording, false to stop.
  bool onRecordButton();

  // Returns true if the file picker should be launched.
  bool onLoadButton();

 private:
  uint32_t screenW = 0;
  uint32_t screenH = 0;
  uint32_t insetTop = 0, insetBottom = 0, insetLeft = 0, insetRight = 0;

  // Record button rect (content-area coords), recomputed on init/insets change.
  Rect btnRect;
  Rect loadBtnRect;
  Rect copyBtnRect;
  Rect clearBtnRect;
  Rect histBtnRect;        // toolbar: open/close History panel
  Rect histToggleRect;     // panel header: enable/disable archiving
  Rect histClearAllRect;   // panel header: wipe history

  // History panel state.
  bool                     showHistory_    = false;
  bool                     wasLoadedFromHistory_ = false;
  bool                     historyEnabled_ = true;
  std::vector<std::string> historyEntries_;
  // Hit-test rows for the displayed entries (rect + entry index), cached from
  // the last rebuildCurves so onTouch can resolve taps.
  std::vector<std::pair<Rect, int>> historyRows_;

  std::string archiveText_;
  bool histToggleReq_     = false;
  bool histClearAllReq_   = false;
  bool persistCurrentReq_ = false;

  void emitHistoryPanel(Canvas& c);   // draws the overlay; fills historyRows_

  // ── Settings screen ─────────────────────────────────────────────────────────
  Rect gearRect;                      // status-bar button that opens Settings
  WhisperSettings* settings_ = nullptr;
  bool   settingsSaveReq_ = false;
  int    settingsTab_     = 0;        // 0 = Simple, 1 = Advanced
  float  settingsScroll_  = 0.0f;
  float  settingsMaxScroll_ = 0.0f;
  bool   langOpen_        = false;    // language dropdown overlay open
  bool   langJustOpened_  = false;    // flag to ignore immediate tap
  float  langScroll_      = 0.0f;
  int    activeSlider_    = -1;       // index into settingHits_ being dragged

  // A hit region + the mutation it performs, described as plain data (no
  // std::function) so rebuilding the form every interactive frame allocates
  // nothing. `p` points into the bound WhisperSettings; f0/f1/f2/i0 are params.
  enum class SettingAction : uint8_t { Back, Reset, Tab, Toggle, Step, Seg, OpenLang, Slider };
  struct SettingHit {
    Rect rect; SettingAction kind; void* p = nullptr;
    float f0 = 0, f1 = 0, f2 = 0; int i0 = 0;
  };
  std::vector<SettingHit> settingHits_;     // all interactive regions this frame
  std::vector<widgets::ListRow> langRows_;  // language dropdown hit rows

  void emitSettings(Canvas& c, std::vector<float>* quadsOut);       // draws Settings screen; fills settingHits_
  void applySettingHit(const SettingHit& h, float px);
  bool onTouchSettings(float px, float py);  // DOWN dispatch; true if handled

  // Scrollable transcription / error text view.
  TextArea textArea_;
  bool  dragInText_   = false;
  float lastTouchY_   = 0.0f;
  float downTouchX_   = 0.0f;
  float downTouchY_   = 0.0f;
  float maxDragDistSq_= 0.0f;
  bool  copyRequested_ = false;

  // GPU-scroll state (text geometry is static; scroll is a shader offset).
  float scrollOffset_ = 0.0f;   // px scrolled (0 = top)
  float maxScroll_    = 0.0f;
  bool  pinBottom_    = false;
  uint32_t textVertStart_ = 0;  // first transcription glyph vertex in the buffer
  float bandX_ = 0, bandY_ = 0, bandW_ = 0, bandH_ = 0;  // text band scissor (px)

  void relayout();  // recompute btnRect from screen size + insets
};
