#!/usr/bin/env python3
"""Prebuild exact Duet cover thumbnails on an XTEINK SD card."""

from __future__ import annotations

import argparse
import json
import os
import posixpath
import re
import shutil
import struct
import subprocess
import tempfile
import urllib.parse
import zipfile
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath
from xml.etree import ElementTree

from duet_storage_paths import (
    DUET_STATE_ROOT,
    DUET_THUMBS_ROOT,
    LEGACY_BOOKS_ROOT,
    LEGACY_STATE_ROOT,
    mounted_path,
)


GRID_DIMENSIONS = {
    "x3": ((193, 282), (123, 181), (93, 140)),
    "x4": ((196, 286), (125, 183), (94, 142)),
}
CAROUSEL_DIMENSIONS = ((230, 338), (160, 234))
SHARED_DIMENSIONS = ((120, 180), (123, 180))
ADAPTIVE_DIMENSIONS = ((125, 188), (296, 444), (350, 525))
HOME_CAROUSEL_DIMENSIONS = ((296, 468), (200, 390))
IMAGE_SUFFIXES = {".jpg", ".jpeg", ".png", ".webp", ".gif"}
RASTER_RECIPE = "burkes-gamma115-unsharp030-v1"
LEGACY_THUMBNAIL_PATTERN = re.compile(r"^thumb_(\d+)(?:x(\d+))?(_fit)?\.bmp$")
SHARDED_THUMBNAIL_PATTERN = re.compile(r"^(\d+)_(\d+)x(\d+)(_fit)?\.bmp$")


def fnv1a64(value: str) -> int:
    result = 14695981039346656037
    for byte in value.encode("utf-8"):
        result ^= byte
        result = (result * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return result


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1].lower()


def normalized_zip_path(base: str, href: str) -> str:
    decoded = urllib.parse.unquote(href.split("#", 1)[0])
    return posixpath.normpath(posixpath.join(base, decoded)).lstrip("/")


def find_zip_name(names: list[str], requested: str) -> str | None:
    normalized = requested.lstrip("/")
    if normalized in names:
        return normalized
    folded = normalized.casefold()
    return next((name for name in names if name.casefold() == folded), None)


def container_rootfile(archive: zipfile.ZipFile, names: list[str]) -> str:
    container_name = find_zip_name(names, "META-INF/container.xml")
    if not container_name:
        raise ValueError("missing META-INF/container.xml")
    root = ElementTree.fromstring(archive.read(container_name))
    for node in root.iter():
        if local_name(node.tag) == "rootfile" and node.attrib.get("full-path"):
            return node.attrib["full-path"]
    raise ValueError("container.xml has no rootfile")


def image_from_wrapper(
    archive: zipfile.ZipFile,
    names: list[str],
    wrapper_path: str,
) -> str | None:
    wrapper_name = find_zip_name(names, wrapper_path)
    if not wrapper_name:
        return None
    try:
        root = ElementTree.fromstring(archive.read(wrapper_name))
    except ElementTree.ParseError:
        return None
    wrapper_dir = posixpath.dirname(wrapper_name)
    for node in root.iter():
        if local_name(node.tag) not in {"img", "image"}:
            continue
        href = (
            node.attrib.get("src")
            or node.attrib.get("href")
            or node.attrib.get("{http://www.w3.org/1999/xlink}href")
        )
        if not href:
            continue
        candidate = normalized_zip_path(wrapper_dir, href)
        actual = find_zip_name(names, candidate)
        if actual and PurePosixPath(actual).suffix.lower() in IMAGE_SUFFIXES:
            return actual
    return None


def epub_cover_name(archive: zipfile.ZipFile) -> str:
    names = archive.namelist()
    opf_requested = container_rootfile(archive, names)
    opf_name = find_zip_name(names, opf_requested)
    if not opf_name:
        raise ValueError(f"missing package document {opf_requested}")
    root = ElementTree.fromstring(archive.read(opf_name))
    opf_dir = posixpath.dirname(opf_name)

    manifest: dict[str, tuple[str, str, str]] = {}
    cover_id = ""
    guide_href = ""
    spine_ids: list[str] = []
    for node in root.iter():
        tag = local_name(node.tag)
        if tag == "item":
            item_id = node.attrib.get("id", "")
            href = node.attrib.get("href", "")
            if item_id and href:
                manifest[item_id] = (
                    href,
                    node.attrib.get("media-type", ""),
                    node.attrib.get("properties", ""),
                )
        elif tag == "meta" and node.attrib.get("name", "").casefold() == "cover":
            cover_id = node.attrib.get("content", "")
        elif tag == "reference" and "cover" in node.attrib.get("type", "").casefold():
            guide_href = node.attrib.get("href", "")
        elif tag == "itemref" and node.attrib.get("idref"):
            spine_ids.append(node.attrib["idref"])

    candidates: list[str] = []
    if cover_id and cover_id in manifest:
        candidates.append(manifest[cover_id][0])
    for href, _media_type, properties in manifest.values():
        if "cover-image" in properties.split():
            candidates.append(href)
    if guide_href:
        candidates.append(guide_href)
    for item_id, (href, media_type, _properties) in manifest.items():
        if "cover" in item_id.casefold() or "cover" in PurePosixPath(href).name.casefold():
            if media_type.startswith("image/") or media_type in {"application/xhtml+xml", "text/html"}:
                candidates.append(href)

    seen: set[str] = set()
    for href in candidates:
        requested = normalized_zip_path(opf_dir, href)
        if requested in seen:
            continue
        seen.add(requested)
        actual = find_zip_name(names, requested)
        if not actual:
            continue
        if PurePosixPath(actual).suffix.lower() in IMAGE_SUFFIXES:
            return actual
        wrapped = image_from_wrapper(archive, names, actual)
        if wrapped:
            return wrapped

    # PDF-derived EPUBs sometimes omit every formal cover marker but place the
    # cover image on the first spine page. Follow the declared reading order
    # before falling back to filename heuristics.
    for item_id in spine_ids:
        item = manifest.get(item_id)
        if not item:
            continue
        href, media_type, _properties = item
        if media_type not in {"application/xhtml+xml", "text/html"}:
            continue
        wrapped = image_from_wrapper(
            archive,
            names,
            normalized_zip_path(opf_dir, href),
        )
        if wrapped:
            return wrapped

    fallback_images = [
        name
        for name in names
        if PurePosixPath(name).suffix.lower() in IMAGE_SUFFIXES
        and "cover" in PurePosixPath(name).name.casefold()
    ]
    if fallback_images:
        return max(fallback_images, key=lambda name: archive.getinfo(name).file_size)
    raise ValueError("no supported cover image found")


def valid_one_bit_bmp(path: Path, width: int, height: int) -> bool:
    try:
        with path.open("rb") as handle:
            header = handle.read(30)
        if len(header) < 30 or header[:2] != b"BM":
            return False
        bmp_width = struct.unpack_from("<i", header, 18)[0]
        bmp_height = abs(struct.unpack_from("<i", header, 22)[0])
        bits_per_pixel = struct.unpack_from("<H", header, 28)[0]
        return bmp_width == width and bmp_height == height and bits_per_pixel == 1
    except OSError:
        return False


def burkes_dither(gray: bytes, width: int, height: int) -> bytes:
    if len(gray) != width * height:
        raise ValueError(f"expected {width * height} grayscale bytes, got {len(gray)}")

    output = bytearray(width * height)
    current = [0] * width
    following = [0] * width
    kernel = ((1, 0, 8), (2, 0, 4), (-2, 1, 2), (-1, 1, 4), (0, 1, 8), (1, 1, 4), (2, 1, 2))

    for y in range(height):
        reverse = (y & 1) != 0
        x_values = range(width - 1, -1, -1) if reverse else range(width)
        row_offset = y * width
        for x in x_values:
            adjusted = max(0, min(255, gray[row_offset + x] + current[x]))
            quantized = 0 if adjusted < 128 else 255
            output[row_offset + x] = quantized
            error = adjusted - quantized
            for dx, dy, weight in kernel:
                target_x = x - dx if reverse else x + dx
                if target_x < 0 or target_x >= width:
                    continue
                target_row = current if dy == 0 else following
                target_row[target_x] += int((error * weight * 78) / 3200)
        current, following = following, [0] * width

    return bytes(output)


def write_one_bit_bmp(path: Path, pixels: bytes, width: int, height: int) -> None:
    if len(pixels) != width * height:
        raise ValueError(f"expected {width * height} dithered pixels, got {len(pixels)}")

    row_size = ((width + 31) // 32) * 4
    image_size = row_size * height
    pixel_offset = 14 + 40 + 8
    file_size = pixel_offset + image_size
    file_header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, pixel_offset)
    info_header = struct.pack(
        "<IiiHHIIiiII",
        40,
        width,
        height,
        1,
        1,
        0,
        image_size,
        2835,
        2835,
        2,
        2,
    )
    palette = b"\x00\x00\x00\x00\xff\xff\xff\x00"

    with path.open("wb") as handle:
        handle.write(file_header)
        handle.write(info_header)
        handle.write(palette)
        for y in range(height - 1, -1, -1):
            packed = bytearray(row_size)
            row_offset = y * width
            for x in range(width):
                if pixels[row_offset + x] >= 128:
                    packed[x // 8] |= 1 << (7 - (x % 8))
            handle.write(packed)


def render_thumbnail(
    magick: str,
    source: Path,
    destination: Path,
    width: int,
    height: int,
    adaptive_contain: bool,
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(".bmp.tmp")
    resize_geometry = f"{width}x{height}" if adaptive_contain else f"{width}x{height}^"
    command = [
        magick,
        str(source),
        "-auto-orient",
        "-background",
        "white",
        "-alpha",
        "remove",
        "-alpha",
        "off",
        "-colorspace",
        "Gray",
        "-resize",
        resize_geometry,
        "-background",
        "white",
        "-gravity",
        "center",
        "-extent",
        f"{width}x{height}",
        "-gamma",
        "1.15",
        "-unsharp",
        "0x0.45+0.3+0.02",
        "-depth",
        "8",
        "gray:-",
    ]
    gray = subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    pixels = burkes_dither(gray, width, height)
    write_one_bit_bmp(temporary, pixels, width, height)
    if not valid_one_bit_bmp(temporary, width, height):
        temporary.unlink(missing_ok=True)
        raise ValueError(f"ImageMagick produced an invalid {width}x{height} 1-bit BMP")
    os.replace(temporary, destination)


def card_path_for(book: Path, card_root: Path) -> str:
    return "/" + book.relative_to(card_root).as_posix()


def thumbnail_shard_key(cache_hash: str) -> str:
    return cache_hash[-2:].rjust(2, "0")


def thumbnail_path_for_hash(
    card_root: Path,
    cache_hash: str,
    width: int,
    height: int,
    adaptive_contain: bool,
) -> Path:
    fit_suffix = "_fit" if adaptive_contain else ""
    return (
        mounted_path(card_root, DUET_THUMBS_ROOT)
        / thumbnail_shard_key(cache_hash)
        / f"{cache_hash}_{width}x{height}{fit_suffix}.bmp"
    )


def thumbnail_path(
    card_root: Path,
    book_path: str,
    width: int,
    height: int,
    adaptive_contain: bool,
) -> Path:
    cache_hash = str(fnv1a64(book_path))
    return thumbnail_path_for_hash(
        card_root,
        cache_hash,
        width,
        height,
        adaptive_contain,
    )


def cleanup_legacy_thumbnails(
    card_root: Path,
    known_cache_hashes: set[str],
    dry_run: bool,
) -> dict[str, int]:
    """Move legacy thumbnails into shards without touching reader state."""
    cache_root = mounted_path(card_root, LEGACY_BOOKS_ROOT)
    result = {
        "eligible_cache_dirs": 0,
        "migrated_thumbnail_files": 0,
        "removed_duplicate_thumbnail_files": 0,
        "preserved_invalid_thumbnail_files": 0,
        "preserved_conflicting_thumbnail_files": 0,
        "removed_empty_cache_dirs": 0,
        "removed_orphan_thumbnail_files": 0,
        "removed_orphan_cache_dirs": 0,
        "preserved_stateful_cache_dirs": 0,
        "migrated_sharded_thumbnail_files": 0,
        "removed_sharded_duplicate_files": 0,
        "removed_appledouble_files": 0,
        "removed_empty_shard_dirs": 0,
        "preserved_invalid_sharded_files": 0,
        "preserved_conflicting_sharded_files": 0,
    }
    if cache_root.is_dir():
        for cache_hash in sorted(known_cache_hashes):
            cache_dir = cache_root / f"epub_{cache_hash}"
            if not cache_dir.is_dir():
                continue

            legacy_thumbnails: list[tuple[Path, re.Match[str]]] = []
            for entry in cache_dir.iterdir():
                match = LEGACY_THUMBNAIL_PATTERN.fullmatch(entry.name) if entry.is_file() else None
                if match:
                    legacy_thumbnails.append((entry, match))
            if not legacy_thumbnails:
                continue

            result["eligible_cache_dirs"] += 1
            removable_thumbnails: set[Path] = set()
            for thumbnail, match in legacy_thumbnails:
                first_dimension = int(match.group(1))
                second_dimension = match.group(2)
                if second_dimension is None:
                    height = first_dimension
                    width = (height * 2 + 1) // 3
                else:
                    width = first_dimension
                    height = int(second_dimension)
                adaptive_contain = bool(match.group(3))

                if not valid_one_bit_bmp(thumbnail, width, height):
                    result["preserved_invalid_thumbnail_files"] += 1
                    continue

                destination = thumbnail_path_for_hash(
                    card_root,
                    cache_hash,
                    width,
                    height,
                    adaptive_contain,
                )
                if destination.exists():
                    if not valid_one_bit_bmp(destination, width, height):
                        result["preserved_conflicting_thumbnail_files"] += 1
                        continue
                    result["removed_duplicate_thumbnail_files"] += 1
                    removable_thumbnails.add(thumbnail)
                    if not dry_run:
                        thumbnail.unlink()
                    continue

                result["migrated_thumbnail_files"] += 1
                removable_thumbnails.add(thumbnail)
                if not dry_run:
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    os.replace(thumbnail, destination)

            remaining_entries = [entry for entry in cache_dir.iterdir() if entry not in removable_thumbnails]
            if remaining_entries:
                result["preserved_stateful_cache_dirs"] += 1
                continue

            result["removed_empty_cache_dirs"] += 1
            if not dry_run:
                cache_dir.rmdir()

        # Older prefill versions used a different book-path hash. Those
        # directories are unreachable by current firmware, but keep any
        # directory containing reader state or an unfamiliar file. Pure
        # thumbnail directories are disposable derived cache.
        for cache_dir in cache_root.glob("epub_*"):
            if not cache_dir.is_dir() or cache_dir.name.removeprefix("epub_") in known_cache_hashes:
                continue
            entries = list(cache_dir.iterdir())
            if not entries or not all(
                entry.is_file() and LEGACY_THUMBNAIL_PATTERN.fullmatch(entry.name) for entry in entries
            ):
                continue
            result["removed_orphan_thumbnail_files"] += len(entries)
            result["removed_orphan_cache_dirs"] += 1
            if not dry_run:
                for entry in entries:
                    entry.unlink()
                cache_dir.rmdir()

    # Accept both the one-digit pre-Alpha-4 shards and Alpha 4's two-digit
    # shards. Migrate only known EPUB hashes; unfamiliar files may belong to
    # XTC or a library outside --books-root and are deliberately preserved.
    thumbs_root = mounted_path(card_root, f"{LEGACY_STATE_ROOT}/thumbs")
    if thumbs_root.is_dir():
        legacy_shards = [
            path
            for path in thumbs_root.iterdir()
            if path.is_dir() and len(path.name) in {1, 2} and path.name.isdigit()
        ]
        for legacy_shard in legacy_shards:
            for thumbnail in list(legacy_shard.iterdir()):
                match = (
                    SHARDED_THUMBNAIL_PATTERN.fullmatch(thumbnail.name)
                    if thumbnail.is_file()
                    else None
                )
                if not match:
                    continue
                cache_hash = match.group(1)
                if cache_hash not in known_cache_hashes:
                    continue
                width = int(match.group(2))
                height = int(match.group(3))
                adaptive_contain = bool(match.group(4))
                if not valid_one_bit_bmp(thumbnail, width, height):
                    result["preserved_invalid_sharded_files"] += 1
                    continue

                destination = thumbnail_path_for_hash(
                    card_root,
                    cache_hash,
                    width,
                    height,
                    adaptive_contain,
                )
                if destination.exists():
                    if not valid_one_bit_bmp(destination, width, height):
                        result["preserved_conflicting_sharded_files"] += 1
                        continue
                    result["removed_sharded_duplicate_files"] += 1
                    if not dry_run:
                        thumbnail.unlink()
                    continue

                result["migrated_sharded_thumbnail_files"] += 1
                if not dry_run:
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    os.replace(thumbnail, destination)

            if not any(legacy_shard.iterdir()):
                result["removed_empty_shard_dirs"] += 1
                if not dry_run:
                    legacy_shard.rmdir()

        # macOS may create AppleDouble metadata while new shard directories
        # are made, so remove sidecars after all migration writes, not before.
        for sidecar in thumbs_root.rglob("._*"):
            if not sidecar.is_file():
                continue
            result["removed_appledouble_files"] += 1
            if not dry_run:
                sidecar.unlink()

    return result


def legacy_thumbnail_source(
    card_root: Path,
    cache_hash: str,
    width: int,
    height: int,
    adaptive_contain: bool,
) -> Path | None:
    fit_suffix = "_fit" if adaptive_contain else ""
    filename = f"{cache_hash}_{width}x{height}{fit_suffix}.bmp"
    candidates = (
        mounted_path(card_root, f"{LEGACY_STATE_ROOT}/thumbs")
        / thumbnail_shard_key(cache_hash)
        / filename,
        mounted_path(card_root, LEGACY_BOOKS_ROOT)
        / f"epub_{cache_hash}"
        / f"thumb_{width}x{height}{fit_suffix}.bmp",
    )
    return next(
        (candidate for candidate in candidates if valid_one_bit_bmp(candidate, width, height)),
        None,
    )


def copy_legacy_thumbnail(
    source: Path,
    destination: Path,
    width: int,
    height: int,
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(".bmp.duet-import.tmp")
    temporary.unlink(missing_ok=True)
    shutil.copyfile(source, temporary)
    if not valid_one_bit_bmp(temporary, width, height):
        temporary.unlink(missing_ok=True)
        raise ValueError(f"Legacy thumbnail copy failed validation: {source}")
    os.replace(temporary, destination)


def find_books(card_root: Path, books_root: Path) -> list[Path]:
    books: list[Path] = []
    for path in books_root.rglob("*"):
        if not path.is_file() or path.suffix.casefold() != ".epub":
            continue
        relative_parts = path.relative_to(card_root).parts
        if any(part.startswith(".") for part in relative_parts):
            continue
        books.append(path)
    return sorted(books, key=lambda path: path.as_posix().casefold())


def parse_cover_overrides(values: list[str]) -> list[tuple[str, Path]]:
    overrides: list[tuple[str, Path]] = []
    for value in values:
        match, separator, image_path = value.partition("=")
        if not separator or not match.strip() or not image_path.strip():
            raise SystemExit("--cover-override must use MATCH=IMAGE_PATH")
        source = Path(image_path).expanduser().resolve()
        if not source.is_file():
            raise SystemExit(f"Cover override does not exist: {source}")
        overrides.append((match.strip().casefold(), source))
    return overrides


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("card_root", type=Path, help="Mounted SD-card root")
    parser.add_argument("--device", required=True, choices=sorted(GRID_DIMENSIONS), help="Target XTEINK model")
    parser.add_argument(
        "--books-root",
        type=Path,
        help="Book directory to scan; defaults to CARD_ROOT/Books when present, otherwise CARD_ROOT",
    )
    parser.add_argument("--force", action="store_true", help="Regenerate valid existing exact-size thumbnails")
    parser.add_argument("--dry-run", action="store_true", help="Report work without writing thumbnails")
    parser.add_argument(
        "--cleanup-legacy-thumbnails",
        action="store_true",
        help=(
            "Explicitly move valid legacy thumbnails from /.crosspoint and /.crossink into /.duet, "
            "remove verified duplicates, and remove a legacy cache directory only when empty"
        ),
    )
    parser.add_argument(
        "--cleanup-legacy-thumbnails-only",
        action="store_true",
        help=(
            "Run only the legacy-thumbnail migration/cleanup pass; do not parse EPUBs or generate new thumbnails"
        ),
    )
    parser.add_argument(
        "--cover-override",
        action="append",
        default=[],
        metavar="MATCH=IMAGE_PATH",
        help="Use an external image for card book paths containing MATCH; repeat as needed",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.cleanup_legacy_thumbnails_only:
        args.cleanup_legacy_thumbnails = True
    card_root = args.card_root.expanduser().resolve()
    if not card_root.is_dir():
        raise SystemExit(f"Card root does not exist: {card_root}")
    books_root = (args.books_root or (card_root / "Books" if (card_root / "Books").is_dir() else card_root)).resolve()
    try:
        books_root.relative_to(card_root)
    except ValueError as exc:
        raise SystemExit("--books-root must be inside the mounted card") from exc

    magick = shutil.which("magick")
    if not args.cleanup_legacy_thumbnails_only and not magick:
        raise SystemExit("ImageMagick's `magick` command is required")

    targets = [
        *((width, height, False) for width, height in GRID_DIMENSIONS[args.device]),
        *((width, height, False) for width, height in CAROUSEL_DIMENSIONS),
        *((width, height, False) for width, height in SHARED_DIMENSIONS),
        *((width, height, False) for width, height in HOME_CAROUSEL_DIMENSIONS),
        *((width, height, True) for width, height in ADAPTIVE_DIMENSIONS),
    ]
    books = find_books(card_root, books_root)
    cover_overrides = parse_cover_overrides(args.cover_override)
    report: dict[str, object] = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "device": args.device,
        "card_root": str(card_root),
        "books_root": "/" + books_root.relative_to(card_root).as_posix(),
        "dimensions": [[width, height] for width, height, _adaptive in targets],
        "raster_recipe": RASTER_RECIPE,
        "cleanup_only": args.cleanup_legacy_thumbnails_only,
        "targets": [
            {"width": width, "height": height, "fit": adaptive}
            for width, height, adaptive in targets
        ],
        "book_count": len(books),
        "generated": 0,
        "valid_existing": 0,
        "legacy_exact_copied": 0,
        "legacy_exact_available": 0,
        "override_matches": 0,
        "failed_books": [],
        "legacy_cleanup": {
            "requested": args.cleanup_legacy_thumbnails,
            "eligible_cache_dirs": 0,
            "migrated_thumbnail_files": 0,
            "removed_duplicate_thumbnail_files": 0,
            "preserved_invalid_thumbnail_files": 0,
            "preserved_conflicting_thumbnail_files": 0,
            "removed_empty_cache_dirs": 0,
            "removed_orphan_thumbnail_files": 0,
            "removed_orphan_cache_dirs": 0,
            "preserved_stateful_cache_dirs": 0,
            "migrated_sharded_thumbnail_files": 0,
            "removed_sharded_duplicate_files": 0,
            "removed_appledouble_files": 0,
            "removed_empty_shard_dirs": 0,
            "preserved_invalid_sharded_files": 0,
            "preserved_conflicting_sharded_files": 0,
        },
    }
    known_cache_hashes: set[str] = set()

    for book in books:
        device_book_path = card_path_for(book, card_root)
        known_cache_hashes.add(str(fnv1a64(device_book_path)))

    if not args.cleanup_legacy_thumbnails_only:
        with tempfile.TemporaryDirectory(prefix="duet-covers-") as temporary_dir:
            temporary_root = Path(temporary_dir)
            for index, book in enumerate(books, start=1):
                device_book_path = card_path_for(book, card_root)
                pending = [
                    (
                        width,
                        height,
                        adaptive,
                        thumbnail_path(card_root, device_book_path, width, height, adaptive),
                    )
                    for width, height, adaptive in targets
                ]
                if not args.force:
                    unresolved: list[tuple[int, int, bool, Path]] = []
                    for width, height, adaptive, destination in pending:
                        if valid_one_bit_bmp(destination, width, height):
                            report["valid_existing"] = int(report["valid_existing"]) + 1
                            continue
                        legacy_source = legacy_thumbnail_source(
                            card_root,
                            str(fnv1a64(device_book_path)),
                            width,
                            height,
                            adaptive,
                        )
                        if legacy_source is None:
                            unresolved.append((width, height, adaptive, destination))
                            continue
                        if args.dry_run:
                            report["legacy_exact_available"] = int(report["legacy_exact_available"]) + 1
                        else:
                            copy_legacy_thumbnail(
                                legacy_source,
                                destination,
                                width,
                                height,
                            )
                            report["legacy_exact_copied"] = int(report["legacy_exact_copied"]) + 1
                        report["valid_existing"] = int(report["valid_existing"]) + 1
                    pending = unresolved
                if not pending:
                    print(f"[{index}/{len(books)}] ready: {device_book_path}")
                    continue
                if args.dry_run:
                    print(f"[{index}/{len(books)}] would generate {len(pending)}: {device_book_path}")
                    continue

                try:
                    source = next(
                        (
                            override_path
                            for match, override_path in cover_overrides
                            if match in device_book_path.casefold()
                        ),
                        None,
                    )
                    extracted_source = source is None
                    if source is None:
                        with zipfile.ZipFile(book) as archive:
                            cover_name = epub_cover_name(archive)
                            suffix = PurePosixPath(cover_name).suffix.lower() or ".img"
                            source = temporary_root / f"cover-{index}{suffix}"
                            source.write_bytes(archive.read(cover_name))
                    else:
                        report["override_matches"] = int(report["override_matches"]) + 1
                    for width, height, adaptive, destination in pending:
                        render_thumbnail(
                            magick,
                            source,
                            destination,
                            width,
                            height,
                            adaptive,
                        )
                        report["generated"] = int(report["generated"]) + 1
                    if extracted_source:
                        source.unlink(missing_ok=True)
                    print(f"[{index}/{len(books)}] generated {len(pending)}: {device_book_path}")
                except (OSError, ValueError, zipfile.BadZipFile, subprocess.CalledProcessError) as exc:
                    report["failed_books"].append({"path": device_book_path, "error": str(exc)})
                    print(f"[{index}/{len(books)}] FAILED: {device_book_path}: {exc}")

    if args.cleanup_legacy_thumbnails:
        cleanup = cleanup_legacy_thumbnails(card_root, known_cache_hashes, args.dry_run)
        report["legacy_cleanup"] = {"requested": True, **cleanup}

    report_path = mounted_path(card_root, f"{DUET_STATE_ROOT}/desktop_cover_prefill.json")
    if not args.dry_run:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 1 if report["failed_books"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
