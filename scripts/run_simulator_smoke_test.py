#!/usr/bin/env python3
"""Build and run the simulator smoke test against an isolated fs_ directory."""

from __future__ import annotations

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

from duet_storage_paths import (
    DUET_BOOKS_ROOT,
    DUET_STATE_ROOT,
    DUET_THUMBS_ROOT,
    canonical_book_cache_path,
    mounted_path,
    stable_book_cache_identity,
)
from prefill_cover_thumbnails import epub_cover_name, render_thumbnail


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BOOK = ROOT / "test" / "epubs" / "test_reader_rendering_matrix.epub"
SIMULATOR_ENVS = {"x3": "simulator-x3", "x4": "simulator"}
CRASH_PATTERNS = (
    "std::bad_alloc",
    "terminating due to uncaught exception",
    "Assertion failed",
    "Segmentation fault",
    "AddressSanitizer",
    "UndefinedBehaviorSanitizer",
)
THEMES = {
    "classic": 0,
    "lyra": 1,
    "lyra-extended": 2,
    "lyra_extended": 2,
    "lyra3": 2,
    "lyra-3-covers": 2,
    "roundedraff": 3,
    "rounded-raff": 3,
    "lyra-carousel": 4,
    "lyra_carousel": 4,
    "carousel": 4,
    "minimal": 5,
    "dashboard": 6,
    "reading-home": 7,
    "reading_home": 7,
}
FILE_BROWSER_DISPLAYS = {
    "one-line": 0,
    "two-lines": 1,
    "covers": 2,
    "carousel": 3,
}
FILE_BROWSER_GRID_LAYOUTS = {"2x2": 0, "3x3": 1, "4x4": 2}


def platformio_cli() -> str:
    override = os.environ.get("PLATFORMIO_CLI")
    if override:
        candidate = Path(override).expanduser()
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
        raise SystemExit(f"PLATFORMIO_CLI is not executable: {candidate}")

    discovered = shutil.which("pio")
    if discovered:
        return discovered

    managed = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    if managed.is_file() and os.access(managed, os.X_OK):
        return str(managed)

    raise SystemExit("PlatformIO CLI not found. Install `pio` or set PLATFORMIO_CLI to its executable path.")


def read_epub_metadata(source: Path) -> tuple[str, str]:
    """Read the OPF title/author so More Info tests use real book identity."""
    try:
        with zipfile.ZipFile(source) as archive:
            container = ET.fromstring(archive.read("META-INF/container.xml"))
            rootfile = container.find(".//{*}rootfile")
            if rootfile is None:
                return source.stem, ""
            opf_path = rootfile.attrib.get("full-path", "")
            package = ET.fromstring(archive.read(opf_path))
            title = package.findtext(".//{http://purl.org/dc/elements/1.1/}title", default="").strip()
            author = package.findtext(".//{http://purl.org/dc/elements/1.1/}creator", default="").strip()
            return title or source.stem, author
    except (KeyError, OSError, ET.ParseError, zipfile.BadZipFile):
        return source.stem, ""


def simulator_program(device: str) -> Path:
    return ROOT / ".pio" / "build" / SIMULATOR_ENVS[device] / "program"


def build_simulator(device: str) -> None:
    environment = SIMULATOR_ENVS[device]
    print(f"Building {device.upper()} simulator...", flush=True)
    proc = subprocess.run([platformio_cli(), "run", "-e", environment], cwd=ROOT)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)


def prepare_fs(temp_root: Path, book: Path, additional_books: list[Path]) -> str:
    books_dir = temp_root / "fs_" / "books"
    books_dir.mkdir(parents=True, exist_ok=True)

    for source in [book, *additional_books]:
        target = books_dir / source.name
        shutil.copy2(source, target)
    return f"/books/{book.name}"


def expand_first_chapter_for_preview_smoke(temp_root: Path, device_path: str) -> None:
    """Make the isolated first chapter long enough to prove preview truncation."""
    epub_path = temp_root / "fs_" / device_path.removeprefix("/")
    tmp_path = epub_path.with_suffix(".preview-smoke.epub")
    with zipfile.ZipFile(epub_path) as source:
        entries = [(info, source.read(info.filename)) for info in source.infolist()]

    target_index = next(
        (index for index, (info, _) in enumerate(entries) if info.filename.endswith("/chapter1.xhtml")),
        None,
    )
    if target_index is None:
        # Real-book regression fixtures already contain a long chapter and do
        # not use the synthetic fixture's chapter1.xhtml naming convention.
        # The page-count assertion below still proves that completion replaced
        # the bounded preview with a longer persistent layout.
        return

    info, raw_html = entries[target_index]
    html = raw_html.decode("utf-8")
    repeated_text = " ".join(
        [
            "This deliberately extended chapter paragraph exercises visible page first layout",
            "while preserving headings punctuation italics and ordinary prose rhythm",
        ]
        * 4
    )
    extra = "".join(f"<p>{index + 1}. {repeated_text}</p>" for index in range(12))
    html = html.replace("</body>", f"{extra}</body>", 1)
    entries[target_index] = (info, html.encode("utf-8"))

    with zipfile.ZipFile(tmp_path, "w") as target:
        for entry_info, data in entries:
            compression = zipfile.ZIP_STORED if entry_info.filename == "mimetype" else entry_info.compress_type
            target.writestr(entry_info, data, compress_type=compression)
    tmp_path.replace(epub_path)


def stage_cover_browser_books(temp_root: Path, source: Path, additional_sources: list[Path], minimum: int = 18) -> list[Path]:
    """Ensure every cover-browser density can cross a page using real staged files."""
    books_dir = temp_root / "fs_" / "books"
    staged = [Path(source.name), *(Path(item.name) for item in additional_sources)]
    occupied_names = {item.name for item in staged}
    copy_index = 1
    while len(staged) < minimum:
        name = f"smoke-cover-{copy_index:02d}.epub"
        copy_index += 1
        if name in occupied_names:
            continue
        shutil.copy2(source, books_dir / name)
        staged.append(Path(name))
        occupied_names.add(name)
    return staged


def stage_reading_home_books(
    temp_root: Path, source: Path, additional_sources: list[Path]
) -> tuple[list[Path], list[str]]:
    """Create a four-book recent shelf, preferring distinct real fixtures."""
    books_dir = temp_root / "fs_" / "books"
    candidates = [source, *additional_sources]
    fixture_sources = [Path(item.name) for item in candidates[:4]]
    device_paths = [f"/books/{item.name}" for item in candidates[:4]]
    for index in range(len(fixture_sources), 4):
        name = f"smoke-reading-home-{index}.epub"
        shutil.copy2(source, books_dir / name)
        fixture_sources.append(Path(name))
        device_paths.append(f"/books/{name}")
    return fixture_sources, device_paths


def stage_font_families(temp_root: Path, family_sources: list[Path]) -> None:
    fonts_root = temp_root / "fs_" / ".fonts"
    fonts_root.mkdir(parents=True, exist_ok=True)
    for source in family_sources:
        shutil.copytree(source, fonts_root / source.name)


def stage_dictionaries(temp_root: Path, dictionaries_root: Path) -> None:
    shutil.copytree(dictionaries_root, temp_root / "fs_" / "dictionaries")


def fnv1a64(value: str) -> int:
    result = 14695981039346656037
    for byte in value.encode("utf-8"):
        result ^= byte
        result = (result * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return result


def thumbnail_shard_key(cache_hash: str) -> str:
    return cache_hash[-2:].rjust(2, "0")


def write_cover_thumbnail(path: Path, seed: int, width: int = 123, height: int = 180) -> None:
    row_bytes = ((width + 31) // 32) * 4
    pixels = bytearray([0xFF] * (row_bytes * height))
    for file_y in range(height):
        display_y = height - 1 - file_y
        for x in range(width):
            border = x < 3 or x >= width - 3 or display_y < 3 or display_y >= height - 3
            title_rule = 25 + seed * 5 <= display_y < 29 + seed * 5 and 15 <= x < width - 15
            motif = (x + display_y + seed * 13) % 47 < 2 and 12 <= x < width - 12
            if border or title_rule or motif:
                pixels[file_y * row_bytes + (x >> 3)] &= ~(0x80 >> (x & 7))

    pixel_offset = 14 + 40 + 8
    file_size = pixel_offset + len(pixels)
    file_header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, pixel_offset)
    dib_header = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 1, 0, len(pixels), 2835, 2835, 2, 0)
    palette = b"\x00\x00\x00\x00\xff\xff\xff\x00"
    path.write_bytes(file_header + dib_header + palette + pixels)


def extract_real_cover(source: Path, destination_root: Path, index: int) -> Path:
    with zipfile.ZipFile(source) as archive:
        cover_name = epub_cover_name(archive)
        suffix = Path(cover_name).suffix.lower() or ".img"
        destination = destination_root / f"cover-{index:02d}{suffix}"
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(archive.read(cover_name))
        return destination


def write_real_cover_variants(
    temp_root: Path,
    source: Path,
    cache_hash: str,
    cache_dir: Path,
    index: int,
    device: str,
) -> None:
    magick = shutil.which("magick")
    if not magick:
        raise RuntimeError("ImageMagick is required for real-cover media fixtures")

    source_cover = extract_real_cover(source, temp_root / "cover-sources", index)
    dimensions = {
        "x3": ((193, 282, False), (123, 181, False), (93, 140, False)),
        "x4": ((196, 286, False), (125, 183, False), (94, 142, False)),
    }[device]
    dimensions = (
        *dimensions,
        (120, 180, False),
        (123, 180, False),
        (160, 234, False),
        (230, 338, False),
        (125, 188, True),
        (296, 444, True),
        (350, 525, True),
        (296, 468, False),
        (200, 390, False),
    )
    thumb_dir = mounted_path(temp_root / "fs_", DUET_THUMBS_ROOT) / thumbnail_shard_key(cache_hash)
    thumb_dir.mkdir(parents=True, exist_ok=True)
    for width, height, adaptive_contain in dimensions:
        fit_suffix = "_fit" if adaptive_contain else ""
        sharded = thumb_dir / f"{cache_hash}_{width}x{height}{fit_suffix}.bmp"
        render_thumbnail(magick, source_cover, sharded, width, height, adaptive_contain)
        shutil.copy2(sharded, cache_dir / f"thumb_{width}x{height}{fit_suffix}.bmp")


def write_cover_signal_fixtures(
    temp_root: Path, sources: list[Path], device: str, real_covers: bool = False
) -> None:
    indexed_stats: list[tuple[int, int, int, int]] = []
    books_dir = temp_root / "fs_" / "books"
    for index, source in enumerate(sources):
        device_path = f"/books/{source.name}"
        cache_hash = str(fnv1a64(device_path))
        cache_path = canonical_book_cache_path(cache_hash)
        cache_dir = mounted_path(temp_root / "fs_", cache_path)
        cache_dir.mkdir(parents=True, exist_ok=True)
        if real_covers:
            write_real_cover_variants(temp_root, books_dir / source.name, cache_hash, cache_dir, index, device)
        else:
            write_cover_thumbnail(cache_dir / "thumb_123x180.bmp", index)
            write_cover_thumbnail(cache_dir / "thumb_230x338.bmp", index, 230, 338)
            write_cover_thumbnail(cache_dir / "thumb_125x188_fit.bmp", index, 125, 188)
            thumb_dir = mounted_path(temp_root / "fs_", DUET_THUMBS_ROOT) / thumbnail_shard_key(cache_hash)
            thumb_dir.mkdir(parents=True, exist_ok=True)
            # Current firmware reads the sharded cache directly. Include both
            # carousel source sizes so the navigation smoke test can verify that
            # already-loaded artwork survives a role change instead of silently
            # exercising obsolete in-cache thumbnail paths.
            write_cover_thumbnail(thumb_dir / f"{cache_hash}_123x180.bmp", index)
            write_cover_thumbnail(thumb_dir / f"{cache_hash}_160x234.bmp", index, 160, 234)
            write_cover_thumbnail(thumb_dir / f"{cache_hash}_230x338.bmp", index, 230, 338)
        reading_seconds = (42 + index * 18) * 60 if index < 2 else 0
        sessions = 3 + index if index < 2 else 0
        flags = (1 if index == 0 else 0) | (2 if index == 1 else 0)
        indexed_stats.append((fnv1a64(stable_book_cache_identity(cache_path)), reading_seconds, sessions, flags))
        if index >= 2:
            continue
        stats = bytearray(83)
        stats[0] = 6
        stats[1:3] = struct.pack("<H", sessions)
        stats[3:7] = struct.pack("<I", reading_seconds)
        stats[7:11] = struct.pack("<I", 54 + index * 21)
        stats[11] = 1 if index == 0 else 0
        stats[73:77] = struct.pack("<I", (42 + index * 18) * 60)
        stats[77:81] = struct.pack("<I", (18 + index * 7) * 60)
        stats[81:83] = struct.pack("<H", 19 + index * 8)
        (cache_dir / "stats_v6.bin").write_bytes(stats)
        if index == 1:
            (cache_dir / "progress.bin").write_bytes(struct.pack("<HHH", 0, 0, 2))

    index_path = mounted_path(temp_root / "fs_", f"{DUET_STATE_ROOT}/library_book_stats_v1.bin")
    index_path.parent.mkdir(parents=True, exist_ok=True)
    indexed_stats.sort(key=lambda entry: entry[0])
    index_data = bytearray(struct.pack("<IBBH", 0x5844494C, 1, 0, len(indexed_stats)))
    for cache_key, reading_seconds, sessions, flags in indexed_stats:
        index_data.extend(struct.pack("<QIHB", cache_key, reading_seconds, sessions, flags))
    index_path.write_bytes(index_data)


def write_dashboard_home_fixture(temp_root: Path, device_path: str) -> None:
    cache_dir = mounted_path(
        temp_root / "fs_",
        canonical_book_cache_path(fnv1a64(device_path)),
    )
    cache_dir.mkdir(parents=True, exist_ok=True)
    # Dashboard asks the EPUB cache for this contained cover before falling
    # back to the ordinary thumbnail path.
    cover_path = cache_dir / "thumb_296x444_fit.bmp"
    if not cover_path.exists():
        write_cover_thumbnail(cover_path, 9, 296, 444)
    (cache_dir / "progress_pct.bin").write_bytes(bytes((1, 6400 & 0xFF, 6400 >> 8)))


def write_reading_home_fixture(temp_root: Path, device_paths: list[str]) -> None:
    progress_basis_points = (6400, 3800, 1700, 8200)
    for index, device_path in enumerate(device_paths):
        cache_dir = mounted_path(
            temp_root / "fs_",
            canonical_book_cache_path(fnv1a64(device_path)),
        )
        cache_dir.mkdir(parents=True, exist_ok=True)
        (cache_dir / "progress.bin").write_bytes(struct.pack("<HHH", 0, index % 2, 2))
        (cache_dir / "progress_pct.bin").write_bytes(
            bytes((1, progress_basis_points[index] & 0xFF, progress_basis_points[index] >> 8))
        )

    # Global stats v4: only the fields shown by Reading Home need fixture values.
    stats = bytearray(163)
    stats[0] = 4
    stats[1:5] = struct.pack("<I", 325)
    stats[5:9] = struct.pack("<I", 23 * 3600 + 52 * 60)
    stats[159:163] = struct.pack("<I", 21 * 3600 + 11 * 60)
    stats_path = mounted_path(temp_root / "fs_", f"{DUET_STATE_ROOT}/global_stats.bin")
    stats_path.parent.mkdir(parents=True, exist_ok=True)
    stats_path.write_bytes(stats)


def write_progress_fixture(temp_root: Path, device_path: str, spine_index: int) -> None:
    cache_dir = mounted_path(
        temp_root / "fs_",
        canonical_book_cache_path(fnv1a64(device_path)),
    )
    cache_dir.mkdir(parents=True, exist_ok=True)
    (cache_dir / "progress.bin").write_bytes(struct.pack("<HHH", spine_index, 0, 0))


def write_book_info_catalog_fixture(temp_root: Path, device_path: str) -> None:
    catalog = mounted_path(temp_root / "fs_", f"{DUET_STATE_ROOT}/library_catalog.tsv")
    catalog.parent.mkdir(parents=True, exist_ok=True)
    cache_dir = mounted_path(
        temp_root / "fs_",
        canonical_book_cache_path(fnv1a64(device_path)),
    )
    cache_dir.mkdir(parents=True, exist_ok=True)
    # Exercise Book Info's deferred, raw 1-bit thumbnail path. The immediate
    # screenshot must still draw its shell before this bitmap is decoded.
    cover_path = cache_dir / "thumb_120x180.bmp"
    if not cover_path.exists():
        write_cover_thumbnail(cover_path, 7, 120, 180)
    description = (
        "Alice follows a hurried White Rabbit into Wonderland, where riddles, strange rules, "
        "and unforgettable characters turn an ordinary afternoon into a surreal adventure."
    )
    catalog.write_text(
        "M\t2\t1\t1\t0\t1\t1\n"
        "A\t0\tLewis Carroll\n"
        "G\t0\tClassic Fantasy\n"
        "P\t0\tNo romance\n"
        f"B\t1\t0\t-1\t0\t0\t2\t{device_path}\tAlice's Adventures in Wonderland\t{description}\n",
        encoding="utf-8",
    )


def write_dictionary_fixture(temp_root: Path) -> None:
    dictionary_dir = temp_root / "fs_" / "dictionaries" / "English"
    dictionary_dir.mkdir(parents=True, exist_ok=True)

    definitions = {
        "book": "noun\nA written work published as pages or in electronic form.",
        "e-reader": "noun\nA portable electronic device designed for reading digital books.",
        "serendipity": "noun\nThe occurrence of a fortunate discovery by chance.",
    }
    dictionary_data = bytearray()
    index_data = bytearray()
    for headword in sorted(definitions, key=str.casefold):
        encoded_definition = definitions[headword].encode("utf-8")
        offset = len(dictionary_data)
        dictionary_data.extend(encoded_definition)
        index_data.extend(headword.encode("utf-8"))
        index_data.append(0)
        index_data.extend(struct.pack(">II", offset, len(encoded_definition)))

    stem = dictionary_dir / "wordnet-smoke"
    stem.with_suffix(".dict").write_bytes(dictionary_data)
    stem.with_suffix(".idx").write_bytes(index_data)
    stem.with_suffix(".ifo").write_text(
        "StarDict's dict ifo file\n"
        "version=2.4.2\n"
        f"wordcount={len(definitions)}\n"
        f"idxfilesize={len(index_data)}\n"
        "bookname=WordNet Smoke Dictionary\n"
        "lang=en\n"
        "sametypesequence=m\n",
        encoding="utf-8",
    )


def run_smoke(args: argparse.Namespace) -> int:
    if args.verify_chapter_transition and args.start_spine is None:
        print("--verify-chapter-transition requires --start-spine", file=sys.stderr)
        return 2
    book = Path(args.book).resolve()
    if not book.exists():
        print(f"Smoke test book not found: {book}", file=sys.stderr)
        return 2
    additional_books = [Path(path).resolve() for path in args.additional_book]
    missing_additional = [path for path in additional_books if not path.exists()]
    if missing_additional:
        print(f"Additional smoke test book not found: {missing_additional[0]}", file=sys.stderr)
        return 2
    sleep_image = Path(args.sleep_image).expanduser().resolve() if args.sleep_image else None
    if sleep_image and not sleep_image.is_file():
        print(f"Sleep image not found: {sleep_image}", file=sys.stderr)
        return 2
    font_families = [Path(path).expanduser().resolve() for path in args.font_preview_family]
    dictionaries_root = Path(args.dictionary_root).expanduser().resolve() if args.dictionary_root else None
    if dictionaries_root and not dictionaries_root.is_dir():
        print(f"Dictionary root directory not found: {dictionaries_root}", file=sys.stderr)
        return 2
    if args.font_root:
        font_root = Path(args.font_root).expanduser().resolve()
        if not font_root.is_dir():
            print(f"Font root directory not found: {font_root}", file=sys.stderr)
            return 2
        font_families.extend(sorted(path for path in font_root.iterdir() if path.is_dir()))
    font_families = list(dict.fromkeys(font_families))
    missing_font_family = [path for path in font_families if not path.is_dir()]
    if missing_font_family:
        print(f"Font preview family directory not found: {missing_font_family[0]}", file=sys.stderr)
        return 2
    if args.font_preview_screenshot_dir:
        staged_names = {path.name for path in font_families}
        required_names = {"Bookerly", "Tinos", "Vollkorn"}
        if not required_names.issubset(staged_names):
            missing_names = ", ".join(sorted(required_names - staged_names))
            print(f"Font preview screenshots require these family directories: {missing_names}", file=sys.stderr)
            return 2

    if args.build:
        build_simulator(args.device)

    program = simulator_program(args.device)
    if not program.exists():
        print(f"Simulator binary not found: {program}", file=sys.stderr)
        print(f"Run: pio run -e {SIMULATOR_ENVS[args.device]}", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory(prefix="crossink-sim-smoke-") as temp_dir_name:
        temp_root = Path(temp_dir_name)
        simulator_book_path = prepare_fs(temp_root, book, additional_books)
        if args.verify_reader_relayout:
            expand_first_chapter_for_preview_smoke(temp_root, simulator_book_path)
        # Mirror the Alpha 5 canonical namespace before fixture state is saved.
        mounted_path(temp_root / "fs_", DUET_STATE_ROOT).mkdir(parents=True, exist_ok=True)
        mounted_path(temp_root / "fs_", DUET_BOOKS_ROOT).mkdir(parents=True, exist_ok=True)
        cover_browser_sources: list[Path] = []
        if args.file_browser_display in {"covers", "carousel"}:
            cover_browser_sources = stage_cover_browser_books(temp_root, book, additional_books)
        reading_home_sources: list[Path] = []
        reading_home_paths: list[str] = []
        if args.reading_home_screenshot:
            reading_home_sources, reading_home_paths = stage_reading_home_books(temp_root, book, additional_books)
        if sleep_image:
            shutil.copy2(sleep_image, temp_root / "fs_" / "sleep.bmp")
        if font_families:
            stage_font_families(temp_root, font_families)
        if dictionaries_root:
            stage_dictionaries(temp_root, dictionaries_root)
        if args.start_spine is not None:
            write_progress_fixture(temp_root, simulator_book_path, args.start_spine)
        if args.file_browser_display in {"covers", "carousel"}:
            write_cover_signal_fixtures(
                temp_root, cover_browser_sources, args.device, args.real_cover_fixtures
            )
        elif args.dashboard_home_screenshot:
            write_cover_signal_fixtures(temp_root, [book], args.device, args.real_cover_fixtures)
        elif args.reading_home_screenshot:
            write_cover_signal_fixtures(
                temp_root, reading_home_sources, args.device, args.real_cover_fixtures
            )
        elif args.book_info_screenshot and args.real_cover_fixtures:
            # More Info uses the same hydrated 120x180 cache variant as the
            # device library. Seed it from the real EPUB cover for public media.
            write_cover_signal_fixtures(temp_root, [book], args.device, True)
        if args.dashboard_home_screenshot:
            write_dashboard_home_fixture(temp_root, simulator_book_path)
        if args.reading_home_screenshot:
            write_reading_home_fixture(temp_root, reading_home_paths)
        if args.library_catalog:
            catalog_source = Path(args.library_catalog).expanduser().resolve()
            if not catalog_source.exists():
                print(f"Library catalog not found: {catalog_source}", file=sys.stderr)
                return 2
            catalog_target = mounted_path(temp_root / "fs_", f"{DUET_STATE_ROOT}/library_catalog.tsv")
            catalog_target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(catalog_source, catalog_target)
        elif args.book_info_screenshot or args.library_search_screenshot or args.library_autocomplete_screenshot:
            write_book_info_catalog_fixture(temp_root, simulator_book_path)
        if args.feature_screenshot_dir and not dictionaries_root:
            write_dictionary_fixture(temp_root)
        sync_fixtures = os.environ.get("CROSSINK_SMOKE_SYNC_FIXTURES")
        if sync_fixtures:
            # Stage real synced_* peer files (e.g. harvested from a device card)
            # so the peer-merge repro check runs against genuine payloads.
            fixtures_root = Path(sync_fixtures).expanduser().resolve()
            sync_root = mounted_path(temp_root / "fs_", DUET_STATE_ROOT)
            sync_root.mkdir(parents=True, exist_ok=True)
            for item in fixtures_root.iterdir():
                if item.is_dir() and item.name.startswith("synced_"):
                    shutil.copytree(item, sync_root / item.name, dirs_exist_ok=True)

        env = os.environ.copy()
        env["CROSSINK_SIMULATOR_SMOKE_TEST"] = "1"
        env["CROSSINK_SIMULATOR_SMOKE_BOOK"] = simulator_book_path
        smoke_book_title, smoke_book_author = read_epub_metadata(book)
        env["CROSSINK_SIMULATOR_SMOKE_BOOK_TITLE"] = smoke_book_title
        env["CROSSINK_SIMULATOR_SMOKE_BOOK_AUTHOR"] = smoke_book_author
        env["CROSSINK_SIMULATOR_SMOKE_PAGE_TURNS"] = str(args.page_turns)
        selected_theme = args.theme or (
            "reading-home"
            if args.reading_home_screenshot
            else ("dashboard" if args.dashboard_home_screenshot else ("minimal" if args.minimal_home_menu_screenshot else None))
        )
        if selected_theme:
            env["CROSSINK_SIMULATOR_SMOKE_THEME"] = str(THEMES[selected_theme])
        if args.file_browser_display:
            env["CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_DISPLAY"] = str(
                FILE_BROWSER_DISPLAYS[args.file_browser_display]
            )
        if args.file_browser_grid_layout:
            env["CROSSINK_SIMULATOR_SMOKE_FILE_BROWSER_GRID_LAYOUT"] = str(
                FILE_BROWSER_GRID_LAYOUTS[args.file_browser_grid_layout]
            )
        if sleep_image:
            env["CROSSINK_SIMULATOR_SMOKE_CUSTOM_SLEEP"] = "1"
        if args.file_browser_screenshot:
            env["CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_FILE_BROWSER"] = "1"
        if args.dashboard_home_screenshot:
            env["CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_DASHBOARD_HOME"] = "1"
            env["CROSSINK_SIMULATOR_SMOKE_FORCE_RTC"] = "1"
            env["CROSSINK_SIMULATOR_LOCAL_DATETIME"] = "2026-07-14T14:42"
        if args.minimal_home_menu_screenshot:
            env["CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_MINIMAL_HOME_MENU"] = "1"
        if args.reading_home_screenshot:
            env["CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_READING_HOME"] = "1"
            env["CROSSINK_SIMULATOR_SMOKE_READING_HOME_BOOKS"] = "|".join(reading_home_paths)
            env["CROSSINK_SIMULATOR_SMOKE_FORCE_RTC"] = "1"
            env["CROSSINK_SIMULATOR_LOCAL_DATETIME"] = "2026-07-14T14:42"
        if args.stats_screenshot_dir:
            env["CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_STATS"] = "1"
            env["CROSSINK_SIMULATOR_SMOKE_FORCE_RTC"] = "1"
            env["CROSSINK_SIMULATOR_LOCAL_DATETIME"] = "2026-07-14T14:42"
        if args.book_info_screenshot:
            env["CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_BOOK_INFO"] = "1"
        if args.library_search_screenshot:
            env["CROSSINK_SIMULATOR_SMOKE_LIBRARY_SEARCH_QUERY"] = args.library_search_screenshot[0]
        if args.library_autocomplete_screenshot:
            env["CROSSINK_SIMULATOR_SMOKE_LIBRARY_AUTOCOMPLETE_QUERY"] = args.library_autocomplete_screenshot[0]
        if args.font_preview_screenshot_dir:
            env["CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_FONT_PREVIEWS"] = "1"
        if args.feature_screenshot_dir:
            env["CROSSINK_SIMULATOR_SMOKE_SCREENSHOT_FEATURES"] = "1"
        if dictionaries_root:
            env["CROSSINK_SIMULATOR_SMOKE_EXPECT_WORDNET"] = "1"
        if args.single_font_preview:
            env["CROSSINK_SIMULATOR_SMOKE_FONT_PREVIEW_FAMILY"] = args.single_font_preview[0]
            env["CROSSINK_SIMULATOR_SMOKE_FONT_PREVIEW_SIZE"] = str(args.font_preview_size)
        if args.expect_font_family_count is not None:
            env["CROSSINK_SIMULATOR_EXPECT_FONT_FAMILY_COUNT"] = str(args.expect_font_family_count)
        if args.expect_synthetic_style_mask is not None:
            env["CROSSINK_SIMULATOR_EXPECT_SYNTHETIC_STYLE_MASK"] = str(args.expect_synthetic_style_mask)
        if args.verify_font_sizes:
            env["CROSSINK_SIMULATOR_VERIFY_SD_FONT_SIZES"] = args.verify_font_sizes
        if args.library_catalog:
            env["CROSSINK_SIMULATOR_SMOKE_EXPECT_LIBRARY_CATALOG"] = "1"
        if args.verify_reader_relayout:
            env["CROSSINK_SIMULATOR_SMOKE_VERIFY_READER_RELAYOUT"] = "1"
        if args.verify_chapter_transition:
            env["CROSSINK_SIMULATOR_SMOKE_VERIFY_CHAPTER_TRANSITION"] = "1"
        if args.headless:
            env.setdefault("SDL_VIDEODRIVER", "dummy")

        print(
            f"Running {args.device.upper()} simulator smoke test with isolated fs_: {temp_root / 'fs_'}",
            flush=True,
        )
        proc = subprocess.run(
            [str(program)],
            cwd=temp_root,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=args.timeout,
        )
        if proc.returncode != 0:
            print(proc.stdout, end="", file=sys.stderr)
            print(f"Simulator smoke test failed with exit code {proc.returncode}", file=sys.stderr)
            return proc.returncode

        if args.verify_reader_relayout:
            expected_start_spine = args.start_spine if args.start_spine is not None else 0
            required_markers = (
                "Using visible-page-first chapter preview:",
                "Chapter layout completion ready:",
                "Stopping preview after 2 pages",
                "Started preview at paragraph",
                "Font relayout completion ready:",
                "Resolved cached paragraph",
                "Changed reader font family to 1",
            )
            missing_markers = [marker for marker in required_markers if marker not in proc.stdout]
            if missing_markers:
                print(
                    "Reader relayout smoke test missed: " + ", ".join(missing_markers),
                    file=sys.stderr,
                )
                print(proc.stdout, end="", file=sys.stderr)
                return 2
            chapter_completion = re.search(
                rf"Chapter layout completion ready: spine={expected_start_spine} pages=(\d+)",
                proc.stdout,
            )
            if chapter_completion is None or int(chapter_completion.group(1)) <= 6:
                print(
                    "Reader chapter preview smoke did not replace its two-page preview with a longer full cache",
                    file=sys.stderr,
                )
                print(proc.stdout, end="", file=sys.stderr)
                return 2
            for marker in required_markers[3:6]:
                if proc.stdout.count(marker) < 2:
                    print(
                        f"Reader relayout smoke test did not complete both spacing and family changes: {marker}",
                        file=sys.stderr,
                    )
                    print(proc.stdout, end="", file=sys.stderr)
                    return 2
            relayout_font_ids = set(
                re.findall(r"Font relayout completion start: spine=\d+ font=(-?\d+)", proc.stdout)
            )
            if len(relayout_font_ids) < 2:
                print(
                    "Reader relayout smoke test did not build two distinct font-family caches",
                    file=sys.stderr,
                )
                print(proc.stdout, end="", file=sys.stderr)
                return 2
            preview_files = list((temp_root / "fs_").rglob("*_rp_*.bin"))
            preview_files.extend((temp_root / "fs_").rglob("*_cp.bin"))
            if preview_files:
                print(
                    f"Reader relayout left a transient preview cache behind: {preview_files[0]}",
                    file=sys.stderr,
                )
                return 2
            persistent_layouts = [
                path
                for path in (temp_root / "fs_").rglob("*_f*.bin")
                if "_rp_" not in path.name
            ]
            if not persistent_layouts:
                print("Reader relayout did not persist a font-specific chapter cache", file=sys.stderr)
                return 2

        if args.verify_chapter_transition:
            expected_spine = args.start_spine + 1
            transition = re.search(
                rf"Loading file: .*, index: {expected_spine} \(",
                proc.stdout,
            )
            if transition is None:
                print(
                    f"Reader chapter-transition smoke never reached spine {expected_spine}",
                    file=sys.stderr,
                )
                print(proc.stdout, end="", file=sys.stderr)
                return 2
            after_transition = proc.stdout[transition.start():]
            if "Rendered page in " not in after_transition:
                print(
                    f"Reader chapter-transition smoke reached spine {expected_spine} but did not paint it",
                    file=sys.stderr,
                )
                print(proc.stdout, end="", file=sys.stderr)
                return 2
            fatal_markers = (
                "EPUB section layout aborted for low heap",
                "Failed to build section after all fallback attempts",
                "chapter exceeds safe layout memory",
            )
            found_fatal = [marker for marker in fatal_markers if marker in after_transition]
            if found_fatal:
                print(
                    "Reader chapter-transition smoke entered the fatal layout path: "
                    + ", ".join(found_fatal),
                    file=sys.stderr,
                )
                print(proc.stdout, end="", file=sys.stderr)
                return 2

        if args.file_browser_screenshot:
            screenshot_source = temp_root / "fs_" / "smoke-file-browser.bmp"
            screenshot_target = Path(args.file_browser_screenshot).expanduser().resolve()
            if not screenshot_source.exists():
                print("File Browser smoke screenshot was not created", file=sys.stderr)
                return 2
            screenshot_target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(screenshot_source, screenshot_target)
            print(f"Saved File Browser screenshot to {screenshot_target}", flush=True)
            marquee_source = temp_root / "fs_" / "smoke-file-browser-marquee.bmp"
            if not marquee_source.exists():
                print("File Browser marquee screenshot was not created", file=sys.stderr)
                return 2
            marquee_target = screenshot_target.with_name(
                f"{screenshot_target.stem}-marquee{screenshot_target.suffix}"
            )
            shutil.copy2(marquee_source, marquee_target)

        if args.file_browser_display == "covers":
            grid_layout = args.file_browser_grid_layout or "2x2"
            down_target = {"2x2": 3, "3x3": 4, "4x4": 5}[grid_layout]
            page_source = {"2x2": 3, "3x3": 7, "4x4": 13}[grid_layout]
            page_target = {"2x2": 5, "3x3": 10, "4x4": 17}[grid_layout]
            expected_moves = (
                "Moved cover selection 0 -> 1",
                f"Moved cover selection 1 -> {down_target}",
                f"Moved cover selection {page_source} -> {page_target}",
            )
            if any(move not in proc.stdout for move in expected_moves):
                print(
                    f"{grid_layout} cover-grid navigation did not stay responsive through a page change",
                    file=sys.stderr,
                )
                return 2
            returned_page_start = 0
            return_source = page_target
            return_target = page_source
            expected_return = f"Moved cover selection {return_source} -> {return_target}"
            if expected_return not in proc.stdout:
                print(f"{grid_layout} cover-grid Back navigation did not restore the previous page", file=sys.stderr)
                for line in proc.stdout.splitlines():
                    if "Cover page" in line or "Moved cover selection" in line:
                        print(line, file=sys.stderr)
                return 2
            warm_page_prefix = f"Cover page {returned_page_start} ("
            warm_page_lines = [line for line in proc.stdout.splitlines() if warm_page_prefix in line]
            warmed_match = (
                re.search(r"reused \d+ signal\(s\), (\d+) bitmap\(s\)", warm_page_lines[-1])
                if warm_page_lines
                else None
            )
            if not warmed_match or int(warmed_match.group(1)) == 0:
                print(f"{grid_layout} previous high-quality page was not retained for immediate Back navigation", file=sys.stderr)
                for line in proc.stdout.splitlines():
                    if "Cover page" in line or "Moved cover selection" in line:
                        print(line, file=sys.stderr)
                return 2
            for direction in ("right", "down", "page", "back"):
                fast_source = temp_root / "fs_" / f"smoke-file-browser-fast-{direction}.bmp"
                if not fast_source.exists():
                    print(f"Four-cover fast-navigation screenshot was not created: {direction}", file=sys.stderr)
                    return 2
                if args.file_browser_screenshot:
                    fast_target = screenshot_target.with_name(
                        f"{screenshot_target.stem}-fast-{direction}{screenshot_target.suffix}"
                    )
                    shutil.copy2(fast_source, fast_target)
            if "Navigated back from /books to /" not in proc.stdout:
                print(f"{grid_layout} cover-grid Back did not leave the book folder", file=sys.stderr)
                return 2
            folder_back_source = temp_root / "fs_" / "smoke-file-browser-folder-back.bmp"
            if not folder_back_source.exists():
                print("Cover-grid folder Back screenshot was not created", file=sys.stderr)
                return 2

        if args.file_browser_display == "carousel":
            expected_moves = ("Moved carousel selection 0 -> 1", "Moved carousel selection 1 -> 2")
            if any(move not in proc.stdout for move in expected_moves):
                print("Carousel input did not advance exactly one book per press", file=sys.stderr)
                return 2
            carousel_reuse = re.search(
                r"Carousel ready at 2: reused \d+ signal\(s\), (\d+) detail\(s\)",
                proc.stdout,
            )
            if not carousel_reuse or int(carousel_reuse.group(1)) < 4:
                print(
                    "Carousel discarded overlapping cover bitmaps after navigation",
                    file=sys.stderr,
                )
                for line in proc.stdout.splitlines():
                    if "Carousel ready at " in line or "Moved carousel selection" in line:
                        print(line, file=sys.stderr)
                return 2
            carousel_lookahead = re.search(
                r"Carousel ready at 2: .*look-ahead (\d+) ready/(\d+) queued",
                proc.stdout,
            )
            if not carousel_lookahead or int(carousel_lookahead.group(1)) < 1:
                print(
                    "Carousel did not retain a directional look-ahead cover after hydration",
                    file=sys.stderr,
                )
                for line in proc.stdout.splitlines():
                    if "Carousel ready at " in line or "Moved carousel selection" in line:
                        print(line, file=sys.stderr)
                return 2

        if args.reader_quick_menu_screenshot:
            screenshot_source = temp_root / "fs_" / "smoke-reader-quick-menu.bmp"
            book_info_source = temp_root / "fs_" / "smoke-reader-book-info.bmp"
            screenshot_target = Path(args.reader_quick_menu_screenshot).expanduser().resolve()
            if not screenshot_source.exists() or not book_info_source.exists():
                print("Reader quick-menu or Book Info smoke screenshot was not created", file=sys.stderr)
                return 2
            screenshot_target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(screenshot_source, screenshot_target)
            book_info_target = screenshot_target.with_name(
                f"{screenshot_target.stem}-book-info{screenshot_target.suffix}"
            )
            shutil.copy2(book_info_source, book_info_target)
            print(
                f"Saved Reader quick-menu and Book Info screenshots to {screenshot_target} and {book_info_target}",
                flush=True,
            )

        if args.dashboard_home_screenshot:
            screenshot_source = temp_root / "fs_" / "smoke-dashboard-home.bmp"
            screenshot_target = Path(args.dashboard_home_screenshot).expanduser().resolve()
            if not screenshot_source.exists():
                print("Dashboard Home smoke screenshot was not created", file=sys.stderr)
                return 2
            screenshot_target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(screenshot_source, screenshot_target)
            print(f"Saved Dashboard Home screenshot to {screenshot_target}", flush=True)

        if args.minimal_home_menu_screenshot:
            screenshot_source = temp_root / "fs_" / "smoke-minimal-home-menu.bmp"
            screenshot_target = Path(args.minimal_home_menu_screenshot).expanduser().resolve()
            if not screenshot_source.exists():
                print("Minimal Home menu smoke screenshot was not created", file=sys.stderr)
                return 2
            screenshot_target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(screenshot_source, screenshot_target)
            print(f"Saved Minimal Home menu screenshot to {screenshot_target}", flush=True)

        if args.reading_home_screenshot:
            screenshot_source = temp_root / "fs_" / "smoke-reading-home.bmp"
            screenshot_target = Path(args.reading_home_screenshot).expanduser().resolve()
            if not screenshot_source.exists():
                print("Reading Home smoke screenshot was not created", file=sys.stderr)
                return 2
            screenshot_target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(screenshot_source, screenshot_target)
            print(f"Saved Reading Home screenshot to {screenshot_target}", flush=True)

        verify_library_cache = args.library_catalog and not args.skip_library_insights_cache_check
        if verify_library_cache and "Loaded cached insights" not in proc.stdout:
            print("Library insights did not use its compact cache on the second load", file=sys.stderr)
            return 2
        if verify_library_cache and "file_reads=0" not in proc.stdout:
            print("Library insights did not use its per-book stats index after invalidation", file=sys.stderr)
            return 2

        if args.stats_screenshot_dir:
            screenshot_dir = Path(args.stats_screenshot_dir).expanduser().resolve()
            screenshot_dir.mkdir(parents=True, exist_ok=True)
            for name in (
                "current",
                "latest-session",
                "progress",
                "book-patterns",
                "trends",
                "activity",
                "daily-minutes",
                "daily-minutes-scrolled",
                "calendar",
                "day-details",
                "day-edit",
                "heatmap",
                "profile",
                "goals",
                "recent",
                "recent-scrolled",
                "weekdays",
                "pace",
                "time-of-day",
                "months",
                "year",
                "devices",
                "sessions-mix",
                "streaks",
                "start-finish",
                "reading-dates",
                "reading-dates-scrolled",
                "reader-dna",
                "dna-details",
                "signature",
                "signature-details",
                "fastest",
                "wrapped",
                "started",
                "started-scrolled",
                "library",
                "taste",
                "series",
                "series-scrolled",
                "this-device",
                "all-devices",
                "book-dates-locked",
                "book-dates-editing",
            ):
                source = temp_root / "fs_" / f"smoke-stats-{name}.bmp"
                if not source.exists():
                    print(f"Reading stats smoke screenshot was not created: {name}", file=sys.stderr)
                    return 2
                shutil.copy2(source, screenshot_dir / source.name)
            print(f"Saved reading stats screenshots to {screenshot_dir}", flush=True)

        if args.feature_screenshot_dir:
            screenshot_dir = Path(args.feature_screenshot_dir).expanduser().resolve()
            screenshot_dir.mkdir(parents=True, exist_ok=True)
            for name in (
                "utilities",
                "achievements",
                "achievements-completed",
                "favorites",
                "dictionary",
                "dictionary-definition",
                "tetris",
            ):
                source = temp_root / "fs_" / f"smoke-{name}.bmp"
                if not source.exists():
                    print(f"Feature smoke screenshot was not created: {name}", file=sys.stderr)
                    return 2
                shutil.copy2(source, screenshot_dir / source.name)
            print(f"Saved feature screenshots to {screenshot_dir}", flush=True)

        if args.book_info_screenshot:
            screenshot_source = temp_root / "fs_" / "smoke-book-info.bmp"
            loaded_source = temp_root / "fs_" / "smoke-book-info-loaded.bmp"
            screenshot_target = Path(args.book_info_screenshot).expanduser().resolve()
            if not screenshot_source.exists() or not loaded_source.exists():
                print("Immediate or hydrated Book Info smoke screenshot was not created", file=sys.stderr)
                return 2
            screenshot_target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(screenshot_source, screenshot_target)
            loaded_target = screenshot_target.with_name(
                f"{screenshot_target.stem}-loaded{screenshot_target.suffix}"
            )
            shutil.copy2(loaded_source, loaded_target)
            print(
                f"Saved immediate and hydrated Book Info screenshots to {screenshot_target} and {loaded_target}",
                flush=True,
            )

        if args.library_search_screenshot:
            screenshot_source = temp_root / "fs_" / "smoke-library-search.bmp"
            screenshot_target = Path(args.library_search_screenshot[1]).expanduser().resolve()
            if not screenshot_source.exists():
                print("Library Search smoke screenshot was not created", file=sys.stderr)
                return 2
            screenshot_target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(screenshot_source, screenshot_target)
            print(f"Saved Library Search screenshot to {screenshot_target}", flush=True)

        if args.library_autocomplete_screenshot:
            screenshot_source = temp_root / "fs_" / "smoke-library-autocomplete.bmp"
            screenshot_target = Path(args.library_autocomplete_screenshot[1]).expanduser().resolve()
            if not screenshot_source.exists():
                print("Library autocomplete smoke screenshot was not created", file=sys.stderr)
                return 2
            screenshot_target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(screenshot_source, screenshot_target)
            filled_source = temp_root / "fs_" / "smoke-library-autocomplete-filled.bmp"
            if not filled_source.exists():
                print("Filled library autocomplete smoke screenshot was not created", file=sys.stderr)
                return 2
            filled_target = screenshot_target.with_name(
                f"{screenshot_target.stem}-filled{screenshot_target.suffix}"
            )
            shutil.copy2(filled_source, filled_target)
            print(f"Saved Library autocomplete screenshot to {screenshot_target}", flush=True)

        if args.font_preview_screenshot_dir:
            screenshot_dir = Path(args.font_preview_screenshot_dir).expanduser().resolve()
            screenshot_dir.mkdir(parents=True, exist_ok=True)
            for name in (
                "tinos-before",
                "tinos-after",
                "vollkorn-before",
                "vollkorn-after",
            ):
                source = temp_root / "fs_" / f"smoke-font-{name}.bmp"
                if not source.exists():
                    print(f"Font preview smoke screenshot was not created: {name}", file=sys.stderr)
                    return 2
                shutil.copy2(source, screenshot_dir / source.name)
            print(f"Saved font preview screenshots to {screenshot_dir}", flush=True)

        if args.single_font_preview:
            screenshot_source = temp_root / "fs_" / "smoke-font-preview.bmp"
            screenshot_target = Path(args.single_font_preview[1]).expanduser().resolve()
            if not screenshot_source.exists():
                print("Single font preview screenshot was not created", file=sys.stderr)
                return 2
            screenshot_target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(screenshot_source, screenshot_target)
            print(f"Saved {args.single_font_preview[0]} preview screenshot to {screenshot_target}", flush=True)

    print(proc.stdout, end="")

    if proc.returncode != 0:
        print(f"Simulator smoke test failed with exit code {proc.returncode}", file=sys.stderr)
        return proc.returncode

    for pattern in CRASH_PATTERNS:
        if pattern in proc.stdout:
            print(f"Simulator smoke test output contained crash pattern: {pattern}", file=sys.stderr)
            return 2

    if "Simulator smoke test passed" not in proc.stdout:
        print("Simulator smoke test did not print its success marker", file=sys.stderr)
        return 2

    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--book", default=str(DEFAULT_BOOK), help="EPUB fixture to copy into the isolated simulator fs_")
    parser.add_argument(
        "--device",
        choices=sorted(SIMULATOR_ENVS),
        default="x4",
        help="Panel geometry and device branch to simulate (default: x4)",
    )
    parser.add_argument(
        "--additional-book",
        action="append",
        default=[],
        help="Additional EPUB to place in the simulated library; may be repeated",
    )
    parser.add_argument("--timeout", type=int, default=45, help="Seconds before the simulator run is treated as hung")
    parser.add_argument("--page-turns", type=int, default=2, help="Number of EPUB page-forward taps to run")
    parser.add_argument(
        "--start-spine",
        type=int,
        help="Seed the EPUB progress file at this zero-based spine index before opening the reader",
    )
    parser.add_argument("--theme", choices=sorted(THEMES), help="UI theme to use during the smoke test")
    parser.add_argument(
        "--file-browser-display",
        choices=sorted(FILE_BROWSER_DISPLAYS),
        help="File browser layout to exercise during the smoke test",
    )
    parser.add_argument(
        "--file-browser-grid-layout",
        choices=sorted(FILE_BROWSER_GRID_LAYOUTS),
        help="CrumBLE-style cover-grid density to exercise",
    )
    parser.add_argument(
        "--file-browser-screenshot",
        help="Save the simulated File Browser framebuffer as a BMP at this path",
    )
    parser.add_argument(
        "--real-cover-fixtures",
        action="store_true",
        help="Extract real EPUB covers and render every device cache size for publication media",
    )
    parser.add_argument(
        "--reader-quick-menu-screenshot",
        help="Save the in-reader quick overlay framebuffer as a BMP at this path",
    )
    parser.add_argument(
        "--dashboard-home-screenshot",
        help="Render the optional Dashboard Home with a seeded cover and save its framebuffer as a BMP",
    )
    parser.add_argument(
        "--minimal-home-menu-screenshot",
        help="Render the centered compact Minimal Home popout and save its framebuffer as a BMP",
    )
    parser.add_argument(
        "--reading-home-screenshot",
        help="Render the CrossPet-derived Reading Home with four seeded recents and save its framebuffer as a BMP",
    )
    parser.add_argument(
        "--stats-screenshot-dir",
        help="Save simulated reading-stats page screenshots into this directory",
    )
    parser.add_argument(
        "--feature-screenshot-dir",
        help="Validate Dictionary and Tetris and save their simulated screens into this directory",
    )
    parser.add_argument(
        "--dictionary-root",
        help="Stage a directory whose children are dictionary language folders",
    )
    parser.add_argument(
        "--book-info-screenshot",
        help="Save a simulated per-book More Info screen as a BMP at this path",
    )
    parser.add_argument(
        "--library-search-screenshot",
        nargs=2,
        metavar=("QUERY", "PATH"),
        help="Run a catalog search and save the simulated result screen as a BMP",
    )
    parser.add_argument(
        "--library-autocomplete-screenshot",
        nargs=2,
        metavar=("QUERY", "PATH"),
        help="Render live library autocomplete suggestions inside the search keyboard",
    )
    parser.add_argument(
        "--font-preview-family",
        action="append",
        default=[],
        help="SD font family directory to stage for font-picker testing; may be repeated",
    )
    parser.add_argument(
        "--font-root",
        help="Stage every font-family directory under this root in the isolated simulator filesystem",
    )
    parser.add_argument(
        "--font-preview-screenshot-dir",
        help="Save before/after Tinos and Vollkorn font-picker screenshots into this directory",
    )
    parser.add_argument(
        "--single-font-preview",
        nargs=2,
        metavar=("FAMILY", "PATH"),
        help="Save a font-picker preview for one staged family",
    )
    parser.add_argument(
        "--font-preview-size",
        type=int,
        default=16,
        choices=(10, 12, 14, 16, 18, 20),
        help="Point size used by --single-font-preview (default: 16)",
    )
    parser.add_argument(
        "--expect-font-family-count",
        type=int,
        help="Assert the exact number of SD font families discovered by the firmware",
    )
    parser.add_argument(
        "--expect-synthetic-style-mask",
        type=lambda value: int(value, 0),
        choices=range(16),
        help=("Assert the selected preview family's synthetic style bits "
              "(regular=1, bold=2, italic=4, bold-italic=8)"),
    )
    parser.add_argument(
        "--verify-font-sizes",
        metavar="FAMILY",
        help="Verify that a staged SD font family preserves and reloads 12 pt versus 14 pt",
    )
    parser.add_argument(
        "--verify-reader-relayout",
        action="store_true",
        help="Verify visible-page-first typography relayout and idle full-chapter cache completion",
    )
    parser.add_argument(
        "--verify-chapter-transition",
        action="store_true",
        help="Verify that scripted page turns cross from --start-spine into the next spine and paint it",
    )
    parser.add_argument("--library-catalog", help="Stage and validate a Duet library_catalog.tsv fixture")
    parser.add_argument(
        "--skip-library-insights-cache-check",
        action="store_true",
        help="Stage the catalog for focused UI tests without requiring its companion stats cache",
    )
    parser.add_argument("--sleep-image", help="Stage a BMP and exercise the custom grayscale sleep-screen path")
    parser.add_argument("--no-build", dest="build", action="store_false", help="Run the existing simulator binary")
    parser.add_argument("--window", dest="headless", action="store_false", help="Show the SDL window instead of using dummy video")
    parser.set_defaults(build=True, headless=True)
    return parser.parse_args()


def main() -> int:
    return run_smoke(parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
