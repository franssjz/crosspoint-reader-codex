# Firmware Constraints

Read this before touching firmware code, render paths, EPUB parsing, storage, or
input handling.

## Hardware Budget

Two firmware targets share this source tree. Shared code must fit the C3.

- Xteink X3/X4 (`default`, `gh_release`, `gh_release_rc`, `slim`): ESP32-C3,
  single-core RISC-V at 160 MHz, about 380 KB usable RAM, no PSRAM, 16 MB
  flash with a 0x640000-byte OTA app slot (the release image sits above 95%
  of it; check `scripts/firmware_budget_report.py` before adding flash).
- Xteink X4 Pro (`x4pro`, `x4pro-gh_release`, `x4pro-gh_release_rc`):
  ESP32-S3, dual-core Xtensa LX7, 8 MB PSRAM, 16 MB flash with 0x7E0000-byte
  stock OTA slots, GT911 touch with a capacitive Home key, warm/cool
  frontlight, native SDMMC card slot, native USB (Serial/JTAG plus USB Drive).
- Display: 800x480 monochrome e-ink on both (SSD1677 or UC8179/UC8279
  controllers, detected at boot by the SDK).
- Framebuffer: 48,000 bytes. The project uses single-buffer mode.
- Storage: SD card plus `.crosspoint/` caches.

## Dual-Core (ESP32-S3) Rules

- Never pass `nullptr` to `taskENTER_CRITICAL`; use a real `portMUX_TYPE`
  spinlock (see `ActivityManager`).
- The render task is pinned with `xTaskCreatePinnedToCore`; do not assume the
  loop and render tasks time-slice on one core.
- Touch input arrives through `MappedInputManager` (taps, swipes, Home key);
  never read the SDK `InputManager` or GPIO directly from activities.
- PSRAM is available on the S3 but shared code must not depend on it.

## Memory Rules

- Keep large buffers off the stack. Treat local arrays above 256 bytes as risky.
- Allocate large buffers once per activity when possible, then release in
  `onExit()`.
- Always check `malloc`/`new` results and log before returning failure.
- Avoid repeated `new`/`delete`, `std::vector` growth, and temporary
  `std::string` construction in hot paths.
- If a vector is necessary, call `.reserve()` before push loops.
- Prefer `static constexpr` for constants and lookup tables.
- Large static data should remain in flash, not DRAM.

## C++ And Platform Pitfalls

- Build uses C++20-ish flags through PlatformIO, with exceptions disabled.
- Do not use exceptions or RTTI-based designs.
- `std::string_view` is not null-terminated. Do not pass `.data()` to C APIs
  unless you first copy to a null-terminated buffer.
- ESP32-C3 can fault on unaligned multi-byte loads. Use `memcpy` from raw byte
  buffers instead of pointer casts.
- `IRAM_ATTR` is required for ISR handlers. Data used by ISR code must be safe
  while flash cache is suspended.
- Do not call task mutex APIs from ISR context. Use the `FromISR` FreeRTOS APIs.

## Rendering And UI

- Never hardcode 800 or 480 for layout. Use renderer screen dimensions and
  oriented viewable bounds.
- Use `MappedInputManager::Button` logical buttons in activities. Raw hardware
  button IDs belong only in mapping code.
- Use UITheme/GUI patterns for layout instead of direct one-off positioning
  unless the surrounding code already does otherwise.
- User-facing strings must go through the translation flow with `tr()`.

## Files To Inspect

- `platformio.ini`: build flags and environments.
- `partitions.csv`: C3 app partition sizes and OTA layout;
  `partitions_x4pro.csv`: the stock X4 Pro table used by the `x4pro*` envs.
- TLS: the `x4pro*` envs build wolfSSL (`FREEINK_NET_WOLFSSL`, TLS 1.3, accepts
  self-signed servers via `setInsecure()`); the C3 envs use the core mbedTLS CA
  bundle through `esp_http_client` (TLS 1.2, public CAs only). Keep
  `src/network/HttpDownloader.cpp` compiling on both paths.
- `src/main.cpp`: activity lifetime and global setup.
- `src/MappedInputManager.cpp`: logical button mapping.
- `lib/hal/`: storage, display, and GPIO wrappers.
- `lib/GfxRenderer/GfxRenderer.cpp`: framebuffer and rendering buffers.
- `src/fontIds.h`: font IDs used by the renderer.
