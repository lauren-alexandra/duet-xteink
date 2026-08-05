---
title: Alpha.9 Device Acceptance
parent: Alpha Testing
nav_order: 1
---

# Duet Alpha.9 Device Acceptance

Use this route to test the exact `v0.1.0-alpha.9` build on an X3 or X4. It does not replace the full [Physical Test Matrix](PHYSICAL_TEST_MATRIX.md); it puts Alpha.9's changed and highest-risk behavior in a practical order.

## Before The Flash

1. Copy the complete SD card to a computer.
2. Keep the currently working firmware BIN available.
3. Verify the device BIN against the published `SHA256SUMS.txt`.
4. Put only the correct device BIN at the SD-card root.
5. Confirm whether the reader is an X3 or X4. Never cross-flash the device-specific binaries.

## Install And Hardware Detection

1. Install the firmware and open **Settings > System**. Confirm it says `Duet 0.1.0-alpha.9`.
2. Confirm the updater leaves the **Update Complete** screen and the reader reaches Home without a reset loop, power-latch failure, or SD Card Error.
3. Let Home settle and confirm the expected active book remains selected.
4. Open that book and confirm it resumes at the expected position.
5. Turn a page, return Home, reopen the book, sleep, and wake. Confirm the position, progress, and reading time remain available.

## Font Relayout And Reading Position

1. Open a known-good EPUB and note the first complete sentence visible on the page.
2. Change font size once. Confirm the text reflows around the same reading location instead of advancing several pages.
3. Change font family, line spacing, and orientation one at a time. Confirm each successful change is retained after leaving and reopening the book.
4. If a complex chapter reports a memory error, reopen the book and confirm Duet restored the last fully rendered font and size rather than saving the failed selection.
5. Continue past the first temporary relayout pages and confirm the reader advances while the complete chapter finishes building.

The displayed screen-page number may change because different typography creates a different number of pages. The text being read should remain anchored.

## Small Caps And Extended Glyphs

1. Open an EPUB containing CSS small caps. Confirm small-cap words render as compact capitals and preserve surrounding bold or italic styling.
2. If you use CJK or another extended script, install a compatible SD font and confirm missing UI glyphs render from that active family instead of appearing as question marks.
3. Change or unload the SD font and confirm the prior fallback does not remain incorrectly cached.

## Clock And Network Features

1. Run **Settings > System > Clock Sync** while connected to WiFi.
2. Confirm the displayed time is correct after returning Home.
3. On X4, restart once and confirm time-dependent network and statistics screens no longer begin from a stale post-flash software clock.

## Existing Duet Features

1. Open Reading Stats from Home and from inside a book.
2. Test 2x2, 3x3, 4x4, and carousel views across a page boundary while covers hydrate.
3. Open More Info and confirm the selected book's cover, title, author, status, and description match.
4. Sleep from Home and from an open book, then wake and confirm no lockup or persistent updater ghosting.
5. If you have two readers, run Nearby Stats Sync in both directions and confirm totals and achievements do not double.

## Evidence To Keep

- Exact version shown on the device.
- Device model and, if known, whether it is a newer hardware/display variant.
- Pass, fail, or intermittent notes for each section.
- Approximate delays when responsiveness matters.
- Settled sleep photos rather than photos taken during a refresh.
- The smallest relevant diagnostic files after reviewing paths and filenames for private information.
- The completed [Physical Test Matrix](PHYSICAL_TEST_MATRIX.md).
