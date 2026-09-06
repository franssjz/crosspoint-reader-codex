#include "components/icons/listIcons.h"

#include <FreeInkUIIcon.h>

#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "components/icons/customListIcons.h"

// Firmware UIIcon -> FreeInkUI bitmap for list rows (SDK-format icons only;
// the legacy drawIcon assets use a different bit layout). Two crisp sizes:
// 24 for single-line rows, 32 for label+subtitle rows.
freeink::ui::BitmapRef listIconFor(const UIIcon icon, const int size) {
  if (size >= 32) {
    switch (icon) {
      case UIIcon::Folder:
        return freeink::ui::bitmapFromIcon(icon_folder_32);
      case UIIcon::Text:
        return freeink::ui::bitmapFromIcon(icon_file_text_32);
      case UIIcon::Image:
        return freeink::ui::bitmapFromIcon(icon_image_32);
      case UIIcon::Book:
        return freeink::ui::bitmapFromIcon(icon_book_32);
      case UIIcon::File:
        return freeink::ui::bitmapFromIcon(icon_file_32);
      case UIIcon::Wifi:
        return freeink::ui::bitmapFromIcon(icon_wifi_32);
      case UIIcon::Library:
        return freeink::ui::bitmapFromIcon(icon_library_32);
      case UIIcon::Hotspot:
        return freeink::ui::bitmapFromIcon(icon_radio_tower_32);
      case UIIcon::Usb:
        return freeink::ui::bitmapFromIcon(icon_usb_32);
      case UIIcon::Bookmark:
        return freeink::ui::bitmapFromIcon(icon_bookmark_32);
      case UIIcon::Trophy:
        return freeink::ui::bitmapFromIcon(icon_trophy_32);
      case UIIcon::Heart:
        return freeink::ui::bitmapFromIcon(icon_heart_32);
      case UIIcon::Recent:
        return freeink::ui::bitmapFromIcon(icon_history_32);
      case UIIcon::Settings:
        return freeink::ui::bitmapFromIcon(icon_settings_2_32);
      case UIIcon::Transfer:
        return freeink::ui::bitmapFromIcon(icon_arrow_right_left_32);
      default:
        return {};
    }
  }
  switch (icon) {
    case UIIcon::Folder:
      return freeink::ui::bitmapFromIcon(icon_folder_24);
    case UIIcon::Text:
      return freeink::ui::bitmapFromIcon(icon_file_text_24);
    case UIIcon::Image:
      return freeink::ui::bitmapFromIcon(icon_image_24);
    case UIIcon::Book:
      return freeink::ui::bitmapFromIcon(icon_book_24);
    case UIIcon::File:
      return freeink::ui::bitmapFromIcon(icon_file_24);
    case UIIcon::Wifi:
      return freeink::ui::bitmapFromIcon(icon_wifi_24);
    case UIIcon::Library:
      return freeink::ui::bitmapFromIcon(icon_library_24);
    case UIIcon::Hotspot:
      return freeink::ui::bitmapFromIcon(icon_radio_tower_24);
    case UIIcon::Usb:
      return freeink::ui::bitmapFromIcon(icon_usb_24);
    case UIIcon::Bookmark:
      return freeink::ui::bitmapFromIcon(icon_bookmark_24);
    case UIIcon::Trophy:
      return freeink::ui::bitmapFromIcon(icon_trophy_24);
    case UIIcon::Heart:
      return freeink::ui::bitmapFromIcon(icon_heart_24);
    default:
      return {};
  }
}
