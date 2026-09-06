from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from release_notes_from_changelog import render_release_notes


# ESP32-C3 (X4/X3 shared image) OTA app slot from partitions.csv; the C3 envs
# link against it, so PlatformIO reports it as their flash total.
APP_PARTITION_SIZE = 6_553_600
# CrossPoint-style slot size. An X4 Pro image above this can only be installed
# on units that carry the stock partition table (the browser flasher and
# partitions_x4pro.csv), not through OTA into a 0x640000-slot table.
BUILD_PARTITION_SIZE = 6_553_600
# Stock Xteink X4 Pro (ESP32-S3) OTA app slot from partitions_x4pro.csv; the
# x4pro envs link against it.
X4PRO_APP_PARTITION_SIZE = 8_257_536
X3_APP_PARTITION_SIZE = 7_798_784
RAM_RE = re.compile(r"RAM:.*?(\d+)\s+bytes\s+from\s+(\d+)\s+bytes")
FLASH_RE = re.compile(r"Flash:.*?(\d+)\s+bytes\s+from\s+(\d+)\s+bytes")


@dataclass(frozen=True)
class ReleaseTarget:
    env: str
    suffix: str
    board: str
    chip_family: str
    slot_size: int
    warn_size: int | None
    manifest_key: str
    local_firmware: str
    manifest_required: bool

    def stem(self, tag: str) -> str:
        return f"{tag}{self.suffix}"


RELEASE_TARGETS: tuple[ReleaseTarget, ...] = (
    ReleaseTarget(
        env="gh_release",
        suffix="",
        board="x4",
        chip_family="ESP32-C3",
        slot_size=APP_PARTITION_SIZE,
        warn_size=None,
        manifest_key="x4",
        local_firmware="firmware.bin",
        manifest_required=True,
    ),
    ReleaseTarget(
        env="x4pro-gh_release",
        suffix="-x4pro",
        board="x4pro",
        chip_family="ESP32-S3",
        slot_size=X4PRO_APP_PARTITION_SIZE,
        warn_size=BUILD_PARTITION_SIZE,
        manifest_key="x4pro",
        local_firmware="firmware-x4pro.bin",
        manifest_required=False,
    ),
)
TARGETS_BY_ENV = {target.env: target for target in RELEASE_TARGETS}

# Manifest device entries: key -> (target whose image it flashes, slot size).
MANIFEST_DEVICES: dict[str, tuple[ReleaseTarget, int]] = {
    "x4": (TARGETS_BY_ENV["gh_release"], APP_PARTITION_SIZE),
    "x3": (TARGETS_BY_ENV["gh_release"], X3_APP_PARTITION_SIZE),
    "x4pro": (TARGETS_BY_ENV["x4pro-gh_release"], X4PRO_APP_PARTITION_SIZE),
}


def run(cmd: list[str], *, env: dict[str, str] | None = None, check: bool = False) -> subprocess.CompletedProcess[str]:
    merged_env = os.environ.copy()
    if env:
        merged_env.update(env)
    return subprocess.run(
        cmd,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        env=merged_env,
        check=check,
    )


def ok(message: str) -> None:
    print(f"[ok] {message}")


def warn(message: str) -> None:
    print(f"[warn] {message}")


def fail(message: str) -> None:
    raise RuntimeError(message)


def get_base_version(project_dir: Path) -> str:
    config = configparser.ConfigParser()
    config.read(project_dir / "platformio.ini", encoding="utf-8")
    return config.get("crosspoint", "version")


def parse_release_tag(tag: str, base_version: str) -> int:
    match = re.fullmatch(rf"{re.escape(base_version)}\.(\d+)-cpr-vcodex", tag)
    if not match:
        fail(f"Tag must match {base_version}.<release>-cpr-vcodex, got {tag!r}")
    return int(match.group(1))


def require_clean_worktree(allow_dirty: bool) -> None:
    status = run(["git", "status", "--short"], check=True).stdout.strip()
    if status and not allow_dirty:
        fail("Working tree is not clean. Commit or stash changes, or pass --allow-dirty.")
    if status:
        warn("Working tree is dirty; continuing because --allow-dirty was set.")
    else:
        ok("working tree is clean")


def require_tag_available(tag: str, allow_existing_tag: bool) -> None:
    local = run(["git", "rev-parse", "--verify", f"refs/tags/{tag}"])
    remote = run(["git", "ls-remote", "--exit-code", "--tags", "origin", f"refs/tags/{tag}"])
    exists = local.returncode == 0 or remote.returncode == 0
    if exists and not allow_existing_tag:
        fail(f"Tag {tag} already exists. Pass --allow-existing-tag to validate an existing tag.")
    if exists:
        warn(f"Tag {tag} already exists; continuing because --allow-existing-tag was set.")
    else:
        ok(f"tag {tag} is available")


def parse_size(regex: re.Pattern[str], output: str, label: str) -> tuple[int, int]:
    match = regex.search(output)
    if not match:
        fail(f"Could not parse {label} usage from PlatformIO output")
    return int(match.group(1)), int(match.group(2))


def build_release(project_dir: Path, tag: str, jobs: int, target: ReleaseTarget) -> str:
    env = {
        "PYTHONIOENCODING": "utf-8",
        "PYTHONUTF8": "1",
        "VCODEX_RELEASE_DRY_RUN": "1",
        "VCODEX_RELEASE_TAG": tag,
    }
    # Prefer the PlatformIO CLI on PATH (CI, pio penv); fall back to the running
    # interpreter's platformio module. A bare "python" does not exist on macOS.
    pio = shutil.which("pio") or shutil.which("platformio")
    launcher = [pio] if pio else [sys.executable, "-X", "utf8", "-m", "platformio"]
    cmd = [*launcher, "run", "-e", target.env, "-j", str(jobs)]
    print(f"[run] {' '.join(cmd)}")
    result = run(cmd, env=env)
    if result.returncode != 0:
        print(result.stdout)
        fail(f"{target.env} release build failed with exit code {result.returncode}")
    ok(f"{target.env} dry-run build succeeded")
    return result.stdout


def write_budget_report(
    project_dir: Path, tag: str, output: str, flash_budget_percent: float, target: ReleaseTarget
) -> None:
    artifacts_dir = project_dir / "artifacts"
    artifacts_dir.mkdir(exist_ok=True)
    stem = target.stem(tag)
    build_log = artifacts_dir / f"{stem}-build.log"
    build_log.write_text(output, encoding="utf-8", newline="\n")

    cmd = [
        sys.executable,
        str(project_dir / "scripts" / "firmware_budget_report.py"),
        "--tag",
        stem,
        "--build-log",
        str(build_log),
        "--metadata",
        str(artifacts_dir / f"{stem}.json"),
        "--out-json",
        str(artifacts_dir / f"{stem}-firmware-budget.json"),
        "--out-md",
        str(artifacts_dir / f"{stem}-firmware-budget.md"),
        "--board",
        target.board,
        "--flash-budget-percent",
        str(flash_budget_percent),
        "--fail-over-budget",
    ]
    result = run(cmd)
    print(result.stdout, end="")
    if result.returncode != 0:
        fail(f"Firmware budget report generation failed for {target.env}")


def validate_budget(output: str, flash_budget_percent: float, target: ReleaseTarget) -> None:
    flash_used, flash_total = parse_size(FLASH_RE, output, f"{target.env} flash")
    ram_used, ram_total = parse_size(RAM_RE, output, f"{target.env} RAM")
    budget_bytes = int(target.slot_size * flash_budget_percent / 100)

    if flash_total != target.slot_size:
        warn(
            f"{target.env}: PlatformIO app partition size changed: {flash_total} bytes "
            f"(expected {target.slot_size})"
        )
    if flash_used > budget_bytes:
        fail(
            f"{target.env}: flash usage {flash_used} bytes exceeds {flash_budget_percent:.1f}% "
            f"of the {target.chip_family} slot ({budget_bytes} bytes)"
        )
    if target.warn_size is not None and flash_used > target.warn_size:
        warn(
            f"{target.env}: flash usage {flash_used} bytes exceeds the {target.warn_size} byte CrossPoint-style "
            f"slot; it fits the stock {target.slot_size} byte {target.chip_family} slot only"
        )

    ok(
        f"{target.env} flash budget: {flash_used}/{target.slot_size} bytes "
        f"({flash_used / target.slot_size * 100:.1f}% of the {target.chip_family} slot)"
    )
    ok(f"{target.env} RAM usage: {ram_used}/{ram_total} bytes ({ram_used / ram_total * 100:.1f}%)")


def validate_artifacts(
    project_dir: Path, tag: str, release_seq: int, base_version: str, target: ReleaseTarget
) -> None:
    stem = target.stem(tag)
    artifact_path = project_dir / "artifacts" / f"{stem}.bin"
    metadata_path = project_dir / "artifacts" / f"{stem}.json"
    if not artifact_path.exists():
        fail(f"Missing packaged {target.env} firmware artifact: {artifact_path}")
    if not metadata_path.exists():
        fail(f"Missing packaged {target.env} metadata artifact: {metadata_path}")

    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    expected_version = f"{base_version}.{release_seq}"
    checks = {
        "artifactName": f"{stem}.bin",
        "environment": target.env,
        "version": expected_version,
        "buildSequence": release_seq,
    }
    for key, expected in checks.items():
        if metadata.get(key) != expected:
            fail(f"{target.env} metadata {key} mismatch: expected {expected!r}, got {metadata.get(key)!r}")
    if "board" in metadata and metadata.get("board") != target.board:
        fail(f"{target.env} metadata board mismatch: expected {target.board!r}, got {metadata.get('board')!r}")

    firmware_bytes = artifact_path.stat().st_size
    if metadata.get("firmwareBytes") != firmware_bytes:
        fail(f"{target.env} metadata firmwareBytes mismatch: expected {firmware_bytes}, got {metadata.get('firmwareBytes')}")
    if firmware_bytes > target.slot_size:
        fail(
            f"Packaged {target.env} firmware is too large for the {target.chip_family} app partition: "
            f"{firmware_bytes} > {target.slot_size} bytes"
        )
    if target.warn_size is not None and firmware_bytes > target.warn_size:
        warn(
            f"Packaged {target.env} firmware ({firmware_bytes} bytes) exceeds the {target.warn_size} byte "
            "CrossPoint-style slot; it installs only on the stock partition table"
        )

    ok(f"{target.env} release artifacts match tag {tag} ({stem}.bin, {firmware_bytes} bytes)")


def validate_release_notes(project_dir: Path, tag: str) -> None:
    notes = render_release_notes(project_dir / "CHANGELOG.md", tag)
    if "## Changes" not in notes or "- " not in notes:
        fail(f"Generated release notes for {tag} do not contain changelog bullets")
    ok(f"release notes generated from CHANGELOG.md for {tag}")


def validate_reading_stats_const_json_guards(project_dir: Path) -> None:
    source = (project_dir / "src" / "JsonSettingsIO.cpp").read_text(encoding="utf-8")
    function_start = source.find("bool JsonSettingsIO::loadReadingStatsDocument")
    function_end = source.find("bool JsonSettingsIO::loadReadingStats(", function_start)
    if function_start < 0 or function_end < 0:
        fail("Could not locate loadReadingStatsDocument for JSON guard validation")

    loader = source[function_start:function_end]
    mutable_guards = (".is<JsonObject>()", ".is<JsonArray>()")
    found = [guard for guard in mutable_guards if guard in loader]
    if found:
        fail(
            "Reading Stats validates const JSON through mutable ArduinoJson types: " + ", ".join(found)
        )
    if ".is<JsonObjectConst>()" not in loader or ".is<JsonArrayConst>()" not in loader:
        fail("Reading Stats const JSON object/array guards are missing")

    ok("Reading Stats uses const-correct ArduinoJson type guards")


def _sha256_file(path: Path) -> tuple[int, str]:
    data = path.read_bytes()
    return len(data), hashlib.sha256(data).hexdigest()


def validate_autoflash_manifest(project_dir: Path) -> None:
    firmware_dir = project_dir / "docs" / "firmware"
    manifest_path = firmware_dir / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    # Flat top-level fields: the historic ESP32-C3 (X4/X3) contract.
    c3_target = TARGETS_BY_ENV["gh_release"]
    c3_path = firmware_dir / c3_target.local_firmware
    if not c3_path.exists():
        fail(f"Auto-flash firmware copy is missing: {c3_path}")
    c3_size, c3_digest = _sha256_file(c3_path)
    if manifest.get("firmwareUrl") != f"firmware/{c3_target.local_firmware}":
        fail(f"Auto-flash manifest must use local firmware/{c3_target.local_firmware}")
    if manifest.get("size") != c3_size:
        fail("Auto-flash manifest size does not match docs/firmware/firmware.bin")
    if manifest.get("sha256") != c3_digest:
        fail("Auto-flash manifest sha256 does not match docs/firmware/firmware.bin")
    if c3_size > c3_target.slot_size:
        fail(f"docs/firmware/firmware.bin is too large for the {c3_target.chip_family} app partition: {c3_size} bytes")
    if (manifest.get("source") or {}).get("type") != "github-release":
        fail("Auto-flash manifest source.type must be github-release")

    devices = manifest.get("devices")
    if not isinstance(devices, dict):
        fail("Auto-flash manifest must carry a `devices` object keyed x4/x3/x4pro")

    tag = manifest.get("version")
    for key, (target, slot_size) in MANIFEST_DEVICES.items():
        entry = devices.get(key)
        if entry is None:
            if target.manifest_required:
                fail(f"Auto-flash manifest devices.{key} is missing")
            warn(f"Auto-flash manifest has no devices.{key} entry ({target.chip_family} asset not published yet)")
            stale = firmware_dir / target.local_firmware
            if stale.exists():
                fail(f"{stale} exists but manifest devices.{key} is missing; re-run the sync script")
            continue
        for field in ("path", "size", "sha256", "downloadUrl", "chipFamily"):
            if field not in entry:
                fail(f"Auto-flash manifest devices.{key} is missing `{field}`")
        if entry["chipFamily"] != target.chip_family:
            fail(f"Auto-flash manifest devices.{key}.chipFamily must be {target.chip_family}, got {entry['chipFamily']!r}")
        if entry["path"] != f"firmware/{target.local_firmware}":
            fail(f"Auto-flash manifest devices.{key}.path must be firmware/{target.local_firmware}, got {entry['path']!r}")
        local_path = firmware_dir / target.local_firmware
        if not local_path.exists():
            fail(f"Auto-flash manifest devices.{key} points at a missing file: {local_path}")
        size, digest = _sha256_file(local_path)
        if entry["size"] != size:
            fail(f"Auto-flash manifest devices.{key}.size does not match {local_path.name}")
        if entry["sha256"] != digest:
            fail(f"Auto-flash manifest devices.{key}.sha256 does not match {local_path.name}")
        if size > slot_size:
            fail(f"Auto-flash devices.{key} firmware is too large for its {slot_size} byte app slot: {size} bytes")
        download_url = str(entry["downloadUrl"])
        if not download_url.endswith(f"/{tag}/{tag}{target.suffix}.bin"):
            fail(f"Auto-flash manifest devices.{key}.downloadUrl does not point at {tag}{target.suffix}.bin: {download_url}")
        ok(f"auto-flash devices.{key} matches {local_path.name} ({target.chip_family}, {size} bytes)")

    ok(f"auto-flash manifest matches published firmware copy ({tag})")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate CPR-vCodex release readiness before pushing a stable tag.")
    parser.add_argument("--tag", required=True, help="Candidate stable tag, e.g. 1.2.0.39-cpr-vcodex")
    parser.add_argument("--jobs", type=int, default=1, help="PlatformIO build jobs (default: 1)")
    parser.add_argument(
        "--flash-budget-percent",
        type=float,
        default=97.5,
        help="Maximum flash usage as a percent of each board's OTA app slot (default: 97.5)",
    )
    parser.add_argument(
        "--env",
        dest="envs",
        action="append",
        choices=sorted(TARGETS_BY_ENV),
        help="Restrict to one release environment (repeatable). Default: all of "
        + ", ".join(target.env for target in RELEASE_TARGETS),
    )
    parser.add_argument("--skip-build", action="store_true", help="Validate existing artifacts without rebuilding")
    parser.add_argument("--allow-dirty", action="store_true")
    parser.add_argument("--allow-existing-tag", action="store_true")
    args = parser.parse_args()

    targets = [TARGETS_BY_ENV[env] for env in (args.envs or [target.env for target in RELEASE_TARGETS])]

    project_dir = Path.cwd()
    try:
        base_version = get_base_version(project_dir)
        release_seq = parse_release_tag(args.tag, base_version)
        require_clean_worktree(args.allow_dirty)
        require_tag_available(args.tag, args.allow_existing_tag)
        validate_release_notes(project_dir, args.tag)
        validate_reading_stats_const_json_guards(project_dir)

        if args.skip_build:
            warn("Skipping release builds; validating existing artifacts only.")
        else:
            for target in targets:
                output = build_release(project_dir, args.tag, args.jobs, target)
                validate_budget(output, args.flash_budget_percent, target)
                write_budget_report(project_dir, args.tag, output, args.flash_budget_percent, target)

        for target in targets:
            validate_artifacts(project_dir, args.tag, release_seq, base_version, target)
        validate_autoflash_manifest(project_dir)
        ok(f"pre-release checks passed for {args.tag} ({', '.join(target.env for target in targets)})")
        return 0
    except RuntimeError as exc:
        print(f"[error] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
