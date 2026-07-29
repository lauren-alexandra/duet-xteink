# Duet v0.1.0-alpha.4 Pause Checklist

Paused and resumed: July 23, 2026

## Safety State

- [x] No device was flashed during this work block.
- [x] No EPUBs or private library data were packaged or exposed.
- [x] Existing unrelated dirty-worktree changes were preserved.
- [x] The moved-book stats identity repair is complete and compiled.
- [x] Distinct X3 and X4 Alpha 4 firmware artifacts were built and packaged.
- [ ] Physical-device acceptance remains pending until Alpha 4 is flashed and
      exercised on both readers.

## Completed And Verified

- [x] X4 Dashboard Home now permits all seven current-book stat rows, matching
      X3 instead of silently capping the X4 at six.
- [x] The seventh row uses **Estimated Finish Date** while a book is in progress
      and **Finished Date** only after that book is marked complete.
- [x] X3 and X4 simulator builds passed after the seven-row Dashboard fix.
- [x] Separate X3 and X4 Dashboard screenshots showed all seven rows fitting.
- [x] Reader DNA and Reading Signature retain separate panels and eight metrics
      each, with the explanatory headings **What you've built** and
      **How you read**.
- [x] The arbitrary overall score and “not a grade” language were removed.
- [x] Streak milestones were expanded through three years with compact labels
      for long values.
- [x] The annual heatmap was enlarged, its summary metrics were moved lower,
      chart-label spacing was cleaned up, and Months/Year were placed together
      before Devices.
- [x] Font-size, font-family, and line-spacing changes now lay out a short
      visible-page preview first and defer cancellable full-chapter indexing.
- [x] Chapter timing diagnostics were added under
      `/.crossink/reader_timing.txt`.
- [x] Same-path detailed book-stat sync now has simulator coverage for
      completion status, finished date, and manual-finished-date state.
- [x] Canonical per-book stats aliases now survive library folder moves, with
      registration, persistence, resolution, snapshot publishing, backup, and
      restore coverage.
- [x] Moving a finished EPUB into `/Read` retains one stats identity and keeps
      completion state available to sync.
- [x] Dashboard Extended processes input before deferred cover work, hydrates
      one cover per idle slice, and queues a lightweight selection-only repaint.
- [x] Dashboard Extended supports six customizable current-book cells plus four
      customizable overall stats, with compact labels and durations that fit on
      both X3 and X4.
- [x] Full X3 and X4 simulator smoke suites passed with the Dashboard Extended
      screenshot and immediate-selector-state regression.

## Sync Status

- [x] The latest nearby reading-stats exchange eventually completed and merged.
- [ ] Confirm on hardware that completion state and Finished Date transfer after
      the moved-book identity repair.
- [ ] KOReader progress sync remains separate from Duet Reading Stats Sync.

## Cover Quality And Picker Work

- [x] Audit exact-size cache selection in 2x2, 3x3, and 4x4 grids, carousel
      center/adjacent covers, Home, Dashboard, and Book Info.
- [x] Reproduce the remaining cover-persistence and per-page hydration failures
      without moving SD access off the input task.
- [x] Preserve immediate cursor movement, previous/current/next grid-page
      preloading, carousel lookahead, and sharded `/.crossink/thumbs/` caches.
- [x] Build representative before/after comparisons for illustrated,
      photographic, pale, dark, text-heavy, gradient, and fine-line covers.
- [x] Compare current Floyd-Steinberg, reduced diffusion with restrained
      contrast/sharpening, the Home-cover treatment, and any justified stronger
      1-bit method at actual X3/X4 sizes.
- [x] Select one dependable raster recipe and apply it to Mac prefill plus
      on-device EPUB/XTC generation where feasible.
- [x] Do not overwrite valid thumbnail caches unless regeneration is explicitly
      requested with `--force`.
- [x] All eight X3/X4 2x2, 3x3, 4x4, and carousel simulator smoke scenarios
      passed with immediate cursor movement and warm-cache persistence checks.
- [x] All picker file access remains on the input task; render callbacks consume
      RAM-only cover state.

## Public Alpha Work

- [x] Alpha tester, testing, cover-prefill, screenshot-plan, and release-note
      drafts exist under `docs/`.
- [x] Reconcile the alpha.4 release notes with the final code after all repairs
      are complete.
- [x] Finish public-source audit and private-library exclusion checks.
- [x] Produce public screenshots using public-domain cover art without private reading
      data.
- [x] Package distinct X3 and X4 `v0.1.0-alpha.4` artifacts after Lauren's
      approval.
- [ ] Replace the draft package label only after both physical readers pass the
      acceptance matrix.

## Resume Order

1. Finish the one-time forced cover prefill on both mounted cards.
2. Stage and checksum the distinct X3/X4 Alpha 4 firmware artifacts.
3. Eject both cards cleanly.
4. Flash and complete the physical-device acceptance matrix.

## Resume Context

- Repository: this checkout's root directory.
- Branch: `repair22-picker-chapter-cache`
- Primary in-progress files:
  - `scripts/prefill_cover_thumbnails.py`
  - `lib/Epub/Epub.cpp`
  - `src/activities/home/FileBrowserActivity.cpp`
  - `src/activities/home/HomeActivity.cpp`
  - `src/activities/home/HomeActivity.h`
  - `src/components/themes/reading_home/ReadingHomeTheme.cpp`
  - `src/simulator/SimulatorSmokeTest.cpp`
- Preserve the rule that all picker SD work stays on the input task. File
  handles bypass the storage mutex, and cross-task SD access has crashed both
  devices before.
