---
name: cpr-firmware
description: Use when modifying CPR-vCodex firmware code, reader behavior, UI activities, rendering, input handling, storage, HAL, EPUB/TXT/XTC parsing, settings, i18n, or ESP32-C3 resource-sensitive C++.
---

# CPR Firmware

Read `agent-docs/firmware-constraints.md` before firmware changes that affect
runtime behavior, memory, display rendering, input, storage, parsing, settings,
or activity lifecycle.

## Workflow

1. Inspect nearby code with `rg` and follow existing patterns before adding a new
   abstraction.
2. Treat RAM, stack, heap fragmentation, and e-ink refresh cost as primary
   design constraints.
3. Keep user-facing strings in the i18n flow: update translation YAML files, run
   `python -X utf8 scripts/gen_i18n.py lib/I18n/translations lib/I18n/`, and do
   not manually edit generated i18n headers.
4. Keep generated HTML headers, caches, PlatformIO outputs, and personal config
   out of commits.
5. Verify with:

```powershell
python -X utf8 -m platformio run -e default -j 1
```

For screen or input changes, run the desktop simulator before reaching for
hardware (`agent-docs/simulator.md`): `pio run -e simulator_x4_pro -t
run_simulator`, or the scripted screenshot flow for repeatable checks.

Use `gh_release` as an additional build when the change is release-facing or
could affect binary size:

```powershell
python -X utf8 -m platformio run -e gh_release -j 1
```

## Guardrails

- Avoid heap allocation in render loops, input loops, parser hot paths, and
  repeated UI refresh paths.
- Keep stack locals small; use owned heap/static storage for large buffers and
  free deterministic temporary allocations.
- Use `MappedInputManager::Button::*` for logical buttons in activities, not raw
  hardware IDs.
- Use orientation-aware dimensions from renderer APIs; avoid raw `800` and
  `480` unless documenting the physical panel.
- Preserve cache format versioning rules when changing serialized EPUB/cache
  structures.
- Report any behavior that still needs real device verification.
