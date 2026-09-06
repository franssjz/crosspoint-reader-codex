#include "KOReaderProfileEditActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>

#include "KOReaderAuthActivity.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr StrId FIELD_NAMES[] = {
    StrId::STR_PROFILE_NAME,      StrId::STR_USERNAME,      StrId::STR_PASSWORD,      StrId::STR_SYNC_SERVER_URL,
    StrId::STR_DOCUMENT_MATCHING, StrId::STR_SEND_METADATA, StrId::STR_SYNC_BEHAVIOR, StrId::STR_SIGN_UP};
}  // namespace

int KOReaderProfileEditActivity::getMenuItemCount() const {
  return isNewProfile ? BASE_ITEMS : BASE_ITEMS + 2;  // +1 Set as Active, +1 Delete
}

void KOReaderProfileEditActivity::onEnter() {
  UiListActivity::onEnter();

  isNewProfile = (profileIndex < 0);
  showSaveError = false;

  if (!isNewProfile) {
    // Edit flow: copy the selected profile into local editable state.
    // Changes are persisted field-by-field through saveProfile().
    const auto* profile = KOREADER_STORE.getProfile(static_cast<size_t>(profileIndex));
    if (profile) {
      editProfile = *profile;
    } else {
      // Profile was deleted between navigation and entering this screen — treat as new
      isNewProfile = true;
      profileIndex = -1;
    }
  }

  for (int i = 0; i < MAX_ITEMS; ++i) {
    fui::ListItem item;
    if (i < BASE_ITEMS) {
      item.label = I18N.get(FIELD_NAMES[i]);
    } else {
      item.label = i == BASE_ITEMS ? tr(STR_SET_ACTIVE_PROFILE) : tr(STR_DELETE_PROFILE);
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems[i] = item;
  }
  syncRows();
}

// Refresh the value column from editProfile. Assigns into the existing
// strings; the row structure (labels, count) never changes within a visit
// except through isNewProfile, which listCount() reads live.
void KOReaderProfileEditActivity::syncRows() {
  // The render task reads rowValues mid-build; rewrite them under the lock.
  RenderLock lock(*this);
  const bool isActive = !isNewProfile && KOREADER_STORE.getActiveIndex() == profileIndex;
  rowValues[0] = editProfile.name.empty() ? std::string(tr(STR_NOT_SET)) : editProfile.name;
  rowValues[1] = editProfile.username.empty() ? std::string(tr(STR_NOT_SET)) : editProfile.username;
  rowValues[2] = editProfile.password.empty() ? std::string(tr(STR_NOT_SET)) : std::string("******");
  rowValues[3] = editProfile.serverUrl.empty() ? std::string(tr(STR_DEFAULT_VALUE)) : editProfile.serverUrl;
  rowValues[4] = editProfile.matchMethod == DocumentMatchMethod::FILENAME ? std::string(tr(STR_FILENAME))
                                                                          : std::string(tr(STR_BINARY));
  rowValues[5] = editProfile.sendMetadata ? std::string(tr(STR_STATE_ON)) : std::string(tr(STR_STATE_OFF));
  rowValues[6] = editProfile.syncBehavior == KOReaderSyncBehavior::SMART ? std::string(tr(STR_SMART_SYNC))
                                                                         : std::string(tr(STR_ASK_EVERY_TIME));
  rowValues[7] = editProfile.username.empty() || editProfile.password.empty()
                     ? std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]"
                     : std::string();
  rowValues[8] = isActive ? std::string(tr(STR_ACTIVE_PROFILE_TAG)) : std::string();
  rowValues[9].clear();
  for (int i = 0; i < MAX_ITEMS; ++i) {
    rowItems[i].value = rowValues[i].empty() ? nullptr : rowValues[i].c_str();
  }
}

const char* KOReaderProfileEditActivity::headerTitle() const {
  return isNewProfile ? tr(STR_ADD_PROFILE) : tr(STR_EDIT_PROFILE);
}

bool KOReaderProfileEditActivity::saveProfile() {
  bool success = false;

  if (isNewProfile) {
    // Create flow: first save inserts a new profile record into the multi-profile store.
    success = KOREADER_STORE.addProfile(editProfile);
    if (success) {
      // After the first successful save, promote to an existing profile so
      // subsequent field edits update in-place rather than creating duplicates.
      isNewProfile = false;
      profileIndex = static_cast<int>(KOREADER_STORE.getCount()) - 1;
    } else {
      LOG_ERR("KRS", "Failed to add KOReader profile");
    }
  } else {
    // Edit flow: update the same profile entry in-place.
    success = KOREADER_STORE.updateProfile(static_cast<size_t>(profileIndex), editProfile);
    if (!success) {
      LOG_ERR("KRS", "Failed to update KOReader profile at index %d", profileIndex);
    }
  }

  showSaveError = !success;
  if (showSaveError) {
    requestUpdate();
  }

  return success;
}

void KOReaderProfileEditActivity::activateIndex(const int index) {
  // Most rows open a sub-screen or repaint with a new value; a lingering tap
  // flash would gray an unrelated element.
  app.clearTapFlash();
  handleSelection(index);
}

void KOReaderProfileEditActivity::handleSelection(const int selectedIndex) {
  // Each field edit is saved immediately so partially configured profiles
  // survive navigation and power-loss scenarios.
  if (selectedIndex == 0) {
    // Profile Name
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editProfile.name = kb.text;
        saveProfile();
        syncRows();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_PROFILE_NAME),
                                                                   editProfile.name, 63, InputType::Text),
                           handler);
  } else if (selectedIndex == 1) {
    // Username
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editProfile.username = kb.text;
        saveProfile();
        syncRows();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_USERNAME),
                                                                   editProfile.username, 63, InputType::Text),
                           handler);
  } else if (selectedIndex == 2) {
    // Password
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editProfile.password = kb.text;
        saveProfile();
        syncRows();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_KOREADER_PASSWORD),
                                                                   editProfile.password, 63, InputType::Password),
                           handler);
  } else if (selectedIndex == 3) {
    // Sync Server URL - prefill with https:// if empty to save typing
    const std::string prefillUrl = editProfile.serverUrl.empty() ? "https://" : editProfile.serverUrl;
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        editProfile.serverUrl = (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
        saveProfile();
        syncRows();
        requestUpdate();
      }
    };
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SYNC_SERVER_URL),
                                                                   prefillUrl, 127, InputType::Url),
                           handler);
  } else if (selectedIndex == 4) {
    // Document Matching - toggle between Filename and Binary
    editProfile.matchMethod = (editProfile.matchMethod == DocumentMatchMethod::FILENAME)
                                  ? DocumentMatchMethod::BINARY
                                  : DocumentMatchMethod::FILENAME;
    saveProfile();
    syncRows();
    requestUpdate();
  } else if (selectedIndex == 5) {
    editProfile.sendMetadata = !editProfile.sendMetadata;
    saveProfile();
    syncRows();
    requestUpdate();
  } else if (selectedIndex == 6) {
    editProfile.syncBehavior = editProfile.syncBehavior == KOReaderSyncBehavior::SMART
                                   ? KOReaderSyncBehavior::ASK_EVERY_TIME
                                   : KOReaderSyncBehavior::SMART;
    saveProfile();
    syncRows();
    requestUpdate();
  } else if (selectedIndex == 7) {
    if (editProfile.username.empty() || editProfile.password.empty() || !saveProfile()) return;
    syncRows();
    startActivityForResult(
        std::make_unique<KOReaderAuthActivity>(renderer, mappedInput, KOReaderAuthActivity::Mode::SIGN_UP, editProfile),
        [this](const ActivityResult&) { requestUpdate(true); });
  } else if (selectedIndex == 8 && !isNewProfile) {
    // Set as Active — persists as the new default for auto-sync and future syncs.
    if (!KOREADER_STORE.setActiveIndex(static_cast<size_t>(profileIndex))) {
      LOG_ERR("KRS", "Failed to set active KOReader profile at index %d", profileIndex);
      showSaveError = true;
    } else {
      showSaveError = false;
    }
    syncRows();
    requestUpdate();
  } else if (selectedIndex == 9 && !isNewProfile) {
    const std::string profileLabel = editProfile.name.empty() ? editProfile.username : editProfile.name;
    startActivityForResult(
        std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE_PROFILE), profileLabel),
        [this](const ActivityResult& result) {
          if (result.isCancelled) {
            requestUpdate(true);
            return;
          }
          if (!KOREADER_STORE.removeProfile(static_cast<size_t>(profileIndex))) {
            LOG_ERR("KRS", "Failed to remove KOReader profile at index %d", profileIndex);
            showSaveError = true;
            requestUpdate(true);
            return;
          }
          finish();
        });
  }
}

void KOReaderProfileEditActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMarginFromScreen(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                                static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rowItems/rowValues were built by onEnter()/syncRows(); only the live
  // count (a new profile gains two rows after its first save) is read here.
  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(getMenuItemCount());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void KOReaderProfileEditActivity::render(RenderLock&&) {
  // Same skeleton as UiListActivity::render, plus the save-error popup drawn
  // over the list (GUI.drawPopup pushes the frame itself).
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; nav.consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  drawFooter();
  if (showSaveError) {
    GUI.drawPopup(renderer, tr(STR_ERROR_GENERAL_FAILURE));
    return;
  }
  renderer.displayBuffer();
}
