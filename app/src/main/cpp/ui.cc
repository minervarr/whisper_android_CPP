#include "ui.hh"

#include <string>
#include <vector>

#include "canvas.hh"
#include "font.hh"
#include "renderer.hh"

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

  // Text panel spans from below the status bar to above the record button.
  float textTop = c.top() + statusH + margin;
  float textBot = btnRect.y - margin;

  // A compact toolbar sits at the top of the text panel: [COPY] [CLEAR],
  // right-aligned. The scrollable text area fills the rest below it.
  float toolbarH = statusH * 0.72f;
  float tbBtnW   = c.w() * 0.24f;
  float tbBtnH   = toolbarH * 0.82f;
  float tbBtnY   = textTop + (toolbarH - tbBtnH) * 0.5f;
  clearBtnRect = {c.right() - margin - tbBtnW, tbBtnY, tbBtnW, tbBtnH};
  copyBtnRect  = {clearBtnRect.x - tbBtnW - c.pad(), tbBtnY, tbBtnW, tbBtnH};

  float taTop = textTop + toolbarH;
  textArea_.setRect(c.left() + margin, taTop, c.w() - margin * 2.0f, textBot - taTop);
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
  if (loadBtnRect.contains(px, py)) { onLoadButton(); return; }
  if (textToolsActive(state) && copyBtnRect.contains(px, py)) {
    if (!transcription.empty()) { copyRequested_ = true; dirty = true; }
    return;
  }
  if (textToolsActive(state) && clearBtnRect.contains(px, py)) {
    if (!transcription.empty()) {
      transcription.clear();
      textArea_.setText("");
      scrollOffset_ = 0.0f;
      dirty = true; geomDirty = true;
    }
    return;
  }
  if (btnRect.contains(px, py)) onRecordButton();
}

void Ui::onDragStart(float px, float py) {
  dragInText_ = textArea_.contains(px, py) && maxScroll_ > 0.5f;
  lastTouchY_ = py;
}

void Ui::onDragMove(float px, float py) {
  if (!dragInText_) return;
  // Finger up (py decreases) => reveal later text => scroll down. This only
  // moves the GPU scroll offset; the glyph geometry is untouched (no rebuild).
  scrollOffset_ += (lastTouchY_ - py);
  if (scrollOffset_ < 0.0f) scrollOffset_ = 0.0f;
  if (scrollOffset_ > maxScroll_) scrollOffset_ = maxScroll_;
  lastTouchY_ = py;
  dirty = true;   // redraw only — no geometry rebuild
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
  //   [ model name zone | status zone | LOAD MODEL button ]
  float loadBtnRight = loadBtnRect.x - c.pad();
  float modelLeft    = c.left() + margin + c.pad();
  float barW         = loadBtnRight - modelLeft;     // shared by name + status
  float modelZoneW   = barW * 0.55f;                 // left: model name
  float modelZoneRight = modelLeft + modelZoneW;
  float statusLeft   = modelZoneRight + c.pad();      // middle: status
  float statusRight  = loadBtnRight;
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

  c.clearClip();

  // ── Transcription / text area ─────────────────────────────────────────────
  bool isError = (state == AppState::Error);
  const std::string& txt = isError ? errorMsg : transcription;

  // Toolbar: COPY / CLEAR, enabled only when there is transcription text.
  if (textToolsActive(state)) {
    bool hasText = !transcription.empty();
    Color tbBg = hasText ? col::btnIdle : col::panel;
    Color tbFg = hasText ? col::text    : col::dim;
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
