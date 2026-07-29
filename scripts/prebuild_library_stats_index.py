#!/usr/bin/env python3
"""Prebuild Duet's compact per-book stats index on an XTEINK SD card."""

from __future__ import annotations

import argparse
import os
import struct
from dataclasses import dataclass
from pathlib import Path

from duet_storage_paths import (
    DUET_BOOKS_ROOT,
    DUET_STATE_ROOT,
    LEGACY_BOOKS_ROOT,
    LEGACY_STATE_ROOT,
    canonical_book_cache_path,
    legacy_book_cache_path,
    mounted_path,
    stable_book_cache_identity,
)


FNV64_BASIS = 14695981039346656037
FNV64_PRIME = 1099511628211
INDEX_MAGIC = 0x5844494C
INDEX_VERSION = 1
FLAG_COMPLETED = 1 << 0
FLAG_PROGRESS = 1 << 1
STATS_NAMES = tuple(f"stats_v{version}.bin" for version in range(6, 0, -1)) + ("stats.bin",)


@dataclass(frozen=True)
class IndexedStats:
    key: int
    reading_seconds: int
    sessions: int
    flags: int


def fnv1a64(value: str) -> int:
    result = FNV64_BASIS
    for byte in value.encode("utf-8"):
        result ^= byte
        result = (result * FNV64_PRIME) & 0xFFFFFFFFFFFFFFFF
    return result


def cache_path_for_book(device_path: str) -> str:
    return canonical_book_cache_path(fnv1a64(device_path))


def read_stats(cache_dirs: tuple[Path, ...]) -> tuple[int, int, bool]:
    for name in STATS_NAMES:
        path = next((directory / name for directory in cache_dirs if (directory / name).is_file()), None)
        if path is None:
            continue
        data = path.read_bytes()
        if len(data) < 11 or data[0] not in range(1, 7):
            continue
        sessions = struct.unpack_from("<H", data, 1)[0]
        reading_seconds = struct.unpack_from("<I", data, 3)[0]
        completed = len(data) >= 12 and data[0] >= 2 and data[11] != 0
        return sessions, reading_seconds, completed
    return 0, 0, False


def catalog_paths(catalog_path: Path) -> list[str]:
    paths: list[str] = []
    with catalog_path.open("r", encoding="utf-8-sig") as source:
        for raw_line in source:
            fields = raw_line.rstrip("\r\n").split("\t", 8)
            if len(fields) >= 8 and fields[0] == "B":
                paths.append(fields[7])
    if not paths:
        raise SystemExit(f"No book records found in {catalog_path}")
    return paths


def build_entries(sd_root: Path, device_paths: list[str]) -> list[IndexedStats]:
    entries: dict[int, IndexedStats] = {}
    for device_path in device_paths:
        cache_path = cache_path_for_book(device_path)
        cache_dir = mounted_path(sd_root, cache_path)
        legacy_cache_dir = mounted_path(sd_root, legacy_book_cache_path(fnv1a64(device_path)))
        sessions, reading_seconds, completed = read_stats((cache_dir, legacy_cache_dir))
        flags = FLAG_COMPLETED if completed else 0
        progress_dirs = (cache_dir, legacy_cache_dir)
        if any(
            (directory / "progress.bin").is_file() or (directory / "progress.bin.bak").is_file()
            for directory in progress_dirs
        ):
            flags |= FLAG_PROGRESS
        key = fnv1a64(stable_book_cache_identity(cache_path))
        entries[key] = IndexedStats(key, reading_seconds, sessions, flags)
    mounted_path(sd_root, DUET_BOOKS_ROOT).mkdir(parents=True, exist_ok=True)
    return sorted(entries.values(), key=lambda entry: entry.key)


def write_index(output_path: Path, entries: list[IndexedStats]) -> None:
    if len(entries) > 0xFFFF:
        raise SystemExit(f"Index has too many entries: {len(entries)}")
    temp_path = output_path.with_suffix(output_path.suffix + ".tmp")
    with temp_path.open("wb") as output:
        output.write(struct.pack("<IBBH", INDEX_MAGIC, INDEX_VERSION, 0, len(entries)))
        for entry in entries:
            output.write(
                struct.pack(
                    "<QIHB",
                    entry.key,
                    entry.reading_seconds,
                    entry.sessions,
                    entry.flags,
                )
            )
        output.flush()
        os.fsync(output.fileno())
    os.replace(temp_path, output_path)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sd_root", type=Path, help="mounted XTEINK SD-card root")
    args = parser.parse_args()

    sd_root = args.sd_root.expanduser().resolve()
    catalog_candidates = (
        mounted_path(sd_root, f"{DUET_STATE_ROOT}/library_catalog.tsv"),
        mounted_path(sd_root, f"{LEGACY_STATE_ROOT}/library_catalog.tsv"),
        mounted_path(sd_root, f"{LEGACY_BOOKS_ROOT}/library_catalog.tsv"),
    )
    catalog_path = next((path for path in catalog_candidates if path.is_file()), catalog_candidates[0])
    if not catalog_path.is_file():
        searched = ", ".join(str(path) for path in catalog_candidates)
        raise SystemExit(f"Library catalog not found; searched: {searched}")

    paths = catalog_paths(catalog_path)
    entries = build_entries(sd_root, paths)
    output_path = mounted_path(sd_root, f"{DUET_STATE_ROOT}/library_book_stats_v1.bin")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    write_index(output_path, entries)

    active = sum(entry.reading_seconds > 0 or entry.sessions > 0 or entry.flags != 0 for entry in entries)
    print(f"Wrote {len(entries)} indexed books ({active} with activity) to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
