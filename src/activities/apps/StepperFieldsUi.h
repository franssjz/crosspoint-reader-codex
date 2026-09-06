#pragma once

#include <GfxRenderer.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/UiAppHost.h"

// Shared FreeInkUI layout for the fork's small "a few stepper fields" editors
// (set date, reading start date, reading-time adjustment). One stepper row per
// field (label, [-] value [+]); the selected field is drawn selected. On touch
// boards a Cancel/OK pair sits at the bottom (button boards keep Back/Confirm
// and their hint chrome). Physical buttons stay in the activity's loop().
struct StepperField {
  const char* label = nullptr;
  const char* value = nullptr;
  // Widest value the field can show, so the controls don't shift while stepping.
  const char* widestValue = nullptr;
};

struct StepperFieldsSpec {
  const StepperField* fields = nullptr;
  int count = 0;
  int selectedField = 0;
  freeink::ui::ActionId fieldAction = 0;      // tap on a row's label: value = field index
  freeink::ui::ActionId decrementAction = 0;  // [-] tapped: value = field index
  freeink::ui::ActionId incrementAction = 0;  // [+] tapped: value = field index
  freeink::ui::ActionId cancelAction = 0;     // touch-only bottom pair
  freeink::ui::ActionId okAction = 0;
};

// Sets the content margin (below the header band, above the button hints),
// lays out the rows and the touch Cancel/OK pair, and leaves the remaining
// body (inset by the theme's side padding) for the caller's hint text.
inline void buildStepperFieldsScreen(UiAppHost::UiScreen& screen, const GfxRenderer& renderer,
                                     const MappedInputManager& mappedInput, const StepperFieldsSpec& spec) {
  namespace fui = freeink::ui;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& theme = screen.theme();
  const int16_t sidePadding = static_cast<int16_t>(metrics.contentSidePadding);
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight),
                                                sidePadding, static_cast<int16_t>(metrics.buttonHintsHeight),
                                                sidePadding});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (mappedInput.hasTouch()) {
    addDialogCancelOk(screen, spec.cancelAction, spec.okAction);
  }

  for (int i = 0; i < spec.count; ++i) {
    const StepperField& field = spec.fields[i];
    fui::StepperRowProps props;
    props.row.label = field.label;
    props.row.action = spec.fieldAction;
    props.row.valueId = static_cast<int16_t>(i);
    props.row.inputMask = fui::InputTouch;
    props.row.state = i == spec.selectedField ? fui::StateSelected : fui::StateNormal;
    props.row.labelText = theme.smallText;
    props.row.labelText.bold = true;
    props.row.valueText = theme.smallText;
    props.row.sidePadding = theme.listSidePadding;
    props.row.radius = theme.listRowRadius;
    props.value = field.value;
    props.widestValue = field.widestValue;
    props.decrement = spec.decrementAction;
    props.decrementValue = static_cast<int16_t>(i);
    props.increment = spec.incrementAction;
    props.incrementValue = static_cast<int16_t>(i);
    props.buttonRadius = theme.controlRadius;
    screen.stepperRow(props);
  }
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  (void)renderer;
}

// Wrapped hint paragraph in the remaining body (measured, reserved, drawn).
inline void addStepperHint(UiAppHost::UiScreen& screen, const char* text, const bool bold = false) {
  namespace fui = freeink::ui;
  const fui::Rect body = screen.body();
  if (!text || body.empty()) return;
  fui::TextStyle style = screen.theme().smallText;
  style.maxLines = 3;
  style.bold = bold;
  const fui::Size size = fui::measureWrappedText(screen.target(), text, style, body.width);
  screen.target().text(screen.takeTop(size.height, screen.theme().spaceXs), text, style);
}
