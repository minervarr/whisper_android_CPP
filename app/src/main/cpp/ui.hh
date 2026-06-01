#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "canvas.hh"  // Rect
#include "textarea.hh"

struct Font;

enum class AppState { LoadingModel, NeedPermission, Idle, Recording, Processing, Error, PickingModel };

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
  bool dragInText() const { return dragInText_; }
  // True when the user just tapped COPY and there is text to copy. The host
  // reads this (and the transcription) to push to the system clipboard, then
  // clears it via takeCopyRequest().
  bool takeCopyRequest();
  // Pin the text view to the bottom (call after appending a new result).
  void scrollTextToBottom() { pinBottom_ = true; dirty = true; geomDirty = true; }

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

  // Scrollable transcription / error text view.
  TextArea textArea_;
  bool  dragInText_   = false;
  float lastTouchY_   = 0.0f;
  bool  copyRequested_ = false;

  // GPU-scroll state (text geometry is static; scroll is a shader offset).
  float scrollOffset_ = 0.0f;   // px scrolled (0 = top)
  float maxScroll_    = 0.0f;
  bool  pinBottom_    = false;
  uint32_t textVertStart_ = 0;  // first transcription glyph vertex in the buffer
  float bandX_ = 0, bandY_ = 0, bandW_ = 0, bandH_ = 0;  // text band scissor (px)

  void relayout();  // recompute btnRect from screen size + insets
};
