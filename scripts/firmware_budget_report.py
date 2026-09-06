from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Any


# Historic name kept for callers that import it; it is the ESP32-C3 slot size.
APP_PARTITION_SIZE = 6_553_600
RAM_RE = re.compile(r"RAM:.*?(\d+)\s+bytes\s+from\s+(\d+)\s+bytes")
FLASH_RE = re.compile(r"Flash:.*?(\d+)\s+bytes\s+from\s+(\d+)\s+bytes")

# Per-board OTA app slot sizes. `slotSize` is the partition the packaged image
# must fit on the device (partitions.csv for the C3 envs, partitions_x4pro.csv
# for the x4pro envs; PlatformIO links each image against its own table).
# `warnSize` (X4 Pro only) is the CrossPoint-style 0x640000 slot: an image
# above it can only be installed on units with the stock X4 Pro table.
BOARD_PROFILES: dict[str, dict[str, Any]] = {
    "x4": {
        "board": "x4",
        "chipFamily": "ESP32-C3",
        "environment": "gh_release",
        "slotSize": 6_553_600,
        "warnSize": None,
    },
    "x4pro": {
        "board": "x4pro",
        "chipFamily": "ESP32-S3",
        "environment": "x4pro-gh_release",
        "slotSize": 8_257_536,
        "warnSize": 6_553_600,
    },
}
X4PRO_SUFFIX = "-x4pro"


def fail(message: str) -> None:
    raise RuntimeError(message)


def parse_size(regex: re.Pattern[str], output: str, label: str) -> tuple[int, int]:
    match = regex.search(output)
    if not match:
        fail(f"Could not parse {label} usage from build log")
    return int(match.group(1)), int(match.group(2))


def pct(used: int, total: int) -> float:
    return used / total * 100 if total else 0.0


def format_bytes(value: int) -> str:
    sign = "-" if value < 0 else ""
    value = abs(value)
    if value >= 1024 * 1024:
        return f"{sign}{value / 1024 / 1024:.2f} MB"
    if value >= 1024:
        return f"{sign}{value / 1024:.1f} KB"
    return f"{sign}{value} B"


def read_metadata(path: Path) -> dict[str, Any]:
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def resolve_board(explicit: str | None, metadata: dict[str, Any], tag: str) -> str:
    """Pick the board profile: CLI flag, then packaged metadata, then tag suffix."""
    if explicit:
        return explicit
    board = metadata.get("board")
    if isinstance(board, str) and board in BOARD_PROFILES:
        return board
    environment = metadata.get("environment")
    if isinstance(environment, str) and environment.startswith("x4pro"):
        return "x4pro"
    if tag.endswith(X4PRO_SUFFIX):
        return "x4pro"
    return "x4"


def build_report(
    tag: str,
    build_log: Path,
    metadata_path: Path,
    flash_budget_percent: float,
    board: str | None = None,
) -> dict[str, Any]:
    output = build_log.read_text(encoding="utf-8", errors="replace")
    flash_used, flash_total = parse_size(FLASH_RE, output, "flash")
    ram_used, ram_total = parse_size(RAM_RE, output, "RAM")
    metadata = read_metadata(metadata_path)
    profile = BOARD_PROFILES[resolve_board(board, metadata, tag)]
    firmware_bytes = metadata.get("firmwareBytes")
    artifact_name = metadata.get("artifactName") or f"{tag}.bin"
    slot_size = int(profile["slotSize"])
    warn_size = profile["warnSize"]
    budget_bytes = int(slot_size * flash_budget_percent / 100)
    warnings: list[str] = []
    if warn_size is not None and flash_used > warn_size:
        warnings.append(
            f"Flash usage {flash_used} bytes exceeds the {format_bytes(warn_size)} build partition "
            f"(still within the {format_bytes(slot_size)} {profile['chipFamily']} slot)"
        )
    if isinstance(firmware_bytes, int) and firmware_bytes > slot_size:
        warnings.append(f"Packaged firmware {firmware_bytes} bytes exceeds the {format_bytes(slot_size)} app slot")

    return {
        "tag": tag,
        "artifactName": artifact_name,
        "board": profile["board"],
        "chipFamily": profile["chipFamily"],
        "environment": metadata.get("environment") or profile["environment"],
        "firmwareBytes": firmware_bytes,
        "flash": {
            "used": flash_used,
            "total": flash_total,
            "percent": round(pct(flash_used, flash_total), 2),
            "remaining": flash_total - flash_used,
            "slotSize": slot_size,
            "slotPercent": round(pct(flash_used, slot_size), 2),
            "budgetPercent": flash_budget_percent,
            "budgetBytes": budget_bytes,
            "budgetRemaining": budget_bytes - flash_used,
            "warnSize": warn_size,
        },
        "ram": {
            "used": ram_used,
            "total": ram_total,
            "percent": round(pct(ram_used, ram_total), 2),
            "remaining": ram_total - ram_used,
        },
        "warnings": warnings,
    }


def render_markdown(report: dict[str, Any]) -> str:
    flash = report["flash"]
    ram = report["ram"]
    firmware_bytes = report.get("firmwareBytes")
    firmware_display = format_bytes(firmware_bytes) if isinstance(firmware_bytes, int) else "n/a"
    budget_status = "OK" if flash["budgetRemaining"] >= 0 else "OVER"
    slot_size = flash.get("slotSize", flash["total"])

    lines = [
        f"## Firmware budget: {report['tag']}",
        "",
        f"- Artifact: `{report['artifactName']}`",
        f"- Board: `{report.get('board', 'x4')}` ({report.get('chipFamily', 'ESP32-C3')}, `{report.get('environment', '')}`)",
        f"- Packaged firmware: {firmware_display}",
        f"- App slot: {format_bytes(slot_size)}",
        f"- Flash usage: {flash['slotPercent']:.1f}% of slot, budget {flash['budgetPercent']:.1f}% ({budget_status})",
        "",
        "| Area | Used | Total | Usage | Remaining |",
        "|---|---:|---:|---:|---:|",
        (
            f"| Flash | {format_bytes(flash['used'])} | {format_bytes(flash['total'])} | "
            f"{flash['percent']:.2f}% | {format_bytes(flash['remaining'])} |"
        ),
        (
            f"| RAM | {format_bytes(ram['used'])} | {format_bytes(ram['total'])} | "
            f"{ram['percent']:.2f}% | {format_bytes(ram['remaining'])} |"
        ),
        "",
        f"Budget headroom: {format_bytes(flash['budgetRemaining'])}",
    ]
    for warning in report.get("warnings") or []:
        lines.append(f"- WARNING: {warning}")
    lines.append("")
    return "\n".join(lines)


def write_report(report: dict[str, Any], json_path: Path, md_path: Path) -> None:
    json_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    markdown = render_markdown(report)
    md_path.write_text(markdown, encoding="utf-8", newline="\n")

    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_path:
        with open(summary_path, "a", encoding="utf-8", newline="\n") as summary:
            summary.write(markdown)
            summary.write("\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Create a visible flash/RAM budget report from a PlatformIO build log.")
    parser.add_argument(
        "--tag",
        required=True,
        help="Artifact stem, e.g. 1.5.0.25-cpr-vcodex or 1.5.0.25-cpr-vcodex-x4pro (the -x4pro suffix selects the X4 Pro slot)",
    )
    parser.add_argument("--build-log", type=Path, required=True)
    parser.add_argument("--metadata", type=Path)
    parser.add_argument("--out-json", type=Path)
    parser.add_argument("--out-md", type=Path)
    parser.add_argument(
        "--board",
        choices=sorted(BOARD_PROFILES),
        help="Override the board profile (default: from metadata `board`/`environment`, else the tag suffix)",
    )
    parser.add_argument("--flash-budget-percent", type=float, default=97.5)
    parser.add_argument("--fail-over-budget", action="store_true")
    args = parser.parse_args()

    try:
        metadata = args.metadata or Path("artifacts") / f"{args.tag}.json"
        out_json = args.out_json or Path("artifacts") / f"{args.tag}-firmware-budget.json"
        out_md = args.out_md or Path("artifacts") / f"{args.tag}-firmware-budget.md"
        report = build_report(args.tag, args.build_log, metadata, args.flash_budget_percent, args.board)
        write_report(report, out_json, out_md)
        print(render_markdown(report))
        if args.fail_over_budget and report["flash"]["budgetRemaining"] < 0:
            fail("Flash usage exceeds the configured firmware budget")
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
