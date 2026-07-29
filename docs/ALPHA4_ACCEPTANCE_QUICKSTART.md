# Duet Alpha.4 Device Acceptance

Use this short route to test the exact `v0.1.0-alpha.4` candidate on an X3 or X4. It does not replace the full [Physical Test Matrix](PHYSICAL_TEST_MATRIX.md); it puts the highest-risk checks in a practical order.

## Before The Flash

1. Copy the complete SD card to a computer.
2. Keep a known-good recovery BIN available.
3. Confirm the device-specific Duet checksum:

   - X3: `1a2cb7da02bc6ccc3cbc1a1992071900a7fc1fe485bd65e65d9407dd545cd144`
   - X4: `95c0abee96d5f8ed8a6ce9a554581c87f03da63e8b4847fdd4675f257fd2b925`

4. Put only the correct device BIN at the SD-card root.

## First Ten Minutes

Record pass, fail, or intermittent for each step.

1. Install the firmware and open **Settings > System**. Confirm it says `Duet 0.1.0-alpha.4`.
2. Let Home settle. Confirm no updater text or previous screen remains visible.
3. Open a known-good EPUB. Confirm it resumes at the expected position.
4. Return to Home. Confirm the same book remains current and controls respond.
5. Sleep from Home, wait for the panel to finish, and photograph the settled sleep frame.
6. Wake, reopen the book, turn one page, then sleep from inside the book.
7. Wake again. Confirm the page, reading time, and progress were saved.

For X4, record the visible refresh count and any residue during both sleep cycles. For X3, record any freeze, delayed control, or unexpected current-book change.

## Library Responsiveness

Use a folder with enough books to cross at least one page boundary.

1. Test 2x2, 3x3, and 4x4 grids.
2. Move the cursor while covers are still hydrating.
3. Move forward one page and back one page.
4. Confirm the previous page does not become a blank reload.
5. Open carousel view and wait for its initial seven-cover window.
6. Move at least five books forward and five backward.
7. Confirm each press moves once and visible covers persist.
8. Leave the folder, return, and note whether cached covers remain available.

Record the folder size, sort mode, SD-card details, and whether [desktop cover prefill](COVER_PREFILL.md) was run.

## Complete Stats Archive

Do this only after making the full SD-card backup.

1. Record global totals, the latest session, Stats Date, synced-reader figures, and statistics for one known book.
2. Open **Settings > System > Reading Stats > All-time Stats > Export All Reading Stats**.
3. Confirm the success screen reports more than zero files and bytes. Record the `.cstats` filename written under `/exports`.
4. For a content-level proof, open a disposable test EPUB, turn exactly one page, and exit to commit the controlled change.
5. Open **Restore Reading Stats**, select the archive from step 3, and confirm the restore.
6. Record the automatic pre-restore safety-copy filename.
7. Confirm the global totals, latest session, Stats Date, synced-reader figures, and known-book statistics match step 1.
8. Confirm the disposable post-export page-turn statistics are gone.

Do not delete the original export or safety copy until both readers have completed acceptance and their full SD-card backups have been checked.

## Two-Reader Checks

Run these in both directions:

1. Nearby Stats Sync from X3 to X4, then X4 to X3.
2. Confirm both devices report success and Days Reading, Daily Average, streaks, sessions, and book statistics converge.
3. Nearby Position Sync for a disposable or easily verified book.
4. Confirm position changes only after explicit approval on the receiving device.

## Evidence To Keep

- Exact version shown on each device.
- Pass/fail notes for every row above.
- Settled sleep photos, not photos taken mid-refresh.
- Approximate delays and refresh counts.
- The smallest relevant files from `/.crossink`, after reviewing paths and filenames for private information.
- The completed [Physical Test Matrix](PHYSICAL_TEST_MATRIX.md).
