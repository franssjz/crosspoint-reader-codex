# Build And Release

Read this before changing build scripts, version strings, GitHub Actions,
release packaging, or firmware budget checks.

## Build Environments

CPR-vCodex ships two firmware binaries from one tag. The environments come in
pairs: an ESP32-C3 env for the Xteink X4/X3 (one image, runtime panel
detection) and an ESP32-S3 env for the Xteink X4 Pro.

| Purpose | ESP32-C3 (X4/X3) | ESP32-S3 (X4 Pro) |
|---|---|---|
| Development, serial logging | `default` | `x4pro` |
| Production release | `gh_release` | `x4pro-gh_release` |
| Release candidate | `gh_release_rc` | `x4pro-gh_release_rc` |
| Smaller local profile | `slim` | - |

The C3 envs link against `partitions.csv`, whose OTA app slots are 0x640000
(6,553,600 bytes). That is the real slot on the X4 (the X3 slot is 0x770000).
The `x4pro*` envs link against `partitions_x4pro.csv`, which mirrors the stock
X4 Pro table (0x7E0000 = 8,257,536-byte slots), so PlatformIO reports the real
slot for both families. The X4 Pro envs also enable upstream's wolfSSL TLS 1.3
stack (`-DFREEINK_NET_WOLFSSL=1`, SecureNet, `patch_wolfssl.py`); the C3 envs
stay on the core mbedTLS because wolfSSL does not fit their slot.

Common commands:

```bash
pio run -e default
pio run -e gh_release
pio run -e x4pro
pio run -e x4pro-gh_release
pio run -t clean
pio check
```

On constrained machines, use `-j 1` for lower memory pressure:

```bash
python -X utf8 -m platformio run -e default -j 1
```

## Versioning

- Base version lives in `platformio.ini` under `[crosspoint]`.
- `scripts/git_branch.py` writes version metadata to
  `artifacts/build-version.json` and C++ symbols to
  `src/version.generated.inc`.
- Runtime code should include `src/version.h` and read `CROSSPOINT_VERSION`
  from there. Avoid adding the version back to global `CPPDEFINES`; doing so
  makes every dev build look dirty to PlatformIO.
- Development builds include a `.devN-<sha>` suffix; `x4pro` dev builds also
  carry `-x4pro` in the version string.
- Release builds are tag-driven when `VCODEX_RELEASE_TAG` or `GITHUB_REF_NAME`
  matches `<base>.<release>-cpr-vcodex`. Both release envs derive the same
  version from the tag; only the artifact name differs.
- Local release counters under `artifacts/` are ignored by git. Only
  `gh_release` advances the counter and rewrites the README release row; the
  `x4pro-gh_release` env never does.

## Release Safety

Run pre-release checks before publishing:

```bash
python scripts/pre_release_check.py --tag 1.5.0.25-cpr-vcodex
```

The check builds both `gh_release` and `x4pro-gh_release` as dry runs
(`VCODEX_RELEASE_DRY_RUN=1`), parses each build log's Flash/RAM lines
separately, writes a per-env budget report, and validates:

- tag format and availability, release notes from `CHANGELOG.md`;
- `artifacts/<tag>.bin` + `<tag>.json` with `environment: gh_release`,
  `board: x4`, size <= 6,553,600;
- `artifacts/<tag>-x4pro.bin` + `<tag>-x4pro.json` with
  `environment: x4pro-gh_release`, `board: x4pro`, size <= 8,257,536, with a
  warning above 6,553,600 (such an image installs only on the stock X4 Pro
  partition table, not by OTA into a CrossPoint-style 0x640000-slot table);
- the auto-flash manifest `devices` entries (see `autoflash-pages.md`).

`--env gh_release` (repeatable) restricts the run to one env, for example on a
machine without the ESP32-S3 toolchain cached. Use `--skip-build` only when the
build artifacts already exist and were produced intentionally.

## Packaging Artifacts

Important scripts:

- `scripts/package_vcodex_bin.py`: packages firmware after PlatformIO builds.
  Envs starting with `x4pro` get the `-x4pro` artifact suffix and
  `board: "x4pro"` in their metadata JSON.
- `scripts/firmware_budget_report.py`: reports flash usage and budget. The
  board profile comes from `--board`, else metadata `board`/`environment`, else
  a `-x4pro` tag suffix. Profiles: `x4` (slot 6,553,600) and `x4pro` (slot
  8,257,536, warning above 6,553,600). Default behaviour for the C3 artifact is
  unchanged.
- `scripts/pre_release_check.py`: release gate.
- `bin/build-vcodex.ps1`: Windows helper for local packaging.

Expected release assets (eight per tag):

| ESP32-C3 (X4/X3) | ESP32-S3 (X4 Pro) |
|---|---|
| `<tag>.bin` | `<tag>-x4pro.bin` |
| `<tag>.json` | `<tag>-x4pro.json` |
| `<tag>-firmware-budget.json` | `<tag>-x4pro-firmware-budget.json` |
| `<tag>-firmware-budget.md` | `<tag>-x4pro-firmware-budget.md` |

Per-env budget:

- `gh_release`: flash budget percent applies to the 6,553,600 byte C3 slot.
- `x4pro-gh_release`: flash budget percent applies to the 8,257,536 byte X4
  Pro slot from `partitions_x4pro.csv`; exceeding 6,553,600 only warns (see
  above).

## CI

GitHub Actions:

- `.github/workflows/ci.yml` runs on pushes and PRs: formatting for changed
  C/C++ files, `cppcheck`, unit tests, and a build matrix of `default`
  (`firmware.bin`) and `x4pro` (`firmware-x4pro.bin`).
- `.github/workflows/release.yml` runs on `*-cpr-vcodex` tags with a two-entry
  matrix (`gh_release` with suffix "", `x4pro-gh_release` with suffix
  `-x4pro`). Each entry builds, writes its budget report with
  `firmware_budget_report.py --tag <tag><suffix>`, validates its artifact
  pair, and uploads its four assets; whichever entry lands first creates the
  release, the other uploads into it.
- `.github/workflows/pre_release_check.yml` is the manual dry run of
  `pre_release_check.py` for both envs and uploads both build logs and budget
  reports.
- `.github/workflows/sync_autoflash_firmware.yml` mirrors the published assets
  into `docs/firmware/` (see `autoflash-pages.md`).

Treat a green CI run as a baseline sanity check, not as proof that hardware
behavior is correct.
