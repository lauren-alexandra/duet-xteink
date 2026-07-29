#!/usr/bin/env python3
"""Create the self-contained Duet release archive from reviewed X3/X4 builds."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ROOT_FILES = (
    "AUTHORS.md",
    "CHANGELOG.md",
    "FEATURES.md",
    "FONT_SOURCES.md",
    "LICENSE",
    "NOTICE",
    "PROJECT_IDENTITY.md",
    "PUBLIC_RELEASE_READINESS.md",
    "README.md",
    "THIRD_PARTY_NOTICES.md",
    "USER_GUIDE.md",
)
DOC_FILES = (
    "ALPHA7_ACCEPTANCE_QUICKSTART.md",
    "ALPHA_TESTING.md",
    "AI_COVER_PREFILL_PROMPT.md",
    "COVER_PREFILL.md",
    "DICTIONARY_SETUP.md",
    "DUET_FULL_FEATURE_TOUR.md",
    "DUET_STORAGE_NAMESPACE_MIGRATION.md",
    "ISSUE_TRIAGE.md",
    "MAINTAINER_RELEASE_RUNBOOK.md",
    "PHYSICAL_TEST_MATRIX.md",
    "activity-manager.md",
    "bionic-reading.md",
    "controls.md",
    "data-cache.md",
    "epub-render-modes.md",
    "file-formats.md",
    "font-build-variants.md",
    "hyphenation-trie-format.md",
    "i18n.md",
    "installation.md",
    "nearby-position-sync.md",
    "reader-features.md",
    "reading-stats-sync.md",
    "sd-card-fonts.md",
    "simulator.md",
    "translators.md",
    "troubleshooting.md",
    "webserver-endpoints.md",
    "webserver.md",
)
DOC_DIRS = (
    "images",
    "media",
    "templates",
)
SCRIPT_FILES = (
    "duet_storage_paths.py",
    "generate_library_catalog.py",
    "package_wordnet_dictionary.py",
    "prebuild_library_stats_index.py",
    "prefill_cover_thumbnails.py",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_file(path: Path) -> Path:
    if not path.is_file():
        raise FileNotFoundError(f"Required release input is missing: {path}")
    return path


def require_dir(path: Path) -> Path:
    if not path.is_dir():
        raise FileNotFoundError(f"Required release directory is missing: {path}")
    return path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="Duet version without the leading v")
    parser.add_argument("--x3", required=True, type=Path, help="Reviewed Duet X3 firmware BIN")
    parser.add_argument("--x4", required=True, type=Path, help="Reviewed Duet X4 firmware BIN")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=ROOT / ".pio" / "build",
        help="Directory that receives the package folder, ZIP, and manifests",
    )
    parser.add_argument(
        "--draft",
        action="store_true",
        help="Add DRAFT to the ZIP name for a package that lacks physical acceptance",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    version = args.version.removeprefix("v")
    if not version:
        raise ValueError("Version cannot be empty")

    x3 = require_file(args.x3.expanduser().resolve())
    x4 = require_file(args.x4.expanduser().resolve())
    expected_x3 = f"Duet-X3-v{version}.bin"
    expected_x4 = f"Duet-X4-v{version}.bin"
    if x3.name != expected_x3:
        raise ValueError(f"X3 artifact must be named {expected_x3}, got {x3.name}")
    if x4.name != expected_x4:
        raise ValueError(f"X4 artifact must be named {expected_x4}, got {x4.name}")

    output_dir = args.output_dir.expanduser().resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    package_name = f"Duet-v{version}"
    package_dir = output_dir / package_name
    if package_dir.exists():
        raise FileExistsError(f"Refusing to overwrite existing release package: {package_dir}")
    firmware_dir = package_dir / "firmware"
    docs_dir = package_dir / "docs"
    scripts_dir = package_dir / "scripts"
    firmware_dir.mkdir(parents=True)
    docs_dir.mkdir()
    scripts_dir.mkdir()

    template = require_file(ROOT / "release" / "README-FIRST.txt").read_text(encoding="utf-8")
    rendered_readme = template.replace("@DUET_VERSION@", version)
    if "@DUET_VERSION@" in rendered_readme:
        raise ValueError("README-FIRST version token was not fully rendered")
    (package_dir / "README-FIRST.txt").write_text(rendered_readme, encoding="utf-8")

    packaged_x3 = firmware_dir / expected_x3
    packaged_x4 = firmware_dir / expected_x4
    shutil.copy2(x3, packaged_x3)
    shutil.copy2(x4, packaged_x4)

    for relative_path in ROOT_FILES:
        source = require_file(ROOT / relative_path)
        shutil.copy2(source, package_dir / source.name)
    for filename in DOC_FILES:
        source = require_file(ROOT / "docs" / filename)
        shutil.copy2(source, docs_dir / filename)
    for dirname in DOC_DIRS:
        source = require_dir(ROOT / "docs" / dirname)
        shutil.copytree(source, docs_dir / dirname)
    for filename in SCRIPT_FILES:
        source = require_file(ROOT / "scripts" / filename)
        shutil.copy2(source, scripts_dir / filename)

    licenses_dir = require_dir(ROOT / "licenses")
    shutil.copytree(licenses_dir, package_dir / "licenses")

    release_notes = require_file(ROOT / "docs" / "releases" / f"v{version}.md")
    shutil.copy2(release_notes, package_dir / "RELEASE_NOTES.md")
    shutil.copy2(release_notes, output_dir / f"RELEASE_NOTES-v{version}.md")

    checksum_lines = [
        f"{sha256(packaged_x3)}  firmware/{expected_x3}",
        f"{sha256(packaged_x4)}  firmware/{expected_x4}",
    ]
    checksum_text = "\n".join(checksum_lines) + "\n"
    (package_dir / "SHA256SUMS.txt").write_text(checksum_text, encoding="ascii")
    (output_dir / "SHA256SUMS.txt").write_text(checksum_text, encoding="ascii")

    archive_suffix = "-DRAFT-firmware" if args.draft else "-firmware"
    archive_base = output_dir / f"Duet-v{version}{archive_suffix}"
    archive_path = Path(f"{archive_base}.zip")
    if archive_path.exists():
        raise FileExistsError(f"Refusing to overwrite existing release archive: {archive_path}")
    archive_path = Path(
        shutil.make_archive(
            str(archive_base),
            "zip",
            root_dir=output_dir,
            base_dir=package_name,
        )
    )

    result = {
        "archive": str(archive_path),
        "draft": args.draft,
        "package_dir": str(package_dir),
        "version": version,
        "x3": {"file": expected_x3, "sha256": sha256(packaged_x3)},
        "x4": {"file": expected_x4, "sha256": sha256(packaged_x4)},
    }
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
