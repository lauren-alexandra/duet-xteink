# Duet Alpha Testing

Duet alpha builds are for volunteers who are comfortable backing up an SD card, flashing firmware, collecting logs, and restoring a known-good build. They are not yet recommended as a first e-reader firmware experience.

The current tester version is `v0.1.0-alpha.7`. Internal repair labels such as `wpm2` and `indexing2` are development history, not Duet release versions.

For a practical first pass on this exact candidate, follow the [Alpha.7 Device Acceptance](ALPHA7_ACCEPTANCE_QUICKSTART.md) route and record the results in the [Physical Test Matrix](PHYSICAL_TEST_MATRIX.md).

## Before Installing

1. Confirm whether the device is an Xteink X3 or X4.
2. Copy the complete SD card to a computer.
3. Keep a known-good firmware BIN and recovery instructions available.
4. Verify the SHA-256 checksum of the Duet BIN.
5. Copy only the BIN for the correct device to the SD-card root.
6. Do not place more than one firmware BIN at the SD-card root.

## Known Issues and Test Targets

These are intentionally public. An alpha tester should know what may fail and what evidence will help improve it.

### Fix Included, Physical Verification Needed

- X4 post-install cleanup detects the first boot of a new device/version from an SD-card marker. Alpha.2 relied on the hardware `AfterFlash` route, but SD-card updates restart as a generic software reset and could leave the updater screen ghosting through Home. Verify that the cleanup occurs once after the alpha.7 installation and does not repeat on ordinary sleep/wake.
- X4 sleep once again shows **Going to sleep**, then paints the selected sleep frame with one final full update. Ordinary X4 wake no longer runs the firmware-flash-only black/white scrub before the Duet splash. Verify the settled sleep image, visible flash count, wake time, and old-screen residue. X3 sleep/wake behavior is intentionally unchanged.
- Library input is intended to remain responsive while covers are generated. Verify this separately in list, grid, and carousel views on both devices.
- Carousel movement now reuses overlapping covers, preloads one hidden cover beyond each visible edge, and requests higher-quality persistent center and adjacent thumbnails. Verify quality, ordinary next/previous movement after the initial seven-cover window hydrates, and persistence after leaving and returning to a folder.
- Grid cursor refreshes on both devices now release off-page cover work and use a full fast refresh when a partial update is unsafe. The X4 display driver also has a no-throw allocation fallback for the specific low-memory crash captured during physical testing.
- Cover grids retain the current page and can hydrate the immediately previous and next pages within a guarded memory budget. Verify forward and backward page changes after the first neighboring pages have loaded.
- New Duet writes use the canonical `/.duet` namespace. Verify the one-time non-destructive import, second boot, and preservation of active book, progress, settings, statistics, achievements, covers, and catalogs. The inherited hidden roots must remain intact as recovery sources.
- Disposable chapter layout caches are sharded to avoid large FAT-directory scans. Verify first-use migration and later chapter transitions.
- Guarded chapter pre-indexing starts earlier and records completion, cancellation, heap deferral, failure, and duration in the reader timing log. At the temporary two-page preview boundary, the next press must trigger completion rather than repainting the same page forever.
- Current-book and Home speed fields now show estimated WPM after at least ten seconds of reading and measurable progress. Historical aggregate trend records still use relative screen-page pace internally because old journal entries do not contain per-book word totals.
- Nearby Stats Sync now exchanges the readers' CRC-protected Stats Date so date-derived figures such as Days Reading and Daily Average can converge. Repeated X3-to-X4 and X4-to-X3 physical verification is still required.
- Complete `.cstats` archives collect canonical state and per-book statistics from `/.duet`, while mapping inherited `/.crossink` and `/.crosspoint` records into the canonical archive layout. Both simulators pass a non-empty content-level export/restore round trip; verify the same flow on physical X3 and X4 cards before relying on it as the only backup.
- Locked sleep-image cycling is adapted from CrumBLE's original one-tap feature. Duet offers Off/1/2/3 clicks and defaults to three to reduce accidental changes. The code intends to count the wake press as the first click, but the reliable physical sequence may be one initial wake press plus three deliberate taps, making it feel like four. Verify the exact sequence separately on X3 and X4 and report whether the first wake press was counted.

### Large-Library Cover Prefill

For a large or multiply organized library, run the [desktop cover prefill](COVER_PREFILL.md) once after loading books. It creates the exact X3/X4 grid and carousel thumbnails on the computer so browsing is ready immediately, while the firmware remains responsible for genuinely new books later.

The process is incremental and produces `/.duet/state/desktop_cover_prefill.json` for verification. The complete AI-assistant prompt appears directly on the cover-prefill page and is also available as a [standalone prompt](AI_COVER_PREFILL_PROMPT.md).

### Known Performance Risks

- First-time on-device cover hydration can remain slow in very large folders, especially with a slower SD card or a low-memory X3. Desktop prefill is strongly recommended for those libraries.
- Complex, image-heavy, or unusually styled EPUB chapters may still require a visible synchronous build when pre-indexing cannot complete.
- X3 and X4 may behave differently under the same library load because their screen geometry and available heap differ.
- Cache regeneration after an update may make the first visit slower than later visits.

### Complete Stats Archives

Alpha.7 preserves the validated complete-archive contract while storing new exports under `/.duet/backups/reading-stats`. The archive includes global totals, journal and ledger history, the session log, Stats Date, library statistics, synced-device data and names, and each book's versioned statistics.

Physical acceptance is still required. Keep the full SD-card backup, confirm the export reports a non-zero file count and byte count, restore the archive, and verify global totals, a known book's statistics, session history, and synced-device figures. A successful simulator run does not prove the physical card or panel path.

### Needs Broader Reproduction

- Very large libraries and folders with more than 1,000 entries.
- Repeated X3-to-X4 and X4-to-X3 position and statistics sync.
- Sleep/wake, achievement notifications, and book resume across long-running use rather than a short smoke test.
- The locked sleep-image cycle gesture at Off, 1, 2, and 3, including whether the default three-click setting requires an additional initial wake press.
- X4 sleep/wake after the alpha.7 install path, including sleep from Home and from an open book.
- Unusual SD-card brands, capacities, filesystems, and near-full cards.

### SD-Card Health

Duet reads and writes books, settings, progress, statistics, and caches on the SD card. Filesystem damage can therefore imitate unrelated firmware failures. An allocation-bitmap error or overlapping cluster may show up as slow folders, missing caches, crashes, or behavior that changes after a reboot.

Do not start by deleting `/.duet`, `/.crossink`, or `/.crosspoint`. Copy the complete card to a computer, then use the operating system's disk utility to verify the filesystem. On macOS, a read-only check is:

```bash
diskutil verifyVolume "/Volumes/NAME OF CARD"
```

If verification reports damage, keep the backup and use Disk Utility First Aid or `diskutil repairVolume` before further firmware diagnosis. Eject the card through the operating system after every computer-side write. See [Troubleshooting](troubleshooting.md#sd-card-filesystem-errors).

## High-Value Test Areas

- Cold boot, sleep, wake, repeated book open/close, and crash recovery.
- Library list, 2x2, 3x3, 4x4, and carousel navigation before and after covers hydrate.
- Large folders, slow SD cards, and libraries ranging from a few books to more than 1,000 files.
- Chapter transitions and guarded next-chapter pre-indexing.
- Search by any title word, author, and series.
- Reading progress, bookmarks, stats, achievements, favorites, and settings across restart and firmware update.
- Nearby and KOReader position sync, with explicit confirmation before moving to another device's position.
- Font selection, every built-in size on each device, a six-size SD-card family, real and simulated styles, dictionary lookup, sleep images, and low-memory books.

## Useful Logs

Exit the affected screen or book when possible so buffered timing data is written before removing the SD card.

- `/.duet/state/reader_timing.txt`
- `/.duet/state/picker_timing.txt`
- `/.duet/state/picker_hb.txt`
- `/.duet/state/nearby_sync_timing.txt`
- `/.duet/state/home_timing.txt`
- `/.duet/state/boot_timing.txt`
- `/.duet/state/desktop_cover_prefill.json`
- `/crash_report.txt`

Attach only the smallest relevant files. Review them first because filenames and book paths may be personal.

## A Good Bug Report

GitHub provides two structured forms:

- **Alpha Test Report** for successful, mixed, or failed test sessions, including useful timings and areas tested.
- **Alpha Bug Report** for one reproducible defect with steps, expected behavior, recovery details, and the smallest relevant evidence.

Include:

- Device model and exact Duet version.
- SD-card brand, capacity, and filesystem if known.
- Approximate number of books in the affected folder.
- Library view and sort mode.
- Exact button sequence and whether covers were still loading.
- Expected behavior, observed behavior, and whether the device recovered.
- Relevant logs and a privacy-safe photo or video.
- Whether the desktop cover prefill was run and whether `failed_books` is empty in `/.duet/state/desktop_cover_prefill.json`.
- Whether a read-only SD-card filesystem check passes.

Never upload an ebook to demonstrate a bug. If a particular EPUB is required, describe its structure or create a minimal public-domain reproduction.

## Copy-Paste Report

```text
Device:
Duet version shown in Settings > System:
Install type: first flash / Duet update
SD card: brand, capacity, filesystem, approximate free space
Affected folder: path and approximate book count
Library view and sort:
Desktop cover prefill: not run / passed / passed with failures
Filesystem verification: not run / passed / reported errors

Steps:
1.
2.
3.

Expected:
Observed:
Recovered without restart: yes / no
Relevant log files:
Privacy-safe photo or video:
```

## Alpha Expectations

- New versions may require cache regeneration.
- Performance can differ between X3 and X4.
- A successful simulator run or firmware build is not physical acceptance.
- Known issues and test results are recorded with each prerelease.
- The current device-by-device record is in [Physical Test Matrix](PHYSICAL_TEST_MATRIX.md).
