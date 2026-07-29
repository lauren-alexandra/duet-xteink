# Duet Storage Namespace Migration

## Status

The migration is implemented in `v0.1.0-alpha.6`. `lib/DuetStorage/` is the source of truth for canonical paths, legacy mappings, retry behavior, and stable per-book identity. Native migration tests cover fresh state, either legacy tree, both trees, existing canonical state, partial and interrupted imports, completed reboot, and per-book identity. Exact physical X3/X4 upgrade behavior remains part of the alpha.6 acceptance route.

## Goal

Duet-owned SD-card state should use `/.duet` as its canonical namespace. The inherited `/.crossink` and `/.crosspoint` directories should remain recognized only as legacy import and recovery sources.

This is a compatibility migration, not a cosmetic folder rename. A successful upgrade must preserve:

- Global settings and the selected Home layout
- The active and recently opened books
- Reading positions and per-book reader settings
- Reading sessions, statistics, corrections, and sync ledgers
- Achievements and unlock history
- Bookmarks, clippings, and looked-up words
- Library catalogs, metadata indexes, and cover thumbnails
- Font discovery caches and performance diagnostics
- KOReader credentials and compatible progress-sync state
- Nearby position and reading-statistics sync state

## Migration Requirements

1. New Duet writes go beneath `/.duet`.
2. Existing `/.crossink` and `/.crosspoint` data is discovered automatically.
3. Migration is idempotent and safe to retry after interruption or power loss.
4. Existing Duet data wins over older legacy data unless a validated merge is required to preserve newer history.
5. Legacy data remains readable as a fallback if migration cannot finish.
6. Duet does not automatically delete the legacy directories.
7. Migration does not recursively collect a large card's cache names in RAM.
8. SD migration and file access stay on the input/storage task and obey the storage mutex.
9. The same migration behavior ships on X3 and X4.
10. Migration never substitutes one device's identity or state for the other.

The implemented canonical layout is:

```text
/.duet/
├── state/
├── books/
├── cache/
│   ├── thumbs/
│   ├── layouts/
│   └── fileindex/
├── backups/
│   └── reading-stats/
└── migration/
    └── legacy-import-v1.complete
```

`/.crossink` maps primarily into `/.duet/state`, `/.crosspoint` maps into both canonical state and per-book records as appropriate, and `/.crossink-stats-backup` maps into `/.duet/backups/reading-stats`. Canonical data wins. The completion marker is written only after the bounded global import succeeds; per-book records migrate lazily as their books are opened.

## Required Acceptance Tests

- Fresh card with no legacy state
- Card containing only `/.crossink`
- Card containing only `/.crosspoint`
- Card containing both legacy directories
- Card containing `/.duet` plus one or both legacy directories
- Interrupted migration followed by reboot
- Repeated boot after successful migration
- X3 upgrade with an active book and existing statistics
- X4 upgrade with an active book and existing statistics
- Book open, position resume, stats display, achievements, covers, settings, and both sync systems after migration

The release candidate must also prove that the Home screen does not fall back to an unrelated recent title after migration.

## Documentation Transition

Historical alpha.1-alpha.4 release notes keep their original `/.crossink` and `/.crosspoint` paths because those paths describe the builds that were actually released or tested.

Current user, contributor, troubleshooting, cache, prefill, and testing documentation uses the implemented `/.duet` paths. Recovery instructions still tell testers to back up all three namespaces because the legacy trees are preserved deliberately and remain useful recovery inputs.
