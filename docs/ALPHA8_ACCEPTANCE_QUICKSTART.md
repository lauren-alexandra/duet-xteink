---
title: Alpha.8 Device Acceptance
parent: Alpha Testing
nav_order: 1
---

# Duet Alpha.8 Device Acceptance

Use this route to test the exact `v0.1.0-alpha.8` build on an X3 or X4. It does not replace the full [Physical Test Matrix](PHYSICAL_TEST_MATRIX.md); it puts the changed and highest-risk behavior in a practical order.

## Before The Flash

1. Copy the complete SD card to a computer.
2. Keep the currently working firmware BIN available.
3. Verify the device BIN against the published `SHA256SUMS.txt`.
4. Put only the correct device BIN at the SD-card root.

## Install And Resume

1. Install the firmware and open **Settings > System**. Confirm it says `Duet 0.1.0-alpha.8`.
2. Let Home settle and confirm the expected active book remains selected.
3. Open that book and confirm it resumes at the expected position.
4. Turn a page, return Home, reopen the book, sleep, and wake. Confirm the position, progress, and reading time remain available.

## Font-Size Direction And Position

These controls and relayout checks are the highest-priority first smoke tests for the exact Alpha.8 artifacts.

1. On X3, open an EPUB and set **Settings > Controls > Side Button Long-press Action** to **Change Font Size**.
2. Hold the physical right side button for about one second. Confirm the font becomes larger.
3. Hold the physical left side button for about one second. Confirm the font becomes smaller.
4. Confirm each change reflows the page without advancing the visible reading position. If the book is on the first page of a chapter, it must remain on that first page.
5. Repeat the position check on X4. Up should increase the font size and Down should decrease it; neither action should move the visible text forward or backward.

The displayed page number may change because a different font size creates a different number of screen pages. The text being read should remain anchored.

## Statistics Navigation And WPM

1. Open Reading Stats from Home and from inside a book.
2. Confirm Current Book displays a numeric WPM for an enriched book with measurable progress and attributable reading time.
3. Move through Pace, Reader DNA Details, Reading Signature, and Signature Details. Confirm qualifying current-book WPM appears without a long metadata-loading pause.
4. Confirm Pace Trend uses a labeled line graph and statistics chart labels do not collide with the chart.
5. Confirm an untouched EPUB without compatible `META-INF/x-locations.json`, or a book without enough progress or reading time, shows a dash rather than a screen-page value mislabeled as WPM.
6. Return directly from the book to Home and confirm the transition completes promptly without losing progress or reading time.

## Achievements And Two-Reader Sync

Run these checks with Alpha.8 on both devices.

1. Nearby Stats Sync from X3 to X4, then X4 to X3.
2. Confirm both devices report success and current-book time, progress-derived WPM, total word count, and detailed statistics converge.
3. Confirm achievement milestones converge without replaying every old achievement popup.
4. Repeat the sync and confirm totals and achievements do not double.
5. Nearby Position Sync for a disposable or easily verified book and confirm position changes only after explicit approval on the receiving device.

## EPUB Preparation

1. Run `python3 scripts/enrich_epub_locations.py --dry-run "/path/to/epub-or-folder"`.
2. Apply with `python3 scripts/enrich_epub_locations.py --backup --backup-dir "/path/to/backup-folder" "/path/to/epub-or-folder"`.
3. Confirm already enriched EPUBs and macOS `._*.epub` sidecars are skipped without failures.
4. Keep `.duetbak` backups outside Ready to Load and outside SD-card book folders.

## Library And Covers

Use a folder with enough books to cross a page boundary.

1. Test 2x2, 3x3, 4x4, and carousel views.
2. Move the picker while covers hydrate, then move forward and backward across a page boundary.
3. Confirm covers persist and navigation does not wait for every thumbnail.
4. Open More Info for a highlighted book and confirm its cover, title, author, status, and description match the selected book.

## Evidence To Keep

- Exact version shown on each device.
- Pass, fail, or intermittent notes for each section.
- Approximate delays when responsiveness matters.
- Settled sleep photos rather than photos taken during a refresh.
- The smallest relevant diagnostic files after reviewing paths and filenames for private information.
- The completed [Physical Test Matrix](PHYSICAL_TEST_MATRIX.md).
