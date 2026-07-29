"""Canonical and legacy SD-card paths shared by Duet desktop tools."""

from __future__ import annotations

from pathlib import Path


DUET_ROOT = "/.duet"
DUET_STATE_ROOT = f"{DUET_ROOT}/state"
DUET_BOOKS_ROOT = f"{DUET_ROOT}/books"
DUET_CACHE_ROOT = f"{DUET_ROOT}/cache"
DUET_THUMBS_ROOT = f"{DUET_CACHE_ROOT}/thumbs"
DUET_LAYOUTS_ROOT = f"{DUET_CACHE_ROOT}/layouts"
DUET_FILE_INDEX_ROOT = f"{DUET_CACHE_ROOT}/fileindex"
DUET_BACKUPS_ROOT = f"{DUET_ROOT}/backups"
DUET_STATS_BACKUP_ROOT = f"{DUET_BACKUPS_ROOT}/reading-stats"
DUET_MIGRATION_ROOT = f"{DUET_ROOT}/migration"

LEGACY_STATE_ROOT = "/.crossink"
LEGACY_BOOKS_ROOT = "/.crosspoint"
LEGACY_STATS_BACKUP_ROOT = "/.crossink-stats-backup"


def mounted_path(card_root: Path, device_path: str) -> Path:
    """Map an absolute device path into a mounted SD-card directory."""
    return card_root / device_path.lstrip("/")


def canonical_book_cache_path(cache_hash: int | str) -> str:
    return f"{DUET_BOOKS_ROOT}/epub_{cache_hash}"


def legacy_book_cache_path(cache_hash: int | str) -> str:
    return f"{LEGACY_BOOKS_ROOT}/epub_{cache_hash}"


def stable_book_cache_identity(canonical_cache_path: str) -> str:
    """Return the pre-Alpha-5 identity used by stats and Nearby Sync keys."""
    prefix = f"{DUET_BOOKS_ROOT}/"
    if canonical_cache_path.startswith(prefix):
        return f"{LEGACY_BOOKS_ROOT}/{canonical_cache_path.removeprefix(prefix)}"
    return canonical_cache_path
