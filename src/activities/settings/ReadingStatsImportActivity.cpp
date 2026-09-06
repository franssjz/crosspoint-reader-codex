#include "ReadingStatsImportActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "util/HeaderDateUtils.h"

namespace fui = freeink::ui;

namespace {
constexpr char READING_STATS_EXPORT_DIR[] = "/exports";
constexpr char READING_STATS_EXPORTED_FILE[] = "stats_exported";
constexpr char READING_STATS_EXPORTED_PATH[] = "/exports/stats_exported";
constexpr char READING_STATS_BACKUP_PREFIX[] = "stats_backup_";

std::string fileNameFromPath(const std::string& path) {
  const size_t pos = path.find_last_of('/');
  return pos == std::string::npos ? path : path.substr(pos + 1);
}

bool isReadingStatsBackupName(const char* name) {
  if (!name || std::strncmp(name, READING_STATS_BACKUP_PREFIX, std::strlen(READING_STATS_BACKUP_PREFIX)) != 0) {
    return false;
  }

  int year = 0;
  unsigned month = 0;
  unsigned day = 0;
  int consumed = 0;
  if (std::sscanf(name, "stats_backup_%4d-%2u-%2u%n", &year, &month, &day, &consumed) != 3 || name[consumed] != '\0') {
    return false;
  }

  return year >= 2024 && month >= 1 && month <= 12 && day >= 1 && day <= 31;
}
}  // namespace

std::vector<std::string> ReadingStatsImportActivity::getImportPaths() {
  std::vector<std::string> paths;
  if (Storage.exists(READING_STATS_EXPORTED_PATH)) {
    paths.emplace_back(READING_STATS_EXPORTED_PATH);
  }

  std::vector<std::string> backupPaths;
  auto dir = Storage.open(READING_STATS_EXPORT_DIR);
  if (dir && dir.isDirectory()) {
    char name[256];
    for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
      if (entry.isDirectory()) {
        entry.close();
        continue;
      }

      entry.getName(name, sizeof(name));
      entry.close();
      if (std::strcmp(name, READING_STATS_EXPORTED_FILE) == 0) {
        continue;
      }
      if (isReadingStatsBackupName(name)) {
        backupPaths.emplace_back(std::string(READING_STATS_EXPORT_DIR) + "/" + name);
      }
    }
  }
  if (dir) {
    dir.close();
  }

  std::sort(backupPaths.begin(), backupPaths.end(), [](const std::string& left, const std::string& right) {
    return fileNameFromPath(left) > fileNameFromPath(right);
  });
  paths.insert(paths.end(), backupPaths.begin(), backupPaths.end());
  return paths;
}

// Derives the row cache from importPaths. Called from onEnter(), never from
// buildScreen().
void ReadingStatsImportActivity::rebuildRowItems() {
  rowNames.clear();
  rowItems.clear();
  rowNames.reserve(importPaths.size());
  rowItems.reserve(importPaths.size());
  for (const auto& path : importPaths) {
    rowNames.push_back(fileNameFromPath(path));
  }
  for (size_t i = 0; i < rowNames.size(); ++i) {
    fui::ListItem item;
    item.label = rowNames[i].c_str();
    item.icon = listIconFor(UIIcon::File);
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void ReadingStatsImportActivity::onEnter() {
  UiListActivity::onEnter();
  importPaths = getImportPaths();
  rebuildRowItems();
}

void ReadingStatsImportActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(importPaths.size())) {
    return;
  }
  app.clearTapFlash();  // the tap leaves this screen
  setResult(ActivityResult{FilePathResult{importPaths[static_cast<size_t>(index)]}});
  finish();
}

void ReadingStatsImportActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void ReadingStatsImportActivity::drawChrome() {
  HeaderDateUtils::drawHeaderWithDate(renderer, tr(STR_IMPORT_READING_STATS), READING_STATS_EXPORT_DIR);
}

void ReadingStatsImportActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the header band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (importPaths.empty()) {
    screen.centeredText(tr(STR_NO_READING_STATS_EXPORT), screen.theme().bodyText);
    return;
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void ReadingStatsImportActivity::drawFooter() {
  const bool empty = importPaths.empty();
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), empty ? "" : tr(STR_SELECT), empty ? "" : tr(STR_DIR_UP),
                                            empty ? "" : tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}
