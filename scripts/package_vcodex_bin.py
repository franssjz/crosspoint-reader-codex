from __future__ import annotations

import json
import os
import re
import shutil
from pathlib import Path

Import("env")

README_PATH = "README.md"
BUILD_VERSION_JSON_PATH = "artifacts/build-version.json"
RELEASE_COUNTER_FILE_TEMPLATE = ".release-counter-{base}.txt"
RELEASE_DRY_RUN_ENV = "VCODEX_RELEASE_DRY_RUN"


def load_build_metadata(project_dir: Path) -> tuple[str, str, int | None]:
    path = project_dir / BUILD_VERSION_JSON_PATH
    if not path.exists():
        return "unknown", "unknown", None

    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return "unknown", "unknown", None

    version = str(data.get("version", "unknown"))
    base_version = str(data.get("baseVersion", infer_base_version(version)))
    build_seq_value = data.get("buildSeq")
    build_seq = int(build_seq_value) if isinstance(build_seq_value, int) else None
    return version, base_version, build_seq


def sanitize_filename(value: str) -> str:
    sanitized = re.sub(r"[^0-9A-Za-z._+-]+", "-", value).strip(".-")
    return sanitized or "unknown"


def counter_token(base_version: str) -> str:
    return re.sub(r"[^0-9A-Za-z]+", "-", base_version).strip("-") or "unknown"


def infer_base_version(version: str) -> str:
    match = re.fullmatch(r"(\d+\.\d+\.\d+)\.\d+(?:\..*)?", version)
    if match:
        return match.group(1)
    match = re.fullmatch(r"(\d+\.\d+\.\d+)(?:-.*)?", version)
    if match:
        return match.group(1)
    return "unknown"


def release_counter_path(project_dir: Path, base_version: str) -> Path:
    return project_dir / "artifacts" / RELEASE_COUNTER_FILE_TEMPLATE.format(base=counter_token(base_version))


def update_readme_release_version(project_dir: Path, artifact_name: str) -> None:
    readme_path = project_dir / README_PATH
    if not readme_path.exists():
        print(f"README release update skipped: missing {readme_path}")
        return

    readme_text = readme_path.read_text(encoding="utf-8")
    release_name = artifact_name[:-4]
    release_url = f"https://github.com/franssjz/cpr-vcodex/releases/tag/{release_name}"
    updated_text, replacements = re.subn(
        r"(\| Current release \(CPR-vCodex\) build \| \[`)([^\]]+)(`\]\()([^)]+)(\) \|)",
        rf"\g<1>{release_name}\g<3>{release_url}\g<5>",
        readme_text,
        count=1,
    )

    if replacements == 0:
        print(f"README release update skipped: marker not found in {readme_path}")
        return

    if updated_text != readme_text:
        readme_path.write_text(updated_text, encoding="utf-8", newline="")
        print(f"Updated README current release to {artifact_name[:-4]}")


def persist_release_counter(project_dir: Path, base_version: str, build_seq: int | None) -> None:
    if build_seq is None:
        print("Release counter update skipped: missing buildSeq in build-version.json")
        return

    counter_path = release_counter_path(project_dir, base_version)
    counter_path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = counter_path.with_suffix(counter_path.suffix + ".tmp")
    temp_path.write_text(f"{build_seq}\n", encoding="utf-8")
    temp_path.replace(counter_path)
    print(f"Advanced release counter to {build_seq} ({counter_path})")


def package_vcodex_bin(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    progname = env.subst("$PROGNAME")
    project_dir = Path(env.subst("$PROJECT_DIR"))

    firmware_path = build_dir / f"{progname}.bin"
    if not firmware_path.exists():
        print(f"vcodex packaging skipped: missing {firmware_path}")
        return

    version, base_version, build_seq = load_build_metadata(project_dir)
    safe_version = sanitize_filename(version)

    output_dir = project_dir / "artifacts"
    output_dir.mkdir(parents=True, exist_ok=True)

    pio_env = env.subst("$PIOENV")
    # Board suffix: the ESP32-S3 X4 Pro envs publish "<tag>-x4pro.bin" next to
    # the C3 "<tag>.bin" so the two binaries never collide in artifacts/ or on
    # the GitHub release. Dev x4pro builds already carry "-x4pro" in their
    # generated version string (scripts/git_branch.py); don't double it.
    board_suffix = ""
    if pio_env.startswith("x4pro") and not safe_version.endswith("-x4pro"):
        board_suffix = "-x4pro"
    artifact_stem = f"{safe_version}-cpr-vcodex{board_suffix}"

    artifact_name = f"{artifact_stem}.bin"
    artifact_path = output_dir / artifact_name
    shutil.copy2(firmware_path, artifact_path)

    metadata = {
        "version": version,
        "safeVersion": safe_version,
        "artifactName": artifact_name,
        "artifactPath": str(artifact_path),
        "firmwareBytes": artifact_path.stat().st_size,
        "sourceBin": str(firmware_path),
        "environment": pio_env,
        "board": "x4pro" if pio_env.startswith("x4pro") else "x4",
    }
    if build_seq is not None:
        metadata["buildSequence"] = build_seq
    metadata_path = output_dir / f"{artifact_stem}.json"
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")

    # Only the C3 release env owns the release counter and the README row; the
    # x4pro release env is built from the same tag and must not advance them.
    if pio_env == "gh_release" and os.environ.get(RELEASE_DRY_RUN_ENV) == "1":
        print(f"Release metadata update skipped: {RELEASE_DRY_RUN_ENV}=1")
    elif pio_env == "gh_release":
        persist_release_counter(project_dir, base_version, build_seq)
        update_readme_release_version(project_dir, artifact_name)

    print(f"Packaged vcodex artifact: {artifact_path}")
    print(f"Wrote vcodex metadata: {metadata_path}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", package_vcodex_bin)
