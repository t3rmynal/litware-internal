#!/usr/bin/env python3
from __future__ import annotations

import argparse
import datetime as dt
import pathlib
import re
import sys
from typing import Iterable

ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_DUMP_DIR = ROOT / "offsets" / "output"
DEFAULT_HEADER = ROOT / "litware-dll" / "src" / "core" / "offsets.h"

MANAGED_SECTIONS: dict[str, list[str]] = {
    "client": [
        "dwCSGOInput",
        "dwEntityList",
        "dwGameEntitySystem",
        "dwGameEntitySystem_highestEntityIndex",
        "dwGameRules",
        "dwGlobalVars",
        "dwGlowManager",
        "dwLocalPlayerController",
        "dwLocalPlayerPawn",
        "dwPlantedC4",
        "dwPrediction",
        "dwSensitivity",
        "dwSensitivity_sensitivity",
        "dwViewAngles",
        "dwViewMatrix",
        "dwViewRender",
        "dwWeaponC4",
    ],
    "engine2": [
        "dwBuildNumber",
        "dwNetworkGameClient",
        "dwNetworkGameClient_clientTickCount",
        "dwNetworkGameClient_deltaTick",
        "dwNetworkGameClient_isBackgroundMap",
        "dwNetworkGameClient_localPlayer",
        "dwNetworkGameClient_maxClients",
        "dwNetworkGameClient_serverTickCount",
        "dwNetworkGameClient_signOnState",
        "dwWindowWidth",
        "dwWindowHeight",
    ],
    "inputsystem": ["dwInputSystem"],
    "matchmaking": ["dwGameTypes"],
    "soundsystem": ["dwSoundSystem", "dwSoundSystem_engineViewData"],
    "buttons": [
        "attack",
        "attack2",
        "back",
        "duck",
        "forward",
        "jump",
        "left",
        "lookatweapon",
        "reload",
        "right",
        "showscores",
        "sprint",
        "turnleft",
        "turnright",
        "use",
        "zoom",
    ],
    "client_interfaces": [
        "ClientToolsInfo_001",
        "EmptyWorldService001_Client",
        "GameClientExports001",
        "LegacyGameUI001",
        "Source2Client002",
        "Source2ClientConfig001",
        "Source2ClientPrediction001",
        "Source2ClientUI001",
    ],
}

TIMESTAMP_RE = re.compile(r"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})(?:\.\d+)? UTC")
CONST_RE = re.compile(
    r"(?:constexpr\s+std::ptrdiff_t|public\s+const\s+nint|pub\s+const)\s+"
    r"((?:r#)?[A-Za-z_][A-Za-z0-9_]*)[^=]*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;"
)
JSON_RE = re.compile(r'"([A-Za-z_][A-Za-z0-9_]*)"\s*:\s*(0x[0-9A-Fa-f]+|\d+)')


class UpdateError(RuntimeError):
    pass


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Update managed blocks in litware-dll/src/core/offsets.h from cs2-dumper output."
    )
    parser.add_argument(
        "inputs",
        nargs="*",
        help="Dump files or directories. Defaults to offsets/output/.",
    )
    parser.add_argument(
        "--header",
        type=pathlib.Path,
        default=DEFAULT_HEADER,
        help=f"Header to update (default: {DEFAULT_HEADER})",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Parse dumps and print what would change without touching offsets.h.",
    )
    return parser.parse_args()


def iter_input_files(inputs: list[str]) -> list[pathlib.Path]:
    raw_paths = [pathlib.Path(p) for p in inputs] or [DEFAULT_DUMP_DIR]
    files: list[pathlib.Path] = []
    for raw in raw_paths:
        path = raw if raw.is_absolute() else (ROOT / raw)
        if path.is_dir():
            files.extend(sorted(p for p in path.rglob("*") if p.is_file()))
        elif path.is_file():
            files.append(path)
    files = [p for p in files if p.name != "README.md"]
    if not files:
        raise UpdateError("No dump files found. Put cs2-dumper output into offsets/output/ or pass files explicitly.")
    return files


def normalize_key(key: str) -> str:
    return key[2:] if key.startswith("r#") else key


def parse_number(raw: str) -> int:
    return int(raw, 16) if raw.lower().startswith("0x") else int(raw, 10)


def parse_dump_files(files: Iterable[pathlib.Path]) -> tuple[dict[str, int], dt.datetime | None]:
    managed_keys = {key for keys in MANAGED_SECTIONS.values() for key in keys}
    seen: dict[str, int] = {}
    newest_ts: dt.datetime | None = None

    for path in files:
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            text = path.read_text(encoding="utf-8-sig")

        for match in TIMESTAMP_RE.finditer(text):
            stamp = dt.datetime.strptime(match.group(1), "%Y-%m-%d %H:%M:%S")
            if newest_ts is None or stamp > newest_ts:
                newest_ts = stamp

        for regex in (CONST_RE, JSON_RE):
            for match in regex.finditer(text):
                key = normalize_key(match.group(1))
                if key not in managed_keys:
                    continue
                value = parse_number(match.group(2))
                previous = seen.get(key)
                if previous is None:
                    seen[key] = value
                elif previous != value:
                    raise UpdateError(
                        f"Conflicting values for {key}: 0x{previous:X} vs 0x{value:X} ({path})"
                    )

    return seen, newest_ts


def replace_namespace_block(text: str, namespace: str, rendered_block: str) -> str:
    pattern = re.compile(
        rf"(^\s*namespace\s+{re.escape(namespace)}\s*\{{\n)(.*?)(^\s*\}})"
        , re.MULTILINE | re.DOTALL,
    )
    match = pattern.search(text)
    if not match:
        raise UpdateError(f"Could not find namespace offsets::{namespace} in offsets.h")
    return text[: match.start(2)] + rendered_block + text[match.start(3):]


def render_section(namespace: str, keys: list[str], values: dict[str, int], existing: str) -> tuple[str, list[str]]:
    missing = [key for key in keys if key not in values]
    if missing:
        return existing, missing
    width = max(len(key) for key in keys)
    lines = [f"        constexpr uintptr_t {key:<{width}} = 0x{values[key]:X};" for key in keys]
    return "\n".join(lines) + "\n", []


def update_header_text(text: str, values: dict[str, int], newest_ts: dt.datetime | None) -> tuple[str, list[str], dict[str, list[str]]]:
    updated_sections: list[str] = []
    skipped: dict[str, list[str]] = {}

    if newest_ts is not None:
        stamp = newest_ts.strftime("%Y-%m-%d %H:%M:%S")
        text = re.sub(
            r"// обновлено по cs2-dumper: .* UTC",
            f"// обновлено по cs2-dumper: {stamp} UTC",
            text,
            count=1,
        )

    for namespace, keys in MANAGED_SECTIONS.items():
        match = re.search(
            rf"(^\s*namespace\s+{re.escape(namespace)}\s*\{{\n)(.*?)(^\s*\}})",
            text,
            re.MULTILINE | re.DOTALL,
        )
        if not match:
            raise UpdateError(f"Could not find namespace offsets::{namespace} in offsets.h")
        current_block = match.group(2)
        rendered, missing = render_section(namespace, keys, values, current_block)
        if missing:
            skipped[namespace] = missing
            continue
        text = replace_namespace_block(text, namespace, rendered)
        updated_sections.append(namespace)

    return text, updated_sections, skipped


def main() -> int:
    args = parse_args()
    header_path = args.header if args.header.is_absolute() else (ROOT / args.header)
    files = iter_input_files(args.inputs)
    values, newest_ts = parse_dump_files(files)
    original = header_path.read_text(encoding="utf-8")
    updated, changed_sections, skipped = update_header_text(original, values, newest_ts)

    print("Using dump files:")
    for path in files:
        rel = path.relative_to(ROOT) if path.is_relative_to(ROOT) else path
        print(f"- {rel}")

    if changed_sections:
        print("\nUpdated namespaces:")
        for namespace in changed_sections:
            print(f"- offsets::{namespace}")
    else:
        print("\nNo managed namespace had a complete set of values; offsets.h was left untouched.")

    if skipped:
        print("\nSkipped namespaces with missing keys:")
        for namespace, missing in skipped.items():
            print(f"- offsets::{namespace}: {', '.join(missing)}")

    if args.dry_run:
        return 0

    if updated != original:
        header_path.write_text(updated, encoding="utf-8")
        print(f"\nUpdated {header_path}")
    else:
        print(f"\nNo file changes written to {header_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except UpdateError as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
