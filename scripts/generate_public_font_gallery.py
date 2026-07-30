#!/usr/bin/env python3
"""Render the public Duet font pack through the firmware simulator."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PACK_CONFIG = ROOT / "release" / "public-font-pack.json"
SMOKE_RUNNER = ROOT / "scripts" / "run_simulator_smoke_test.py"
DEFAULT_OUTPUT_DIR = ROOT / "docs" / "media" / "fonts"
DEFAULT_PAGE = ROOT / "docs" / "font-gallery.md"
SOURCE_LABELS = {
    "google-fonts": "Google Fonts",
    "ebook-fonts": "E-reader-Optimized Families",
    "crossink-fonts": "CrossInk Compatibility Families",
    "ctan-open-fonts": "CTAN Open-Font Families",
    "coelacanth": "Coelacanth",
    "sil-gentium": "SIL Gentium",
}


def slugify(value: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
    if not slug:
        raise ValueError(f"Cannot create a filename for family {value!r}")
    return slug


def load_config() -> dict:
    return json.loads(PACK_CONFIG.read_text(encoding="utf-8"))


def all_families(config: dict) -> list[str]:
    return [family for group in config["source_groups"] for family in group["families"]]


def resolve_magick() -> str:
    command = shutil.which("magick")
    if command:
        return command
    raise SystemExit("ImageMagick is required. Install it so the `magick` command is available.")


def build_simulator() -> None:
    platformio = os.environ.get("PLATFORMIO_CLI") or shutil.which("pio")
    if not platformio:
        managed = Path.home() / ".platformio" / "penv" / "bin" / "pio"
        if managed.is_file() and os.access(managed, os.X_OK):
            platformio = str(managed)
    if not platformio:
        raise SystemExit("PlatformIO CLI not found. Install `pio` or set PLATFORMIO_CLI.")
    subprocess.run([platformio, "run", "-e", "simulator"], cwd=ROOT, check=True)


def extract_pack(font_pack: Path, target: Path) -> Path:
    with zipfile.ZipFile(font_pack) as archive:
        members = [name for name in archive.namelist() if name.startswith("fonts/")]
        if not members:
            raise SystemExit(f"{font_pack} does not contain a fonts/ directory")
        archive.extractall(target, members)
    return target / "fonts"


def render_family(
    family: str,
    font_root: Path,
    output_dir: Path,
    baseline_family: str,
    alternate_baseline: str,
    magick: str,
    force: bool,
) -> tuple[str, Path]:
    baseline = alternate_baseline if family == baseline_family else baseline_family
    family_dir = font_root / family
    baseline_dir = font_root / baseline
    if not family_dir.is_dir():
        raise RuntimeError(f"Missing extracted family directory: {family_dir}")
    if not baseline_dir.is_dir():
        raise RuntimeError(f"Missing extracted baseline directory: {baseline_dir}")

    output_path = output_dir / f"{slugify(family)}.png"
    if output_path.is_file() and not force:
        return family, output_path

    with tempfile.TemporaryDirectory(prefix=f"duet-font-{slugify(family)}-") as temp_name:
        full_bmp = Path(temp_name) / "preview.bmp"
        command = [
            sys.executable,
            str(SMOKE_RUNNER),
            "--device",
            "x4",
            "--no-build",
            "--page-turns",
            "0",
            "--font-preview-family",
            str(family_dir),
            "--font-preview-family",
            str(baseline_dir),
            "--single-font-preview",
            family,
            str(full_bmp),
            "--font-current",
            baseline,
            "--font-preview-size",
            "16",
            "--timeout",
            "60",
        ]
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        if result.returncode != 0:
            raise RuntimeError(
                f"Simulator failed for {family}\nSTDOUT:\n{result.stdout}\nSTDERR:\n{result.stderr}"
            )
        subprocess.run(
            [
                magick,
                str(full_bmp),
                "-crop",
                "480x300+0+0",
                "+repage",
                "-strip",
                str(output_path),
            ],
            check=True,
        )
    return family, output_path


def table_rows(families: list[str], media_root: str) -> list[str]:
    rows = []
    for start in range(0, len(families), 3):
        group = families[start : start + 3]
        labels = " | ".join(group + [""] * (3 - len(group)))
        images = " | ".join(
            [
                f"[![{family} Duet font specimen]({media_root}/{slugify(family)}.png)]({media_root}/{slugify(family)}.png)"
                if family
                else ""
                for family in group + [""] * (3 - len(group))
            ]
        )
        rows.extend([f"| {labels} |", f"| {images} |"])
    return rows


def write_gallery_page(config: dict, page_path: Path) -> None:
    lines = [
        "---",
        "title: All Font Previews",
        "parent: Fonts",
        "nav_order: 1",
        "---",
        "",
        "# All 123 Font Previews",
        "",
        "Every specimen below is rendered from the public pack's actual `.cpfont` file through Duet's X4 simulator. The regular pangram and italic/bold samples use the same 16 pt picker layout shown on the reader. OpenDyslexic alone uses its compact 14 pt specimen because that face is unusually large at the same nominal size.",
        "",
        "[Download the complete 123-family font pack](https://github.com/lauren-alexandra/duet-xteink/releases/download/v0.1.0-alpha.7/Duet-Open-Font-Pack-v1.zip){: .btn .btn-primary }",
        "",
    ]
    media_root = "media/fonts"
    for source_group in config["source_groups"]:
        families = source_group["families"]
        title = SOURCE_LABELS[source_group["id"]]
        lines.extend(
            [
                f"## {title} ({len(families)})",
                "",
                "|  |  |  |",
                "| --- | --- | --- |",
                *table_rows(families, media_root),
                "",
            ]
        )
    page_path.write_text("\n".join(lines), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font_pack", type=Path, help="Local Duet-Open-Font-Pack-v1.zip")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--page", type=Path, default=DEFAULT_PAGE)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--family", action="append", default=[], help="Render only this family; may be repeated")
    parser.add_argument("--no-build", action="store_true", help="Reuse the existing X4 simulator binary")
    parser.add_argument("--force", action="store_true", help="Regenerate specimens that already exist")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    config = load_config()
    expected = all_families(config)
    selected = args.family or expected
    unknown = sorted(set(selected) - set(expected))
    if unknown:
        raise SystemExit(f"Unknown public font family: {unknown[0]}")
    if args.jobs < 1:
        raise SystemExit("--jobs must be at least 1")
    font_pack = args.font_pack.expanduser().resolve()
    if not font_pack.is_file():
        raise SystemExit(f"Font pack not found: {font_pack}")
    output_dir = args.output_dir.expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    if not args.no_build:
        build_simulator()
    magick = resolve_magick()

    with tempfile.TemporaryDirectory(prefix="duet-public-font-pack-") as temp_name:
        font_root = extract_pack(font_pack, Path(temp_name))
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = {
                executor.submit(
                    render_family,
                    family,
                    font_root,
                    output_dir,
                    "Merriweather",
                    "ReadexPro",
                    magick,
                    args.force,
                ): family
                for family in selected
            }
            completed = 0
            for future in concurrent.futures.as_completed(futures):
                family, output_path = future.result()
                completed += 1
                print(f"[{completed}/{len(selected)}] {family}: {output_path}", flush=True)

    if not args.family:
        write_gallery_page(config, args.page.expanduser().resolve())
        print(f"Wrote gallery page: {args.page.expanduser().resolve()}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
