#!/usr/bin/env python3
"""Add Duet word-location metadata to EPUB files.

The firmware uses META-INF/x-locations.json for true words/minute and
reference-page progress. This tool is intentionally conservative: it skips
EPUBs that already contain the manifest unless --force is supplied.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import posixpath
import re
import shutil
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from urllib.parse import unquote
from xml.etree import ElementTree


LOCATIONS_PATH = "META-INF/x-locations.json"
WORDS_PER_REFERENCE_PAGE = 250
WORD_RE = re.compile(r"[\w\u00c0-\uffff]+(?:['’\-][\w\u00c0-\uffff]+)*", re.UNICODE)
SCRIPT_STYLE_RE = re.compile(r"<(script|style)\b[^>]*>.*?</\1>", re.IGNORECASE | re.DOTALL)
TAG_RE = re.compile(r"<[^>]+>")


@dataclass
class SpineItem:
    index: int
    href: str
    zip_name: str
    media_type: str
    word_start: int = 0
    word_count: int = 0
    start_location: int = 0
    end_location: int = 0


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1] if "}" in tag else tag


def find_zip_name(names: list[str], requested: str) -> str | None:
    normalized = requested.lstrip("/")
    if normalized in names:
        return normalized
    folded = normalized.casefold()
    return next((name for name in names if name.casefold() == folded), None)


def normalized_zip_path(base_dir: str, href: str) -> str:
    href = unquote(href.split("#", 1)[0])
    return posixpath.normpath(posixpath.join(base_dir, href)).lstrip("./")


def container_rootfile(archive: zipfile.ZipFile, names: list[str]) -> str:
    container_name = find_zip_name(names, "META-INF/container.xml")
    if not container_name:
        raise ValueError("missing META-INF/container.xml")
    root = ElementTree.fromstring(archive.read(container_name))
    for node in root.iter():
        if local_name(node.tag) == "rootfile" and node.attrib.get("full-path"):
            return node.attrib["full-path"]
    raise ValueError("container.xml has no rootfile")


def parse_spine(archive: zipfile.ZipFile) -> list[SpineItem]:
    names = archive.namelist()
    opf_requested = container_rootfile(archive, names)
    opf_name = find_zip_name(names, opf_requested)
    if not opf_name:
        raise ValueError(f"missing package document {opf_requested}")
    root = ElementTree.fromstring(archive.read(opf_name))
    opf_dir = posixpath.dirname(opf_name)

    manifest: dict[str, tuple[str, str]] = {}
    spine_ids: list[str] = []
    for node in root.iter():
        tag = local_name(node.tag)
        if tag == "item":
            item_id = node.attrib.get("id", "")
            href = node.attrib.get("href", "")
            if item_id and href:
                manifest[item_id] = (href, node.attrib.get("media-type", ""))
        elif tag == "itemref":
            item_id = node.attrib.get("idref", "")
            if item_id:
                spine_ids.append(item_id)

    items: list[SpineItem] = []
    for item_id in spine_ids:
        if item_id not in manifest:
            continue
        href, media_type = manifest[item_id]
        if media_type not in {"application/xhtml+xml", "text/html"}:
            continue
        requested = normalized_zip_path(opf_dir, href)
        zip_name = find_zip_name(names, requested)
        if not zip_name:
            continue
        items.append(SpineItem(index=len(items), href=href, zip_name=zip_name, media_type=media_type))
    if not items:
        raise ValueError("no XHTML spine items found")
    return items


def text_word_count(data: bytes) -> int:
    text = data.decode("utf-8", errors="ignore")
    text = SCRIPT_STYLE_RE.sub(" ", text)
    text = TAG_RE.sub(" ", text)
    text = html.unescape(text)
    return len(WORD_RE.findall(text))


def build_manifest(archive: zipfile.ZipFile) -> dict[str, object]:
    spine = parse_spine(archive)
    total_words = 0
    total_locations = 0
    for item in spine:
        item.word_start = total_words
        item.word_count = text_word_count(archive.read(item.zip_name))
        total_words += item.word_count
        location_count = max(1, (item.word_count + WORDS_PER_REFERENCE_PAGE - 1) // WORDS_PER_REFERENCE_PAGE)
        item.start_location = total_locations + 1
        item.end_location = total_locations + location_count
        total_locations += location_count

    if total_words == 0:
        raise ValueError("no readable words found in spine")

    return {
        "format": "x-locations",
        "version": 1,
        "generator": "Duet scripts/enrich_epub_locations.py",
        "totalLocations": total_locations,
        "totalWords": total_words,
        "wordsPerReferencePage": WORDS_PER_REFERENCE_PAGE,
        "totalReferencePages": (total_words + WORDS_PER_REFERENCE_PAGE - 1) // WORDS_PER_REFERENCE_PAGE,
        "spine": [
            {
                "index": item.index,
                "href": item.href,
                "startLocation": item.start_location,
                "endLocation": item.end_location,
                "wordStart": item.word_start,
                "wordCount": item.word_count,
            }
            for item in spine
        ],
    }


def write_epub_with_manifest(epub_path: Path, manifest: dict[str, object]) -> None:
    with tempfile.NamedTemporaryFile(prefix=epub_path.name, suffix=".tmp", dir=epub_path.parent, delete=False) as tmp:
        tmp_path = Path(tmp.name)

    try:
        with zipfile.ZipFile(epub_path, "r") as source, zipfile.ZipFile(tmp_path, "w") as target:
            infos = source.infolist()
            for info in infos:
                if info.filename == LOCATIONS_PATH:
                    continue
                data = source.read(info.filename)
                compression = zipfile.ZIP_STORED if info.filename == "mimetype" else info.compress_type
                new_info = zipfile.ZipInfo(info.filename, date_time=info.date_time)
                new_info.compress_type = compression
                new_info.external_attr = info.external_attr
                new_info.comment = info.comment
                target.writestr(new_info, data)
            target.writestr(LOCATIONS_PATH, json.dumps(manifest, separators=(",", ":")).encode("utf-8"))
        shutil.copystat(epub_path, tmp_path)
        tmp_path.replace(epub_path)
    except Exception:
        tmp_path.unlink(missing_ok=True)
        raise


def iter_epubs(paths: list[Path]) -> list[Path]:
    out: list[Path] = []
    for path in paths:
        if path.is_dir():
            out.extend(sorted(p for p in path.rglob("*.epub") if p.is_file() and not p.name.startswith("._")))
        elif path.is_file() and path.suffix.casefold() == ".epub" and not path.name.startswith("._"):
            out.append(path)
    return out


def backup_path_for(epub_path: Path, backup_dir: Path | None) -> Path:
    if backup_dir:
        backup_dir.mkdir(parents=True, exist_ok=True)
        source_id = hashlib.sha256(str(epub_path.resolve()).encode("utf-8")).hexdigest()[:12]
        return backup_dir / f"{epub_path.name}.{source_id}.duetbak"
    return epub_path.with_suffix(epub_path.suffix + ".duetbak")


def enrich_epub(epub_path: Path, force: bool, dry_run: bool, backup: bool, backup_dir: Path | None) -> tuple[str, str]:
    try:
        with zipfile.ZipFile(epub_path, "r") as archive:
            if LOCATIONS_PATH in archive.namelist() and not force:
                return "skipped", "already has x-locations"
            manifest = build_manifest(archive)
        if dry_run:
            return "would-write", f"{manifest['totalWords']} words"
        if backup:
            backup_path = backup_path_for(epub_path, backup_dir)
            if not backup_path.exists():
                shutil.copy2(epub_path, backup_path)
        write_epub_with_manifest(epub_path, manifest)
        return "updated", f"{manifest['totalWords']} words"
    except (OSError, ValueError, zipfile.BadZipFile, ElementTree.ParseError) as exc:
        return "failed", str(exc)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="+", type=Path, help="EPUB files or folders to scan")
    parser.add_argument("--force", action="store_true", help="regenerate even when x-locations already exists")
    parser.add_argument("--backup", action="store_true", help="create a .duetbak copy before updating each EPUB")
    parser.add_argument("--backup-dir", type=Path, help="write .duetbak files to this folder instead of beside each EPUB")
    parser.add_argument("--dry-run", action="store_true", help="report what would change without writing")
    args = parser.parse_args(argv)

    epubs = iter_epubs(args.paths)
    counts = {"updated": 0, "skipped": 0, "would-write": 0, "failed": 0}
    for epub_path in epubs:
        status, detail = enrich_epub(epub_path, args.force, args.dry_run, args.backup, args.backup_dir)
        counts[status] += 1
        print(f"{status:11} {epub_path} ({detail})")

    print(
        "summary: "
        + ", ".join(f"{key}={counts[key]}" for key in ("updated", "would-write", "skipped", "failed"))
    )
    return 1 if counts["failed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
