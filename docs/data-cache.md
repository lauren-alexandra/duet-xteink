---
title: Data Cache
nav_order: 16
---

# Data Cache

Duet caches data aggressively on the SD card to minimize RAM use. The ESP32-C3 has about 380 KB of usable RAM, so rebuilding every book structure in memory on every open would be too expensive.

Current Duet writes beneath one canonical hidden root, `/.duet`, divided by purpose:

- `/.duet/state/` stores frequently opened device, library, statistics, sync, credential, and diagnostic state.
- `/.duet/books/` stores path-keyed durable per-book progress, statistics, settings, metadata, and base covers.
- `/.duet/cache/` stores disposable thumbnails, chapter layouts, and file indexes.
- `/.duet/backups/reading-stats/` stores complete `.cstats` archives.
- `/.duet/migration/` records completed compatibility imports.

This layout avoids repeatedly scanning a large per-book directory for hot settings and statistics while keeping disposable cover/layout work out of durable per-book records. Existing `/.crossink`, `/.crosspoint`, and `/.crossink-stats-backup` data remains untouched as migration and recovery input. Global state imports in a bounded first-boot pass; large per-book records migrate lazily when their books are opened.

## Directory Layout

```text
.duet/
├── state/
│   ├── crossink-settings.json  # Compatibility-named current Duet settings
│   ├── state.json              # Last-opened book and sleep/session state
│   ├── recent.json             # Recent books list
│   ├── favorites.json          # Favorite books and ordering
│   ├── launcher_layout.bin     # Customized Home and Apps layout
│   ├── achievements.bin        # Persistent achievement unlock ledger
│   ├── wifi.json               # Saved Wi-Fi networks
│   ├── opds.json               # Saved OPDS servers
│   ├── koreader.json           # KOReader Sync credentials
│   ├── bookmarks/              # Bookmark files, one per book
│   ├── clippings/              # EPUB clipping/highlight files, one per book
│   ├── global_stats.bin        # Device/global reading totals
│   ├── reading_journal.bin     # Dated reading history
│   ├── reading_ledger_v1.bin   # Per-date book attribution
│   ├── reading_stats_clock_v1.bin
│   ├── synced_stats/           # Received device summaries
│   ├── synced_journals/        # Received dated histories
│   ├── synced_ledgers/         # Received attribution ledgers
│   ├── synced_book_stats/      # Received per-book summaries
│   ├── library_catalog.tsv     # Optional desktop-generated catalog
│   ├── library_insights_v1.bin # Derived catalog/statistics index
│   ├── home_carousel_cache.bin # Lyra Carousel Home snapshot cache
│   └── sleep_frame.bin         # Temporary sleep-overlay framebuffer
├── books/
│   ├── epub_12471232/          # Each EPUB uses a stable path-derived cache
│   │   ├── progress.bin        # Reading position
│   │   ├── stats_v7.bin        # Current per-book reading statistics
│   │   ├── reader_settings.bin # Per-book settings and render profile
│   │   ├── cover.bmp           # Extracted/generated base cover
│   │   ├── book.bin            # Metadata, spine, and table of contents
│   │   └── css_rules.cache     # Parsed CSS rules
│   ├── xtc_12471232/           # XTC progress, statistics, and base cover
│   ├── txt_12471232/           # TXT/Markdown progress and page index
│   └── .attic/                 # Recoverable orphan moves
├── cache/
│   ├── thumbs/                 # Sharded exact-size cover thumbnails
│   ├── layouts/                # Sharded disposable chapter/page layouts
│   └── fileindex/              # File-browser indexes
├── backups/
│   └── reading-stats/          # Complete .cstats archives
└── migration/
    └── legacy-import-v1.complete
```

The listing is representative rather than exhaustive. Temporary files, backups, performance logs, and derived library indexes may also appear.

## Clearing Cache Data

Do not manually delete `/.duet` or either legacy hidden root as a first troubleshooting step. Deleting `/.duet/state` removes current settings, recent books, favorites, bookmarks, clippings, credentials, global statistics, sync records, and achievements. Deleting `/.duet/books` removes durable per-book progress, statistics, settings, covers, and metadata. `/.duet/cache` is disposable in principle, but use the supported UI actions so durable state and current cache identity remain intact.

To clear EPUB/XTC render caches from the device UI without deleting settings or global stats, use:

**Settings > System > Files & Cache > Clear Reading Cache**

## Book Moves And Cache Identity

Cache folders are path-based. Moving a book file can create a new cache directory, so the moved copy may start with fresh reading progress unless the firmware migrates the cache for that move. Duet migrates cache and bookmark data for the built-in move-to-Read flow and related file-browser move actions.

EPUB reader font, page layout, styling, and reading-aid settings normally come from the global Reader settings. If those settings are changed from inside an EPUB, Duet stores a per-book override in that book's `reader_settings.bin`; books without that override continue to follow the global defaults. EPUB render mode is also stored per book so a problematic title can be switched to Balanced or Light rendering from the File Browser or Recent Books long-press menus before opening it.

EPUB clippings and highlights live outside the EPUB render-cache folder in `/.duet/state/clippings/`. Each book gets a binary clipping file named from the book type and the CRC32 of the book path. The same clipping record powers the in-reader highlight, the clipping list, and jump-back behavior. Duet also appends a Kindle-style text export to `/My Clippings.txt` on the SD-card root; that export is human-readable and append-only, so deleting a clipping in the UI removes the in-app saved clipping but does not rewrite old text already exported to `/My Clippings.txt`.

Cache data is cleared by supported Duet delete/move flows. If you remove or rename books outside Duet by editing the SD card directly, old cache folders may remain until you clear reading cache.

Complete reading-statistics archives live in:

```text
/.duet/backups/reading-stats/
```

For binary file layout details, see [File Formats](./file-formats.md).
