# Cover-picker freeze — investigation brief (for Codex)

## The symptom
Opening the **All Books** shelf (~266 EPUBs, pure book folder, no subfolders) in the
cover-grid/carousel picker leaves the device **stuck**: the screen stops updating and the
user force-restarts. Reported twice on hardware (Xteink X3 and X4), on builds
`1.4.0-RC-lauren-x3x4-repair18-1-{tiny,xlarge}`. Small folder levels (7 entries) are
verified fast: 34 ms load / 22 ms first paint per the on-card breadcrumb.

Two fixes were already shipped and did NOT cure it — treat their hypotheses as
disproven-or-insufficient:
1. Idle-gating of background cover work (`COVER_WORK_MIN_IDLE_MS=1200`,
   `COVER_GENERATION_MIN_IDLE_MS=3500`, `FileBrowserActivity.cpp:81-82`).
2. Memoization of legacy-thumbnail probes (each legacy path probed at most once per
   session): `UITheme.cpp:229-240` and `Epub.cpp` `migrateLegacyThumb` (~line 184).

## Data context you must know
- On July 17 the library was reorganized under `/Books`. Cache identity is the book
  *path* hash, so most of the 266 books have **new cache dirs**: thumbnails may not
  exist yet at any size, and old caches are orphaned.
- `/.crosspoint/` holds ~270+ cache dirs. FAT has no directory index: **every path
  resolution inside it is a linear scan** — worst case seconds per miss. This physics is
  the root cause of every past perf incident. Hot files were moved to `/.crossink/`
  (small dir) for this reason; thumbnails live sharded under `/.crossink/thumbs/<hex>/`.
- SdFat on hardware: only one open handle per file.

## STATUS as of repair18-16 (2026-07-23, FINAL HANDOFF BRIEF)

SOLVED and user-verified: input responsiveness ("the picker is working
flawlessly"), instant placeholders (35-52 ms), fast lists, index-only badges
(zero SD each), sharded thumbnail lookups, OOM crash fixed, boot/sleep
branding and ghosting fixed, breadcrumb telemetry throughout.

THE ONE REMAINING PROBLEM: covers. Complete proven chain:
1. genQ=9 both devices — generation queue wiring works.
2. The reader's 96 KB heap floor blocked all generation (measured shelf idle:
   72-77 KB free, 55-57 KB maxAlloc). Fixed 18-10: picker gate 64/44 KB.
3. Ungated generation locked input for MINUTES per never-opened book: the
   build is secretly cache CONSTRUCTION — creating a dir + several files
   inside a ~2,400-2,700-entry FAT root, a linear scan per create. Users
   force-restarted; 18-13 added a 20 s runaway breaker (queue abandoned for
   the visit, "genslow" heartbeat).
4. Latest heartbeats (both devices): phase=gen detail=0 — generation now
   STARTS (gate passes) and sessions end mid-first-build. Pre-cleanup grind,
   exactly as modeled.
5. The strategic fix shipped 18-13/18-14: Settings > System > Clean Library
   Cache moves orphaned cache dirs (~90% of the root, from a July 17 library
   reorganization) to /.crosspoint/.attic. First version aborted both
   devices — holding 2,400 names needed a 64 KB reserve (LAW: never hold
   unbounded name sets; batch in a few KB). 18-14 batches 128 per pass.

## FINAL TEST RESULTS (2026-07-23, handoff point — user's devices run 18-17)
- X4 Clean Library Cache COMPLETED: 112 moved, 1196 kept, 1 failed. 1196
  kept >> 266 books — the card likely holds older copies of the library
  (pre-reorg backup folder), whose caches are legitimately live. Root is
  therefore still ~2,300 entries; ask the user about consolidating, or scope
  a purge mode to a canonical folder.
- X4 All Books after cleanup: appeared frozen for minutes, then A COVER
  RENDERED and input returned. THE PIPELINE IS PROVEN END-TO-END: build ->
  sharded write -> reload -> paint all work; the breaker then abandoned the
  rest of the queue as designed. The sole remaining defect is per-book build
  DURATION (cache construction inside a still-crowded ~2,300-entry root) and
  the fact that a build in flight blocks input and cannot be interrupted.
  Your problem statement is exactly: make working cover generation fast
  and/or interruptible. KNOWN GAP: the 20 s breaker only trips AFTER a slow
  build completes.
- X3 Clean Library Cache: wedges at "0 | 0" (first <=127 entries of the
  walk) on 18-17's hardened two-phase scan, while identical code completed
  on X4 — X3-card-specific. 18-18 (sealed, packaged, NOT yet flashed) adds
  /.crossink/purge_hb.txt naming each directory entered: flash X3 with it,
  reproduce, force-restart, read the breadcrumb. Also worth: read-only
  filesystem check of the X3 card from a computer.
- User escape hatch for any freeze: hold power ~10-15 s.

## DECISION TREE — read the user's latest test result first
The user is flashing 18-16 and running: Clean Library Cache → All Books →
idle. Harvest /.crossink/picker_timing.txt + picker_hb.txt from the card.
- Covers appear, each a few seconds: core problem SOLVED. Your work is the
  architecture polish (below) — generation that never blocks input at all.
- "genslow <ms>" in the heartbeat: builds still exceed 20 s post-purge.
  Measure WHERE (instrument generateThumbBmpInternal phases: cache build vs
  cover decode vs BMP write); suspect remaining root crowding (verify entry
  count — did the purge complete? SUCCESS screen shows moved/kept), the
  epub itself (huge images), or the metadata build.
- Purge itself fails/crashes again: read crash_report.txt (MEPC), check
  PurgeOrphanCachesActivity — scanning recursion, batching loop, rename
  failures (attic collisions leave failedCount>0).
- gens=0 again with genQ>0 and no genskip/genslow: re-check gate order and
  deadlines (pendingCoverGenerationNextAt pushed +30 s after skips).

## What 18-13..18-16 shipped (current HEAD)
- NEW: Settings > System > "Clean Library Cache"
  (src/activities/settings/PurgeOrphanCachesActivity.{h,cpp}): scans the card
  for live books (recursive, skips dot-dirs, depth cap 8), computes live cache
  names (Epub::cachePathForFilePath fnv64; XTC mirrors Xtc.h:31 std::hash),
  then MOVES unmatched epub_*/xtc_* dirs (isBookCacheDirectoryName gate) to
  /.crosspoint/.attic/ via rename. Nothing deleted; stats/bookmarks inside
  orphans stay recoverable. ~2,400 of ~2,700 entries are expected orphans
  from a July 17 library reorganization; root should shrink to ~300.
- Generation stays ENABLED with a runaway circuit breaker: any single build
  >20 s abandons the queue for the visit (heartbeat "genslow" with ms) — a
  crowded root degrades covers but can never freeze input for minutes again.
- Boot splash paints FULL_REFRESH (was ghosting the firmware-updater screen
  behind the mark); first home paint per power-on also FULL_REFRESH.

## Expected 18-13 test sequence (tell the user if she hasn't done it)
Flash → Settings > System > Clean Library Cache (few minutes, has progress)
→ open All Books → idle. Post-purge each generation should be seconds; if
any build still exceeds 20 s the breaker trips and picker_hb.txt says
"genslow <ms>".

## Remaining work (this is yours)
1. The correct architecture: generation that runs regardless of input and
   aborts within ~100 ms of a button press. Constraint learned via the 18-4
   crash pair: FsFile handles bypass the HalStorage mutex, so cross-task SD
   is forbidden unless ALL other SD is suspended during the operation
   (handshake), or a cooperative cancel callback is threaded through
   Epub::load and the thumbnail pipeline (Epub::readItemContentsToStream
   already accepts StreamCancelCallback — a wedge to build on).
3. Possibly pre-generate covers where blocking is expected (a "Preparing
   covers N/266" maintenance screen, like sync prep) so browse never builds.
4. LibraryInsights/timeline duplicates: attic'd orphan stats no longer feed
   local scans (this FIXES the duplicate-timeline-rows symptom) but verify
   nothing user-visible regresses; person-level history lives in
   /.crossink journals/ledgers and synced_* files and is unaffected.
5. The X3 updater OOM (18-5 era): SD firmware update needed recovery mode
   (UP-mapped button + power from off) when heap was bloated. Fixed by the
   memo diet, but worth a defensive look at the updater's buffer strategy.

## Original ranked hypotheses (verify, don't trust)
**H1 — Blind window during folder entry.** `navigateIntoDirectory` sets
`folderTransitionInProgress=true` (`FileBrowserActivity.cpp:1255`), and *every* render
path bails while it's set (2147, 2189, 2210, 2251, 2326, 3278, 3379, 3682). The flag
clears only at 1309, *after* `loadFiles()` returns. For a first visit to a 266-book
folder, `loadFiles` → `loadFilesIntoVector`/index build (595-724, recursion at 571) can
take a very long time on FAT — during which nothing paints and input appears dead:
indistinguishable from a freeze. Needs: measurement, then either a pre-transition
"opening…" paint, chunked/yielding index build with progress, or both.

**H2 — Paint suppression that never lifts.** `cancelCoverRenderForExit()`
(1314-1321) sets `folderTransitionInProgress=true` and **nothing ever clears it** on
that path. Called when opening a book from the picker (1470) and when leaving to Home
(1264, 1493). If the same `FileBrowserActivity` instance ever resumes after that call
(activity model: check whether onSelectBook/onGoHome destroy it), every subsequent
render bails forever → permanent freeze that survives all thumbnail fixes. Even if the
activity is normally destroyed, this is a landmine; consider clearing the flag in
`onEnter`/resume or scoping it properly.

**H3 — Remaining synchronous SD storms.** (a) `loadBookSignal` fallback does up to
~10 `Storage.exists` calls *per book* against crowded `/.crosspoint` when a book isn't
in the LibraryInsights cache (2402-2412) — the insights cache magic was recently bumped
(LIN2), so a stale/absent cache makes the whole shelf take the fallback. (b)
`lib/Xtc/Xtc.cpp` thumb paths are **not** memoized (Epub and UITheme are) and
`getThumbBmpPath(w,h)` re-runs `xtcEnsureThumbShardDir` every call (~300-330). (c)
First-visit thumbnail *generation* opens each EPUB (`generateThumbBmpInternal`,
`Epub.cpp:1104`) — 266 pending generations post-reorg; gated to 3500 ms idle, but audit
what the visible page does synchronously (`coverThumbPathForEntry` callers: 1102, 2917,
2593-2604).

## Telemetry available (and its blind spot)
- `/.crossink/picker_timing.txt` — written **only on clean exit**
  (`FileBrowserActivity.cpp:813-822`): entries, loadFiles ms, firstRender ms,
  signal/generation counts+totals+max. A frozen session leaves NOTHING — strongly
  consider adding a heartbeat breadcrumb (e.g. rewrite a small
  `/.crossink/picker_hb.txt` at phase boundaries: enter → loadFiles done → first paint →
  each N covers) so the next hardware freeze tells us exactly where it died.
- Also on card: `boot_timing`, `home_timing`, `reader_timing`, crash reports with MEPC/RA.
- Serial logs exist but the user flashes via SD, typically without a serial console.

## Constraints (project law)
- ESP32-C3, single-core, ~380 KB heap, no PSRAM, single 48 KB framebuffer.
- **Input first, always**: placeholders paint immediately; background work yields the
  moment a button is pressed. No blocking multi-second windows without a painted status.
- Read `AGENTS.md` / `CLAUDE.md` (this repo) for memory/HAL/error rules, and
  `.claude/CONTEXT.md` for durable gotchas.
- Process: **one build in flight** — land everything on the current cycle
  (next would be `repair18-2`), bump version in `platformio.ini`, add a `CHANGELOG.md`
  entry, no unrelated cleanup.
- Never touch sleep/wake gesture code in this effort.

## Build & verify
- `pio run -e tiny` (X3) and `pio run -e xlarge` (X4); simulator:
  `python3 scripts/run_simulator_smoke_test.py --device x3` (add `--timeout 600`).
- Hardware test: flash both bins, open All Books (266 EPUBs), expect: instant
  placeholder paint, buttons responsive throughout, covers filling progressively,
  clean-exit breadcrumb written, second visit fast.
- Definition of done: no perceived freeze on first visit to a 266-book shelf, and a
  heartbeat breadcrumb that would localize any future freeze from the card alone.
