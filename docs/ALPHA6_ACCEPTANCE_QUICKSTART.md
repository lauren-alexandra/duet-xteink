# Duet Alpha.6 Device Acceptance

Use this short route to test the exact `v0.1.0-alpha.6` candidate on an X3 or X4. It does not replace the full [Physical Test Matrix](PHYSICAL_TEST_MATRIX.md); it puts the highest-risk checks in a practical order.

## Before The Flash

1. Copy the complete SD card to a computer.
2. Keep a known-good recovery BIN available.
3. Verify the device BIN against the published `SHA256SUMS.txt`.
4. Put only the correct device BIN at the SD-card root.

## First Boot And Migration

1. Install the firmware and open **Settings > System**. Confirm it says `Duet 0.1.0-alpha.6`.
2. Let Home settle. Confirm no updater text or previous screen remains visible.
3. Confirm `/.duet/migration/legacy-import-v1.complete` exists after a successful bounded import.
4. Confirm the previous active book, progress, settings, statistics, achievements, covers, and catalog remain available. Duet must not delete the inherited `/.crossink`, `/.crosspoint`, or `/.crossink-stats-backup` sources.
5. Restart once more. Confirm the migration does not repeat visibly and the same active book remains selected.

## Reader And Chapter Indexing

1. Open a known-good EPUB and confirm it resumes at the expected position.
2. Read through the first two visible pages of a new chapter. At the temporary preview boundary, confirm **Indexing** completes and the next press advances instead of repainting the same page indefinitely.
3. Change one typography setting that forces relayout, then repeat the preview-boundary check.
4. After at least ten seconds of reading and measurable progress, confirm the current-book speed field shows WPM.
5. Return to Home. Confirm the same book remains current and controls respond.

## Sleep And Wake

1. Sleep from Home, wait for the panel to finish, and photograph the settled sleep frame.
2. Wake, reopen the book, turn one page, then sleep from inside the book.
3. Wake again. Confirm the page, reading time, and progress were saved.
4. On X4, record the visible refresh count and any settled residue. On X3, record any freeze, delayed control, or unexpected current-book change.

## Library Responsiveness

Use a folder with enough books to cross at least one page boundary.

1. Test 2x2, 3x3, and 4x4 grids.
2. Move the cursor while covers are still hydrating.
3. Move forward one page and back one page.
4. Confirm the neighboring page prefetch does not block input and the previous page does not become a blank reload.
5. Open carousel view and wait for its initial seven-cover window.
6. Move at least five books forward and five backward.
7. Confirm each press moves once, the newly entering outside cover is ready when possible, and visible covers do not blank and reload on every ordinary move.
8. Leave the folder, return, and confirm exact thumbnails persist.

Record the folder size, sort mode, SD-card details, and whether [desktop cover prefill](COVER_PREFILL.md) was run.

## Complete Stats Archive

Do this only after making the full SD-card backup.

1. Record global totals, the latest session, Stats Date, synced-reader figures, and statistics for one known book.
2. Export a complete `.cstats` archive from the Reading Stats backup tools.
3. Confirm the success screen reports more than zero files and bytes and that the archive appears under `/.duet/backups/reading-stats`.
4. For a content-level proof, open a disposable test EPUB, turn exactly one page, and exit to commit the controlled change.
5. Restore the archive and record the automatic pre-restore safety-copy filename.
6. Confirm the global totals, latest session, Stats Date, synced-reader figures, and known-book statistics match step 1.
7. Confirm the disposable post-export page-turn statistics are gone.

Do not delete the original export or safety copy until both readers have completed acceptance and their full SD-card backups have been checked.

## Two-Reader Checks

Run these in both directions:

1. Nearby Stats Sync from X3 to X4, then X4 to X3.
2. Confirm both devices report success and Days Reading, Daily Average, streaks, sessions, Started Books, dates, and per-book statistics converge.
3. Repeat sync without new reading and confirm totals do not double.
4. Nearby Position Sync for a disposable or easily verified book.
5. Confirm position changes only after explicit approval on the receiving device.

## Evidence To Keep

- Exact version shown on each device.
- Pass, fail, or intermittent notes for every row above.
- Settled sleep photos, not photos taken mid-refresh.
- Approximate delays and refresh counts.
- The smallest relevant diagnostic files from `/.duet/state`, after reviewing paths and filenames for private information.
- The completed [Physical Test Matrix](PHYSICAL_TEST_MATRIX.md).
