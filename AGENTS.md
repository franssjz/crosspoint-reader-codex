# CPR-vCodex Agent Guide

CPR-vCodex is a CrossPoint Reader fork for the Xteink X4 e-reader. Its goal is
stable reading first, then careful improvements such as reading statistics,
browser auto-flash, release checks, and fork/upstream cherry-picks that are worth
the risk.

Use this file as the always-on map. For task workflows, load the matching repo
skill from `.agents/skills/`; for deeper project details, read the linked
`agent-docs/` reference only when it is relevant.

## Skill Router

- `.agents/skills/cpr-firmware`: firmware code, UI activities, rendering,
  input, HAL, parsers, storage, settings, i18n, and constrained C++ changes.
- `.agents/skills/cpr-release`: version bumps, release builds, GitHub releases,
  tags, CI, GitHub Pages, auto-flash, manifests, and release documentation.
- `.agents/skills/cpr-upstream-sync`: syncing with upstream CrossPoint Reader or
  comparing/porting ideas from CrossInk, crosspet, papyrix, and other forks.
- `.agents/skills/cpr-reading-stats`: reading statistics, streaks,
  achievements, manual corrections, import/export, stats screens, and the
  browser stats editor.

## Always-On Rules

- Preserve stability over feature size. The default/`gh_release` builds target
  the ESP32-C3 (Xteink X4/X3), which has about 380 KB usable RAM and no PSRAM;
  the `x4pro*` builds target the ESP32-S3 (Xteink X4 Pro) with 8 MB PSRAM. Code
  shared by both targets must fit the C3 budget.
- The X4 Pro build (`x4pro`, `x4pro-gh_release`, `x4pro-gh_release_rc` envs)
  runs on a dual-core ESP32-S3, so SMP rules apply: never pass NULL spinlocks to
  FreeRTOS critical sections, keep the render task pinned to one core, and do
  not assume single-core ordering between tasks.
- Prefer existing project patterns and nearby code over new abstractions.
- Inspect relevant files with `rg` before changing firmware behavior.
- User-facing device text must use the `tr()` i18n flow. Logs may be plain text.
- Avoid new heap allocation in render loops, input loops, parser hot paths, and
  repeated UI refresh paths.
- Do not edit generated outputs by hand when a generator exists.
- Do not use symlinks for `CLAUDE.md`, `AGENTS.md`, or skill files; Windows
  checkout must remain safe.
- Do not commit, tag, publish releases, or push unless the user explicitly asks.
- For releases, the browser auto-flash firmware must come from the latest
  published GitHub release asset, not from an arbitrary local build.

## Quick Commands

```powershell
python -X utf8 -m platformio run -e default -j 1
python -X utf8 -m platformio run -e gh_release -j 1
python -X utf8 scripts/pre_release_check.py --tag <tag>
python -X utf8 scripts/sync_autoflash_firmware.py --repo franssjz/cpr-vcodex
```

If `pio` is available directly, `pio run -e default` and `pio run -e gh_release`
are equivalent. On Windows, prefer running commands from the repository root.

## MCP And Tools

No project MCP server is required for normal firmware work. Use the GitHub and
browser plugins when they are available. Add MCP only when it supplies a stable
external tool or authoritative live data that the repo itself cannot provide,
and keep tokens/secrets in user-level Codex config, never in this repository.
For the rationale and maintenance policy, read
`agent-docs/codex-agent-setup.md`.

## Compatibility

`CLAUDE.md` exists only as a short compatibility entrypoint for Claude Code.
Keep shared guidance here, task workflows in `.agents/skills/`, and long-lived
reference material in `agent-docs/`.
