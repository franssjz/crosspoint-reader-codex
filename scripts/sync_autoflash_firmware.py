from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from urllib.parse import quote
import urllib.request
from pathlib import Path
from typing import Any


DEFAULT_REPO = "franssjz/cpr-vcodex"
# Historic name kept for callers that still import it; it is the C3 slot size.
APP_PARTITION_SIZE = 6_553_600
MIN_FIRMWARE_SIZE = 1_000_000
VERSION_RE = re.compile(r"\b\d+\.\d+\.\d+\.\d+(?:[.-][0-9A-Za-z]+)?-[0-9A-Za-z._-]*cpr-vcodex\b")
FIRMWARE_TAG_RE = re.compile(r"^\d+\.\d+\.\d+\.\d+(?:[.-][0-9A-Za-z]+)?-cpr-vcodex$")
# Release asset URLs: group "suffix" distinguishes the ESP32-S3 X4 Pro asset
# (`<tag>-x4pro.bin`) from the C3 asset (`<tag>.bin`) so each is rewritten to
# its own download URL instead of every URL collapsing onto the C3 one.
DOWNLOAD_URL_RE = re.compile(
    r"https://github\.com/[^/]+/[^/]+/releases/download/[^/]+/(?P<name>[^\"'\s<>/]+?)(?P<suffix>-x4pro)?\.bin"
)
FIRMWARE_VERSION_PREFIX_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)\.(\d+)")
FALLBACK_RELEASE_RE = re.compile(r"(const FALLBACK_RELEASE = \{.*?\n    \};)", re.DOTALL)


@dataclass(frozen=True)
class FirmwareTarget:
    """One flashable image published by a release.

    key: manifest `devices` key and flash.html `firmwareKey`.
    asset_suffix: release asset suffix after the tag (`<tag><suffix>.bin`).
    local_name: file name under docs/firmware/.
    slot_size: OTA app slot size the image must fit.
    required: whether a stable release must carry this asset.
    """

    key: str
    board: str
    chip_family: str
    asset_suffix: str
    local_name: str
    slot_size: int
    required: bool
    environment: str


C3_TARGET = FirmwareTarget(
    key="x4",
    board="x4",
    chip_family="ESP32-C3",
    asset_suffix="",
    local_name="firmware.bin",
    slot_size=6_553_600,
    required=True,
    environment="gh_release",
)
X4PRO_TARGET = FirmwareTarget(
    key="x4pro",
    board="x4pro",
    chip_family="ESP32-S3",
    asset_suffix="-x4pro",
    local_name="firmware-x4pro.bin",
    slot_size=8_257_536,
    required=False,
    environment="x4pro-gh_release",
)
FIRMWARE_TARGETS: tuple[FirmwareTarget, ...] = (C3_TARGET, X4PRO_TARGET)

# The X3 shares the C3 image but has a larger OTA slot; the manifest exposes it
# as its own device entry so flash.html can pick per-device limits uniformly.
X3_SLOT_SIZE = 7_798_784

# Device entries in the manifest. Each maps to the target whose image it flashes.
DEVICE_ENTRIES: tuple[tuple[str, FirmwareTarget, int], ...] = (
    ("x4", C3_TARGET, C3_TARGET.slot_size),
    ("x3", C3_TARGET, X3_SLOT_SIZE),
    ("x4pro", X4PRO_TARGET, X4PRO_TARGET.slot_size),
)


def request_json(url: str, token: str | None) -> Any:
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "cpr-vcodex-autoflash-sync",
    }
    if token:
        headers["Authorization"] = f"Bearer {token}"

    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.loads(response.read().decode("utf-8"))


def download_bytes(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "cpr-vcodex-autoflash-sync"})
    with urllib.request.urlopen(request, timeout=300) as response:
        return response.read()


def select_firmware_asset(release: dict[str, Any]) -> dict[str, Any]:
    """Return the required ESP32-C3 (`<tag>.bin`) asset or raise."""
    tag = str(release["tag_name"])
    assets = release.get("assets") or []
    if not assets:
        raise RuntimeError(f"Release {tag} has no downloadable assets")

    exact_name = f"{tag}.bin"
    for asset in assets:
        if asset.get("name") == exact_name:
            return asset

    for asset in assets:
        if asset.get("name") == "firmware.bin":
            return asset

    # Only fall back to "the single .bin" when the release is not a two-binary
    # release; an X4 Pro asset must never be mistaken for the C3 image.
    bin_assets = [
        asset
        for asset in assets
        if str(asset.get("name", "")).endswith(".bin")
        and not str(asset.get("name", "")).endswith(f"{X4PRO_TARGET.asset_suffix}.bin")
    ]
    if len(bin_assets) == 1:
        return bin_assets[0]

    names = ", ".join(str(asset.get("name", "<unnamed>")) for asset in assets)
    raise RuntimeError(f"Could not choose firmware asset from release {tag}. Assets: {names}")


def select_target_asset(release: dict[str, Any], target: FirmwareTarget) -> dict[str, Any] | None:
    """Return the asset for `target`, or None when an optional asset is absent."""
    if target.asset_suffix == "":
        return select_firmware_asset(release)

    tag = str(release["tag_name"])
    assets = release.get("assets") or []
    candidates = (f"{tag}{target.asset_suffix}.bin", f"firmware{target.asset_suffix}.bin")
    for wanted in candidates:
        for asset in assets:
            if asset.get("name") == wanted:
                return asset

    if target.required:
        names = ", ".join(str(asset.get("name", "<unnamed>")) for asset in assets)
        raise RuntimeError(f"Release {tag} is missing required asset {candidates[0]}. Assets: {names}")
    return None


def firmware_tag_sort_key(tag: str) -> tuple[int, int, int, int, int, str] | None:
    if not FIRMWARE_TAG_RE.fullmatch(tag):
        return None

    prefix = tag.removesuffix("-cpr-vcodex")
    match = FIRMWARE_VERSION_PREFIX_RE.match(prefix)
    if not match:
        return None

    numbers = tuple(int(part) for part in match.groups())
    stable = 1 if prefix == ".".join(str(number) for number in numbers) else 0
    return (*numbers, stable, tag)


def fetch_firmware_release_by_tag(repo: str, tag: str, token: str | None) -> dict[str, Any]:
    release = request_json(
        f"https://api.github.com/repos/{repo}/releases/tags/{quote(tag, safe='')}",
        token,
    )
    if not isinstance(release, dict):
        raise RuntimeError(f"GitHub release response for {tag} was not an object")
    if release.get("draft") or release.get("prerelease"):
        raise RuntimeError(f"Release {tag} is not a stable published firmware release")
    if not FIRMWARE_TAG_RE.fullmatch(str(release.get("tag_name", ""))):
        raise RuntimeError(f"Release {tag} is not a CPR-vCodex firmware release")

    select_firmware_asset(release)
    return release


def fetch_latest_firmware_release(repo: str, token: str | None) -> dict[str, Any]:
    releases = request_json(f"https://api.github.com/repos/{repo}/releases?per_page=50", token)
    if not isinstance(releases, list):
        raise RuntimeError("GitHub releases response was not a list")

    candidates: list[tuple[tuple[int, int, int, int, int, str], dict[str, Any]]] = []
    for release in releases:
        if release.get("draft") or release.get("prerelease"):
            continue

        tag = str(release.get("tag_name", ""))
        sort_key = firmware_tag_sort_key(tag)
        if sort_key is None:
            continue

        select_firmware_asset(release)
        candidates.append((sort_key, release))

    if candidates:
        return max(candidates, key=lambda item: item[0])[1]

    raise RuntimeError("Could not find a stable CPR-vCodex firmware release")


def write_atomic(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(delete=False, dir=path.parent) as tmp:
        tmp.write(data)
        tmp_path = Path(tmp.name)
    tmp_path.replace(path)


def asset_download_url(repo: str, tag: str, target: FirmwareTarget) -> str:
    return f"https://github.com/{repo}/releases/download/{tag}/{tag}{target.asset_suffix}.bin"


def verify_firmware(target: FirmwareTarget, asset: dict[str, Any], firmware: bytes) -> None:
    firmware_size = len(firmware)
    expected_size = asset.get("size")
    label = asset.get("name") or f"{target.key} firmware"
    if expected_size is not None and int(expected_size) != firmware_size:
        raise RuntimeError(f"Downloaded {label} size mismatch: asset={expected_size}, downloaded={firmware_size}")
    if firmware_size < MIN_FIRMWARE_SIZE:
        raise RuntimeError(f"Downloaded {label} is suspiciously small: {firmware_size} bytes")
    if firmware_size > target.slot_size:
        raise RuntimeError(
            f"Downloaded {label} is too large for the {target.chip_family} app partition: "
            f"{firmware_size} > {target.slot_size} bytes"
        )


def build_manifest(
    repo: str,
    release: dict[str, Any],
    images: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    """Build docs/firmware/manifest.json.

    `images` maps target key -> {"asset": <github asset>, "size": int, "sha256": str,
    "downloadUrl": str}. The C3 entry ("x4") is mandatory; "x4pro" is optional.

    The top level keeps the historic flat C3 fields (version/firmwareUrl/
    downloadUrl/size/sha256/source) so older page copies keep working, and adds
    a `devices` object keyed x4/x3/x4pro plus an ESP Web Tools style `builds`
    list with one entry per chip family.
    """
    tag = str(release["tag_name"])
    c3 = images[C3_TARGET.key]
    c3_asset = c3["asset"]

    devices: dict[str, Any] = {}
    builds: list[dict[str, Any]] = []
    for device_key, target, slot_size in DEVICE_ENTRIES:
        image = images.get(target.key)
        if image is None:
            continue
        asset = image["asset"]
        devices[device_key] = {
            "board": target.board,
            "chipFamily": target.chip_family,
            "environment": target.environment,
            "path": f"firmware/{target.local_name}",
            "asset": asset.get("name"),
            "downloadUrl": image["downloadUrl"],
            "size": image["size"],
            "sha256": image["sha256"],
            "appSlotSize": slot_size,
            "assetUpdatedAt": asset.get("updated_at"),
        }
    for target in FIRMWARE_TARGETS:
        if target.key in images:
            builds.append(
                {
                    "chipFamily": target.chip_family,
                    "board": target.board,
                    "parts": [{"path": target.local_name, "offset": 65536}],
                }
            )

    return {
        "name": "CPR-vCodex",
        "version": tag,
        "firmwareUrl": f"firmware/{C3_TARGET.local_name}",
        "downloadUrl": c3["downloadUrl"],
        "size": c3["size"],
        "sha256": c3["sha256"],
        "source": {
            "type": "github-release",
            "repo": repo,
            "tag": tag,
            "asset": c3_asset.get("name"),
            "publishedAt": release.get("published_at"),
            "assetUpdatedAt": c3_asset.get("updated_at"),
        },
        "devices": devices,
        "new_install_prompt_erase": False,
        "builds": builds,
    }


def rewrite_download_urls(text: str, urls_by_suffix: dict[str, str]) -> str:
    """Rewrite release download URLs, keeping the C3/X4 Pro asset distinction."""

    def replace(match: re.Match[str]) -> str:
        suffix = match.group("suffix") or ""
        return urls_by_suffix.get(suffix, match.group(0))

    return DOWNLOAD_URL_RE.sub(replace, text)


def update_text_file(path: Path, tag: str, download_url: str, x4pro_download_url: str | None = None) -> bool:
    if not path.exists():
        return False

    original = path.read_text(encoding="utf-8")
    updated = VERSION_RE.sub(tag, original)
    urls_by_suffix = {"": download_url}
    if x4pro_download_url:
        urls_by_suffix[X4PRO_TARGET.asset_suffix] = x4pro_download_url
    updated = rewrite_download_urls(updated, urls_by_suffix)
    if updated == original:
        return False

    path.write_text(updated, encoding="utf-8", newline="")
    return True


def _js_string(value: Any) -> str:
    return json.dumps(str(value))


def render_fallback_release(manifest: dict[str, Any]) -> str:
    """Render the `const FALLBACK_RELEASE = {...};` block embedded in flash.html.

    The block must keep a four-space indent and end with `\\n    };` so
    FALLBACK_RELEASE_RE can locate it again on the next sync.
    """
    source = manifest.get("source") or {}
    lines = [
        "const FALLBACK_RELEASE = {",
        f"      version: {_js_string(manifest['version'])},",
        f"      firmwareUrl: {_js_string(manifest['firmwareUrl'])},",
        f"      downloadUrl: {_js_string(manifest['downloadUrl'])},",
        f"      size: {int(manifest['size'])},",
        f"      sha256: {_js_string(manifest['sha256'])},",
        "      source: {",
        f"        type: {_js_string(source.get('type', 'github-release'))},",
        f"        repo: {_js_string(source.get('repo', DEFAULT_REPO))},",
        f"        tag: {_js_string(source.get('tag', manifest['version']))},",
        f"        asset: {_js_string(source.get('asset', ''))}",
        "      },",
        "      devices: {",
    ]
    device_items = list((manifest.get("devices") or {}).items())
    for index, (key, device) in enumerate(device_items):
        trailing = "," if index < len(device_items) - 1 else ""
        lines.extend(
            [
                f"        {key}: {{",
                f"          chipFamily: {_js_string(device['chipFamily'])},",
                f"          path: {_js_string(device['path'])},",
                f"          asset: {_js_string(device.get('asset', ''))},",
                f"          downloadUrl: {_js_string(device['downloadUrl'])},",
                f"          size: {int(device['size'])},",
                f"          sha256: {_js_string(device['sha256'])}",
                f"        }}{trailing}",
            ]
        )
    lines.extend(["      }", "    };"])
    return "\n".join(lines)


def update_flash_fallback(path: Path, manifest: dict[str, Any]) -> bool:
    if not path.exists():
        return False

    original = path.read_text(encoding="utf-8")
    match = FALLBACK_RELEASE_RE.search(original)
    if not match:
        return False

    block = match.group(1)
    updated_block = render_fallback_release(manifest)
    if updated_block == block:
        return False

    updated = original[: match.start(1)] + updated_block + original[match.end(1) :]
    path.write_text(updated, encoding="utf-8", newline="")
    return True


def sync_autoflash(repo: str, project_dir: Path, token: str | None, tag: str | None = None) -> str:
    release = fetch_firmware_release_by_tag(repo, tag, token) if tag else fetch_latest_firmware_release(repo, token)
    tag = str(release["tag_name"])
    firmware_dir = project_dir / "docs" / "firmware"

    images: dict[str, dict[str, Any]] = {}
    for target in FIRMWARE_TARGETS:
        asset = select_target_asset(release, target)
        if asset is None:
            print(f"Release {tag} has no {target.chip_family} asset ({tag}{target.asset_suffix}.bin); skipping {target.key}")
            stale = firmware_dir / target.local_name
            if stale.exists():
                stale.unlink()
                print(f"Removed stale {stale.relative_to(project_dir)} (no matching asset in {tag})")
            continue

        download_url = str(asset["browser_download_url"])
        firmware = download_bytes(download_url)
        verify_firmware(target, asset, firmware)
        images[target.key] = {
            "asset": asset,
            "downloadUrl": download_url,
            "size": len(firmware),
            "sha256": hashlib.sha256(firmware).hexdigest(),
            "bytes": firmware,
        }

    for target in FIRMWARE_TARGETS:
        image = images.get(target.key)
        if image is not None:
            write_atomic(firmware_dir / target.local_name, image["bytes"])

    manifest = build_manifest(repo, release, images)
    (firmware_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")

    c3 = images[C3_TARGET.key]
    x4pro = images.get(X4PRO_TARGET.key)
    # Even when the release lacks the X4 Pro asset, rewrite any existing X4 Pro
    # URL to the new tag so the docs never point at a mismatched release.
    x4pro_url = x4pro["downloadUrl"] if x4pro else asset_download_url(repo, tag, X4PRO_TARGET)
    for relative in ("README.md", "docs/assets/site.js", "docs/index.html", "docs/flash.html"):
        update_text_file(project_dir / relative, tag, c3["downloadUrl"], x4pro_url)
    update_flash_fallback(project_dir / "docs" / "flash.html", manifest)

    env_path = os.environ.get("GITHUB_ENV")
    if env_path:
        with open(env_path, "a", encoding="utf-8") as env_file:
            env_file.write(f"AUTOFLASH_VERSION={tag}\n")
            env_file.write(f"AUTOFLASH_X4PRO={'1' if x4pro else '0'}\n")

    print(f"Synced auto-flash firmware to {tag}")
    for target in FIRMWARE_TARGETS:
        image = images.get(target.key)
        if image is None:
            print(f"{target.chip_family} ({target.key}): not published in {tag}")
            continue
        print(f"{target.chip_family} ({target.key}) asset: {image['asset'].get('name')}")
        print(f"{target.chip_family} ({target.key}) size: {image['size']}")
        print(f"{target.chip_family} ({target.key}) SHA-256: {image['sha256']}")
    return tag


def main() -> int:
    parser = argparse.ArgumentParser(description="Sync GitHub Pages auto-flash firmware from latest stable release.")
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", DEFAULT_REPO))
    parser.add_argument("--project-dir", type=Path, default=Path.cwd())
    parser.add_argument("--tag", default=os.environ.get("AUTOFLASH_TAG"))
    args = parser.parse_args()

    try:
        sync_autoflash(args.repo, args.project_dir.resolve(), os.environ.get("GITHUB_TOKEN"), args.tag)
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
