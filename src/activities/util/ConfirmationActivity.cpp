#include "ConfirmationActivity.h"

#include <I18n.h>

#include "HalDisplay.h"
#include "components/UITheme.h"

ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& heading, const std::string& body)
    : Activity("Confirmation", renderer, mappedInput), heading(heading), body(body) {}

void ConfirmationActivity::onEnter() {
  Activity::onEnter();

  lineHeight = renderer.getLineHeight(fontId);
  const int maxWidth = renderer.getScreenWidth() - (margin * 2);
  safeHeading.clear();
  bodyLines.clear();
  bodyLines.reserve(MAX_BODY_LINES);

  if (!heading.empty()) {
    safeHeading = renderer.truncatedText(fontId, heading.c_str(), maxWidth, EpdFontFamily::BOLD);
  }
  // Explanatory confirmation text must remain readable instead of losing its
  // consequence at the edge of a single truncated line. Newlines delimit
  // paragraphs, while the bounded line count keeps the button area clear.
  if (!body.empty()) {
    size_t start = 0;
    while (start <= body.size() && static_cast<int>(bodyLines.size()) < MAX_BODY_LINES) {
      const size_t newline = body.find('\n', start);
      const std::string paragraph =
          body.substr(start, newline == std::string::npos ? std::string::npos : newline - start);
      if (paragraph.empty()) {
        bodyLines.emplace_back();
      } else {
        const int remaining = MAX_BODY_LINES - static_cast<int>(bodyLines.size());
        auto wrapped = renderer.wrappedText(fontId, paragraph.c_str(), maxWidth, remaining, EpdFontFamily::REGULAR);
        for (auto& line : wrapped) {
          bodyLines.push_back(std::move(line));
        }
      }
      if (newline == std::string::npos) break;
      start = newline + 1;
    }
  }

  int totalHeight = 0;
  if (!safeHeading.empty()) totalHeight += lineHeight;
  totalHeight += static_cast<int>(bodyLines.size()) * lineHeight;
  if (!safeHeading.empty() && !bodyLines.empty()) totalHeight += spacing;

  // Text sits in the upper part of the screen so the confirmation popup
  // (centered) doesn't cover it. Multi-line bodies are pulled further up so the
  // whole block still ends above the popup.
  const int screenHeight = renderer.getScreenHeight();
  startY = screenHeight / 6;
  if (startY + totalHeight > screenHeight / 2 - spacing) {
    startY = screenHeight / 2 - spacing - totalHeight;
  }
  if (startY < margin) startY = margin;

  const char* options[] = {I18N.get(StrId::STR_CANCEL), I18N.get(StrId::STR_CONFIRM)};
  confirmPopup.show(safeHeading.c_str(), options, 2, 0, [this](int idx) {
    ActivityResult res;
    res.isCancelled = (idx != 1);
    setResult(std::move(res));
    finish();
  });

  requestUpdate(true);
}

void ConfirmationActivity::render(RenderLock&& lock) {
  renderer.clearScreen();

  int currentY = startY;
  LOG_DBG("CONF", "currentY: %d", currentY);
  // Draw Heading
  if (!safeHeading.empty()) {
    renderer.drawCenteredText(fontId, currentY, safeHeading.c_str(), true, EpdFontFamily::BOLD);
    currentY += lineHeight + spacing;
  }

  // Draw Body
  for (const auto& line : bodyLines) {
    renderer.drawCenteredText(fontId, currentY, line.c_str(), true, EpdFontFamily::REGULAR);
    currentY += lineHeight;
  }

  if (confirmPopup.processRender(renderer, mappedInput)) return;

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}

void ConfirmationActivity::loop() {
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  // Popup dismissed without a selection (Back button or tap outside): cancel.
  ActivityResult res;
  res.isCancelled = true;
  setResult(std::move(res));
  finish();
}
