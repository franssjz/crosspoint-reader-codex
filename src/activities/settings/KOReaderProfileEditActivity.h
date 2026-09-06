#pragma once

#include <string>

#include "KOReaderCredentialStore.h"
#include "activities/UiListActivity.h"

/**
 * Edit screen for a single KOReader sync profile.
 * Shows Name, Username, Password, Server URL, and Document Matching fields.
 * Existing profiles also show "Set as Active" and "Delete" options.
 * Used for both adding new profiles and editing existing ones.
 */
class KOReaderProfileEditActivity final : public UiListActivity {
 public:
  /**
   * @param profileIndex Index into KOReaderCredentialStore's profile list, or -1 for a new profile
   */
  explicit KOReaderProfileEditActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int profileIndex = -1)
      : UiListActivity("KOReaderProfileEdit", renderer, mappedInput), profileIndex(profileIndex) {}

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  // Editable fields: Name, Username, Password, Server URL, Document Matching,
  // Send Metadata, Sync Behavior, Sign Up. Existing profiles also show Set as
  // Active and Delete (BASE_ITEMS + 2).
  static constexpr int BASE_ITEMS = 8;
  static constexpr int MAX_ITEMS = BASE_ITEMS + 2;

  int listCount() const override { return getMenuItemCount(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

  int profileIndex;
  KOReaderProfile editProfile;
  bool isNewProfile = false;
  bool showSaveError = false;

  // Row cache: fixed capacity (the item count is at most MAX_ITEMS). Labels
  // are static tr() strings; the value strings mirror editProfile and are
  // refreshed by syncRows() after every change, never from buildScreen().
  std::string rowValues[MAX_ITEMS];
  freeink::ui::ListItem rowItems[MAX_ITEMS]{};

  int getMenuItemCount() const;
  void syncRows();
  void handleSelection(int selectedIndex);
  bool saveProfile();
};
