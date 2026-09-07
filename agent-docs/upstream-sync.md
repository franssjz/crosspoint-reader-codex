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

## Checks After Sync

Run at minimum:

```bash
python -m py_compile scripts/git_branch.py scripts/sync_autoflash_firmware.py scripts/pre_release_check.py scripts/firmware_budget_report.py
pio run -e default
```

Use `pio run -e gh_release` before any release-facing change.
