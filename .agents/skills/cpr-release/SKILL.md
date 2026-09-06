---
name: cpr-release
description: Use when preparing CPR-vCodex releases, version bumps, GitHub tags, firmware artifacts, release notes, CI checks, GitHub Pages, browser auto-flash, docs/firmware/manifest.json, or docs/flash.html.
---

# CPR Release

Read `agent-docs/build-and-release.md` and `agent-docs/autoflash-pages.md`
before release work.

## Invariants

- Do not commit, tag, push, or publish unless the user explicitly asks.
- One tag publishes two binaries: `<tag>.bin` (ESP32-C3, X4/X3, env
  `gh_release`) and `<tag>-x4pro.bin` (ESP32-S3, X4 Pro, env
  `x4pro-gh_release`). Both must be built and budget-checked before tagging.
- The browser auto-flash flow must point to the latest published GitHub release
  assets, never an arbitrary local build. `docs/firmware/firmware-x4pro.bin`
  is created only by the sync script from a published asset.
- Release documentation, manifests, Pages data, and GitHub release assets must
  agree on the same tag and firmware binaries.
- Keep release commands reproducible from the repository root.

## Workflow

1. Check `git status --short`, current branch, and recent tags before editing.
2. Update version strings, README, changelog, release notes, Pages manifest, and
   any release-facing docs requested by the user.
3. Build both release environments:

```powershell
python -X utf8 -m platformio run -e gh_release -j 1
python -X utf8 -m platformio run -e x4pro-gh_release -j 1
```

4. Run the release checker with the intended tag. It rebuilds both envs as dry
   runs, writes `artifacts/<tag>-firmware-budget.{json,md}` and
   `artifacts/<tag>-x4pro-firmware-budget.{json,md}`, and validates both
   artifact pairs plus the auto-flash manifest:

```powershell
python -X utf8 scripts/pre_release_check.py --tag <tag>
```

   Use `--env gh_release` to restrict to one env only when the other toolchain
   is unavailable locally, and say so in the report.

5. If a GitHub release is created or updated, sync auto-flash metadata from the
   published release (fetches `<tag>.bin` and, when present, `<tag>-x4pro.bin`):

```powershell
python -X utf8 scripts/sync_autoflash_firmware.py --repo franssjz/cpr-vcodex
```

6. Verify the GitHub release carries all eight assets (`<tag>[-x4pro].bin`,
   `.json`, `-firmware-budget.json`, `-firmware-budget.md`), CI status, and
   Pages `flash.html`/manifest after publish (the manifest `devices` object
   should list `x4`, `x3`, and `x4pro`). If Pages is stale, inspect the
   workflow rather than editing generated output by hand.

## Report Back

Include the final tag, both artifact paths, each firmware's size against its
slot (C3 6,553,600; X4 Pro 8,257,536) if measured, checks run, and anything
that still depends on GitHub Actions, Pages propagation, or hardware testing
(the X4 Pro browser flash path has no maintainer hardware validation yet).
