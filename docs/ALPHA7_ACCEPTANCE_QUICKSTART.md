# Duet Alpha.7 Device Acceptance

Use this route to test the exact `v0.1.0-alpha.7` build on an X3 or X4. It does not replace the full [Physical Test Matrix](PHYSICAL_TEST_MATRIX.md); it puts the changed and highest-risk behavior in a practical order.

## Before The Flash

1. Copy the complete SD card to a computer.
2. Keep the currently working firmware BIN available.
3. Verify the device BIN against the published `SHA256SUMS.txt`.
4. Put only the correct device BIN at the SD-card root.

## Install And Resume

1. Install the firmware and open **Settings > System**. Confirm it says `Duet 0.1.0-alpha.7`.
2. Let Home settle and confirm the expected active book remains selected.
3. Open that book and confirm it resumes at the expected position.
4. Turn a page, return Home, reopen the book, sleep, and wake. Confirm the position, progress, and reading time remain available.

## Statistics Navigation And WPM

1. Open Reading Stats from Home and from inside a book.
2. Move through Current, Progress, Book, Device, Synced, Devices, Trends, Pace, Reader DNA, Signature, Streaks, Start/Finish, Fastest, and the remaining pages.
3. Confirm every button press changes the selected page before expensive derived content loads.
4. Confirm Fastest, Start/Finish, and Devices may briefly show **Loading** but do not trap navigation or freeze the reader.
5. Confirm the Book, Device, Synced, and Devices pages sit together.
6. For a cataloged EPUB with saved progress and reading time, confirm Current Book and applicable pace pages show WPM. A book without enough data may show a dash.
7. Confirm the Streaks page names the days still needed for the next milestone above the personal longest streak.
8. Change a reading-date value or other statistic, return to a derived page, and confirm the cached summary updates.

## Font Download Back-Out

1. Open **Settings > Reader > Font Options > Manage Fonts**.
2. Enter the download route so the WiFi picker opens.
3. Back out without connecting.
4. Confirm the reader returns normally without restarting and the installed font families remain available.
5. If testing a real download, connect to WiFi, complete the manifest load, then confirm the font registry reloads after leaving the screen.

## Library And Covers

Use a folder with enough books to cross a page boundary.

1. Test 2x2, 3x3, 4x4, and carousel views.
2. Move the picker while covers hydrate, then move forward and backward across a page boundary.
3. Confirm covers persist and navigation does not wait for every thumbnail.
4. Long-press or open More Info for a highlighted book and confirm its cover, title, author, status, and description match the selected book.
5. For a large or multiply organized library, run [Desktop Cover Prefill](COVER_PREFILL.md) and confirm `/.duet/state/desktop_cover_prefill.json` reports no failed books.

## Two-Reader Checks

Run these in both directions:

1. Nearby Stats Sync from X3 to X4, then X4 to X3.
2. Confirm both devices report success and totals do not double when the same state is synced again.
3. Nearby Position Sync for a disposable or easily verified book.
4. Confirm position changes only after explicit approval on the receiving device.

## Evidence To Keep

- Exact version shown on each device.
- Pass, fail, or intermittent notes for each section.
- Approximate delays, including how quickly a stats page first responds and when its content finishes loading.
- Settled sleep photos rather than photos taken during a refresh.
- The smallest relevant diagnostic files from `/.duet/state`, after reviewing paths and filenames for private information.
- The completed [Physical Test Matrix](PHYSICAL_TEST_MATRIX.md).
