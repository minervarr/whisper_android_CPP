#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "canvas.hh"  // Rect

struct Font;

enum class AppState { LoadingModel, NeedPermission, Idle, Recording, Processing, Error };

class Ui {
 public:
  void init(uint32_t screenW, uint32_t screenH);
  // System-bar insets in pixels (status bar, navigation/gesture bar, notches).
  void setInsets(uint32_t top, uint32_t bottom, uint32_t left, uint32_t right);
  void onTouch(float px, float py);
  // Pass a loaded Font to use OTF rendering; nullptr falls back to stroke glyphs.
  void rebuildCurves(std::vector<float>& out, const Font* font = nullptr) const;

  bool dirty = true;

  // State written by main thread
  AppState    state       = AppState::LoadingModel;
  std::string modelName;          // shown in status bar
  std::string transcription;      // latest result (full text)
  std::string errorMsg;           // shown in error state

  // Called from main thread when record button is pressed/released
  // Returns true if the app should start recording, false to stop.
  bool onRecordButton();

 private:
  uint32_t screenW = 0;
  uint32_t screenH = 0;
  uint32_t insetTop = 0, insetBottom = 0, insetLeft = 0, insetRight = 0;

  // Record button rect (content-area coords), recomputed on init/insets change.
  Rect btnRect;

  void relayout();  // recompute btnRect from screen size + insets
};
