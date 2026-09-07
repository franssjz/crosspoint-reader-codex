#include "ConfirmationActivity.h"

#include <I18n.h>

#include "../../components/UITheme.h"
#include "HalDisplay.h"

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

  startY = (renderer.getScreenHeight() - totalHeight) / 2;

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

  // Draw UI Elements
  const auto labels = mappedInput.mapLabels("", "", I18N.get(StrId::STR_CANCEL), I18N.get(StrId::STR_CONFIRM));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}

void ConfirmationActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    ActivityResult res;
    res.isCancelled = false;
    setResult(std::move(res));
    finish();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }
}
