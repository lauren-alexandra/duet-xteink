---
title: Reading Stats Sync
nav_order: 6
---

# Reading Stats Sync

Duet can merge reading history directly between two nearby X3/X4 readers over ESP-NOW. It does not use WiFi, a server, or an account. Both readers must run the same Duet sync protocol.

## What Gets Synced

Protocol 5 exchanges a CRC-checked snapshot of each reader's:

- all-time totals
- per-book summary statistics
- detailed per-book dates, time, sessions, pages, pace, and reading status
- daily reading journal
- per-day, per-book attribution ledger
- clockless Stats Date
- device name

After a successful exchange, all-device totals, streaks, heatmaps, calendars, day counts, averages, library insights, series data, and achievement threshold evaluation can use the merged history. The achievement unlock ledger itself is device-local and is not transferred by Nearby Stats Sync or `.cstats`; milestones can be re-derived from imported history.

Nearby Stats Sync does **not** transfer EPUB files, settings, WiFi passwords, bookmarks, favorites, sleep screens, or the exact page currently open. Use KOReader Sync or in-book Nearby Position Sync for reading position.

## Using It

1. Open **Sync Reading Stats** on both readers.
2. Wait until both show the ready screen.
3. Press **Sync** on one reader only.
4. Keep both readers awake and on the sync screen until both report success.
5. Open Reading Stats on each device and compare the all-device totals, Stats Date, streak, and days read. Current-book reading position is a separate sync operation.

The first sync after a library change can spend time preparing detailed book records. Later syncs reuse the cached snapshot. Back can cancel preparation.

## Storage

Canonical sync state lives under `/.duet/state`; durable per-book records live under `/.duet/books`. Inherited `/.crossink` and `/.crosspoint` files remain read/import fallbacks during migration.

```text
.duet/
`-- state/
    |-- global_stats.bin
    |-- library_book_stats_v1.bin
    |-- library_book_details_v1.bin
    |-- reading_journal.bin
    |-- reading_ledger_v1.bin
    |-- reading_stats_clock_v1.bin
    |-- synced_stats/
    |-- synced_book_stats/
    |-- synced_book_details/
    |-- synced_journals/
    |-- synced_ledgers/
    |-- synced_stats_dates/
    `-- synced_names/
```

Each synced directory stores one validated snapshot per peer device. Incoming files are written to a temporary `.part` file, checked for expected size and CRC, and only then promoted. Repeated syncs replace that peer's prior snapshot instead of adding the same history again.

## Date Convergence

Xteink readers do not share one dependable wall clock. Duet therefore syncs the CRC-protected Stats Date used for day grouping. When both dates are valid, the later date is adopted. This aligns date-derived figures such as Days Read, Daily Average, streaks, and calendar placement without rewriting book start/finish dates or reading durations.

## Diagnostics

`/.duet/state/nearby_sync_timing.txt` records the last outcome, elapsed time, which payloads were sent and received, and late acknowledgement counts. Include that file with a bug report when one reader reports success and the other times out.

Do not publish real sync snapshots or full SD-card archives in a public issue: they may contain book titles, reading history, device names, and personal library metadata.

## Recovery

Before manual repair, back up the whole SD card.

- A leftover `.part` file can be removed after both readers are off.
- Removing one peer's file from every `synced_*` directory forgets that peer's last imported snapshot.
- Do not copy one reader's complete `/.duet`, `/.crossink`, or `/.crosspoint` folder onto the other card. Device-local settings, caches, and identities must remain separate.
- Use **Export All Reading Stats** before deleting or replacing any primary stats file.
