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

// Word-wrap: break str into lines no wider than maxW at the given size.
std::vector<std::string> wordWrap(const Canvas& c, const std::string& str,
                                  float maxW, float size) {
  std::vector<std::string> lines;
  if (str.empty()) return lines;

  std::string current;
  for (size_t i = 0; i <= str.size(); i++) {
    char ch = (i < str.size()) ? str[i] : '\0';
    if (ch == '\n' || ch == '\0') {
      lines.push_back(current);
      current.clear();
      continue;
    }
    std::string trial = current + ch;
    if (c.textWidth(trial.c_str(), size) > maxW && !current.empty()) {
      size_t sp = current.rfind(' ');
      if (sp != std::string::npos && sp > 0) {
        lines.push_back(current.substr(0, sp));
        current = current.substr(sp + 1);
      } else {
        lines.push_back(current);
        current.clear();
      }
    }
    current += ch;
  }
  return lines;
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
  dirty = true;
}

void Ui::relayout() {
  std::vector<float> unused;  // Canvas only used here for its geometry helpers
  Canvas c(unused, screenW, screenH, nullptr,
           float(insetTop), float(insetBottom), float(insetLeft), float(insetRight));
  float btnH = c.h() * BTN_FRAC;
  btnRect = {c.left(), c.bottom() - btnH, c.w(), btnH};
}

bool Ui::onRecordButton() {
  if (state == AppState::Idle) {
    state = AppState::Recording;
    dirty = true;
    return true;
  }
  if (state == AppState::Recording) {
    state = AppState::Processing;
    dirty = true;
    return false;
  }
  return false;
}

void Ui::onTouch(float px, float py) {
  if (btnRect.contains(px, py)) onRecordButton();
}

void Ui::rebuildCurves(std::vector<float>& out, const Font* font) const {
  out.clear();
  out.reserve(800 * Renderer::CURVE_FLOATS);

  Canvas c(out, screenW, screenH, font,
           float(insetTop), float(insetBottom), float(insetLeft), float(insetRight));

  c.clear(col::bg);

  // ── Status bar ─────────────────────────────────────────────────────────────
  float statusH = c.h() * STATUS_FRAC;
  c.rect(c.left(), c.top(), c.w(), statusH, col::panel);

  const char* stateLabel = "LOADING";
  Color labelColor = col::dim;
  switch (state) {
    case AppState::NeedPermission: stateLabel = "NEED MIC PERMISSION"; break;
    case AppState::Idle:       stateLabel = "IDLE";       labelColor = col::green;  break;
    case AppState::Recording:  stateLabel = "RECORDING";  labelColor = col::red;    break;
    case AppState::Processing: stateLabel = "PROCESSING"; labelColor = col::yellow; break;
    case AppState::Error:      stateLabel = "ERROR";      labelColor = col::red;    break;
    default: break;
  }

  float statSize = statusH * 0.42f;
  float statY    = c.top() + (statusH - statSize) * 0.5f;
  c.textRight(stateLabel, c.right() - c.pad(), statY, statSize, labelColor);

  if (!modelName.empty()) {
    float mnSize = statusH * 0.34f;
    float mnY    = c.top() + (statusH - mnSize) * 0.5f;
    c.text(modelName.c_str(), c.left() + c.pad(), mnY, mnSize, col::dim);
  }

  // ── Transcription / text area ─────────────────────────────────────────────
  float textTop = c.top() + statusH + c.pad();
  float textBot = btnRect.y - c.pad();
  float textH   = textBot - textTop;
  c.rect(c.left(), textTop, c.w(), textH, col::panel);

  const std::string& txt = (state == AppState::Error) ? errorMsg : transcription;
  if (!txt.empty()) {
    float size   = textH * 0.06f;
    float lineH  = size * 1.4f;
    float maxW   = c.w() - 2.0f * c.pad();
    Color tc     = (state == AppState::Error) ? col::red : col::text;

    std::vector<std::string> lines = wordWrap(c, txt, maxW, size);
    int maxLines  = int(textH / lineH) - 1;
    int startLine = int(lines.size()) - maxLines;
    if (startLine < 0) startLine = 0;

    float ly = textTop + c.pad();
    for (int i = startLine; i < int(lines.size()); i++) {
      if (ly + size > textBot - c.pad()) break;
      c.text(lines[i].c_str(), c.left() + c.pad(), ly, size, tc);
      ly += lineH;
    }
  } else if (state == AppState::Idle) {
    float size = textH * 0.05f;
    c.textCentered("TAP RECORD TO START", c.left() + c.w() * 0.5f,
                   textTop + textH * 0.5f - size * 0.5f, size, col::dim);
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
  c.button(btnRect.x, btnRect.y, btnRect.w, btnRect.h, btnLabel, btnBg, col::text);
}
