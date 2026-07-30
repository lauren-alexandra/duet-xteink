#!/usr/bin/env python3
"""Validate and package Duet's reviewed public SD-card font collection."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import shutil
import struct
import sys
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CONFIG = ROOT / "release" / "public-font-pack.json"
README_SOURCE = ROOT / "release" / "font-pack" / "README-FIRST.md"
LICENSES_SOURCE = ROOT / "release" / "font-pack" / "licenses"
FONT_SOURCES_SOURCE = ROOT / "FONT_SOURCES.md"
CPF_MAGIC = b"CPFONT\0\0"


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_config(path: Path) -> tuple[dict, dict[str, dict]]:
    config = json.loads(path.read_text(encoding="utf-8"))
    family_sources: dict[str, dict] = {}
    for group in config["source_groups"]:
        for family in group["families"]:
            if family in family_sources:
                raise ValueError(f"Family appears in more than one source group: {family}")
            family_sources[family] = group
    return config, family_sources


def cpfont_version(path: Path) -> int:
    with path.open("rb") as handle:
        header = handle.read(12)
    if len(header) != 12 or header[:8] != CPF_MAGIC:
        raise ValueError(f"Invalid .cpfont header: {path}")
    return struct.unpack_from("<H", header, 8)[0]


def validate_family(font_root: Path, family: str, sizes: list[int], expected_version: int) -> list[tuple[int, Path]]:
    family_root = font_root / family
    if not family_root.is_dir():
        raise ValueError(f"Missing reviewed family directory: {family_root}")

    expected = {size: family_root / f"{family}_{size}.cpfont" for size in sizes}
    actual = {path for path in family_root.glob("*.cpfont") if path.is_file() and not path.name.startswith("._")}
    expected_paths = set(expected.values())
    if actual != expected_paths:
        missing = sorted(str(path.name) for path in expected_paths - actual)
        extra = sorted(str(path.name) for path in actual - expected_paths)
        raise ValueError(f"Unexpected files for {family}: missing={missing} extra={extra}")

    validated: list[tuple[int, Path]] = []
    for size, path in expected.items():
        version = cpfont_version(path)
        if version != expected_version:
            raise ValueError(f"{path} uses .cpfont v{version}; expected v{expected_version}")
        validated.append((size, path))
    return validated


def write_tsv(path: Path, fieldnames: list[str], rows: list[dict[str, str]]) -> None:
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def create_zip(staging_root: Path, zip_path: Path) -> None:
    zip_path.parent.mkdir(parents=True, exist_ok=True)
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
        for path in sorted(staging_root.rglob("*")):
            if path.is_file():
                archive.write(path, path.relative_to(staging_root))


def package(font_root: Path, config_path: Path, output: Path) -> None:
    config, family_sources = load_config(config_path)
    sizes = [int(size) for size in config["sizes"]]
    expected_version = int(config["cpfont_version"])
    family_names = sorted(family_sources)

    with tempfile.TemporaryDirectory(prefix="duet-public-font-pack-") as temporary:
        staging_root = Path(temporary) / f"Duet-Open-Font-Pack-v{config['pack_version']}"
        output_fonts = staging_root / "fonts"
        output_fonts.mkdir(parents=True)

        manifest_rows: list[dict[str, str]] = []
        source_rows: list[dict[str, str]] = []
        checksum_lines: list[str] = []

        for family in family_names:
            group = family_sources[family]
            validated = validate_family(font_root, family, sizes, expected_version)
            destination_family = output_fonts / family
            destination_family.mkdir()

            source_rows.append(
                {
                    "family": family,
                    "source_group": group["id"],
                    "source_url": group["source"],
                    "revision": group["revision"],
                    "license_note": group["license_note"],
                }
            )

            for size, source_path in validated:
                destination = destination_family / source_path.name
                shutil.copy2(source_path, destination)
                relative = destination.relative_to(staging_root).as_posix()
                digest = file_sha256(destination)
                manifest_rows.append(
                    {
                        "family": family,
                        "size_pt": str(size),
                        "path": relative,
                        "bytes": str(destination.stat().st_size),
                        "sha256": digest,
                        "source_group": group["id"],
                        "source_url": group["source"],
                        "revision": group["revision"],
                        "license_note": group["license_note"],
                    }
                )
                checksum_lines.append(f"{digest}  {relative}")

        shutil.copy2(README_SOURCE, staging_root / "README-FIRST.md")
        shutil.copy2(FONT_SOURCES_SOURCE, staging_root / "FONT_SOURCES.md")
        shutil.copytree(LICENSES_SOURCE, staging_root / "licenses")
        write_tsv(
            staging_root / "FONT_PACK_SOURCES.tsv",
            ["family", "source_group", "source_url", "revision", "license_note"],
            source_rows,
        )
        write_tsv(
            staging_root / "FONT_PACK_MANIFEST.tsv",
            [
                "family",
                "size_pt",
                "path",
                "bytes",
                "sha256",
                "source_group",
                "source_url",
                "revision",
                "license_note",
            ],
            manifest_rows,
        )
        (staging_root / "SHA256SUMS.txt").write_text("\n".join(checksum_lines) + "\n", encoding="utf-8")
        create_zip(staging_root, output)

    print(
        json.dumps(
            {
                "output": str(output),
                "families": len(family_names),
                "files": len(manifest_rows),
                "sizes": sizes,
                "cpfont_version": expected_version,
                "bytes": output.stat().st_size,
                "sha256": file_sha256(output),
            },
            indent=2,
        )
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font-root", type=Path, required=True, help="Directory containing reviewed family folders")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        package(args.font_root.resolve(), args.config.resolve(), args.output.resolve())
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
