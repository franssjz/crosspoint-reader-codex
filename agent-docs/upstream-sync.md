# Upstream Sync

Read this before syncing with `crosspoint-reader/crosspoint-reader`, comparing
third-party forks, or resolving fork divergence.

## Repository Roles

- `crosspoint-reader-master`: official upstream reference.
- `cpr-vcodex`: this fork, focused on reading consistency and statistics.
- `crosspet` and `papyrix`: third-party forks worth scanning for ideas.
- `freeink-sdk`: hardware SDK submodule/dependency area used by the firmware.

## Sync Strategy

- Prefer selective cherry-picks over blind large merges when upstream changed
  architecture near CPR-vCodex features.
- Keep CPR-vCodex-specific UX and stats behavior unless upstream fixes a real
  bug or improves compatibility.
- Document sync points in `CHANGELOG.md` when the relationship to upstream would
  otherwise be confusing.
- Avoid rewriting history on `master`.

## What To Compare

- Firmware compatibility fixes.
- EPUB/TXT parser bug fixes.
- Memory and flash savings.
- Display/input reliability.
- SDK updates.
- Release workflow changes.

## Conflict Hotspots

- `src/activities/reader/`
- `src/activities/apps/`
- `src/ReadingStatsStore.*`
- `src/util/ReadingStatsAnalytics.*`
- `platformio.ini`
- `scripts/`
- `docs/flash.html` and `docs/assets/site.js`

## 2026-09 Full Merge Of Upstream `develop`

On 2026-09-05 the fork merged upstream `crosspoint-reader/crosspoint-reader`
branch `develop` (head `233f93ff`, previous common commit `63d5094f` from
2026-05-11) instead of cherry-picking. This was a deliberate exception to the
selective-cherry-pick rule above: the Xteink X4 Pro (ESP32-S3, touch, Home key,
frontlight) requires upstream's FreeInkUI touch architecture, and porting it
piecemeal would have re-implemented most of upstream's UI layer.

Decisions taken during that merge (keep them when syncing again):

- Settings, app state, recent books, and Wi-Fi credentials stay on the fork's
  JSON persistence (`JsonSettingsIO`, `SettingsList`). Upstream's
  `PersistableStore` migration was not adopted; upstream's new settings fields
  were added to the fork structs with JSON keys matching upstream's names.
  `screenInverted`, `focusReadingEnabled`, and `moveFinishedToReadFolder` are
  aliases of the fork's `darkMode`, `bionicReading`, and `moveCompletedBooks`.
- The reader font size moved to upstream's point-size model (`fontPointSize`,
  `ReaderFontSizes`); legacy `fontSize` slots are migrated on load.
- Bookmarks and highlights stay on the fork's `BookmarkStore` v5 and
  `BookmarksActivity`; upstream's `EpubReaderBookmarksActivity`/`BookmarkFile`
  are not wired. The fork's StarDict dictionary (`DictionaryStore`) stays;
  upstream's `src/util/Dictionary*` registry is not used.
- Night mode: only the fork's renderer dark mode is applied; upstream's
  per-render panel inversion was removed from `ActivityManager`.
- Build: upstream's `[base]`/`[firmware_tuned]` layout and the `x4pro*`
  environments were adopted (the latter link against the fork's
  `partitions_x4pro.csv`, the stock X4 Pro table, instead of upstream's shared
  `partitions.csv`); the fork keeps
  its generated version (`scripts/git_branch.py`), release scripts, and
  auto-flash flow. `FREEINK_DEVICE_X4/X3` live only in the C3 envs.
- `freeink-sdk` submodule moved to `cb9167d5`.
- Flash budget: upstream's wolfSSL/SecureNet TLS 1.3 stack is built only for
  the `x4pro*` envs (on the C3 the downloader falls back to `esp_http_client`
  on the core mbedTLS CA bundle, so self-signed HTTPS servers are no longer
  accepted there), unused NotoSerif and
  OpenDyslexic font data was removed, and the ten UI languages upstream added
  after the fork's 23 were not adopted (Hebrew stays because the keyboard
  layout table references it). Re-adding any of them needs the C3 release
  image to stay under 0x640000 bytes.
- `HalClock` keeps the fork's UTC API but runs on the SDK `Rtc` driver, so the
  X3 (DS3231) and X4 Pro (BM8563) share one clock path.

Post-merge conflict hotspots are the same as before plus `src/main.cpp`,
`src/CrossPointSettings.*`, `src/SettingsList.*`, the 19 FreeInkUI list screens,
and `src/activities/reader/EpubReaderActivity.cpp`.

## Checks After Sync

Run at minimum:

```bash
python -m py_compile scripts/git_branch.py scripts/sync_autoflash_firmware.py scripts/pre_release_check.py scripts/firmware_budget_report.py
pio run -e default
```

Use `pio run -e gh_release` before any release-facing change.
