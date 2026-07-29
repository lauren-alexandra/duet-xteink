---
title: Complete Feature Tour
nav_order: 2
---

# Duet: the complete feature tour

Duet is an independent, one-person open-source firmware project for the Xteink X3 and X4, created and maintained by Lauren Landau with coding tools working under her direction and review. It began as Lauren's personal CrossInk fork and grew into a reading-focused system for people who want a tiny e-reader to feel like a real library: cover browsing, search and book details, deep reading statistics, customizable Home screens, direct X3/X4 sync, dictionaries, fonts, achievements, and recovery tools that do not treat the SD card as disposable.

For the source-verified technical inventory, see the [Feature and Lineage Catalog][1]. For exact file-level attribution, audited revisions, and transitive credits, see [Third-Party Notices][2].

> **Current status: early alpha.** Duet is already running on both the X3 and X4. This release is meant to widen the test pool across different devices, SD cards, libraries, and reading habits so the remaining hardware-specific rough edges can be found and fixed before a stable release.

## Duet at a glance

The current source contains:

- 8 Home themes
- 6 book-browser layouts
- 33 top-level reading-statistics pages
- 22 configurable launcher destinations
- 12 sleep-screen modes
- 108 persistent achievement milestones
- 26 UI translation catalogs

Those totals describe everything available in Duet, not features all invented by Duet. This tour uses four plain-English credit labels throughout:

- **Inherited:** present in the CrossPoint or CrossInk foundation.
- **Adapted:** identifiable upstream code was brought in and changed for Duet.
- **Informed by:** another project supplied the idea or interaction pattern, while Duet's implementation is local.
- **Original to Duet:** created for this fork.

Some features carry more than one label because Duet often extends an inherited system or combines an upstream idea with new implementation work.

## The reader

Duet keeps the compact, button-driven reader at the center of the experience. It reads EPUB, XTC, XTCH, TXT, and Markdown files. Markdown currently uses the TXT reader rather than a separate Markdown renderer.

**Lineage:** The reader core, formats, file browser, EPUB image handling, and most basic navigation are inherited from CrossPoint through CrossInk. Duet extends that foundation rather than replacing it.

EPUBs can display baseline JPEG and PNG artwork with memory-aware handling. The reader can display images normally, replace them with placeholders, or suppress them. GIF and progressive JPEG remain unsupported. The file browser also includes a BMP viewer and can select compatible images for sleep use.

Inside a book, the left-side quick overlay gives immediate access to:

- Chapter
- Dictionary
- Go To
- Sync
- Reading Stats
- Tilt page turn when the sensor is available
- Auto Turn
- Spacing
- Reader Options
- More

The **Sync** action opens a choice between KOReader position sync and CrossInk's Nearby position sync. **More** opens the full reader menu, where Book Info, bookmarks, clippings, dictionary history, screenshots, QR position sharing, completion state, cache/stat tools, and the less frequently used reader controls live.

**Lineage:** The in-book menu foundation comes from CrossInk. Its quick-settings direction was informed by CPR-vCodex; the left-side layout, action set, sync chooser, and integration with Duet's newer tools are Duet work.

The inherited CrossPoint reader foundation includes:

- Persistent per-book position across restarts and firmware updates when SD state is preserved
- Chapter navigation, percentage jumps, footnotes, and quick footnote return
- Hardware and in-reader screenshots saved to the SD card
- Return to Home or the file browser when leaving a book
- Optional top, bottom, or hidden XTC status bar

CrossInk adds:

- Auto Page Turn from 5 to 120 seconds
- Mark Finished and Mark Unread, with optional move to `/Read`
- The foundation for removing finished books from Recents

Tilt page turning and sensor-aware availability are inherited from CrossPoint. CrossInk expanded the control to four direction mappings, and Duet carries that X3 behavior into the shared X3/X4 reader. Duet also extends the inherited finish, Recents, and exit flows so removing a book from Recents does not delete its state, active time is flushed safely, and post-reading statistics can be shown.

### Bookmarks and clippings

**Lineage:** CrossPoint supplies clipping storage, highlights, and the `/My Clippings.txt` copy; bookmark behavior descends through CrossPoint/CrossInk. Duet integrates them into the current Saved Items and reader-menu flows.

Bookmarks can be created or removed while reading and browsed later through Saved Items. Text selections can be saved as clippings, shown again as highlights, browsed per book, and appended to the familiar `/My Clippings.txt` file.

Duet stores the structured clipping data under `/.duet/state/clippings` and keeps the text file as a portable, human-readable copy. Existing inherited clipping files remain available to the migration and recovery path.

### Typography and page layout

**Lineage:** Per-book typography, margins, alignment, embedded styles, hyphenation, and image controls are inherited from CrossPoint/CrossInk. CrossInk added Bionic Reading, Guide Dots, forced indents, and its built-in font work. Duet adds the line-spacing preset UI, expanded status choices, synthetic style fallbacks, and difficult-book integration described below. Dark Reader and the expanded text-rendering controls have CrossPoint/CPR-vCodex lineage with Duet integration.

Reader settings have two scopes. **Settings -> Reader** changes the global defaults used by EPUBs that do not have an override. Changing an option through **Reader Menu -> Reader Options** while an EPUB is open automatically creates or updates that EPUB's `reader_settings.bin`. A book with an override keeps it even when the global defaults change. To make that book follow the global defaults again, hold **Confirm** on it in Browse Files or Recent Books and choose **Reset Book Reader Settings**.

Per-book overrides can cover:

- Font family and size
- Margin and orientation
- Paragraph alignment
- Tight, Normal, and Wide line-spacing presets
- Granular line-spacing adjustment
- Embedded publisher styles
- Hyphenation
- Image display, placeholder, or suppression
- Reading aids and render profile

Text can be left, centered, right-aligned, justified, or left to the book's own style. Margins range from 5 to 40 pixels. Where an EPUB provides usable location metadata, publisher page numbers can remain stable even when the reader layout changes.

Display controls include:

- Normal, Dark, and Extra Dark text
- Auto, Fast, Half, and Full refresh modes
- Dark Reader Mode
- Bionic Reading in Off, Normal, and Subtle modes
- Guide Dots
- Forced first-line indents
- Anti-aliasing
- Configurable status-bar content and progress style

When a selected font lacks a bold or italic face, Duet can synthesize the missing style.

### Difficult books and chapter changes

**Lineage:** This starts with the inherited CrossPoint/CrossInk layout engine. Balanced, Light, and Safe Mode fallbacks, guarded low-memory retries, and input-aware chapter pre-indexing are original Duet reliability work.

EPUBs are not always polite. Duet offers CrossInk Default, Balanced, Light, and Safe Mode render profiles, plus guarded low-memory retries for chapters that cannot be laid out normally.

The next chapter can be indexed after the visible page becomes idle. That work can cancel or defer when input arrives, heap falls, or allocation pressure rises. Its timing log records completion, cancellation, deferral, failure, and duration so a long chapter transition can be diagnosed instead of guessed at.

### Hyphenation and localization

**Lineage:** The hyphenation and translation systems are inherited from CrossPoint/CrossInk. Duet carries that system into both public builds, adds and maintains newer catalog strings, and supplies safe English fallback behavior.

The public X3 and X4 profiles retain all ten bundled hyphenation languages. Duet currently carries 26 interface translation catalogs:

- English
- Spanish
- French
- German
- Czech
- Slovak
- Brazilian Portuguese
- Russian
- Swedish
- Romanian
- Catalan
- Ukrainian
- Belarusian
- Italian
- Polish
- Finnish
- Danish
- Dutch
- Turkish
- Kazakh
- Hungarian
- Lithuanian
- Slovenian
- Valencian
- Vietnamese
- Hebrew

If a newer string is missing from a catalog, the interface safely falls back to English.

## The library

**Lineage:** The list browser and folder navigation are inherited from CrossPoint. CrossInk supplied Recent Books and related cover work. Duet combines those foundations with adapted grid/carousel designs **and its own search, metadata, cache, filter, and input-first systems**.

Duet has six book-browser layouts:

1. One-line list
2. Two-line list
3. 2x2 cover grid
4. 3x3 cover grid
5. 4x4 cover grid
6. Five-cover carousel

Folders stay in the fast list interface. Cover Grid and Carousel apply at the book level, where they are useful.

### Cover grids

**Lineage:** Grid geometry is adapted from CrumBLE; Duet supplies the current rendering, labels, badges, pagination, and input-first cover pipeline.

The three grid densities let each book keep a separate title and author line, plus reading-state badges, page count, and book count. Duet separates cursor/input state from cover hydration so navigation can begin before every image is ready. Large-library consistency while covers are still being generated remains one of the important alpha test areas.

### Five-cover carousel

**Lineage:** The engine is adapted from CrossInk Carousel, cover-forward interaction is informed by CrossPoint Flow, and the current cache, input, text, count, and lookahead behavior is Duet work.

The carousel shows the selected cover in the center, two neighboring covers on each side, separate title and author text, the book count, and adjacent lookahead so the next entering cover can already be prepared.

### High-quality thumbnail pipeline

**Lineage:** The exact-size thumbnail system, sharded paths, bounded shared cover cache, carousel lookahead, neighboring grid-page prefetch, and desktop prefill are original Duet performance work built around the adapted views.

Grid and carousel covers use exact layout-specific 1-bit thumbnails rather than stretching one generic small image into every slot. The cache is sharded to avoid crowded FAT directories, shares bounded decoded cover data across views, looks ahead in the carousel, and can prefetch the neighboring grid pages.

For a large or multiply organized library, the optional [desktop cover prefill][3] creates every cover thumbnail on a computer in one incremental pass. The firmware still generates covers for genuinely new books later. This is especially useful after loading a large library because the reader does not have to extract hundreds of EPUB covers while the user is trying to browse.

The repository also includes a ready-to-paste [computer-assistant prompt][4] for Codex, Claude CoWork, Perplexity Computer, or another local assistant. Nobody has to use an assistant; the same document includes the direct terminal commands.

### Sorting, filters, and saved views

**Lineage:** Recent Books descends from CrossInk. Sorting, reading-state filters, status badges, and the current path-repair behavior are Duet work. Favorites was informed by CPR-vCodex, with a local Duet implementation.

Book views can sort by title or author. The library can filter to:

- All
- In Progress
- Unread
- Finished

Favorites can be added, removed, reordered, opened, or sent to More Info. If a finished book moves to `/Read`, Duet can repair the favorite's stored path.

Recent Books can use a list or 3x3 cover grid. Ordinary navigation does not open and parse EPUBs, and removing a title from Recents does not erase its progress or reading statistics.

### Smart search and More Info

**Lineage:** Ranked title/author/series search, the optional catalog format, More Info, and their Calibre/tracker integration are original to Duet. CPR-vCodex's analytics direction informed the catalog-backed library pages, but their implementation here is local.

The optional library catalog lets Duet search without opening every EPUB. Ranked autocomplete and results can match any title word, author, or series. A short Confirm press opens the selected book; a one-second Confirm hold opens More Info.

More Info can show:

- Cover
- Title and author
- Series and series index
- Genre
- Spice level
- Reading state and progress
- Catalog description
- Open action

The same catalog supplies Library Overview, Reading Taste, and Series Progress without forcing the device to repeatedly parse book files. It can be generated from a Calibre library or another reviewed tracker export.

### Clean Library Cache

**Lineage:** Original to Duet.

Clean Library Cache compares the books currently present with Duet's cache folders. It refuses to clean after an incomplete or empty scan, works in bounded batches, and moves confirmed orphaned caches into the recoverable `/.duet/books/.attic` folder instead of deleting them.

## Home, arranged your way

Duet includes eight Home themes:

1. Classic
2. Minimal
3. Dashboard
4. Dashboard Extended
5. Lyra
6. Lyra Extended
7. Lyra Carousel
8. RoundedRaff

**Lineage:** Home is intentionally a mixed family rather than an all-Duet feature set.

Classic, Lyra, Lyra Extended, Lyra Carousel, and RoundedRaff descend from the CrossPoint/CrossInk Home family. Minimal comes from CrossInk. Dashboard is original to Duet. Dashboard Extended adapts the current-book card, recent-cover row, stat strip, bottom navigation, and Tools direction from CrossPet, then connects them to Duet's own statistics and cover caches.

### Home Stats picker

**Lineage:** Original to Duet.

On stat-bearing themes, Home is not locked to somebody else's idea of the most important number. Seven Dashboard rows, two footer slots, and four strip slots can each choose from 18 values:

- None
- Book Time
- Time Left
- Progress
- Daily Average
- Pace
- Sessions
- Average Session
- Days Reading
- Estimated Finish
- Streak
- Reader Type
- This Device Time
- All Devices Time
- This Device Books
- All Devices Books
- Today
- Total Sessions

### Home and Apps launcher

**Lineage:** Customizable launcher placement was informed by CPR-vCodex. Duet's 22-destination catalog, Home/Apps layout store, ordering UI, and protected escape routes are a local implementation.

Shortcuts can be placed, hidden, and reordered on Home and Apps. Required escape routes are protected so customization cannot strand the reader in a screen with no way back.

The complete catalog has 22 destinations:

1. Browse Files
2. Search Library
3. Recent Books
4. Reading Stats
5. Heatmap
6. Reading Profile
7. Saved Items
8. Favorites
9. Achievements
10. Dictionary
11. Tetris
12. If Found
13. Screen Clean
14. Nearby Stats Sync
15. File Transfer
16. OPDS
17. KOReader Sync setup
18. Sleep
19. Read Me
20. Apps
21. Customize Home & Apps
22. Settings

These are launcher destinations, not 22 separate apps. `Apps` itself appears only on Home. Browse Files and Settings remain in Apps so a compact Home menu can stay focused.

The default Home shortcuts are Search, Recent Books, Reading Stats, Saved Items, Favorites, Sleep, Nearby Stats Sync, If Found, and Apps.

## The reading-statistics lab

**Lineage:** CrossInk supplied the foundational reading-statistics system, and CPR-vCodex supplied important analytics ideas and the original reading-profile radar direction. Duet substantially expands that lineage into the current data model, merge-aware views, and 33-page statistics lab.

Duet's statistics are intended to answer useful questions, not merely make a large number go up. It tracks time, pages, sessions, dates, pace, completions, book attribution, streaks, device contributions, and retained session detail.

### What counts

**Lineage:** Basic reading-time and per-book tracking descend from CrossInk/CPR-vCodex. Deep-sleep commit behavior was adapted through CrumBLE from aalu's reading-stats fix. The time-versus-counted-session rule, daily attribution ledger, exact session log, and idempotent integration are Duet work. Stats Date and manual-correction ideas have CPR-vCodex lineage and were expanded locally.

Reading time accumulates while a supported book is actively open, including a short reading span with no page turn. A session enters the session count and session history only after at least one page turn. Its pre-turn reading time is still retained.

Active time is committed before deep sleep through an idempotent path, so repeating the commit does not double-count the same span. Books under `/ignore_stats/` retain their own progress without contributing to aggregate statistics.

The daily journal stores exact daily time, sessions, pages, completions, and recent session records. A separate attribution ledger records which books contributed time and pages on each date and labels older history that cannot be attributed precisely. The session log records start time, duration, pages, and book for every retained counted session.

Because these readers do not have a dependable real-time clock, Duet also keeps a CRC-protected Stats Date. It can be edited deliberately or updated from NTP while connected.

### All 33 top-level pages

**Lineage:** This complete 33-page set and its merge-aware navigation are Duet work built on inherited statistics records and CPR-vCodex analytics direction. Individual pages reuse inherited measurements where appropriate.

| # | Page | What it shows |
| --: | --- | --- |
| 1 | Current Book | Active or latest book/session, progress, time left, estimated WPM, totals, and dates |
| 2 | Book Progress | Progress graph and estimated-completion context |
| 3 | Book Patterns | Per-book sessions, pace, time, and reading pattern |
| 4 | Trends | Today, last 7 days, last 30 days, and current-year summaries |
| 5 | Activity Chart | Recent activity against the daily goal |
| 6 | Daily Minutes | Scrollable 90-day daily history with day detail |
| 7 | Monthly Calendar | Calendar with per-day book drill-down |
| 8 | Heatmap | Twelve months of reading intensity |
| 9 | Reading Profile | Raw seven-day grid of 12 recent measurements |
| 10 | Goals | Daily goal and goal-streak detail |
| 11 | Recent Sessions | Scrollable exact session history |
| 12 | Weekday Pattern | Reading distribution by weekday |
| 13 | Pace Trend | Relative 30-day screen-page pace and direction; historical journals do not contain the per-book word totals needed to reconstruct WPM |
| 14 | Time of Day | Morning, afternoon, evening, and night distribution |
| 15 | Monthly Trend | Reading by month |
| 16 | Year Line | Cumulative current-year line and page-turn total |
| 17 | Device Split | Per-device comparison after device data has been synced |
| 18 | Session Lengths | Short, medium, and long session distribution |
| 19 | Streak Milestones | Current and achieved streak levels |
| 20 | Started / Finished | Monthly starts compared with completions |
| 21 | Reading Dates | Scrollable per-book start and finish history |
| 22 | Reader DNA | Six-axis 30-day radar plus raw measurements and overall score |
| 23 | Reader DNA Details | Measurements and scoring context behind Reader DNA |
| 24 | Reading Signature | Plain-language summary of recent reading habits |
| 25 | Signature Metrics | Raw measurements behind the reading signature |
| 26 | Fastest Reads | Completed books ranked by elapsed reading days |
| 27 | Wrapped | Twelve-month time, days, completions, streak, weekday, and average-session summary |
| 28 | Started Books | Scrollable in-progress list with estimates |
| 29 | Library Overview | Catalog-backed counts and completion |
| 30 | Reading Taste | Catalog-backed genre, spice, and author patterns |
| 31 | Series Progress | Scrollable series completion |
| 32 | This Device | Device-local aggregate statistics |
| 33 | All Devices | Merged aggregate statistics after synced data exists |

Daily Minutes and Monthly Calendar also open day-detail and correction subviews. Reading Dates opens a guarded Book Dates editor. Time adjustments are validated and reversible. Start and finish dates require a deliberate hold before editing.

### Reading Profile and Reader DNA are different

**Lineage:** CPR-vCodex's reading-profile diamond supplied the original radar idea. Duet separates the raw seven-day Reading Profile from the new six-axis, 30-day Reader DNA model and keeps the underlying measurements visible.

Reading Profile shows 12 raw measurements from the last seven days: reading days, goal days, known time, average reading day, sessions, average session, best day, completions, pages, current streak, longest streak, and timed/read days.

Reader DNA uses a 30-day window and normalizes six dimensions into a radar: Habit, Volume, Focus, Pace, Streak, and Finisher. The raw measurements stay visible so the score is not presented as magic or personality science.

### Local, merged, and repairable

**Lineage:** Device-local statistics begin in CrossInk. Person-level merged views, the complete cross-root `.cstats` format, validation, staged restore, safety export, and rollback behavior are Duet work.

This Device and All Devices remain separate views. After nearby sync, merged history can feed calendars, streaks, profiles, and library analytics while the device-local contribution stays inspectable.

Complete `.cstats` archives include recognized global, journal, ledger, session, clock, library, synced-device, and per-book statistics across both storage roots. Restore validates archive structure and per-entry CRCs, creates a safety export first, stages replacements, and can roll back after a failed restore.

Non-empty content-level export and restore round trips pass both device simulators. Physical X3/X4 archive acceptance remains part of the alpha test matrix.

## Sync without pretending every sync is the same

Duet has three distinct sync paths:

### Nearby reading-position sync

**Lineage:** CrossInk supplied Nearby Position Sync, including direct ESP-NOW exchange, book matching, position comparison, and the explicit Apply flow. Duet carries it into the shared X3/X4 build and adds later integration and reliability hardening.

Two Duet readers compare their positions in the current EPUB directly over ESP-NOW. The screen shows both positions and moves only after the reader explicitly chooses Apply. It does not silently send someone backward because one device opened an older position.

### Nearby reading-statistics sync

**Lineage:** CrossInk supplied the original two-device statistics-sync concept. The current ESP-NOW protocol, per-book/detail exchange, attribution merge, peer snapshots, and idempotent convergence layer are Duet work.

Two Duet readers directly exchange:

- Global totals
- Per-book summaries and detail
- Daily journal
- Per-date attribution ledger
- Stats Date
- Device name
- Retained peer snapshots

The merge is designed to be idempotent so repeated exchanges converge without double-counting. Repeated real-device convergence, interrupted sync, and asymmetric X3/X4 completion are still explicit alpha test targets.

### KOReader Sync

**Lineage:** The KOReader Sync foundation is inherited from CrossPoint. Duet integrates it into the current reader overlay and makes the local/remote choice explicit beside Nearby position sync.

KOReader Sync uses an account and handles remote/local position comparison for a book. Duet offers Apply Remote and Upload Local. It does not replace Nearby Stats Sync, and Nearby Stats Sync does not replace KOReader position sync.

## Achievements

**Lineage:** The first 62 milestone thresholds are adapted from CPR-vCodex. Duet adds 46 milestones plus the current persistent ledger, recovery, retroactive adoption, batched popup, and timing-safe refresh integration.

Duet contains 108 persistent achievement milestones:

- 62 thresholds adapted from CPR-vCodex
- 46 milestones original to Duet

The CPR-vCodex set covers books started, sessions, books finished, reading time, goal days, goal streaks, bookmarks, and longest sessions.

The Duet set covers reading days, reading streaks, screen pages, series started, series completed, spice levels, morning reading, night reading, weekend reading, and using two devices.

Unlocks persist in `/.duet/state/achievements.bin`, recover from a backup file, and can be retroactively adopted from existing history if the unlock ledger is missing. A popup can list every achievement unlocked in one batch and open **See All**. Achievement refresh work is deferred from timing-sensitive navigation paths.

The unlock ledger is not currently stored inside `.cstats`; restored reading history can re-derive many milestones.

## Fonts

**Lineage:** Lexend Deca, Bitter, the fallback stack, device-tuned built-in sizes, and the `.cpfont` foundation come through CrossInk/CrossPoint. Duet adds the categorized picker, matched-size A/B comparison, synthetic missing-style fallbacks, catalog cache, and public redistribution policy.

The public firmware includes two selectable built-in reading families:

- Lexend Deca
- Bitter

ChareInk7 is part of the fallback stack, not a third selectable built-in. Noto Emoji, Noto Sans Symbols, and Noto Sans CJK SC provide limited symbol/language fallback. Inter is the UI face, with IBM Plex Sans Hebrew fallback.

Built-in reading sizes are device-specific:

- X3: 10, 12, 14, and 16 pt
- X4: 16, 18, and 20 pt

Additional `.cpfont` families can be installed under `/.fonts/` or `/fonts/`. The font picker organizes them into:

- Serif
- Sans Serif
- Mono/Typewriter
- Accessibility
- Handwritten/Script
- Blackletter/Decorative

A family exposes the sizes actually installed and can provide Regular, Italic, Bold, and Bold Italic. Duet synthesizes missing bold and italic styles where needed. The picker includes compact Normal, Italic, and Bold specimens plus an A/B comparison view at matched sizes.

A cached font catalog avoids rescanning every family on every boot and recovers when an installed font is replaced or invalid.

The initial public alpha does **not** bundle the maintainer's personal 130-family SD-card collection. A separate public pack can follow only after every source, generated file, license, Reserved Font Name rule, and checksum is reviewed. [Font Sources][5] records that audit.

## Dictionary and reference tools

**Lineage:** The StarDict engine, word selection, suggestions, definitions, and lookup history are adapted from CPR-vCodex. Recovery and low-memory hardening are informed by CrumBLE's SEEK-derived work; Duet integrates and extends both paths.

Duet uses StarDict dictionaries installed under `/dictionaries/<Dictionary Name>/`. A package needs matching `.ifo`, `.idx`, and uncompressed `.dict` files, with optional `.syn`. A compressed `.dict.dz` must be extracted first.

The reader can:

- Prepare and switch between multiple dictionaries
- Select a word while reading
- Show suggestions and definitions without leaving the page
- Keep per-book looked-up-word history

That combined implementation includes corrupt-cache recovery, yielding index builds, visible progress, low-memory preflight, and preservation of SD-font glyphs.

## Sleep screens, power, and display care

**Lineage:** Sleep/wake, Custom, Cover, and Quick Resume foundations are inherited from CrossPoint/CrossInk. CrossInk also supplied its statistics sleep screens and control foundation. Duet adds Dashboard sleep, PNG Page Overlay composition, large-folder caching, and device-specific refresh work; locked Power-click cycling is adapted from CrumBLE, and awake multi-click detection is adapted from CrossPet.

Duet has 12 sleep-screen modes:

1. Blank
2. Dark
3. Light
4. Custom
5. Cover
6. Cover + Custom
7. Page Overlay
8. Reading Stats
9. Minimal
10. Minimal Stats
11. Dashboard
12. Quick Resume

Minimal Stats is currently available on X3 and hidden in the X4 settings UI.

Custom mode can rotate BMP files from `/.sleep` or `/sleep`, with a root `sleep.bmp` fallback. Cover modes support Fit or Crop and None, Contrast, or Inverted filtering.

Page Overlay places sleep artwork over the stored monochrome reader page. BMP white behaves as transparent; PNG alpha can preserve the page underneath. PNG sleep images are accepted in Page Overlay mode, not in every sleep mode.

While locked, an optional 1-, 2-, or 3-click Power gesture can cycle to a fresh sleep image and return directly to sleep. CrumBLE supplied the original one-tap behavior; Duet made the count configurable and defaults to three at Lauren's direction because one tap was too easy to trigger accidentally. Although the code intends to count the initial wake press as click one, the current physical builds can require one wake press followed by the three taps, making the default gesture feel like four physical presses. That is a disclosed alpha input quirk, not a fourth setting.

While awake, single, double, triple, and long Power actions can be configured separately. Long Menu and Back actions are configurable too. Possible actions include Sleep, page turn, bookmark, stats, Mark Finished, refresh, font change, Guide Dots, Bionic Reading, page-turn mode, Sync Progress, File Transfer, Calibre Wireless, network/hotspot, screenshot, Dark Mode, footnotes, Browse Files, and clipping. Tilt appears only when the sensor is available.

Quick Resume can preserve the visible screen for a faster return, with a configurable timeout. Reading time is still committed before deep sleep.

Screen Clean and device-specific sleep/wake handling provide deliberate full refresh paths for e-ink ghosting. Exact X4 panel behavior remains a physical test concern; the project does not claim that one refresh recipe behaves identically on every panel.

## Physical controls

**Lineage:** Button remapping and long-press behavior are inherited from CrossPoint/CrossInk. Duet extends the action catalog and integrates the CrossPet-derived awake multi-click detector across X3 and X4.

The side buttons can be disabled or mapped as Previous/Next, Next/Previous, or Next/Next. Their long press can be Off, chapter skip, font-size change, or orientation.

Front-button orientation awareness can be Off, apply to navigation buttons, or apply to all buttons. Front-button long press can be Off, chapter skip, font-size change, or orientation.

Together with the configurable Power, Menu, and Back actions, this lets the reader support a straightforward default layout or a heavily personalized button scheme without requiring touch input.

## Small tools that earn their place

**Lineage:** If Found, Screen Clean, Apps, and Favorites have CPR-vCodex design lineage with local Duet implementations. Tetris is adapted from Biscuit. Saved Items builds on inherited bookmark/clipping systems, and Read Me is Duet's on-device documentation route.

Duet's launcher also includes a few focused utilities:

- **If Found** displays the owner's chosen recovery message from `/if_found.txt`.
- **Screen Clean** performs deliberate deep-refresh cycles when the panel needs clearing.
- **Tetris** is adapted from Biscuit for Duet's buttons, layout, and e-ink refresh behavior.
- **Read Me** keeps an on-device guide available without another screen.
- **Saved Items** brings bookmarked books and saved material together.

Duet does not treat every launcher destination as a separate "app." These utilities sit beside direct routes to Search, Recents, stats, transfer, OPDS, sync, settings, and the file browser.

## Transfer and connectivity

**Lineage:** Browser transfer, WebDAV, Calibre Wireless, OPDS, firmware-update plumbing, USB transfer, and the basic font upload/download paths are inherited from CrossPoint. CrossInk adds the web EPUB optimizer, with CPR-vCodex/CrossInk lineage around later font-catalog work. Duet retains those systems and adds its public release-safety rules around them.

Duet retains the CrossPoint/CrossInk transfer foundation:

- Browser-based file management over joined Wi-Fi or a device hotspot
- WebDAV
- Calibre Wireless with the CrossPoint Calibre device plugin
- Multiple OPDS catalogs with Basic authentication
- Web EPUB optimization
- Font download and upload
- USB serial transfer support for compatible tools

The web portal can upload, download, rename, move, and delete SD-card files.

First Duet installation or recovery uses a device-matched custom USB BIN. A Duet-to-Duet update can use one firmware BIN at the SD-card root through **Settings > System > SD Firmware Update**. Wi-Fi update plumbing remains inherited, but public alpha downloads are released through the canonical GitHub repository.

## Reliability, storage, and recovery

**Lineage:** Persistence, basic crash reporting, cache movement, and the simulator begin in CrossPoint/CrossInk. Duet adds the canonical namespaced storage model, complete stats archives and guarded restore, recoverable cache cleanup, expanded breadcrumbs, device smoke paths, privacy auditing, and deterministic release tooling. Rolling global-stat backups have CPR-vCodex/CrossInk lineage with Duet integration.

Duet writes new state under `/.duet`: frequently accessed records in `/state`, durable per-book records in `/books`, disposable thumbnails and layouts in `/cache`, reading-stat archives in `/backups/reading-stats`, and migration markers in `/migration`. Existing `/.crossink`, `/.crosspoint`, and `/.crossink-stats-backup` files remain untouched as import and recovery sources; bounded global state imports on first boot and large per-book state migrates lazily as books are opened.

This split came from measured behavior on large development cards: crowded FAT directories could turn ordinary state lookups into multi-second scans. Moving hot files, sharding cover/layout caches, caching font discovery, and prefilling thumbnails on a computer reduce those repeated paths. Exact timing still depends on the device, SD card, library size, and folder structure.

Other recovery and diagnostic work includes:

- Debounced settings and progress persistence
- Bounded caches sized for the X3 memory limit
- Cache-safe book moves
- Recoverable `.attic` cleanup instead of eager deletion
- Rolling global-stat backups
- SD-card crash reports with faulting program counter and build identity
- Breadcrumb logs for boot, Home, picker, sync, reader transitions, and chapter pre-index timing
- X3 and X4 simulator smoke paths
- Seeded public statistics and media fixtures
- Deterministic release names and SHA-256 checksums
- Privacy and source-count audits

## X3 and X4 differences

**Lineage:** One shared X3/X4 source tree begins in the inherited firmware architecture. The current paired public profiles, shared-feature policy, and device-specific tuning are Duet integration work.

Duet uses one shared source tree for both readers, with device-specific build profiles and display geometry.

Current intentional differences include:

- X3 built-in font sizes: 10, 12, 14, 16 pt
- X4 built-in font sizes: 16, 18, 20 pt
- X3 can expose tilt page turn when its QMI8658 sensor is available
- Minimal Stats sleep mode is visible on X3 and hidden on X4
- Display refresh and memory limits require device-specific tuning even when the user-facing feature is shared

Unless a feature is explicitly device-specific, changes are intended for both firmware targets.

## Features unique to Duet

Duet is a fork and says so plainly. "Unique to Duet" means the named implementation or behavior was authored for this fork. When an addition sits on an inherited screen, protocol concept, or credited design reference, only the specific Duet extension is claimed.

### Reading and book UX

- Balanced, Light, and Safe Mode fallback orchestration with progressively safer layout retries
- Idle-only, cancellable next-chapter pre-indexing with timing telemetry
- The current compact left-side reader-overlay layout, combined action set, and sync chooser
- Expanded reader status choices and the line-spacing preset/granular interface
- Synthetic bold and italic fallbacks for incomplete SD-font families
- Categorized font browsing, compact specimens, matched-size A/B comparison, and recoverable font-cache behavior
- Book Info integration with reading state, catalog metadata, saved items, dictionary history, sync, and recovery actions

### Library, search, and covers

- Input-first cover browsing that separates cursor movement from SD-card cover hydration
- The five-cover library-carousel adaptation, adjacent lookahead, and title/author presentation
- Duet's rendering and input pipeline around the credited CrumBLE grid geometry
- Exact layout-specific thumbnails, sharded cache paths, shared base-cover cache, carousel lookahead, and neighboring grid-page prefetch
- Desktop X3/X4 cover prefill
- Author/title sorting, reading-state filters, and progress badges
- Ranked title-word/author/series search with autocomplete and More Info actions
- The optional library-catalog format, metadata-rich More Info screen, Library Overview, Reading Taste, and Series Progress
- The local Favorites implementation with ordering and moved-book path repair
- Clean Library Cache with bounded validation and recoverable `.attic` moves

### Home, statistics, and achievements

- Dashboard and the 18-value Home Stats picker
- Duet's Dashboard Extended implementation and stats/cache integration around CrossPet's credited design direction
- The local 22-destination customizable Home/Apps launcher with protected escape routes
- The current 33-page statistics lab
- Separate raw Reading Profile and normalized Reader DNA views with visible measurements
- The time-versus-counted-session rule, exact session log, and per-date book-attribution ledger
- Expanded per-book records, correction flow, and person-level merged statistics
- Forty-six additional achievements plus the persistent ledger, recovery, retroactive adoption, batched popup, and timing-safe refresh integration

### Device-to-device statistics

- The current Nearby Reading Stats Sync protocol and merge layer
- Idempotent repeated convergence, retained peer snapshots, and device identity
- Separate This Device and All Devices views feeding calendars, streaks, profiles, and library analytics

Nearby Position Sync is inherited from CrossInk. KOReader Sync is inherited from CrossPoint.

### Display, power, performance, and recovery

- PNG Page Overlay composition over the stored reader page
- Dashboard sleep presentation and large-folder sleep-image inventory caching
- Duet's expanded Power-action system around credited CrossPet/CrossInk foundations
- Shared X3/X4 integration with device-specific geometry, memory, refresh, and ghosting work
- Canonical `/.duet` state, books, cache, backup, and migration areas with non-destructive legacy import
- Complete `.cstats` archives with validation, CRCs, safety export, staged restore, and rollback
- Cache-safe book moves, breadcrumb performance telemetry, expanded simulators, physical-acceptance records, and deterministic public-release tooling

The canonical item-by-item inventory is the [Features Unique To Duet](../FEATURES.md#features-unique-to-duet) section of the feature catalog. The next section records upstream lineage instead of flattening everything into "ours" or "theirs."

## Credits and lineage

| Project | Relationship to Duet | Audited revision |
| --- | --- | --- |
| [CrossPoint Reader][6] | Original firmware foundation: device/display abstractions, readers, file browser, progress, chapters, footnotes, transfer, OPDS, KOReader Sync, sleep/wake, localization, simulator, and build system | `6e8dbd7f239eb562daa5de81362c0025ce833dd8` |
| [CrossInk][7] | Direct Duet baseline: typography and fallback work, Minimal Home, Bionic Reading, Guide Dots, forced indents, controls, sleep screens, finished-book flow, initial stats/sync, Nearby Position Sync, Auto Turn, Recent Books, and web EPUB optimizer | `b7f6708f96d05e5851f8bcfaaf57bc0e91dc0567` |
| [CrossInk Carousel][8] | Carousel engine directly adapted and extended for Duet's five-cover library view | `bf2076d7acc5a5993d11a2d3246c943c26c44a1d` |
| [CrossPoint Flow][9] | Cover-forward browsing direction and interaction reference; Duet does not claim its Lua implementation | `6bc3bcd92f9c8db991ac59a262df3ac00948bae1` |
| [CrumBLE][10] | Grid geometry, dictionary-hardening patterns, locked Power-click sleep cycling, large-folder ideas, and deep-sleep time commit | `a4c5507d14041a92389903675fb5edf94e39c8a8` |
| [CPR-vCodex][11] | StarDict engine/UI, first 62 achievements, and reading-stat/Apps/Favorites/If Found/Screen Clean design lineage | `0111c4811bbc4ec95cbbe7577212ec4eea5b8cd5` |
| [CrossPet][12] | Dashboard Extended layout and awake multi-click Power detector, both substantially adapted locally | `be9b86d1d2a3196144b01f27b78417cf62a122c8` |
| [Biscuit][13] | Tetris implementation adapted for Duet's controls, layout, and e-ink refresh behavior | `483ac2951bc98b71bafacb18adbcac99da04bbe0` |

CrumBLE credits its dictionary base to [SEEK Reader][14] and its reading-time fix to [aalu's CrossPoint fork][15]. Duet preserves those transitive credits. The firmware and all directly adapted projects above use the MIT license; font licenses are handled separately.

Copyright in Duet's original modifications belongs to Lauren Landau. Upstream copyrights remain with their authors. Forks, modifications, redistribution, and commercial use are allowed under MIT when the required notices are kept. The goal is not to stop forks. It is to keep authorship, credit, project identity, and the canonical maintainer clear.

## What Duet deliberately does not include

Duet does not currently include:

- Flashcards
- A virtual pet
- Weather, Pomodoro, chess, Sudoku, Minesweeper, or 2048
- CrossPet's Bluetooth keyboard/page-turner build
- A dedicated sleep-image preview and ordering manager
- PDF rendering
- A general web browser
- RSS or news reading
- Audio or audiobook playback
- Typed notes or a general productivity suite

That boundary is intentional. Duet is a reading system, not an attempt to turn the X3 or X4 into a tiny phone.

## What the alpha still needs to prove

The current public-alpha test targets include:

- Grid and carousel navigation while covers are still hydrating
- Forward and backward grid pages after neighboring-page prefetch
- Very large folders and slower SD cards
- Chapter transitions and cancellable background pre-indexing
- Repeated sleep/wake and open/close use over several days
- X4 refresh behavior and ghosting on different panels
- Nearby statistics convergence in both directions
- Nearby position comparison and explicit Apply
- Physical X3/X4 `.cstats` export and restore
- Font, dictionary, More Info, search, and all 33 stats pages

Known constraints:

- On-reader cover generation can still be slow in a huge unprefilled library.
- A difficult EPUB chapter can still need visible layout work when background indexing could not finish.
- X3 and X4 can behave differently under the same library and SD-card load.
- Damaged SD-card filesystems can imitate firmware defects because the books and persistent state live on the card.
- The achievement unlock ledger is not yet part of `.cstats`.
- The personal 130-family font collection is not bundled.
- Physical acceptance of the exact public candidate is still pending.

## Testing and privacy

Start with [Alpha Testing][16], the short [alpha.6 acceptance route][17], and the full [Physical Test Matrix][18].

A useful report includes:

- Device model
- Exact Duet version
- SD-card brand, capacity, and filesystem
- Approximate library and folder size
- View, sort mode, and cover-prefill state
- Exact button sequence
- Expected and actual result
- The smallest relevant timing or crash log

Do not publish ebooks, complete SD-card archives, credentials, private catalogs, `if_found.txt`, or raw personal reading history. Public screenshots use fabricated statistics. Approved real book titles and covers may appear in the interface, but the corresponding EPUBs and extracted cover files are not release assets.

## Install and recovery

**Lineage:** USB installation, SD updates, and the inherited updater plumbing come from CrossPoint/CrossInk. Duet supplies the device-explicit public BIN names, version checks, checksums, backup-first instructions, and release gates.

Public alpha builds belong on the canonical [Duet Releases][19] page with device-specific filenames and SHA-256 checksums.

1. Back up the SD card.
2. Download the X3 or X4 BIN that exactly matches the device.
3. For first installation or recovery, use the CrossPoint web installer's **Custom .bin** option.
4. For a Duet-to-Duet update, put exactly one firmware BIN at the SD-card root and use **Settings > System > SD Firmware Update**.
5. After flashing, confirm the expected Duet version under **Settings > System** before a long reading session.
6. Keep a known-good recovery BIN available.

Both devices must run the same Duet release before using Nearby Sync.

## Where to go next

- [README][20]: project front page and installation entry point
- [Feature and Lineage Catalog][21]: canonical technical inventory
- [Third-Party Notices][22]: exact provenance
- [Alpha Testing][23]: known issues, logs, and reporting
- [Cover Prefill][24]: computer-side thumbnail generation
- [User Guide][25]: everyday use
- [Public Screenshot and Demo Plan][26]: media rules and shot list
- [Public Release Readiness][27]: remaining release gates

Duet is open source under MIT. The public alpha is an invitation to test it carefully, report honestly, and help make the X3 and X4 nicer places to read.

[1]: ../FEATURES.md
[2]: ../THIRD_PARTY_NOTICES.md
[3]: COVER_PREFILL.md
[4]: AI_COVER_PREFILL_PROMPT.md
[5]: ../FONT_SOURCES.md
[6]: https://github.com/crosspoint-reader/crosspoint-reader
[7]: https://github.com/uxjulia/CrossInk
[8]: https://github.com/chintanvajariya/CrossInk-Carousel
[9]: https://github.com/ideo2004-afk/crosspoint-reader-lua
[10]: https://github.com/imshentastic/CrumBLE
[11]: https://github.com/franssjz/cpr-vcodex
[12]: https://github.com/trilwu/crosspet
[13]: https://github.com/yattsu/biscuit
[14]: https://github.com/seek-reader/seek
[15]: https://github.com/aaludon/crosspoint-reader-aalu
[16]: ALPHA_TESTING.md
[17]: ALPHA6_ACCEPTANCE_QUICKSTART.md
[18]: PHYSICAL_TEST_MATRIX.md
[19]: https://github.com/lauren-alexandra/duet-xteink/releases
[20]: ../README.md
[21]: ../FEATURES.md
[22]: ../THIRD_PARTY_NOTICES.md
[23]: ALPHA_TESTING.md
[24]: COVER_PREFILL.md
[25]: ../USER_GUIDE.md
[26]: SCREENSHOT_PLAN.md
[27]: ../PUBLIC_RELEASE_READINESS.md
