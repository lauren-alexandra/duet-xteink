#!/usr/bin/env python3
"""Fail a Duet release build on common versioning or privacy mistakes."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REQUIRED_FILES = (
    "AUTHORS.md",
    "CONTRIBUTING.md",
    "LICENSE",
    "NOTICE",
    "PROJECT_IDENTITY.md",
    "PUBLIC_RELEASE_READINESS.md",
    "SECURITY.md",
    "THIRD_PARTY_NOTICES.md",
    "docs/ALPHA_TESTING.md",
    "docs/ALPHA7_ACCEPTANCE_QUICKSTART.md",
    "docs/COVER_PREFILL.md",
    "docs/PHYSICAL_TEST_MATRIX.md",
    "release/README-FIRST.txt",
)
BLOCKED_STATE_NAMES = {
    "achievements.bin",
    "crash_report.txt",
    "desktop_cover_prefill.json",
    "global_stats.bin",
    "if_found.txt",
    "library_catalog.tsv",
    "reading_journal.bin",
    "reader_timing.txt",
    "session_log.bin",
}
BLOCKED_STATE_SUFFIXES = (".cpfont", ".cstats")
ALLOWED_EPUB_PREFIXES = ("test/epubs/", "test/language/RTL/")
TEXT_SKIP_PREFIXES = (
    "freeink-sdk/",
    "lib/expat/",
    "lib/I18n/translations/",
    "test/",
)
TEXT_SKIP_SUFFIXES = (
    ".bin",
    ".bmp",
    ".epub",
    ".gif",
    ".ico",
    ".jpeg",
    ".jpg",
    ".png",
    ".woff",
    ".woff2",
)
LOCAL_HOME_RE = re.compile(r"(?:/Users/|/home/)[A-Za-z0-9._-]+/")
WINDOWS_HOME_RE = re.compile(r"[A-Za-z]:\\\\Users\\\\[A-Za-z0-9._-]+\\\\")
FORMATTED_PHONE_RE = re.compile(
    r"(?<!\d)(?:\+?1[\s.-])?(?:\([2-9]\d{2}\)[\s.-]?|[2-9]\d{2}[\s.-])"
    r"[2-9]\d{2}[\s.-]\d{4}(?!\d)"
)
PLAIN_PHONE_RE = re.compile(r"(?<!\d)[2-9]\d{2}[2-9]\d{2}\d{4}(?!\d)")
SHA256_RE = re.compile(r"(?<![0-9A-Fa-f])[0-9A-Fa-f]{64}(?![0-9A-Fa-f])")
PUBLIC_PROSE_SUFFIXES = (".ini", ".json", ".md", ".toml", ".txt", ".yaml", ".yml")
COPY_SUFFIX_RE = re.compile(r" 2(?:\.[^/]+)?$")
COPY_SUFFIX_SCAN_SKIP = {".git", ".pio", "build"}


def candidate_files() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "-co", "--exclude-standard", "-z"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return sorted({item for item in result.stdout.decode().split("\0") if item})


def read_text(relative_path: str) -> str | None:
    if relative_path.startswith(TEXT_SKIP_PREFIXES) or relative_path.lower().endswith(TEXT_SKIP_SUFFIXES):
        return None
    path = ROOT / relative_path
    if not path.is_file() or path.stat().st_size > 5_000_000:
        return None
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return None


def parse_versions() -> tuple[str, str]:
    config = (ROOT / "platformio.ini").read_text(encoding="utf-8")
    development = re.search(r"^crossink_version\s*=\s*(\S+)\s*$", config, re.MULTILINE)
    public = re.search(r"^crossink_public_version\s*=\s*(\S+)\s*$", config, re.MULTILINE)
    if not development or not public:
        raise ValueError("platformio.ini is missing a Duet version field")
    return development.group(1), public.group(1)


def main() -> int:
    errors: list[str] = []
    warnings: list[str] = []

    for path in ROOT.rglob("*"):
        if any(part in COPY_SUFFIX_SCAN_SKIP for part in path.relative_to(ROOT).parts):
            continue
        if COPY_SUFFIX_RE.search(path.name):
            errors.append(f"copy-suffix path must be removed or renamed: {path.relative_to(ROOT)}")

    for relative_path in REQUIRED_FILES:
        if not (ROOT / relative_path).is_file():
            errors.append(f"missing required release file: {relative_path}")

    try:
        development_version, public_version = parse_versions()
    except ValueError as error:
        errors.append(str(error))
        development_version = public_version = ""

    if development_version and development_version != public_version:
        errors.append(
            "platformio.ini versions differ: "
            f"development={development_version} public={public_version}"
        )

    if public_version:
        release_notes = ROOT / "docs" / "releases" / f"v{public_version}.md"
        if not release_notes.is_file():
            errors.append(f"missing release notes: {release_notes.relative_to(ROOT)}")

        readme = (ROOT / "README.md").read_text(encoding="utf-8")
        for device in ("X3", "X4"):
            artifact = f"Duet-{device}-v{public_version}.bin"
            if artifact not in readme:
                errors.append(f"README.md does not name current {device} artifact: {artifact}")

        test_matrix = (ROOT / "docs" / "PHYSICAL_TEST_MATRIX.md").read_text(encoding="utf-8")
        if f"v{public_version}" not in test_matrix:
            errors.append("physical test matrix does not identify the current version")

    release_template = (ROOT / "release" / "README-FIRST.txt").read_text(encoding="utf-8")
    if release_template.count("@DUET_VERSION@") < 4:
        errors.append("release/README-FIRST.txt is not fully version-templated")

    for relative_path in candidate_files():
        lowered = relative_path.lower()
        name = Path(relative_path).name.lower()
        if name in BLOCKED_STATE_NAMES or lowered.endswith(BLOCKED_STATE_SUFFIXES):
            errors.append(f"private device-state artifact would be published: {relative_path}")
        if lowered.endswith(".epub") and not relative_path.startswith(ALLOWED_EPUB_PREFIXES):
            errors.append(f"ebook outside the approved test fixtures: {relative_path}")

        text = read_text(relative_path)
        if text is None:
            continue
        if LOCAL_HOME_RE.search(text) or WINDOWS_HOME_RE.search(text):
            errors.append(f"local computer home path found in: {relative_path}")
        public_prose = (
            "/" not in relative_path
            or relative_path.startswith((".github/", "docs/", "release/"))
        ) and relative_path.lower().endswith(PUBLIC_PROSE_SUFFIXES)
        phone_scan_text = SHA256_RE.sub("", text)
        if FORMATTED_PHONE_RE.search(phone_scan_text) or (
            public_prose and PLAIN_PHONE_RE.search(phone_scan_text)
        ):
            errors.append(f"phone-number-shaped text found in: {relative_path}")

    if not errors:
        warnings.append("Human review is still required for screenshots, font licenses, and git history.")

    for warning in warnings:
        print(f"WARNING: {warning}")
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        print(f"Public release audit failed with {len(errors)} issue(s).", file=sys.stderr)
        return 1

    print(f"Public release audit passed for Duet v{public_version}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
