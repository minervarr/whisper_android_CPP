#include "ui.hh"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "canvas.hh"
#include "font.hh"
#include "renderer.hh"
#include "widgets.hh"

namespace {

// Vertical split of the safe content area.
constexpr float STATUS_FRAC = 0.10f;  // status bar
constexpr float BTN_FRAC    = 0.22f;  // record button

// Shorten str with a trailing ellipsis until it fits within maxW at size.
std::string elide(const Canvas& c, const std::string& str, float maxW, float size) {
  if (maxW <= 0.0f) return std::string();
  if (c.textWidth(str.c_str(), size) <= maxW) return str;
  std::string s = str;
  while (!s.empty()) {
    s.pop_back();
    std::string trial = s + "...";
    if (c.textWidth(trial.c_str(), size) <= maxW) return trial;
  }
  return std::string();
}

}  // namespace

void Ui::init(uint32_t w, uint32_t h) {
  screenW = w;
  screenH = h;
  relayout();
}

void Ui::setInsets(uint32_t top, uint32_t bottom, uint32_t left, uint32_t right) {
  if (top == insetTop && bottom == insetBottom &&
      left == insetLeft && right == insetRight)
    return;
  insetTop = top; insetBottom = bottom; insetLeft = left; insetRight = right;
  relayout();
  dirty = true; geomDirty = true;
}

void Ui::relayout() {
  std::vector<float> unused;  // Canvas only used here for its geometry helpers
  Canvas c(unused, screenW, screenH, nullptr,
           float(insetTop), float(insetBottom), float(insetLeft), float(insetRight));
  
  float margin = c.w() * 0.04f;
  float btnMarg = margin * 0.5f;

  float btnH     = c.h() * BTN_FRAC - margin * 1.5f;
  float statusH  = c.h() * STATUS_FRAC;
  
  btnRect     = {c.left() + margin, c.bottom() - margin - btnH, c.w() - margin * 2.0f, btnH};
  
  // "LOAD MODEL" button sits in the status bar, right-aligned
  float lbW   = c.w() * 0.32f;
  float lbH   = (statusH - margin) - btnMarg * 2.0f;
  float lbY   = c.top() + margin * 0.5f + btnMarg;
  loadBtnRect = {c.right() - margin - lbW - c.pad(), lbY, lbW, lbH};

  // Settings button: just left of LOAD MODEL, wide enough for its "SET" label.
  float gearW = c.w() * 0.13f;
  gearRect = {loadBtnRect.x - gearW - btnMarg, lbY, gearW, lbH};

  // Text panel spans from below the status bar to above the record button.
  float textTop = c.top() + statusH + margin;
  float textBot = btnRect.y - margin;

  // A compact toolbar sits at the top of the text panel: [COPY] [CLEAR],
  // right-aligned. The scrollable text area fills the rest below it.
  float toolbarH = statusH * 0.72f;
  float tbBtnW   = c.w() * 0.20f;
  float tbBtnH   = toolbarH * 0.82f;
  float tbBtnY   = textTop + (toolbarH - tbBtnH) * 0.5f;
  clearBtnRect = {c.right() - margin - tbBtnW, tbBtnY, tbBtnW, tbBtnH};
  copyBtnRect  = {clearBtnRect.x - tbBtnW - c.pad(), tbBtnY, tbBtnW, tbBtnH};
  histBtnRect  = {copyBtnRect.x  - tbBtnW - c.pad(), tbBtnY, tbBtnW, tbBtnH};

  float taTop = textTop + toolbarH;
  textArea_.setRect(c.left() + margin, taTop, c.w() - margin * 2.0f, textBot - taTop);

  // History panel header buttons (shown over the text area when the panel is
  // open): [CLEAR ALL] ............ [ON/OFF], aligned to the text-area top.
  float hdrH = tbBtnH;
  histClearAllRect = {c.left() + margin, taTop, tbBtnW * 1.2f, hdrH};
  histToggleRect   = {c.right() - margin - tbBtnW, taTop, tbBtnW, hdrH};
}

bool Ui::onLoadButton() {
  if (state == AppState::Idle || state == AppState::Error) {
    state = AppState::PickingModel;
    dirty = true; geomDirty = true;
    return true;
  }
  return false;
}

bool Ui::onRecordButton() {
  if (state == AppState::Idle) {
    state = AppState::Recording;
    wasLoadedFromHistory_ = false;
    dirty = true; geomDirty = true;
    return true;
  }
  if (state == AppState::Recording) {
    state = AppState::Processing;
    dirty = true; geomDirty = true;
    return false;
  }
  return false;
}

// Whether the text toolbar (COPY/CLEAR) is interactive in the current state.
static bool textToolsActive(AppState s) {
  return s == AppState::Idle || s == AppState::Recording ||
         s == AppState::Processing || s == AppState::Error;
}

void Ui::onTouch(float px, float py) {
  if (state == AppState::Settings) { onTouchSettings(px, py); return; }

  if (loadBtnRect.contains(px, py)) { onLoadButton(); return; }

  // Gear opens the Settings screen (available whenever the app is usable).
  if (gearRect.contains(px, py) &&
      (state == AppState::Idle || state == AppState::Error ||
       state == AppState::Recording || state == AppState::Processing)) {
    state = AppState::Settings;
    settingsScroll_ = 0.0f; langOpen_ = false; activeSlider_ = -1;
    dirty = true; geomDirty = true;
    return;
  }

  // History panel interactions take priority while it's open.
  if (showHistory_) {
    if (histBtnRect.contains(px, py)) {          // toolbar HISTORY toggles closed
      showHistory_ = false; dirty = true; geomDirty = true; return;
    }
    if (histToggleRect.contains(px, py)) {
      historyEnabled_ = !historyEnabled_;
      histToggleReq_ = true; dirty = true; geomDirty = true; return;
    }
    if (histClearAllRect.contains(px, py)) {
      historyEntries_.clear();
      histClearAllReq_ = true; dirty = true; geomDirty = true; return;
    }
    for (const auto& row : historyRows_) {
      if (row.first.contains(px, py)) {
        transcription = historyEntries_[(size_t)row.second];
        textArea_.setText(transcription);
        showHistory_ = false;
        wasLoadedFromHistory_ = true;
        scrollOffset_ = 0.0f; pinBottom_ = true;
        persistCurrentReq_ = true;
        dirty = true; geomDirty = true;
        return;
      }
    }
    return;  // swallow taps inside the open panel
  }

  if (textToolsActive(state) && histBtnRect.contains(px, py)) {
    showHistory_ = true; dirty = true; geomDirty = true; return;
  }
  if (textToolsActive(state) && copyBtnRect.contains(px, py)) {
    if (!transcription.empty()) { copyRequested_ = true; dirty = true; }
    return;
  }
  if (textToolsActive(state) && clearBtnRect.contains(px, py)) {
    if (!transcription.empty()) {
      // Archive the current session before clearing (host writes it to disk if
      // archiving is enabled).
      archiveText_ = transcription;
      transcription.clear();
      textArea_.setText("");
      scrollOffset_ = 0.0f;
      persistCurrentReq_ = true;
      if (wasLoadedFromHistory_) {
        showHistory_ = true;
        wasLoadedFromHistory_ = false;
      }
      dirty = true; geomDirty = true;
    }
    return;
  }
  if (btnRect.contains(px, py)) onRecordButton();
}

void Ui::onDragStart(float px, float py) {
  lastTouchY_ = py;
  downTouchX_ = px;
  downTouchY_ = py;
  maxDragDistSq_ = 0.0f;
  if (state == AppState::Settings) {
    // Grab a slider if one is under the finger; otherwise this drag scrolls.
    activeSlider_ = -1;
    if (!langOpen_) {
      for (int i = 0; i < (int)settingHits_.size(); i++) {
        if (settingHits_[i].kind == SettingAction::Slider &&
            settingHits_[i].rect.contains(px, py)) {
          activeSlider_ = i;
          applySettingHit(settingHits_[i], px);   // jump to touched position
          return;
        }
      }
    }
    dragInText_ = false;
    return;
  }
  dragInText_ = textArea_.contains(px, py) && maxScroll_ > 0.5f;
}

void Ui::onDragMove(float px, float py) {
  float dx = px - downTouchX_;
  float dy = py - downTouchY_;
  float distSq = dx * dx + dy * dy;
  if (distSq > maxDragDistSq_) maxDragDistSq_ = distSq;

  if (state == AppState::Settings) {
    if (activeSlider_ >= 0 && activeSlider_ < (int)settingHits_.size()) {
      applySettingHit(settingHits_[activeSlider_], px);  // drag captured slider
    } else {
      // Scroll the form (content scrolls with the finger).
      float* scroll = langOpen_ ? &langScroll_ : &settingsScroll_;
      *scroll -= (py - lastTouchY_);
      if (*scroll < 0.0f) *scroll = 0.0f;
      if (langOpen_) {
        if (*scroll > maxScroll_) *scroll = maxScroll_;
        scrollOffset_ = *scroll;
      } else {
        if (*scroll > settingsMaxScroll_) *scroll = settingsMaxScroll_;
        geomDirty = true;
      }
    }
    lastTouchY_ = py;
    dirty = true;
    return;
  }
  if (!dragInText_) return;
  // Finger up (py decreases) => reveal later text => scroll down. This only
  // moves the GPU scroll offset; the glyph geometry is untouched (no rebuild).
  scrollOffset_ += (lastTouchY_ - py);
  if (scrollOffset_ < 0.0f) scrollOffset_ = 0.0f;
  if (scrollOffset_ > maxScroll_) scrollOffset_ = maxScroll_;
  lastTouchY_ = py;
  dirty = true;   // redraw only — no geometry rebuild
}

void Ui::onDragEnd(float px, float py, bool isUp) {
  activeSlider_ = -1;

  // Handle tap-vs-drag for language list selection on touch-up
  if (state == AppState::Settings && langOpen_ && isUp) {
    if (langJustOpened_) {
      langJustOpened_ = false;
    } else if (maxDragDistSq_ < 400.0f) { // ~20px drag threshold
      if (downTouchY_ >= bandY_ && downTouchY_ <= bandY_ + bandH_ &&
          downTouchX_ >= bandX_ && downTouchX_ <= bandX_ + bandW_) {
        float contentY = downTouchY_ - bandY_ + langScroll_;
        float rowH = screenH * 0.07f;
        int idx = (int)(contentY / rowH);
        const auto& opts = languageOptions();
        if (idx >= 0 && idx < (int)opts.size()) {
          settings_->language = opts[(size_t)idx].code;
          settingsSaveReq_ = true;
        }
      }
      langOpen_ = false; 
      dirty = true; geomDirty = true;
    }
  }
}

bool Ui::takeCopyRequest() {
  bool r = copyRequested_;
  copyRequested_ = false;
  return r;
}

void Ui::rebuildCurves(std::vector<float>& out, const Font* font,
                       const MsdfFont* msdf, std::vector<float>* quadsOut) {
  out.clear();
  out.reserve(800 * Renderer::CURVE_FLOATS);
  if (quadsOut) quadsOut->clear();

  Canvas c(out, screenW, screenH, font,
           float(insetTop), float(insetBottom), float(insetLeft), float(insetRight));
  c.useMsdf(msdf, quadsOut);

  c.clear(col::bg);

  // Settings screen replaces the whole UI (chrome only — no GPU-scrolled text unless langOpen_).
  if (state == AppState::Settings) {
    emitSettings(c, quadsOut);
    if (!langOpen_) {
      textVertStart_ = quadsOut ? (uint32_t)(quadsOut->size() / Renderer::MSDF_VERT_FLOATS) : 0;
      maxScroll_ = 0.0f;
    }
    return;
  }

  // ── Status bar (Floating Pill Design) ──────────────────────────────────────────────
  float margin = c.w() * 0.04f;
  float statusH = c.h() * STATUS_FRAC;
  float floatingStatusH = statusH - margin;
  float statusY = c.top() + margin * 0.5f;
  
  c.rect(c.left() + margin, statusY, c.w() - margin * 2.0f, floatingStatusH, col::panel, floatingStatusH * 0.5f);

  const char* stateLabel = "LOADING";
  Color labelColor = col::dim;
  switch (state) {
    case AppState::NeedPermission: stateLabel = "NEED MIC PERMISSION"; break;
    case AppState::Idle:         stateLabel = "IDLE";         labelColor = col::green;  break;
    case AppState::Recording:    stateLabel = "RECORDING";    labelColor = col::red;    break;
    case AppState::Processing:   stateLabel = "PROCESSING";   labelColor = col::yellow; break;
    case AppState::Error:        stateLabel = "ERROR";        labelColor = col::red;    break;
    case AppState::PickingModel: stateLabel = "PICKING...";   labelColor = col::yellow; break;
    default: break;
  }

  // Confine everything in the status bar so it can never bleed downward.
  c.setClip(c.left() + margin, statusY, c.w() - margin * 2.0f, floatingStatusH);

  // Static layout zones (independent of string length so the status label
  // never pushes the model name around when the state changes):
  //   [ model name zone | status zone | SET | LOAD MODEL button ]
  // The shared bar ends at the gear's left edge so the status label can never
  // overlap the SET button.
  float barRight     = gearRect.x - c.pad();
  float modelLeft    = c.left() + margin + c.pad();
  float barW         = barRight - modelLeft;         // shared by name + status
  float modelZoneW   = barW * 0.55f;                 // left: model name
  float modelZoneRight = modelLeft + modelZoneW;
  float statusLeft   = modelZoneRight + c.pad();      // middle: status
  float statusRight  = barRight;
  float statusZoneW  = statusRight - statusLeft;
  float statusCenter = (statusLeft + statusRight) * 0.5f;

  // Status label: centered in its fixed zone, shrunk only if it overflows
  // that zone. It expands symmetrically about statusCenter for every state.
  float statSize  = floatingStatusH * 0.38f;
  float statFloor = floatingStatusH * 0.20f;
  while (statSize > statFloor &&
         c.textWidth(stateLabel, statSize) > statusZoneW)
    statSize -= floatingStatusH * 0.02f;
  float statY = statusY + (floatingStatusH - statSize) * 0.5f;
  c.textCentered(stateLabel, statusCenter, statY, statSize, labelColor);

  // Model name: left-aligned in its own fixed zone. Shrink to fit the full
  // name first, then ellipsize only if even the floor size is too wide.
  if (!modelName.empty()) {
    float mnSize  = floatingStatusH * 0.30f;
    float mnFloor = floatingStatusH * 0.16f;
    while (mnSize > mnFloor &&
           c.textWidth(modelName.c_str(), mnSize) > modelZoneW)
      mnSize -= floatingStatusH * 0.02f;
    float mnY = statusY + (floatingStatusH - mnSize) * 0.5f;
    std::string mn = elide(c, modelName, modelZoneW, mnSize);
    if (!mn.empty())
      c.text(mn.c_str(), modelLeft, mnY, mnSize, col::dim);
  }

  // "LOAD MODEL" button (available when idle or error)
  bool canPick = (state == AppState::Idle || state == AppState::Error);
  Color lbBg  = canPick ? col::btnIdle : col::panel;
  Color lbFg  = canPick ? col::text    : col::dim;
  
  float btnMarg = margin * 0.5f;
  float lbY = statusY + btnMarg;
  float lbH = floatingStatusH - btnMarg * 2.0f;
  c.button(loadBtnRect.x, lbY, loadBtnRect.w, lbH,
           "LOAD MODEL", lbBg, lbFg, lbH * 0.5f);

  // Settings button.
  c.button(gearRect.x, lbY, gearRect.w, lbH, "SET", col::btnIdle, col::text, lbH * 0.5f);

  c.clearClip();

  // ── Transcription / text area ─────────────────────────────────────────────
  bool isError = (state == AppState::Error);
  const std::string& txt = isError ? errorMsg : transcription;

  // Toolbar: HISTORY / COPY / CLEAR. COPY+CLEAR are enabled only when there is
  // transcription text; HISTORY is always available.
  if (textToolsActive(state)) {
    bool hasText = !transcription.empty();
    Color tbBg = hasText ? col::btnIdle : col::panel;
    Color tbFg = hasText ? col::text    : col::dim;
    Color hBg  = showHistory_ ? col::btnIdle : col::panel;
    c.button(histBtnRect.x,  histBtnRect.y,  histBtnRect.w,  histBtnRect.h,
             "HISTORY", hBg, col::text, histBtnRect.h * 0.5f);
    c.button(copyBtnRect.x,  copyBtnRect.y,  copyBtnRect.w,  copyBtnRect.h,
             "COPY",  tbBg, tbFg, copyBtnRect.h * 0.5f);
    c.button(clearBtnRect.x, clearBtnRect.y, clearBtnRect.w, clearBtnRect.h,
             "CLEAR", tbBg, tbFg, clearBtnRect.h * 0.5f);
  }

  if (txt.empty() && state == AppState::Idle) {
    // Centered placeholder over the (empty) text area.
    float taTop = copyBtnRect.y + copyBtnRect.h + c.pad();
    float taBot = btnRect.y - c.pad();
    float size  = c.h() * 0.022f;
    c.textCentered("TAP RECORD TO START", c.left() + c.w() * 0.5f,
                   (taTop + taBot) * 0.5f - size * 0.5f, size, col::dim);
  }

  // ── Record button ───────────────────────────────────────────────────────
  Color btnBg = col::btnIdle;
  const char* btnLabel = "RECORD";
  switch (state) {
    case AppState::Recording:  btnBg = col::btnRec;  btnLabel = "STOP";       break;
    case AppState::Processing: btnBg = col::btnWait; btnLabel = "PROCESSING"; break;
    case AppState::Error:
    case AppState::Idle:       btnBg = col::btnIdle; btnLabel = "RECORD";      break;
    default:                   btnBg = col::btnWait; btnLabel = "WAIT";        break;
  }
  c.setClip(btnRect.x, btnRect.y, btnRect.w, btnRect.h);
  c.button(btnRect.x, btnRect.y, btnRect.w, btnRect.h, btnLabel, btnBg, col::text, btnRect.h * 0.5f);
  c.clearClip();

  // History panel overlays the text area; when open we don't emit the
  // scrollable transcription at all.
  if (showHistory_) {
    emitHistoryPanel(c);
    textVertStart_ = quadsOut
        ? (uint32_t)(quadsOut->size() / Renderer::MSDF_VERT_FLOATS) : 0;
    maxScroll_ = 0.0f; scrollOffset_ = 0.0f;
    Rect tar = textArea_.rect();
    bandX_ = tar.x; bandY_ = tar.y; bandW_ = tar.w; bandH_ = tar.h;
    return;
  }

  // ── Transcription glyphs (emitted last → contiguous, GPU-scrollable range) ──
  textVertStart_ = quadsOut
      ? (uint32_t)(quadsOut->size() / Renderer::MSDF_VERT_FLOATS) : 0;
  textArea_.setColors(isError ? col::red : col::text, col::panel);
  textArea_.setText(txt);
  if (msdf) {
    textArea_.emitStatic(c, font);  // all lines, static; host scrolls on GPU
    maxScroll_ = textArea_.contentHeight() - textArea_.viewHeight();
    if (maxScroll_ < 0.0f) maxScroll_ = 0.0f;
    if (pinBottom_) { scrollOffset_ = maxScroll_; pinBottom_ = false; }
    if (scrollOffset_ > maxScroll_) scrollOffset_ = maxScroll_;
    Rect tar = textArea_.rect();
    bandX_ = tar.x; bandY_ = tar.y; bandW_ = tar.w; bandH_ = tar.h;
  } else {
    textArea_.render(c, font);  // CPU fallback (no MSDF atlas)
  }
}

// Draws the History overlay (opaque panel + header controls + entry rows) and
// records hit-test rects into historyRows_ for onTouch. Newest entries first.
void Ui::emitHistoryPanel(Canvas& c) {
  Rect tar = textArea_.rect();
  c.rect(tar.x, tar.y, tar.w, tar.h, col::bg, c.pad());

  float hdrH      = histToggleRect.h;
  float titleSize = hdrH * 0.46f;
  c.textCentered("HISTORY", tar.x + tar.w * 0.5f,
                 tar.y + (hdrH - titleSize) * 0.5f, titleSize, col::dim);

  Color tgBg = historyEnabled_ ? col::green : col::btnIdle;
  c.button(histToggleRect.x, histToggleRect.y, histToggleRect.w, histToggleRect.h,
           historyEnabled_ ? "ON" : "OFF", tgBg, col::text, histToggleRect.h * 0.5f);
  c.button(histClearAllRect.x, histClearAllRect.y, histClearAllRect.w, histClearAllRect.h,
           "CLEAR ALL", col::btnIdle, col::text, histClearAllRect.h * 0.5f);

  historyRows_.clear();
  if (historyEntries_.empty()) {
    c.textCentered("NO HISTORY YET", tar.x + tar.w * 0.5f,
                   tar.y + tar.h * 0.5f, hdrH * 0.42f, col::dim);
    return;
  }

  float rowH    = hdrH * 1.25f;
  float rowSize = rowH * 0.40f;
  float gap     = c.pad() * 0.5f;
  float y       = tar.y + hdrH + c.pad();
  for (int idx = (int)historyEntries_.size() - 1; idx >= 0; --idx) {
    if (y + rowH > tar.y + tar.h) break;  // no scroll yet — clip overflow
    Rect r = {tar.x + c.pad(), y, tar.w - c.pad() * 2.0f, rowH};
    c.rect(r.x, r.y, r.w, r.h, col::panel, rowH * 0.18f);

    std::string preview = historyEntries_[(size_t)idx];
    size_t nl = preview.find('\n');
    if (nl != std::string::npos) preview = preview.substr(0, nl);
    preview = elide(c, preview, r.w - c.pad() * 2.0f, rowSize);
    c.text(preview, r.x + c.pad(), r.y + (rowH - rowSize) * 0.5f, rowSize, col::text);

    historyRows_.push_back({r, idx});
    y += rowH + gap;
  }
}

// ── Settings screen ──────────────────────────────────────────────────────────
// Apply the mutation described by a settings hit. Centralised so taps and slider
// drags share one code path; px is only used by sliders.
void Ui::applySettingHit(const SettingHit& h, float px) {
  using A = SettingAction;
  switch (h.kind) {
    case A::Back:  state = AppState::Idle; langOpen_ = false; settingsSaveReq_ = true; break;
    case A::Reset: *settings_ = WhisperSettings{}; settingsSaveReq_ = true; break;
    case A::Tab:   settingsTab_ = h.i0; settingsScroll_ = 0.0f; break;
    case A::Toggle: { bool* b = (bool*)h.p; *b = !*b; settingsSaveReq_ = true; break; }
    case A::Step:  { int* v = (int*)h.p;
                     *v = std::clamp(*v + (int)h.f0, (int)h.f1, (int)h.f2);
                     settingsSaveReq_ = true; break; }
    case A::Seg:   { *(bool*)h.p = (h.i0 != 0); settingsSaveReq_ = true; break; }
    case A::OpenLang: langOpen_ = true; langJustOpened_ = true; langScroll_ = 0.0f; break;
    case A::Slider:{ float* f = (float*)h.p;
                     float t = widgets::sliderValueAt(h.rect, px);
                     *f = h.f0 + t * (h.f1 - h.f0); settingsSaveReq_ = true; break; }
  }
  dirty = true;
  if (state == AppState::Settings && activeSlider_ >= 0) quadsDirty = true;
  else geomDirty = true;
}

void Ui::emitSettings(Canvas& c, std::vector<float>* quadsOut) {
  WhisperSettings& s = *settings_;
  settingHits_.clear();
  if (settingHits_.capacity() < 64) settingHits_.reserve(64);
  using A = SettingAction;
  auto addHit = [&](const Rect& r, A k, void* p = nullptr,
                    float f0 = 0, float f1 = 0, float f2 = 0, int i0 = 0) {
    settingHits_.push_back({r, k, p, f0, f1, f2, i0});
  };

  float margin = c.w() * 0.04f;
  float x = c.left() + margin;
  float w = c.w() - margin * 2.0f;
  float y0 = c.top() + margin * 0.5f;
  float hb = c.h() * 0.06f;
  float gap = hb * 0.25f;

  // Header: [< BACK]   SETTINGS   [RESET]
  Rect backR    = {x, y0, w * 0.22f, hb};
  Rect resetR   = {x + w - w * 0.24f, y0, w * 0.24f, hb};
  c.button(backR.x, backR.y, backR.w, backR.h, "< BACK", col::btnIdle, col::text, hb * 0.4f);
  c.button(resetR.x, resetR.y, resetR.w, resetR.h, "RESET", col::btnIdle, col::dim, hb * 0.4f);
  c.textCentered("SETTINGS", c.left() + c.w() * 0.5f, y0 + hb * 0.28f, hb * 0.42f, col::text);
  addHit(backR,  A::Back);
  addHit(resetR, A::Reset);

  // Tabs: [ Basic | Sample | Tune | Limits | Misc ]
  Rect tabRow = {x, y0 + hb + gap, w, hb};
  widgets::drawSegmented(c, tabRow, {"Basic", "Sample", "Tune", "Limits", "Misc"}, settingsTab_);
  for (int i = 0; i < 5; i++)
    addHit(widgets::segmentRectAt(tabRow, 5, i), A::Tab, nullptr, 0, 0, 0, i);

  float contentTop = y0 + 2 * hb + 2 * gap;
  float contentBot = c.bottom() - margin;
  c.setClip(x, contentTop, w, contentBot - contentTop);

  float rowH = c.h() * 0.072f;
  float rgap = rowH * 0.30f;   // generous spacing so rows never visually collide
  float yCur = contentTop - settingsScroll_;
  auto row = [&](float hMul = 1.0f) { Rect r{x, yCur, w, rowH * hMul}; yCur += rowH * hMul + rgap; return r; };
  auto vis = [&](const Rect& r) { return r.y + r.h > contentTop && r.y < contentBot; };

  auto toggle = [&](const char* label, bool& f) {
    Rect r = row();
    if (!vis(r)) return;
    widgets::drawToggle(c, r, f, label);
    addHit(r, A::Toggle, &f);
  };
  auto stepI = [&](const char* label, int& f, int step, int lo, int hi) {
    Rect r = row();
    if (!vis(r)) return;
    char vb[16]; std::snprintf(vb, sizeof vb, "%d", f);
    widgets::drawStepper(c, r, label, vb);
    auto g = widgets::stepperGeom(r);
    addHit(g.minus, A::Step, &f, (float)-step, (float)lo, (float)hi);
    addHit(g.plus,  A::Step, &f, (float) step, (float)lo, (float)hi);
  };
  auto slider = [&](const char* label, float& f, float lo, float hi) {
    Rect r = row();
    if (!vis(r)) return;
    float t = (hi > lo) ? (f - lo) / (hi - lo) : 0.0f;
    char vb[16]; std::snprintf(vb, sizeof vb, "%.2f", f);
    widgets::drawSlider(c, r, t, label, vb);
    // Only the track grabs the slider; the label/value areas fall through to
    // scrolling. Keep full row height for an easy touch target.
    widgets::SliderGeom sg = widgets::sliderGeom(r, t);
    addHit({sg.bar.x, r.y, sg.bar.w, r.h}, A::Slider, &f, lo, hi);
  };
  auto segBool = [&](const char* label, bool& f, const char* offL, const char* onL) {
    Rect r = row();
    if (!vis(r)) return;
    float ts = r.h * 0.40f;
    c.text(label, r.x, r.y + (r.h - ts) * 0.5f, ts, col::text);
    Rect segR = {r.x + r.w * 0.42f, r.y, r.w * 0.58f, r.h};
    widgets::drawSegmented(c, segR, {offL, onL}, f ? 1 : 0);
    addHit(widgets::segmentRectAt(segR, 2, 0), A::Seg, &f, 0, 0, 0, 0);
    addHit(widgets::segmentRectAt(segR, 2, 1), A::Seg, &f, 0, 0, 0, 1);
  };
  auto group = [&](const char* t) {
    yCur += rgap * 1.2f;          // extra breathing room above each section
    Rect r = row(0.8f);
    if (vis(r)) widgets::drawGroupHeader(c, r, t);
  };

  auto langRow = [&]() {
    Rect r = row();
    if (!vis(r)) return;
    const std::string* lbl = &s.language;
    const auto& opts = languageOptions();
    for (const auto& o : opts) if (o.code == s.language) { lbl = &o.label; break; }
    widgets::drawDropdownField(c, r, "Language", *lbl);
    addHit(r, A::OpenLang);
  };
  auto promptRow = [&]() {
    Rect r = row();
    float ts = r.h * 0.40f;
    c.text("Initial prompt", r.x, r.y + (r.h - ts) * 0.5f, ts, col::text);
    c.textRight(s.initial_prompt.empty() ? "(set later)" : "(set)",
                r.x + r.w, r.y + (r.h - ts) * 0.5f, ts, col::dim);
  };

  // When the language picker is open it covers the content area. The picker
  // panel lives in the coverage layer but the form labels are MSDF (always
  // composited on top), so the only way to hide them is to not emit them.
  if (!langOpen_) {
    if (settingsTab_ == 0) {
      // Basic
      group("Language & translation");
      langRow();
      toggle("Translate to English", s.translate);
      toggle("Force language detection", s.detect_language);
      stepI("Threads", s.n_threads, 1, 1, maxThreads());
    } else if (settingsTab_ == 1) {
      // Sample
      group("Sampling strategy");
      segBool("Strategy", s.use_beam_search, "Greedy", "Beam");
      stepI("beam_size", s.beam_size, 1, 1, 16);
      stepI("best_of", s.best_of, 1, 1, 16);
    } else if (settingsTab_ == 2) {
      // Tune
      group("Decoder tunables");
      slider("temperature", s.temperature, 0.0f, 1.0f);
      slider("temperature_inc", s.temperature_inc, 0.0f, 1.0f);
      slider("length_penalty", s.length_penalty, -1.0f, 2.0f);
      slider("entropy_thold", s.entropy_thold, 0.0f, 5.0f);
      slider("logprob_thold", s.logprob_thold, -5.0f, 0.0f);
      slider("no_speech_thold", s.no_speech_thold, 0.0f, 1.0f);
    } else if (settingsTab_ == 3) {
      // Limits
      group("Token / context limits");
      stepI("n_max_text_ctx", s.n_max_text_ctx, 1024, 0, 16384);
      stepI("max_len", s.max_len, 8, 0, 512);
      stepI("max_tokens", s.max_tokens, 8, 0, 512);
      stepI("audio_ctx", s.audio_ctx, 64, 0, 1500);

      group("Prompt");
      promptRow();
      toggle("Carry initial prompt", s.carry_initial_prompt);
    } else if (settingsTab_ == 4) {
      // Misc
      group("Behaviour");
      toggle("Suppress blank", s.suppress_blank);
      toggle("Suppress non-speech", s.suppress_nst);
      toggle("Single segment", s.single_segment);
      toggle("Split on word", s.split_on_word);
      toggle("TinyDiarize (tdrz)", s.tdrz_enable);

      group("Debug");
      toggle("Print progress", s.print_progress);
      toggle("Print realtime", s.print_realtime);
    }
  }

  c.clearClip();

  float contentPx = (yCur - contentTop + settingsScroll_);
  settingsMaxScroll_ = std::max(0.0f, contentPx - (contentBot - contentTop));
  if (settingsScroll_ > settingsMaxScroll_) settingsScroll_ = settingsMaxScroll_;

  if (langOpen_) {
    Rect area = {x, contentTop, w, contentBot - contentTop};
    c.rect(area.x, area.y, area.w, area.h, col::panel2, c.pad());
    
    const auto& opts = languageOptions();
    int sel = 0;
    for (int i = 0; i < (int)opts.size(); i++)
      if (opts[(size_t)i].code == s.language) { sel = i; break; }
      
    textVertStart_ = quadsOut ? (uint32_t)(quadsOut->size() / Renderer::MSDF_VERT_FLOATS) : 0;
    
    float rowH = c.h() * 0.07f;
    float s_size = rowH * 0.42f;
    
    langRows_.clear();
    for (int i = 0; i < (int)opts.size(); i++) {
      float ry = area.y + i * rowH;
      Rect r = {area.x, ry, area.w, rowH};
      Color tcol = (i == sel) ? col::accent : col::text;
      c.text(opts[(size_t)i].label, r.x + c.pad(), r.y + (rowH - s_size) * 0.5f, s_size, tcol);
      langRows_.push_back({r, i}); // Still add to langRows_ for scroll boundaries
    }
    
    bandX_ = area.x; bandY_ = area.y; bandW_ = area.w; bandH_ = area.h;
    maxScroll_ = std::max(0.0f, (float)opts.size() * rowH - area.h);
    if (langScroll_ > maxScroll_) langScroll_ = maxScroll_;
    scrollOffset_ = langScroll_;
  }
}

// DOWN dispatch for the Settings screen. Always returns true (swallows the tap).
bool Ui::onTouchSettings(float px, float py) {
  if (langOpen_) {
    // Tap selection now handled in onDragEnd on touch-up.
    return true;
  }
  for (const auto& h : settingHits_)
    if (h.rect.contains(px, py)) { applySettingHit(h, px); return true; }
  return true;
}
