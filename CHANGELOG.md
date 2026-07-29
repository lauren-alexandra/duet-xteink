## [Unreleased]

### Fixed

- The deterministic public statistics fixture now connects its dummy Pace history to staged EPUB word counts, so the Pace Trend gallery screenshot contains representative WPM bars, 7-day and 30-day averages, and a trend instead of an empty chart.
- The public statistics fixture now varies fabricated session lengths, page counts, start times, daily totals, and calendar gaps so the gallery demonstrates realistic differences across Sessions, Calendar, Heatmap, Trends, and related pages.

## [0.1.0-alpha.7] - 2026-07-29

### Changed

- Current-book, Pace Trend, Reader DNA, and Reading Signature pace fields now use estimated words per minute when Duet can connect attributable reading time to a cataloged book's word count and saved progress.
- Book, Device, Synced, and Devices statistics pages now sit together near the beginning of the horizontal tab strip.
- Fastest Reads, Start/Finish, and Device Split load and cache their derived data after the initial page shell paints, so navigation does not perform the complete scan before responding.
- The Streaks page targets the first milestone above the personal longest streak and reports the consecutive days the current streak still needs.
- Backing out of the Manage Fonts WiFi picker preserves the installed-font registry and no longer restarts the reader.
- The public gallery now publishes 69 representative shared-interface images instead of duplicate X3 and X4 sets, and the search sequence's More Info screen matches its highlighted Love Theoretically result.
- The Desktop Cover Prefill page now includes the complete copy-ready AI assistant prompt directly on the page.

### Verification

- All 117 host tests pass.
- X3 and X4 simulator smoke tests pass.
- Both public firmware targets build within the OTA app partition.

## [0.1.0-alpha.6] - 2026-07-28

### Added

- Canonical `/.duet` storage with separate state, books, thumbnail/layout/file-index cache, reading-stat backup, and migration areas.
- Non-destructive first-boot import and lazy per-book migration from inherited `/.crossink`, `/.crosspoint`, and `/.crossink-stats-backup` data.
- Clean public X3/X4 versioning and deterministic alpha.6 firmware/package names.
- Self-contained firmware package contents, including current tester documents, cover-prefill scripts, feature and user guides, font source records, and redistributed built-in font licenses.

### Changed

- Current-book and Home speed fields now show estimated WPM after at least ten seconds and measurable progress; historical aggregate trend math retains relative screen-page pace where old records lack word totals.
- Chapter-preview completion starts immediately at the temporary preview boundary, preventing the reader from repainting the last preview page indefinitely.
- Chapter extraction and layout work uses smaller interruptible chunks with expanded completion, cancellation, deferral, failure, and duration telemetry.
- Carousel cover reuse includes one hidden lookahead cover in each direction, and grid browsing can prefetch the immediately neighboring pages inside guarded memory limits.
- Nearby Reading Stats Sync includes the latest merge, acknowledgement, progress, idle-timeout, Started Books, date, and chart-layout repairs.
- Complete statistics archives use canonical Duet paths while preserving recognized legacy-path import and restore compatibility.

### Release

- This is the first public tester prerelease. The source, builds, package, checksums, tester guidance, known risks, project identity, and complete lineage catalog are published together.
- Physical-device results remain tracked separately from build and simulator success; broader X3/X4 acceptance is the purpose of the early alpha.

## [0.1.0-alpha.4] - 2026-07-23

### Fixed

- Complete `.cstats` archives now collect global, journal, ledger, session, date, library, and synced-device statistics from `/.crossink` while preserving each book's `stats*.bin` files from its `/.crosspoint` cache.
- Restoring an older archive that stored root statistics under `/.crosspoint` now routes those files into the current `/.crossink` locations instead of reviving unread legacy state.
- Archive regression coverage now performs a non-empty X3 and X4 round trip, changes every major class of protected data before restore, and verifies that files added after the backup are removed from the restored snapshot.

### Verification

- Full X3 and X4 simulator smoke tests passed the CRC validation, safety-copy, rollback-safe replacement, and content-level restore contract.
- Physical X3/X4 export and restore acceptance remains required before public publication.

## [0.1.0-alpha.3] - 2026-07-23

### Fixed

- X4 post-install cleanup now keys off a persisted device-and-firmware version marker instead of the hardware wake reason. SD-card firmware updates restart as a generic software reset, so alpha.2 could skip the cleanup and leave the updater screen ghosting through Home.
- The SD firmware updater no longer performs a redundant full-panel scrub immediately before restarting. The newly installed firmware performs the cleanup once, on its first boot; ordinary X4 sleep/wake keeps the quieter alpha.2 sequence.
- X3 boot and sleep waveforms remain unchanged.

### Known Limitation

- Complete `.cstats` export and restore is not accepted in this alpha after root statistics moved to `/.crossink`. The current collector still scans the legacy `/.crosspoint` tree; use SD-card backups and do not rely on the archive feature until a later release records a passing non-empty X3/X4 round trip.

## [0.1.0-alpha.1] - 2026-07-23

Initial Duet alpha, continuing from internal development checkpoint Repair 18.24.

### Fixed

- Rebranded public-facing firmware, simulator, web portal, translations, updater, release artifacts, and documentation as Duet. Compatibility storage paths and internal identifiers remain unchanged so existing settings, progress, statistics, and caches survive the rename.
- Public firmware artifacts are now device-explicit: `Duet-X3-v0.1.0-alpha.1.bin` and `Duet-X4-v0.1.0-alpha.1.bin`.
- OTA now checks Duet's canonical release repository, recognizes the branded device artifact first, accepts legacy firmware names during migration, and compares alpha, beta, and release-candidate versions correctly.
- Nearby stats sync now exchanges each reader's CRC-protected Stats Date after the reading ledger and adopts the newer valid date. Date-derived home figures such as Days Reading and Daily Average therefore agree without changing book start dates, finish dates, sessions, reading time, or progress.
- Passive nearby sync now clears its ledger and Stats Date receive gates for every new attempt instead of allowing a previous transfer's state to satisfy a later sync.
- Desktop cover prefill accepts repeatable `--cover-override MATCH=IMAGE_PATH` arguments for malformed or coverless EPUBs while preserving every valid exact-size thumbnail already on the card.
- Carousel navigation now keeps the four overlapping book covers in RAM when the selection moves, preloads one hidden cover beyond each visible edge, upgrades the center artwork quietly when a sharper size is available, and can paint an ordinary next or previous move without blanking the newly entering outside cover once the seven-cover window has hydrated.
- Grid cursor refreshes on both X3 and X4 now release off-page cover prefetches when heap is tight and fall back to a full fast refresh when a partial update is unsafe. The X4 panel driver additionally reuses one no-throw display scratch buffer instead of two, fixing the X4-specific allocation abort.

## [1.4.0-RC-lauren-x3x4-repair18-23] - 2026-07-23

### Fixed

- Nearby stats sync now waits for the fourth and final reading-ledger file on both readers before showing success. A reader that finishes first also acknowledges valid retransmitted tail packets, preventing the other reader from timing out after the data has otherwise transferred.
- Nearby sync writes `/.crossink/nearby_sync_timing.txt` with all four receive gates and late-ack counts for hardware diagnosis.
- Grid and carousel cover generation now checks the exact sharded thumbnail destination instead of mistaking a legacy fallback file for the requested image. Grid cells upgrade undersized cached covers at their real cell dimensions, and the selected cover can paint before the rest of the page finishes hydrating.

## [v1.4.0] - 2026-07-04

### Added

- Bionic Reading now offers Off, Normal, and Subtle modes; Subtle uses a shorter bold prefix while preserving the same low-memory fallback behavior.
- Configurable sleep-screen cycling from the locked device using Off, 1, 2, or 3 brief Power clicks, with three clicks as this build's default and incomplete gestures returning to sleep without changing the image.
- Expanded reading statistics with current-session signals, trends, daily goals, recent sessions, and a durable 366-day reading journal.
- Calendar day details now attribute reading time and page turns to individual books, with safe per-book time correction by date and clearly labeled legacy time that predates attribution.
- Reading Stats now use a horizontally scrolling top tab strip and include a book-progress gauge, 14-day activity chart, 12-month intensity heatmap, and library-completion gauge.
- Reading Stats now include a scrollable 90-day daily-minutes history whose selected date opens book-level details.
- Reading Stats now include a scrollable Started Books view with per-book reading time, sessions, and direct Book Info access.
- Optional Show Stats After Reading opens the current-book stats page after an explicit EPUB or XTC exit, while sleep continues to save time silently.
- Complete reading-stat archives can now be exported to and restored from CRC-checked `.cstats` files, with full validation, rollback-safe replacement, and an automatic pre-restore safety copy.
- Clockless devices now retain a CRC-protected Stats Date, use valid internal/NTP time when available, initialize from the firmware build date, and expose a manual date editor for accurate daily history after a cold start.
- Current Book statistics now distinguish a live session from the latest completed session and show counted-session time, session count, page-turn context, start/finish dates, and an accurately labeled days-since-start span.
- Reader Text Darkness now offers Normal, Dark, and Extra Dark levels, and Reader Refresh Mode offers Auto, Fast, Half, and Full refresh behavior.
- Guarded next-chapter pre-indexing now waits until the current page is visible and idle, yields during ZIP extraction and HTML layout, cancels for reader input or low heap/largest-block headroom, and retries at most once after interruption.
- Tracker-backed Library Overview, Reading Taste, and Series Progress pages, plus cover status badges and All/In Progress/Unread/Finished library filters.
- A compact SD-card library catalog generator that connects Calibre genre, spice, author, and series metadata to device reading statistics.
- Long-pressing Open on a highlighted EPUB or XTC now exposes More Info, including its cover, author, series, genre, spice level, reading status, progress, and a catalog-backed summary without opening the book.
- Library search can now find and open books by title, author, or series from Home and book-list action menus, with ranked type-ahead completions that do not load EPUB files.
- Optional 4-Cover Grid and Cover Carousel modes for book-only File Browser shelves, with persistent settings, lazy exact-size thumbnails, reading-status signals, and author-first sorting preserved behind title-only labels. Folder levels retain the standard list layout.
- Highlighted one-line File Browser titles now slowly scroll when they are too long to fit, pause at each end, and reset when navigation moves to another book.
- Dashboard UI theme for the Home screen, showing the current book cover and reading stats.
- Dashboard Extended UI theme for the Home screen, with a current-book card, saved chapter context, three recent covers, current progress, time read, estimated time left, pace, sessions, start and projected-finish dates, and a compact Today/Streak/Total/Sessions strip. It keeps its cover work bounded for X3 stability rather than caching full screens in memory.
- Nearby Position Sync for sending or applying the current EPUB position between two CrossInk devices over ESP-NOW.
- Web EPUB optimizer support for CrossInk location metadata, so optimized EPUBs can keep better progress and stable page numbers.
- Reading Stats support for XTC and XTCH books, including the XTC reader menu, Home and sleep screen stats, mark finished, delete stats, and cache-clearing preservation.

### Changed

- Repair18-22 moves disposable chapter HTML and pagination files into sharded `/.crossink/layouts` directories, avoiding repeated FAT scans across the 1,200-1,450-entry `/.crosspoint` root measured on Lauren's cards. Existing chapter caches migrate lazily; progress, bookmarks, stats, and durable book metadata stay in place.
- Next-chapter pre-indexing now starts earlier, retries after input and temporary low-memory interruptions, and records completion, cancellation, heap-skip, failure, and duration counters in `/.crossink/reader_timing.txt`.
- Cooperative cover-generation polling now preserves button edges for the normal picker loop, so a press that cancels generation also moves the selection instead of being consumed.
- Carousel artwork no longer duplicates low-resolution fallbacks in two RAM caches. The selected card and all four neighbors receive persistent higher-resolution targets sized for their rendered positions, leaving enough X4 heap to create the sharp replacements.
- Book folders can now sort author-first or title-first from Settings > System/Files and the browser long-press menu. Title sorting uses the library filename convention (`Author -- Title -- ...`) so large indexed shelves can reorder without opening every EPUB.
- Carousel covers now upgrade every visible slot to the sharper home-card thumbnail size instead of settling for old low-res grid thumbnails; grid cover cache reads also hydrate in slightly larger idle batches while expensive generation stays cancellable.
- Cover-grid and carousel thumbnail generation is now cooperatively cancellable while browsing. The picker still keeps all SD-card work on the input task, but EPUB first-load indexing, cover extraction, and JPG/PNG thumbnail conversion now stop when a button press is observed, so uncached covers cannot trap the cursor behind a multi-second or multi-minute build.
- Fixed an out-of-memory crash at the home screen (both devices, repair18-5): session-static probe caches now store 8-byte hashes instead of full path strings (~33 KB reclaimed), the cache-root enumeration lives in a compact children-only table that only the cover browser pays for, and background thumbnail generation returned to the input task — file handles bypass the storage mutex, so cross-task file work is unsafe.
- Reading-status badges now come purely from the in-RAM library index — zero SD work per badge. On-card telemetry showed every SD-probing strategy losing to the ~2,400-2,700-entry cache root (6.3 s per badge probing individually; 23 s per slice for a capped enumeration retry). Books with local reading history that have never been through stats sync show no badge until the next sync.
- Cover thumbnails now generate in the background while you browse: generation runs on the render task (the reader's guarded-preindex pattern), one book per pass, so buttons stay live and covers stream in during any pause. Previously generation during browsing was disabled entirely, so books whose thumbnails were lost to a library reorganization never got covers back.
- Clean Library Cache writes heartbeat breadcrumbs to the card (each directory entered, plus progress within large ones), so any freeze names its exact location.
- Clean Library Cache's scan no longer wedges: directories are fully read and closed before recursing, per-directory and total caps detect corrupted (cycling) directory chains, progress paints every 128 entries, and an incomplete or empty scan refuses to move anything rather than risk exiling live caches.
- The Duet mark sits 13 px right of ink-centroid center on boot and sleep screens so it centers visually over the text.
- The fallback sleep screen now says Duet under the mark.
- Cold boots scrub the panel with a black-then-white full cycle before the splash, lifting the firmware updater's burned-in ghost that a single full refresh could not clear on the X4 panel.
- Clean Library Cache no longer aborts on large cache roots: it works in 128-entry batches (a few KB of RAM) instead of holding every orphan name at once, and live-cache comparisons use 8-byte hashes instead of strings.
- New Settings > System tool: Clean Library Cache. Moves cache folders for books that no longer exist into a backup attic (nothing deleted). Real cards carried ~2,400-2,700 cache entries for 266 books after a library reorganization; every SD path lookup paid a linear scan of that directory. After cleaning, scans are roughly 9x faster and browse-time cover generation becomes viable.
- Cover generation during browsing stays enabled, now with a runaway circuit breaker: if a single cover build exceeds 20 seconds the queue is abandoned for that visit, so a crowded cache root can degrade covers but never freeze input for minutes. : on-device testing proved each never-opened book costs minutes of blocked input (its full cache must be built inside a ~2,700-entry directory). The queue and telemetry remain; generation returns once it is cancellable or the cache root is cleaned of orphaned entries.
- Cover thumbnails finally generate on large shelves: generation was gated behind the reader's 96 KB heap floor while the shelf idles at a healthy ~72-77 KB free with a 55 KB contiguous block (proven by on-card telemetry: 9 covers queued, all skipped). A picker-specific budget sized from the measured reality replaces it.
- The boot splash itself now paints with a full waveform, so firmware-updater and crash screens no longer ghost behind the Duet mark during the splash hold.
- The first home paint after every boot is a full deep refresh, clearing boot-splash ghosting (most visible on the X4 panel).
- Cover generation now frees the off-page bitmap cache when heap is short instead of silently skipping, and records every heap-skip (with live heap numbers) in the on-card picker breadcrumbs — "no covers" is now diagnosable from the card alone.
- The Duet mark's stem now rides the curve of the ink drop (approved by eye, one pixel at a time).
- The Duet mark is a touch bolder everywhere it appears (boot splash, quick-resume loading icon, web portal): wider stem, fuller flag, chunkier drop and dots.
- The boot splash with the Duet mark now lingers about three seconds so it can actually be seen; recovery and panic boots skip the hold.
- Page turns no longer rewrite the progress files on every page paint; position is kept in RAM and persisted on chapter changes, menu or sync entry, reader exit, and sleep, or after at most ten turns or one minute. This makes rapid page flips snappier and greatly reduces SD-card wear.
- The reader status bar now caches book size, chapter offsets, and the chapter title per chapter instead of re-reading them from the metadata cache on every page paint.
- Opening a cached book no longer parses and discards the entire CSS rule cache just to check its header, and books whose stylesheets exceed the CSS rule limit no longer re-run a futile full CSS rebuild on every open.
- Inline images no longer pause layout for a fixed 50 ms after each extraction, making image-heavy chapters open noticeably faster the first time.
- Nearby Stats Sync now keeps transfer files open across a whole file instead of opening and syncing per 192-byte chunk, fills each radio packet (228-byte chunks), and reuses the detailed per-book snapshot when no stats changed since the last sync — large-library syncs complete much faster.
- Nearby Stats Sync and Nearby Position Sync now leave their screens automatically after three idle minutes so a forgotten sync screen cannot hold the radio at full power and drain the battery.
- Highlighted File Browser titles now scroll through the name twice and then rest, instead of refreshing the panel indefinitely while the cursor sits on a long title.
- Reopening the same book no longer rewrites the recent-books list, app state, or last-active-book files when nothing changed.
- The Lyra 3 Covers Home theme now caches book context like the Carousel and Reading Home themes, so moving between covers no longer reloads metadata and stats from the SD card.
- Personal X3/X4 builds now include only English hyphenation patterns, freeing about 323 KB of firmware space per build; public builds keep all ten languages.
- The periodic memory log now also reports the render task's stack high-water mark so its 16 KB reservation can be validated on hardware.

- Reading Profile now reports raw seven-day measures instead of opaque Habit, Engagement, Stability, and Depth scores.
- New installs default the reader long-press Confirm shortcut to Bookmark; existing explicit shortcut choices, including Off, remain untouched.
- Reading Trends now compares Today, Last 7 Days, Last 30 Days, and This Year with reading time, qualifying sessions, active days, and books finished; Monthly Calendar also reports books finished.
- Large EPUBs and SD-card font-heavy books now build pages with less temporary memory churn, reducing slow first opens and low-memory failures.
- EPUB indexing now avoids more small temporary allocations while processing text and HTML attributes.
- Home and sleep screens now do more EPUB cover and thumbnail work on demand, reducing reader startup work and reusing cached cover data where possible.
- The 4-Cover Grid now paints cached covers and stable placeholders immediately, then prepares at most one missing thumbnail per loop instead of blocking the whole shelf.
- Built-in reader font choices have been reduced to Lexend Deca and Bitter, reducing firmware size while keeping fallback glyph coverage.
- Home now frees more temporary cover UI memory before generating EPUB thumbnails, improving cover creation for optimized books under low-memory conditions.
- Cover-grid and carousel hydration now wait until the interactive placeholder frame has painted, perform at most one spaced SD read per idle slice, and open exact cached thumbnails directly instead of repeating EPUB cache probes.
- Cover-grid page transitions settle sooner, carousel detail loading is limited to the selected cover and its nearest neighbors, and the outer carousel covers return to a slimmer perspective treatment.
- Home now includes cache-only current-book reading statistics in its first frame and no longer performs a second full repaint merely because optional deferred metadata or cover work completed.
- The compact Home menu now keeps If Found directly available while Browse Files and Settings remain under Apps.
- Font comparisons use compact Normal, Italic, and Bold `Aa` specimens; OpenDyslexic uses a 14 pt picker specimen while other families use 16 pt.

### Removed

- Teensy firmware builds are no longer produced for releases or release candidates.

### Removed

- The hidden Home long-press that swapped the displayed current book for the next recent one. Held or delayed button presses could trigger it accidentally, silently flipping Home to an older title (the recurring "stale current book" complaint); the swap rotated recents and was later persisted, making the wrong book stick.

### Added

- Chapter-indexing times are now recorded per session (count, last, and worst build) in the reader timing breadcrumbs, so multi-minute "INDEXING" waits can be diagnosed from the card.

### Added

- Repair15 phase-1 instrumentation: book-open-to-first-paint, early vs. settled page-render times, stats-commit duration at book exit (`/.crosspoint/reader_timing.txt`), and Home entry-to-paint (`/.crosspoint/home_timing.txt`). Measurement only; each file is overwritten per transition and diagnosable from a card mount.

### Added

- The Duet mark (ink-drop eighth note with companion dots) is now the firmware's loading icon and the web portal logo.
- The Device Split stats page now shows real device names: each sync saves the peer's name learned during the handshake, and the local row uses this device's own name.
- The heatmap page gives the maps more room: larger heat cells and compact metric boxes (active days / current streak / longest streak) instead of boxes that absorbed all remaining screen height.
- Series Progress now tracks up to 48 series (was 16) — the page always scrolled, but long libraries hit the old cap and looked truncated.
- Reader DNA is now self-documenting: a two-column legend explains every axis with its score and the raw measurement behind it (days read of 30, time over 30 days, average session, pages/min, current streak, finished/started over 12 months) plus the overall score.
- Book Timeline groups by title so a book whose cache identity changed during a library reorganization appears as one row, and rows sort by most recent reading.
- Cover shelves find their thumbnails again: the grid and carousel searched only the pre-relocation thumbnail location, so relocated and freshly generated covers were invisible and every visit re-queued full regeneration while paying a directory scan per probe. Shelf lookups now resolve to the sharded thumbnail home with pure string work.
- Reading-status badges no longer freeze large shelves: the per-book fallback ran ~10 existence probes against the crowded cache root (each a linear scan, most for books whose cache folder does not exist); it now answers everything with a single directory open, and per-book results are remembered across page turns.
- EPUB cache-path checks that run on every book construction are now remembered for the session, so browsing hundreds of books stops re-scanning the cache root.
- The cover browser can no longer be left permanently unable to paint by an exit that never completed; the paint-suppression flag now self-heals after 2.5 seconds.
- A picker heartbeat breadcrumb (`/.crossink/picker_hb.txt`) records the last phase entered, so any future freeze names its exact location from the card alone.
- Large cover shelves no longer freeze on first visit after the thumbnail relocation: the expensive legacy-thumbnail probe now runs at most once per path per session instead of repeating every browser pass, restoring the paint-first spaced-slice hydration contract.
- Cover thumbnails relocated to small sharded directories under `/.crossink/thumbs` with automatic move-on-first-touch migration. Thumbnails previously lived inside each book's cache directory, where every open paid a linear scan over thousands of cache entries — the core reason cover grids and the carousel crawled. Existing thumbnails are renamed across as they are first displayed (a cheap metadata move, not a copy), so shelves get faster as you browse.
- Fixed a crash opening Reading Stats: the tab-label array and tab-bar cap were still sized for the original 18 pages while the pager grew to 31, overflowing the stack on entry.
- Full session logging: every reading session now appends its exact local start time, length, pages, and book to a durable on-card log (separate from all existing stats files). A pure recorder for now — it feeds future hour-by-hour and reading-rhythm analytics once history accumulates.
- "Book Timeline" (Gantt-style bars of when each recent book was read over the last 60 days, from the attribution ledger), "Fastest Reads" (top five finished books ranked by days from start to finish), and estimated finish dates on every row of the Started Books page.
- Five more Reading Stats pages: "Year in Reading" (cumulative 365-day line with a pages-turned odometer), "Session Mix" (a histogram of typical session lengths over 90 days), "Streaks" (current/longest streak with a 3-7-14-30-60-100 day milestone ladder), "Started vs Finished" (paired monthly bars of books begun and completed over 12 months), and "Reader DNA" — the hexagonal profile radar returns, expanded from the retired four-axis diamond to six scored axes (Habit, Volume, Focus, Pace, Streak, Finisher) with an overall score.
- Three more Reading Stats pages: "By Time of Day" (four-bucket bar chart with peak hours), "By Month" (last 12 calendar months of reading time), and "Device Split" (per-device share bars with time, sessions, and finished books for this device and every synced peer).
- Three new Reading Stats pages: "By Weekday" (a bar chart of your reading time by day of week over the last year, with best-day highlights), "Pace Trend" (daily pages-per-minute over the last 30 days with 7-day vs 30-day comparison and a speeding-up/slowing-down verdict), and "Reading Wrapped" (a 12-month summary card: time, days read, books finished, longest streak, best weekday, average session). All three read the merged all-devices history.
- The Home Stats picker now drives every stat-bearing Home theme: the Reading Home theme's six continue-card cells follow stat rows 1-6 and its four-tile strip has its own four picker slots (Settings > Display > Home Stats), and the Minimal theme's two stat rows follow the footer slots. Two new catalog entries: Today's Reading and total Sessions.
- Customizable Dashboard Home stats: Settings > Display > Home Stats lets each of the seven stat rows and the two footer slots be chosen from the full stat catalog (book time, time left, progress, daily average, pages/min, sessions, average session, days reading, est. finish, streak, time-of-day reader, and device-local or all-devices totals and books read). Defaults match the X3's original layout on both devices, and date-based stats now appear on the X4 using its Stats Date instead of being hidden.

### Fixed

- Nearby Stats Sync now also transfers the per-book daily attribution ledger (protocol v4), and calendar day details merge peer ledgers — synced reading time now shows against its actual books instead of "Older reading (book unknown)".
- Journal-backed stats pages (Monthly Calendar, Heatmap, Trends, Daily Minutes, Profile, Goals, Recent Sessions) now pair the merged journal with the merged reading-day history, so Days Read, calendar shading, and current/longest streaks agree across synced devices instead of each device reporting only its own days. Home streak and time-of-day reader tiles are person-level for the same reason.
- Cover grids and the carousel no longer swallow button presses: background cover work (probes and thumbnail generation) waits for a genuine idle window after the last press, so navigation always finds a live input loop. The multi-minute "frozen shelf" while thumbnails generated is gone; generation resumes a few seconds after you stop navigating.
- The SD firmware update completion hint now wraps instead of being clipped off both screen edges.
- The X4 panel is scrubbed before the post-update restart so the updater's "Updating..." text no longer ghosts over the Home screen after a firmware update.
- Fixed a crash on the stats screens after the first successful Nearby Stats Sync: aggregating a peer's reading journal placed a ~3KB journal object on the task stack (repeatedly, one per peer), overflowing it. All journal instances are now heap-allocated. This path only executes once peer sync files exist, which is why it appeared immediately after the first working sync.
- Crash reports now record the faulting program counter and return address (MEPC/RA). CPU exceptions never ran the abort hook, so previous reports had an empty panic reason and no crash location.
- X4 sleep entry now recognizes residue after any fast or windowed update instead of waiting for twelve. A dirty panel gets one white full-waveform scrub followed by a fast final sleep paint from the clean baseline; the separate "Entering sleep" repaint is skipped on X4, avoiding a three-paint transition. X3 keeps its existing controller-specific cleaning path.
- Home no longer swallows button presses right after boot: the deferred per-book stats pass is sliced to one book per loop pass (each book costs seconds of FAT directory scanning), so input gets a beat between every book, and the cover/carousel warm-up passes wait for several seconds of genuine idle before starting.
- Hot system files (settings, state, recents, stats, indexes, sync snapshots, font catalog, timing telemetry) moved from the crowded per-book cache directory `/.crosspoint` into the near-empty `/.crossink`. On FAT, every file-open scans the containing directory linearly, so with thousands of book-cache entries each open cost multiple seconds; this one change removes that tax from boot, Home, sync preparation, and settings. Existing data is renamed across automatically on the first boot after updating (a one-time migration that may add up to a couple of minutes to that single boot).
- Nearby sync discovery hardening: WiFi persistence and auto-reconnect are disabled during sync so a remembered access point cannot pull the radio off the sync channel mid-discovery.
- Ordinary reading no longer invalidates the sync stats snapshot: closing a book now updates that book's record inside the published snapshot in place, so the next Nearby Stats Sync starts instantly instead of re-crawling the whole library. Only a stats restore forces a rebuild. Receiving a peer's files during sync also no longer invalidates the local snapshot.
- Back now aborts sync preparation on a tap: the button is polled every folder instead of every 16, so presses can no longer fall between polls at slow crawl speeds. The snapshot clean-marker write is also verified and logged loudly on failure instead of silently forcing the next sync to re-crawl.
- A frozen device can no longer result from the wait-for-paint used by sync and boot screens: the wait is now bounded (8s) and the firmware proceeds unpainted instead of blocking the main loop forever (Back button included).
- A reader parked on the "no reader found" error screen now joins automatically when the other reader starts broadcasting, like the ready screen always did.
- Removed the 3-minute idle auto-exit from both nearby sync screens (per Lauren): screens stay open until dismissed.
- Boot reaches a visible, usable screen sooner: favorites, achievements, KOReader credentials, and OPDS servers now load just after the first frame starts painting instead of before display init. Each load pays a linear scan of the crowded `.crosspoint` directory, which boot timing showed costing whole seconds.
- The X4 sleep deghost is now a true white-inversion scrub inside the display controller (fill white, full drive, then the sleep image) instead of re-driving the current image twice — real residue removal, same single extra blink.
- Nearby Stats Sync preparation now shows a live folders-scanned counter, can be aborted with Back at any point, and writes its phase timings to a card file for diagnosis.
- Returning to Home (from Settings, the reader, or closing the Home menu) now presents the first frame with the stronger clean-swap refresh, so the dismissed screen can no longer ghost through on the X3. In-Home navigation keeps the fast refresh.
- Boot no longer re-scans the entire SD font library: discovery results are cached in `/.crosspoint/font_catalog_v1.bin` and reused, with an automatic full rescan (and cache rewrite) whenever the saved font fails to load or fonts are replaced. On a 130-family card this removes several seconds from every boot. Boot timing now also splits the font phase into discovery vs. load, and adds settings/gesture/stores/panel phase breadcrumbs.
- Booting and returning Home no longer flashes once per cover: cover loading now repaints the screen a single time when all covers have settled instead of after each decode. This mattered most on the X4, whose display driver shows every repaint as a distinct flash.
- Nearby Stats Sync preparation no longer probes every library cache directory for stats files: the per-book index now names the books to load, cutting first-sync preparation from minutes of frozen screen on large libraries to seconds. The "Preparing reading stats" frame is also now guaranteed to be painted before preparation starts.
- Pressing Sync on Nearby Stats Sync no longer looks like a dead button: the screen now shows "Preparing reading stats" immediately while the per-book snapshot is built (which can take a while on the first sync over a large library), the preparation walk itself does roughly half as many SD lookups per book, and the 60-second sync timeout now starts when discovery starts instead of being consumed by preparation.
- The X4 sleep screen no longer shows ghosted text from the book or screen used before sleep: sleep entry runs an extra full-refresh scrub pass when — and only when — enough fast page refreshes have accumulated since the last full clear to leave residue. A casual sleep from Home costs no extra blink; sleep after a reading session pays one scrub blink instead of showing ghosted text. The X3 driver already scrubs and is unchanged.
- Reading an XTC book now updates the last-active-book marker, so Home no longer keeps highlighting the previously read EPUB after an XTC session.
- A book is now promoted to "current" only after its first page actually paints. Backing out or sleeping during a misopened book no longer rewrites the active-book state, recents order, and last-active marker — the mechanism that made an accidental one-second open permanently take over the Home screen.
- Boot now records phase timings (hardware init, SD mount, settings, display, dispatch, and how long the firmware waits for the power button to be released) to `/.crosspoint/boot_timing.txt` a few seconds after startup, as groundwork for diagnosing slow sleep/wake.
- Cover and thumbnail generation can no longer reboot the device when memory runs low: the PNG decoder's working context moved off the main task stack, and its dithering buffers now fail gracefully (falling back to plain quantization or a missing cover) instead of aborting on allocation failure.
- A CSS rule cache that becomes corrupted mid-file is now deleted at chapter-build time so the next open rebuilds it, instead of silently rendering without styles forever.
- Locked-screen Power cycling uses the previously proven post-GPIO route with a 1.2-second inter-click window; the experimental pre-serial interrupt and extra pre-cycle panel refresh were withdrawn after physical X3 regressions.
- Home no longer constructs and indexes the highlighted EPUB merely to draw its first frame, preserving the contiguous heap needed to open the book without a device reset.
- Cover-grid selection now has a snapshot-independent partial-refresh fallback, so every directional press remains visibly responsive while exact cover art and reading signals are still loading.
- Cover-grid background hydration keeps the last clean selection snapshot alive until its replacement frame paints, removing the intermittent interval where the logical selection moved but the focus ring appeared frozen.
- Cover grids now require exact X3-density thumbnails instead of retaining a visibly coarse 123x180 fallback; missing exact art remains a stable title placeholder until the high-quality tile is available.
- Cover Carousel side cards now preserve a normal book-cover aspect ratio, making the high-resolution neighboring artwork visibly cleaner instead of squeezing it into narrow perspective strips.
- Current and longest reading streaks now combine the detailed journal with preserved legacy reading-day history, so older active days still count without being misreported as one-second timed sessions.
- XTC sessions now populate the per-book/day reading ledger used by calendar drilldowns and manual time corrections.
- Books under `/ignore_stats/` now keep local progress and finished status without contributing reading time, sessions, history, or completed-book totals to aggregate statistics.
- Restoring reading statistics now clears derived in-memory library indexes immediately, so restored totals appear without restarting.
- Reading-time averages now use only days with real detailed timing while still retaining presence-only legacy days for activity and streak calculations.
- In-book dictionary screens now keep SD-font glyphs prewarmed until the underlying reader page is drawn, preventing the page text from turning into question marks, and use the actual reader foreground color instead of the Bionic mode value.
- Bionic Reading now remains visibly distinct when an SD font only contains a regular face by using a measured synthetic-bold fallback.
- Font previews now use the selected point size consistently, so families such as Vollkorn and Tinos no longer appear misleadingly tiny in the picker.
- Current Book statistics include an active reading session as soon as it reaches one minute, without waiting for the reader to close before showing it.

- EPUB Reading Stats no longer drops unsaved page-turn counts after viewing the stats screen mid-session.
- KOSync now frees SD-card font registry memory before TLS requests and releases the EPUB before upload, reducing sync low-memory failures with many SD fonts installed.
- Web file manager actions now handle filenames with special characters safely and reject unsafe rename characters before saving.
- Auto Turn interval settings and related action prompts opened from long-press shortcuts now stay open after releasing the shortcut button.
- EPUB footnote previews no longer show clipped status-bar labels or misleading reader progress indicators.
- Font selection no longer reopens the font preview after choosing a font.
- EPUB chapters now rebuild stale publisher CSS caches instead of opening without the book's styling.
- Large SD-card font EPUBs no longer overlap characters after font or line-spacing changes, and clipping selection can fall back to a built-in UI font when needed.
- EPUB cover and thumbnail generation is more reliable with custom SD-card fonts selected.
- Web EPUB optimizer no longer leaves transparent PNG artwork blank or replaced by alt text.
- Web EPUB optimizer now rasterizes SVG images to JPEG so optimized EPUBs preserve more dividers and artwork on-device.
- Unsupported SVG images in EPUB chapters are now skipped silently instead of triggering low-memory image warnings.
- Nearby Position Sync now silently restarts back into the reader after using ESP-NOW, matching other WiFi sync flows and reducing post-sync memory fragmentation.
- EPUB grayscale page turns on X3 now use the grayscale-aware display base, reducing the moment where new text appears too dark before the anti-aliased overlay finishes.
- EPUB chapters with many inline anchors, footnote links, or malformed XHTML are less likely to fail or get stuck on the indexing screen.
- EPUB chapters with large publisher style caches and SD-card fonts now keep more heap available during indexing and retry lighter render modes after low-memory layout failures.
- EPUB opening and image rendering now handle more low-memory allocation failures without rebooting, and landscape image pages read less cached image data during tiled grayscale rendering.
- EPUB clipping selection now follows right-to-left line order when selecting Hebrew and other RTL text.
- Lyra Carousel no longer shows a blank carousel after returning from WiFi-related File Transfer screens and moving between the menu row and book row.
- Generated SD-card font packages now include the same core glyph coverage as built-in reader fonts.
- EPUB clipping selection now works from footnote previews.
- Web EPUB optimizer now keeps image references in malformed or XML-declared chapters aligned with renamed JPEG files.
- Manage Fonts no longer crashes while loading the font list on devices with many SD-card font families installed.

## [v1.3.4] - 2026-06-24

### Added

- File Browser now indexes large SD-card folders so directories with many books can be browsed without loading every filename into memory at once.
- EPUB text clipping with saved highlights, clipping lists, and Kindle-style `/My Clippings.txt` export.
- `Create Clipping` is now available as a reader shortcut for short/long Power, long-press Menu, and long-press Back actions.
- Per-book EPUB options for font, layout, styling, reading aids, and render modes, including `CrossInk Default`, `Balanced`, and `Light` modes for difficult books.
- Arena allocator (`lib/Memory/Arena.h`) for burst-then-discard allocation patterns - reduces heap fragmentation during EPUB parsing and page layout over long reading sessions.
- Optimized EPUBs now store location metadata at `META-INF/x-locations.json`.
- X3 SD-card writes now use the RTC for file timestamps when the clock is available.

### Changed

- The EPUB reader menu now splits the growing menu into 3 screens, labels per-book settings as `Book Options`, and avoids showing duplicate `Orientation` controls.
- The `Inverted` sleep cover filter now flips Minimal and Reading Stats sleep screens to black text on a white background.

### Fixed

- Calibre Wireless transfer status no longer stacks the last received-file message on top of the upload percentage.
- X3 Tilt Direction now labels left/right choices as `Left-Right` and `Right-Left`, with existing left/right preferences migrated to keep the same physical tilt behavior.
- EPUB layout now honors publisher page-break CSS, avoids stretching justified spaces before closing punctuation, and keeps large CSS rule sets in a smaller disk-backed lookup cache.
- EPUB first-open conversion now uses more compact OPF manifest lookups and streams cover-wrapper parsing to avoid large temporary heap buffers on books with huge manifests.
- EPUB chapters that run out of memory now retry with `Balanced`, `Light`, and final `Safe Mode` rendering before showing an error, apply the same fallbacks during next-chapter pre-indexing, and let book action menus reset a book's reader settings if Safe Mode still cannot open it.
- EPUB reader font-size changes now restore the current chapter position by content instead of jumping far backward after re-indexing.
- Reading Stats now use the reader's last live book time-left estimate instead of showing a separate fallback estimate.
- Per-book reading stats now migrate compatible legacy `stats.bin` files into the `stats_v5.bin` flow instead of resetting when only the old filename exists.
- Lyra Carousel Home menu rendering now avoids extra label allocations that could crash builds under low memory.
- Lyra Carousel Home cover refresh no longer risks a reboot when memory is tight after returning to or selecting a recent book.
- EPUB image-heavy chapters no longer risk a reboot while saving their reading cache under low memory.
- TXT readers now stay open when pressing a page-turn button at the end of the file.
- Long-press reader shortcuts that open another screen no longer close or confirm it again when releasing the shortcut button.
- RoundedRaff's header battery icon and percentage now sit lower to avoid clipping at the top edge.
- Lyra Carousel now keeps the Home header current when rendering the menu or restoring cached carousel frames, preventing stale battery and clock values while navigating between books.
- Web file manager multi-delete now handles larger selections without failing after a small batch.
- Portuguese EPUBs now use Portuguese hyphenation rules instead of leaving long words unhyphenated when Hyphenation is enabled.
- Progressive JPEG EPUB covers now render more smoothly in generated cover and thumbnail BMP assets.
- EPUB section layout now flushes long text runs earlier when Bionic Reading or Guide Dots are enabled, reducing low-memory failures on difficult books.
- Footnotes in EPUBs with very large shared notes sections no longer cause long stalls when opened.
- Firmware updates now follow GitHub asset redirects before streaming the install.
- Tiled grayscale rendering now serializes display transfers on the shared SPI bus to avoid display glitches during SD activity.

## [v1.3.3] - 2026-06-13

### Added

- `File Browser Display` in `Settings > System > Files & Cache` for choosing one-line or two-line file browser rows across all themes, while preserving Minimal users' existing two-line display on upgrade.
- `Hide File Extension` in `Settings > System > Files & Cache` for expanding file-browser filenames by hiding the right-side extension label.
- Device Name in Settings > System > Device for customizing the KOReader Sync and Nearby Stats Sync device label.
- Additional shortcut options and new ability to add custom shortcuts for Long-press Back Action.
- Delete Reading Stats actions in the EPUB reader and book action menus for clearing one book's stats without deleting its cache.

### Changed

- CrossInk settings now save to `/.crosspoint/crossink-settings.json`, with a one-time fallback migration from `/.crosspoint/settings.json`, so switching between firmware builds is less likely to reset preferences.
- The X3 clock visibility setting is now phrased as `Hide Clock`, with existing `Show Clock` preferences migrated to the matching hide behavior.

### Fixed

- RoundedRaff's date shown in settings now sits lower on X3 devices instead of overlapping the battery.
- Clear Bookmark List now asks for confirmation before deleting a book's bookmarks.
- Clear Reading Cache now preserves per-book reading stats while continuing to leave all-time reading stats untouched.
- Moving finished EPUBs to `/Read` now consistently preserves reading progress, per-book stats, bookmarks, and resume state.
- Book settings option lists now return to the submenu they were opened from when pressing Back.
- Lyra Carousel now refreshes its cached Home icon row after OPDS, Reading Stats, or Bookmarks icons appear or disappear.
- KOReader Sync failure screens now wrap long error messages and shut down WiFi cleanly before returning to the book.
- Sleep Screen > Cover now generates the current book cover on demand instead of falling back to the dark sleep screen when the setting is changed after opening a book.
- File Browser now previews PNG images instead of trying to open them as EPUBs, and hides common macOS and Windows metadata files.
- File Browser now refreshes immediately after falling back to the root folder from a stale saved path.
- File Browser now stops loading oversized folders before low memory can crash the device and shows a memory error instead.
- TXT reader long-press Power page turns now work when Long Power Button is set to Page Turn.
- SD-card font read failures no longer risk a reboot while cleaning up the failed file read.
- Page Overlay sleep screens no longer force EPUB chapters to re-index after waking.
- Page Overlay sleep screens now use the current screen as the overlay background outside the reader instead of trying to rebuild a stale book page.

## [v1.3.2] - 2026-06-10

### Added

- Current date in the top-right Settings header on X3 devices.
- Dark Reader Mode for EPUB and TXT reading screens, plus shortcut actions for the power button and front-button long press.
- File Browser long-press folder action for choosing a custom sleep-image folder instead of only `/.sleep` or `/sleep`.
- Expanded X3 Reading Stats, including streaks, time charts, editable dates, all-time backups, reset controls, an idle-time threshold, and the `Minimal Stats` sleep screen.
- `Reset Reading Pace` in the EPUB reader menu when Time Left is enabled, for clearing only the time-left pace estimate while keeping book reading stats.

### Changed

- Display, Reader, and Controls settings now open list menus instead of cycling through options one by one.
- Reading time and time-left pace tracking now ignore page intervals longer than the configured idle-time threshold.
- Web portal pages now use shared templates, stylesheet, and logo assets, reducing on-device page size and improving browser caching.
- Already-cached EPUBs now open directly to the first page without an extra book-loading popup refresh.
- Reader font-size choices now show point sizes like `10 pt` instead of names like `Tiny`.

### Fixed

- Inverted reader menus now honor orientation-aware side-button navigation.
- EPUB book time-left estimates now wait for more session pace samples and use a progress-based floor after pace data exists, reducing swings from unusually short or long pages.
- Deleting an EPUB book cache now preserves that book's reading stats and pace data.
- X3 clock settings now have clearer UTC offset editing, and `Sync Date/Time` can use saved WiFi networks automatically.
- Home, Lyra Carousel, WiFi setup, and SD-card font flows now release memory more aggressively to avoid freezes or crashes on constrained builds.
- Vietnamese settings labels no longer show replacement diamonds after generated translation offsets shifted.
- KOReader Sync now lands correctly at chapter starts and shows more specific connection guidance.
- EPUB bookmarks saved under the old unstable path hash now show up again, including for books moved to `/Read`.
- SD-card font downloads now use versioned direct S3-hosted HTTP endpoints with CRC validation, avoiding GitHub release redirects and ESP32-C3 TLS stalls when loading the font catalog.
- EPUB text blocks now keep the book's alignment style when an inline image appears before the text.

## [v1.3.1] - 2026-05-28

### Added

- EPUB reading-position improvements, including bookmark anchors, bookmark preview snippets, and optional chapter/book time-left estimates.
- Nearby Reading Stats sync with separate totals for this device and all synced CrossInk readers.
- Per-server OPDS filename settings so downloaded books can use either Author - Title or Title - Author.
- EPUB render heap diagnostics that include the largest allocatable block, not just total free heap.

### Changed

- Moved the X3 reader clock into a new top-centered status bar and moved clock settings to Settings > System > Device.
- Reworked Display, Reader, Controls, in-reader options, and larger System settings groups so related options open as submenus.
- Improved OPDS and font download responsiveness by reducing progress-update overhead and temporarily disabling WiFi power saving during transfers.
- Book selection now shows a loading popup before EPUB indexing or cache loading begins.
- Delayed the automatic finished-book prompt until the reader leaves the chapter where they reach 99%.

### Fixed

- WiFi settings screen now keeps the displayed MAC address consistent with the router-visible WiFi address.
- Reader UI issues with inverted menu button hints, Lyra Carousel popups, and Auto Page Turn interval persistence.
- Web uploads and KOReader Sync progress saves now preserve progress, stats, settings, and valid resume data for refreshed book files.
- OPDS low-memory handling now shows a specific parser-buffer memory message and releases SD-card fonts before catalog loading.
- EPUB cache, CSS, table, SD-card font, and allocation failure paths now recover, retry, or stop cleanly under low memory instead of opening unstyled pages, failing unnecessarily, or risking a reboot.
- EPUB text with invisible word-joiner characters no longer shows replacement diamonds for missing font glyphs.
- Clarified the low-memory EPUB image warning so it says some or all images may be missing.

## [v1.3.0] - 2026-05-21

### Added

- Back/Cancel support while downloading books from OPDS catalogs.
- Recent Books long-press menu in both List and Grid views with delete, cache delete, completion, and remove-from-recents actions.
- Minimal sleep screen option that shows the current book cover and reading progress on a dark background.
- More detailed WiFi connection debug logs for scans, selected networks, status changes, disconnect reasons, and timeouts.
- 9pt `Itty Bitty` reader font size, plus build flags for omitting Itty Bitty and Large reader font assets in size-constrained firmware variants.
- In-reader confirmation message when a shortcut turns tilt-to-turn on or off.

### Fixed

- WiFi and OPDS connection-flow edge cases: manual Settings connections now show the connected status before continuing, copied or corrupted saved-password files are rejected before use, OPDS retries show loading before requests, and large OPDS feeds fail safely under low memory instead of rebooting.
- Reader and Home UI polish issues, including landscape status-bar settings, missing Vietnamese labels, File Browser and Lyra Carousel icon alignment, cover thumbnail artifacts, and duplicate Home progress/stat loading.
- EPUB cache and low-memory handling now use stable cache folder keys, migrate older cache folders where possible, rebuild stale section caches, lay out very long text blocks earlier, stream table fallback content when heap is tight, and clarify the warning text.
- Sleep-entry, network, and SD-card font download reliability improvements: cached sleep-screen assets are reused, OPDS pages idle normally after load, the X3 tilt sensor sleeps outside the reader, WiFi power saving is disabled during transfers, WebDAV stack usage is lower, longer stalls are tolerated, interrupted font files are retried, and active reader fonts are freed when needed.
- Remaining reader service edge cases, including an XTC chapter selector crash on memory-constrained builds, SD-card font size selection, SD-card font-size shortcuts skipping manually installed sizes, and KOReader Sync login compatibility with self-hosted servers that return valid JSON on success.

### Changed

- Modified upstream "page-as-sleep" behavior into a new `Sleep Screen > Quick Resume` option, which also keeps `Quick Resume on Timeout` on, and renamed the timeout-only toggle.
- Improved reader and browser menu behavior by moving the Footnotes shortcut above Select Chapter, wrapping long book titles in action menus, and reducing progress-screen repaint work during OPDS and SD font downloads.

## [v1.2.11.1] - 2026-05-15

### Changed

- Removed Medium font size from `xlarge` build to get it below the size limit

### Fixed

- Lyra Carousel is now included by activating the build flag `DCROSSINK_ENABLE_LYRA_CAROUSEL=1`

---

## [v1.2.11] - 2026-05-14

### Added

- New personal theme: "Minimal"
- Custom sleep timer picker so `Time to Sleep` can be set from 1 to 30 minutes instead of cycling fixed presets.
- In-reader Controls shortcut for customizing buttons without leaving the book.
- Bookmark cleanup shortcuts: hold Select on a bookmark to delete it, or hold Open on a book in Bookmarks to clear that book's bookmark list.
- Confirmation message after deleting a book's cache from the reader or File Browser.
- File Browser long-press action for deleting an EPUB or XTC book's cache.
- Downloaded-font size range setting so SD-card fonts can use compact, default, or large point-size sets.
- File Browser long-press action for marking EPUB books as finished or unfinished.

### Changed

- Hardened deep sleep entry by shutting WiFi down before waiting for the power button to be released.
- Raised the web file-transfer filename limit from 100 to 150 bytes so longer uploaded filenames are preserved.
- Made the in-reader Reader Options menu include the same Reader settings and actions as Settings > Reader.
- Split SD-card font descriptions and supported languages into separate lines in the font download screen.

### Fixed

- Inline EPUB images no longer disappear in landscape when their bottom edge slightly overlaps the screen margin.
- Reduced unnecessary low-memory image suppression for JPEG-heavy EPUB chapters and added CSS heap diagnostics during chapter rebuilds.
- Allowed wider inline JPEG images in EPUBs to render when they still fit the total pixel and heap safety limits.
- SD-card font picker no longer reopens immediately after selecting a font from Settings > Reader > Font Family.
- In-reader font-size changes now work for SD-card fonts.
- In-reader SD-card font changes now rebuild the current EPUB page layout consistently.

## [v1.2.10] - 2026-05-11

### Added

- `Recent Books View` setting so the dedicated Recent Books screen can switch between the classic list and a 3x3 cover grid.
- More flexible reader controls, including orientation-aware front/side button settings, nav-only or all-button front inversion, tilt page turn shortcuts, and side-button long-press rotation actions.
- Per-session auto page turn interval picker with values from 5 to 120 seconds.
- File Browser Home/Back long-press action for toggling hidden files and folders.
- EPUB rendering and diagnostics improvements, including visible `<hr>` separators and heap logs around section rebuilds, image extraction, page serialization, and sleep-cache rebuilds.
- Reader font coverage for block redactions, black-square ornaments, Greek category letters, and turned-comma punctuation (PR #104).
- Simulator tools for testing sleep/wake behavior and smoke-testing common screens and EPUB reader menus.

### Changed

- Reduced Controls settings section spacing so the grouped controls fit better on X3 screens.
- Made front reader long-press actions trigger when the hold delay is reached while normal page turns still trigger on release.
- Used the fast EPUB spine/TOC indexing path for books with 300+ spine entries so heavily split books build `book.bin` faster on first open.
- Allowed the web file manager and WebDAV to browse dot-prefixed hidden files when hidden files are enabled, matching the device file browser.

### Fixed

- Reader button and shortcut behavior, including X3 power-button wake filtering, folder delete long-press timing, and WiFi scan/connect screens that could not be exited while work was in progress.
- RoundedRaff home-menu, keyboard, and button-hint rendering issues so Settings remains reachable and compact labels no longer overlap or disappear.
- Font and glyph handling now reduces persistent SD-card font advance-cache memory, releases optional font caches before image extraction only when heap is tight, and shows a visible replacement symbol when compact UI fonts lack `U+FFFD`.
- KOReader Sync authentication diagnostics and an in-reader sync crash, including clearer handling when a server or proxy returns non-JSON content.
- EPUB text rendering for redactions, whitespace-only XHTML text nodes, simple black CSS span backgrounds, list bullets in `<li><p>...</p></li>` items, and very long base64-like text runs.
- EPUB image, thumbnail, and section-rebuild stability so image-heavy chapters use less temporary memory, scale images more reliably, avoid stale dimensions, and suppress optional image work earlier under heap pressure.
- EPUB low-memory and cache safety now skips optional next-chapter indexing and sleep-page cache rebuilds when heap is tight, fails safely with a malformed-book warning and Home exit path, rebuilds incompatible fork-written caches, and handles low-memory CSS parsing, truncated SD writes, invalid serialized strings, and failed temp-cache promotion.
- Home no longer crashes after clearing reading cache when the source EPUB cache is missing.
- Reader prewarm behavior now skips image decoding, keeps mixed-style font glyphs cached together, and avoids section rebuilds for render-quality-only option changes.
- Concurrent render/storage crashes are avoided by serializing `GfxRenderer` scratch-buffer access, shared SPI bus access, and failed SPI lock cleanup.
- Recent Books, EPUB/XTC thumbnail caches, deleted-folder metadata, and XTC cover scaling now keep cached book data in sync and grid covers fill their slots correctly.
- Simulator build configuration now lets SDL2 and simulator-provided network/OTA shims compile cleanly.

---

## [v1.2.9.1] - 2026-05-03

### Changed

- Cleaned up EPUB table rendering by removing synthetic row/cell labels and defaulting table cells to readable left alignment
- Allow simple EPUB tables with full-width note rows so a single `colspan` cell spanning the whole table no longer forces the entire table back to paragraph fallback

### Fixed

- Power-button shortcut conflicts outside the reader so reader-only actions fall back to `Confirm` while Sleep, Refresh, Screenshot, Sync Progress, and File Transfer remain real power actions.
- Potential crash when using `Go to %` in EPUBs.
- Potential crash when entering sleep with Page Overlay enabled if the cached EPUB page data is invalid.
