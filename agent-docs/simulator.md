# Desktop Simulator

The firmware runs on the desktop through
[crosspoint-simulator](https://github.com/crosspoint-reader/crosspoint-simulator):
a native PlatformIO build that swaps `lib/hal/` for a host HAL and paints the
e-ink framebuffer in an SDL2 window. It is the fastest way to check screens,
touch layouts, list conversions, and web-server routes without a device. It
does not model e-ink refresh, panel controllers, heap limits, deep sleep, or
OTA (firmware updates are stubbed out).

## Setup (once)

1. `brew install sdl2` (Linux: `libsdl2-dev libssl-dev`).
2. `platformio.ini` fetches the simulator from the fork
   `boydj/crosspoint-simulator`, branch `cpr-vcodex`. The upstream simulator
   only knows upstream's HAL; that branch adds the fork's `HalClock` UTC API,
   `HalGPIO::coldBootImpliesPowerButton`, `HalTiltSensor::readGyro`, plus host
   shims for `<Esp.h>`, `<esp_heap_caps.h>`, SdFat `common/FsDateTime.h`,
   `MD5Builder::getBytes`, the `esp_http_client` calls in `lib/KOReaderSync`,
   and the Wi-Fi channel/BSSID overloads. To work on the simulator itself,
   clone it beside this repo and point the env at the checkout from the
   gitignored `platformio.local.ini`:

   ```ini
   [env:simulator]
   lib_deps =
     ${env:simulator.lib_deps}
     simulator=symlink://../crosspoint-simulator
   ```

   Merge `crosspoint-reader/crosspoint-simulator` `main` into that branch when
   pulling firmware updates; keep the fork's additions small and additive.
3. Put EPUBs in `fs_/books/` (gitignored). That directory is the simulated SD
   card root; `fs_/.crosspoint/` holds the simulated caches and settings.

## Build and run

```bash
pio run -e simulator_x4_pro -t run_simulator   # X4 Pro: touch, Home key, frontlight
pio run -e simulator -t run_simulator          # X4 (buttons only)
pio run -e simulator_x3 -t run_simulator       # X3 landscape, tilt sensor
```

Keys: arrows = side/front buttons, Return = Confirm, Escape = Back, P = Power,
S = sleep, H = Home key (X4 Pro), mouse = tap and swipe on touch profiles.

Simulator builds report the version `<base>.sim-<sha>` and do not touch the
release or dev counters (`scripts/git_branch.py`).

## Scripted checks and screenshots

The binary accepts a timed input script and screenshot schedule, which makes
screen reviews repeatable and needs no desktop automation permissions:

```bash
mkdir -p qa-artifacts
CROSSPOINT_SIM_INPUT_SCRIPT='2000:TAP:240,530;3200:SWIPE:400,10,400,300,300;4400:HOME:100;5400:QUIT' \
CROSSPOINT_SIM_SCREENSHOTS='2700:qa-artifacts/apps.bmp;3900:qa-artifacts/frontlight.bmp' \
  .pio/build/simulator_x4_pro/program
```

Actions: `BACK`, `ENTER`, `LEFT`, `RIGHT`, `UP`, `DOWN`, `POWER`, `SLEEP`,
`HOME`, `QUIT`, `TAP:x,y[,hold]`, `SWIPE:x1,y1,x2,y2[,ms]`. Coordinates are
logical display pixels. Screenshots are BMP at the host's drawable resolution
(2x on Retina); convert with `sips -s format png`. `CROSSPOINT_SIM_FREE_HEAP`
and `CROSSPOINT_SIM_MAX_ALLOC_HEAP` fake low-memory conditions. The web server
binds `http://127.0.0.1:8080/` (`CROSSPOINT_SIM_HTTP_PORT` to move it).

## When the simulator stops compiling

- A missing `Hal*` method or a fork-only ESP-IDF include belongs in the fork's
  simulator branch (see the simulator's `FORKING.md`); keep stubs one-line and
  additive so merging upstream simulator changes stays trivial.
- A host-portability problem in shared code (for example libc++ needing a
  complete type where GCC did not) is fixed in this repo.
- Do not edit `.pio/libdeps/*/simulator`; PlatformIO discards it.
