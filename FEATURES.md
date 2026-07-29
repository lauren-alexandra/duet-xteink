# Duet Feature And Lineage Catalog

This is the canonical feature inventory for the current Duet development branch. It records what is implemented, what each feature does, and where its code or design lineage began. It is not a claim that every item has completed physical-device acceptance on the exact public candidate. See [PUBLIC_RELEASE_READINESS.md](PUBLIC_RELEASE_READINESS.md) and [docs/PHYSICAL_TEST_MATRIX.md](docs/PHYSICAL_TEST_MATRIX.md) for the remaining release gates.

Duet is created and maintained by Lauren Landau as an independent, one-person project. The lineage labels below credit every inherited or adapted foundation separately from Lauren's original Duet work.

## How To Read The Lineage Labels

- **CrossPoint base**: inherited from CrossPoint Reader through CrossInk.
- **CrossInk**: present in Duet's recorded CrossInk baseline, or built from a CrossInk-specific feature.
- **Adapted**: identifiable upstream code was ported and then changed locally.
- **Informed by**: the upstream project supplied the design, behavior, or algorithmic direction; Duet's implementation is local.
- **Duet**: original to this fork.

Many features have more than one label because Duet often extends an inherited system or combines upstream ideas with a new implementation.

## Verification Labels

- **Implemented** means the current source contains the feature and its reachable UI or service path.
- **Simulator-verified** means the X3 and X4 simulator smoke paths exercise the feature or its underlying contract.
- **Hardware acceptance pending** means the exact public BINs still need the corresponding real-device check. A successful build, simulator run, or staged SD card is not treated as physical acceptance.

Unless a row says otherwise, the features below are **Implemented**. Exact alpha.6 hardware status remains in the physical test matrix.

## Current Source Totals

- 8 Home themes
- 6 book-browser layouts: one-line list, two-line list, 2x2 grid, 3x3 grid, 4x4 grid, and five-cover carousel
- 33 top-level statistics pages
- 22 configurable launcher destinations
- 12 sleep-screen modes
- 108 persistent achievement milestones
- 26 UI translation catalogs

## Audited Baselines

| Project | Role in Duet | Audited revision |
| --- | --- | --- |
| [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) | Original firmware foundation, represented by the CrossPoint side of the recorded CrossInk merge | `6e8dbd7f239eb562daa5de81362c0025ce833dd8` |
| [CrossInk](https://github.com/uxjulia/CrossInk) | Direct firmware baseline for Duet | `b7f6708f96d05e5851f8bcfaaf57bc0e91dc0567` |
| [CrossInk Carousel](https://github.com/chintanvajariya/CrossInk-Carousel) | Carousel engine adapted for the library | `bf2076d7acc5a5993d11a2d3246c943c26c44a1d` |
| [CrossPoint Flow](https://github.com/ideo2004-afk/crosspoint-reader-lua) | Cover-forward browsing design reference | `6bc3bcd92f9c8db991ac59a262df3ac00948bae1` |
| [CrumBLE](https://github.com/imshentastic/CrumBLE) | Grid geometry, sleep cycling, time-commit behavior, and dictionary hardening patterns | `a4c5507d14041a92389903675fb5edf94e39c8a8` |
| [CPR-vCodex](https://github.com/franssjz/cpr-vcodex) | Dictionary implementation, achievement thresholds, and analytics/app design lineage | `0111c4811bbc4ec95cbbe7577212ec4eea5b8cd5` |
| [Biscuit](https://github.com/yattsu/biscuit) | Tetris implementation | `483ac2951bc98b71bafacb18adbcac99da04bbe0` |
| [CrossPet](https://github.com/trilwu/crosspet) | Dashboard Extended layout and awake Power multi-click detector | `be9b86d1d2a3196144b01f27b78417cf62a122c8` |

The exact file-level attribution and transitive SEEK/aalu credits are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Features Unique To Duet

"Unique to Duet" means the named implementation or behavior was authored for this fork. When a feature sits on an inherited screen, protocol concept, or design reference, only Duet's specific extension is claimed here; the upstream foundation remains credited in the main catalog and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

### Reading And Book UX

- Balanced, Light, and Safe Mode fallback orchestration for difficult EPUBs, including up to three progressively safer layout attempts.
- Guarded next-chapter pre-indexing that waits for an idle visible page and cancels or defers for input, low heap, or allocation pressure.
- Chapter pre-index timing records covering completion, cancellation, deferral, failure, and duration.
- The current compact left-side reader-overlay layout, combined action set, and KOReader/Nearby position-sync chooser, built on the inherited in-book menu foundation.
- Duet's expanded reader status choices, line-spacing preset interface, and granular spacing controls.
- Synthetic bold and italic fallbacks for SD fonts whose installed family lacks those faces.
- The categorized font picker, compact Normal/Italic/Bold specimen layout, matched-size A/B comparison, and recoverable font-catalog cache.
- Per-book Book Info integration with reading state, catalog metadata, saved items, dictionary history, sync, and recovery actions.

### Library, Search, And Covers

- Input-first cover browsing that separates cursor movement and placeholders from slower SD-card cover hydration.
- The library adaptation of the five-cover carousel, including adjacent lookahead, persistent exact thumbnails, and title/author presentation.
- Duet's grid rendering and input pipeline around the CrumBLE-derived 2x2, 3x3, and 4x4 geometry.
- Layout-specific high-quality 1-bit thumbnails, sharded cache paths, a bounded shared base-cover cache, carousel lookahead, and neighboring grid-page prefetch.
- Desktop cover prefill that generates the exact X3/X4 cache assets before a large library reaches the device.
- Author/title sorting and All, In Progress, Unread, and Finished filters with progress badges.
- Ranked search across any title word, author, or series, including autocomplete, short-press Open, and long-press More Info.
- The optional `library_catalog.tsv` format and More Info screen with cover, series, genre, spice, progress, description, and Open.
- Catalog-backed Library Overview, Reading Taste, and Series Progress implementations.
- The local Favorites implementation with ordering, direct Open, More Info, and path repair after a book moves to `/Read`.
- Clean Library Cache with bounded scanning, incomplete-scan refusal, and recoverable `.attic` moves instead of deletion.

### Home, Statistics, And Achievements

- The Dashboard Home theme and its dense current-book statistics.
- The Home Stats picker: seven Dashboard rows, two footer slots, and four strip slots configurable from 18 values.
- Duet's Dashboard Extended implementation, statistics integration, and cover-cache behavior, using CrossPet's credited layout direction.
- Duet's customizable 22-destination Home/Apps launcher implementation with protected escape routes.
- The current 33-page statistics lab and its chart, calendar, trend, drill-down, correction, library, device, and summary pages.
- The separate raw seven-day Reading Profile and normalized 30-day six-axis Reader DNA model, with visible underlying measurements.
- The time-versus-counted-session rule: all active reading time is retained, while a counted session requires at least one page turn.
- Exact retained session logging plus the per-date book-attribution ledger.
- Expanded per-book records covering time, counted sessions, pages, pace, dates, completion state, last read, and estimated time left.
- Person-level merged statistics that keep This Device and All Devices inspectable separately.
- Duet's manual date/time correction flow with validation, deliberate date-edit holds, safety copies, and rollback.
- Forty-six additional achievements plus the persistent ledger, `.bak` recovery, retroactive adoption, batched popup, and timing-safe refresh integration.

### Device-To-Device Statistics

- The current Nearby Reading Stats Sync protocol and merge layer for global totals, per-book summaries/details, journals, attribution ledgers, Stats Date, device names, and retained peer snapshots.
- Idempotent repeated-merge behavior designed to converge without double-counting.
- Device-local and person-level aggregate separation across calendars, streaks, profiles, and library analytics.
- Device Split and synced-device identity records.

Nearby Position Sync is **not** unique to Duet; it is inherited from CrossInk. KOReader Sync is inherited from CrossPoint.

### Display, Power, And Device Integration

- The PNG Page Overlay compositor that reveals the stored monochrome reader page through transparent regions.
- Dashboard sleep presentation and Duet's large-folder sleep-image inventory cache.
- Duet's configurable Off/1/2/3-click extension to CrumBLE's locked one-tap sleep-image cycling, including the Lauren-selected three-click default.
- Duet's expanded Power-action mapping system around the credited CrossPet multi-click detector and inherited CrossInk controls.
- Shared X3/X4 integration with device-specific geometry, memory profiles, refresh handling, and X4 ghosting repairs.

### Performance, Recovery, And Release Tooling

- Duet's canonical `/.duet` storage namespace, split into state, durable per-book records, disposable caches, backups, and migration records, with non-destructive import and read fallback from inherited `/.crossink`, `/.crosspoint`, and `/.crossink-stats-backup` data.
- Complete `.cstats` archives spanning global, journal, ledger, session, clock, library, synced-device, and per-book records.
- Archive header/path/size/count validation, per-entry CRCs, legacy-root mapping, automatic pre-restore safety export, staged replacement, and rollback.
- Cache-safe book moves that preserve durable progress and statistics while disposable layout data can rebuild.
- Boot, Home, picker, sync, reader-transition, and chapter-pre-index breadcrumb telemetry.
- Expanded X3/X4 simulator fixtures, whole-interface smoke paths, archive round trips, and physical-acceptance records.
- Deterministic public build names, checksums, privacy auditing, release packaging, catalog generation, and release-gate tooling.

## Devices, Storage, And Formats

| Feature | Details | Lineage |
| --- | --- | --- |
| XTEINK X3 and X4 | One shared source tree with device-specific display geometry and public build profiles. | CrossPoint base; CrossInk; Duet integration |
| SD-backed state | Books, settings, progress, statistics, fonts, dictionaries, covers, sleep images, and recoverable caches live on the SD card. | CrossPoint base; CrossInk; Duet storage split |
| Reading formats | EPUB, XTC, XTCH, TXT, and Markdown. Markdown currently uses the TXT reader rather than a separate Markdown renderer. | CrossPoint base; Duet Markdown routing |
| Image viewer | BMP viewing and sleep-image selection from the file browser. | CrossPoint base; CrossInk |
| EPUB image support | Baseline JPEG/PNG EPUB rendering, image display/placeholder/suppress modes, and memory-aware image handling. GIF and progressive JPEG remain unsupported. | CrossPoint base; Duet hardening |
| Public hyphenation build | The public X3/X4 targets retain all ten bundled hyphenation languages. The private size-optimized builds do not. | CrossPoint base; public-release configuration by Duet |
| UI localization | 26 catalogs: English, Spanish, French, German, Czech, Slovak, Brazilian Portuguese, Russian, Swedish, Romanian, Catalan, Ukrainian, Belarusian, Italian, Polish, Finnish, Danish, Dutch, Turkish, Kazakh, Hungarian, Lithuanian, Slovenian, Valencian, Vietnamese, and Hebrew. Missing newer strings safely fall back to English. | CrossPoint base; CrossInk; Duet additions |

## Reading And Navigation

| Feature | Details | Lineage |
| --- | --- | --- |
| Persistent reading position | Per-book progress survives restart and firmware updates when the SD state is preserved. | CrossPoint base |
| Bookmarks | Create/remove bookmarks in the reader and browse all bookmarked books from Saved Items. | CrossInk; CrossPoint improvements |
| Clippings | Select and save text, browse per-book saved clippings, render them as highlights, and append a Kindle-style text copy to `/My Clippings.txt`. | CrossPoint base; Duet menu integration |
| Chapters and Go To | Table-of-contents navigation, chapter skipping, and percentage jumps. | CrossPoint base |
| Footnotes | Footnote navigation, quick return, and configurable Power-button handling. | CrossPoint base |
| Reader quick overlay | Left-side overlay for Chapter, Dictionary, Go To, Sync, Stats, Tilt where available, Auto Turn, Spacing, Reader Options, and More. Sync opens a KOReader/Nearby chooser; More opens the full menu. | CrossInk menu foundation; informed by CPR-vCodex quick settings; Duet layout and actions |
| Full reader menu | Reader and control options, Book Info, completion state, bookmarks, clippings, dictionary history, screenshots, QR position sharing, cache/stat actions, and reader utilities. | CrossPoint base; CrossInk; Duet additions |
| Auto Page Turn | Configurable 5-120 second interval. | CrossInk |
| Tilt Page Turn | Optional X3 tilt-to-turn with four direction mappings; hidden when the QMI8658 sensor is unavailable. | CrossPoint base; CrossInk four-direction expansion; Duet shared-build integration |
| Finished-book workflow | Mark finished/unread, optional 99% prompt, optional move to `/Read`, and optional removal of read books from Recents. | CrossInk; Duet library integration |
| Screenshots | Hardware shortcut and in-reader screenshot action with files saved to the SD card. | CrossPoint base |
| Reader exit behavior | Return to Home or file browser, optional post-reading stats, and active-session flush. | CrossPoint base; CrossInk; Duet |
| XTC status bar | Optional top, bottom, or hidden XTC status bar. | CrossPoint base |

## Reader Layout, Typography, And Accessibility

| Feature | Details | Lineage |
| --- | --- | --- |
| Global defaults and per-book reader overrides | **Settings -> Reader** changes the defaults used by books without an override. **Reader Menu -> Reader Options** while an EPUB is open automatically saves font, size, margin, orientation, alignment, line spacing, embedded style, hyphenation, images, reading aids, and render profile for that EPUB. **Reset Book Reader Settings** removes the override and returns the book to the global defaults. | CrossPoint base; CrossInk; Duet |
| Line spacing | Tight, Normal, and Wide presets plus granular percentage adjustment. | CrossPoint base; Duet preset UI |
| Margins and alignment | 5-40 px margins; justified, left, center, right, or book-defined alignment. | CrossPoint base |
| Embedded styles and images | Toggle publisher styles; display, placeholder, or suppress images. | CrossPoint base |
| Publisher page numbers | Optional stable/publisher page-number support where the EPUB supplies usable location metadata. | CrossPoint base; Duet optimizer integration |
| Text rendering | Anti-aliasing, Normal/Dark/Extra Dark text, and Auto/Fast/Half/Full reader refresh modes. | CrossPoint base; CrossInk/CPR-vCodex lineage; Duet integration |
| Dark Reader Mode | White-on-black reader presentation. | CPR-vCodex/CrossPoint lineage; Duet integration |
| Bionic Reading | Off, Normal, and Subtle modes. Missing bold faces use a measured synthetic overprint. | CrossInk; Duet synthetic-style fallback |
| Guide Dots | Optional visual guide dots between words. | CrossInk |
| Forced indents | Optional first-line indents for EPUBs that otherwise render as an unbroken wall of text. | CrossInk |
| Render profiles | CrossInk Default plus Balanced, Light, and Safe Mode fallbacks for difficult or memory-heavy books. | CrossPoint/CrossInk foundation; Duet |
| Low-memory retries | Up to three guarded page-layout attempts with progressively safer profiles. | Duet |
| Chapter pre-indexing | Next-chapter indexing runs only after the visible page is idle and can cancel or defer for input, low heap, or allocation pressure. Timing records include completion, cancellation, deferral, failure, and duration. | Duet |
| Status bar | Toggle chapter pages, stable pages, book percentage, progress bar type/thickness, book/chapter title, time left, battery, and XTC position. | CrossPoint base; Duet additions |

## Fonts

| Feature | Details | Lineage |
| --- | --- | --- |
| Selectable built-ins | Lexend Deca and Bitter. | CrossInk, with Duet size reduction |
| Built-in fallback stack | ChareInk7 fills selected missing reader glyphs; Noto Emoji, Noto Sans Symbols, and Noto Sans CJK SC provide limited symbol/language fallback; Inter is the UI face with IBM Plex Sans Hebrew fallback. ChareInk7 is not a separate selectable built-in in the public build. | CrossInk |
| X3 built-in fallback sizes | 10, 12, 14, and 16 pt in `x3-public`. | CrossInk build variants; Duet public profile |
| X4 built-in fallback sizes | 16, 18, and 20 pt in `x4-public`. | CrossInk build variants; Duet public profile |
| SD-card font families | `.cpfont` families discovered from `/.fonts/` or `/fonts/`, grouped as Serif, Sans Serif, Mono/Typewriter, Accessibility, Handwritten/Script, and Blackletter/Decorative. | CrossPoint/CrossInk SD fonts; Duet catalog and grouping |
| SD-card sizes | A family exposes the sizes actually installed. The standard Duet generation set uses 10, 12, 14, 16, 18, and 20 pt on either X3 or X4, but the firmware does not invent missing files. | CrossInk format; Duet registry |
| Font styles | Real Regular, Italic, Bold, and Bold Italic where present; synthetic bold and/or italic when a family lacks a face. | CrossPoint/CrossInk format; Duet fallbacks |
| Font picker | Category browsing, compact Normal/Italic/Bold specimen rows, style-availability handling, and consistent preview sizing. | Duet |
| A/B comparison | Two installed families rendered side by side at matched sizes. | Duet |
| Font catalog cache | Cached discovery avoids rescanning every installed family on each boot; invalid or replaced font files trigger recovery/rescan. | Duet |
| Font acquisition | Device download, browser upload, or direct SD copy. The initial alpha distributes only the licensed built-in font notices; a reviewed public SD pack is a separate future asset. | CrossPoint/CrossInk; CPR-vCodex ideas; Duet release policy |

See [FONT_SOURCES.md](FONT_SOURCES.md) for the family-by-family source, license, Reserved Font Name, and redistribution audit.

## Library, Covers, And Discovery

| Feature | Details | Lineage |
| --- | --- | --- |
| Folder browser | One-line and two-line list views preserve fast folder navigation. | CrossPoint base |
| Cover browser | Cover Grid and Carousel are selectable at the book level while folders remain list-based. | Duet integration |
| Cover grids | 2x2, 3x3, and 4x4 book grids with separate title and author lines, page count, and book count. | Adapted from CrumBLE geometry; Duet rendering/input pipeline |
| Five-cover carousel | Centered selected cover, two covers on each side, title and author, count, and adjacent lookahead. | Adapted from CrossInk Carousel; informed by CrossPoint Flow; Duet cache/input work |
| Sorting | File-browser book views can sort by author or title. | Duet |
| Input-first navigation | Selection placeholders and cursor state are separated from cover hydration so input can be accepted before every cover is ready. Large-library consistency remains an alpha test target. | Duet |
| Exact thumbnails | Layout-specific high-quality 1-bit thumbnails, sharded cache paths, a bounded shared base-cover cache, carousel lookahead, and grid neighboring-page prefetch. | Duet |
| Desktop cover prefill | `scripts/prefill_cover_thumbnails.py` builds the exact X3/X4 thumbnail sizes on a computer; firmware still generates thumbnails for newly added books. | Duet |
| Recent Books | List or 3x3 cover grid, no EPUB parsing during ordinary navigation, and remove-from-Recents without deleting book state. | CrossInk; Duet performance work |
| Reading filters | All, In Progress, Unread, and Finished filters plus status badges. | Duet |
| Favorites | Add/remove, reorder, open, More Info, and path repair when a book moves to `/Read`. | Informed by CPR-vCodex; Duet implementation |
| Smart search | Ranked autocomplete and results across any title word, author, or series from the optional library catalog. | Duet |
| Search result actions | Short press opens the selected book; long press opens More Info. | Duet |
| More Info | Cover, title, author, series/index, genre, spice, reading state/progress, catalog description, and Open. | Duet |
| Library catalog | Optional Calibre/tracker-derived `library_catalog.tsv` supplies search, summaries, genre, spice, series, and library analytics without opening EPUBs. | Duet |
| Library analytics | Library Overview, Reading Taste, and Series Progress use the catalog plus local/synced book stats. | CPR-vCodex analytics philosophy; Duet implementation |
| Clean Library Cache | Bounded scan compares live books with cache directories, refuses incomplete/empty scans, and moves confirmed orphans to recoverable `/.duet/books/.attic` instead of deleting them. | Duet |

## Home Themes And Launcher

The eight selectable Home themes are:

1. Classic
2. Minimal
3. Dashboard
4. Dashboard Extended
5. Lyra
6. Lyra Extended
7. Lyra Carousel
8. RoundedRaff

| Feature | Details | Lineage |
| --- | --- | --- |
| Classic, Lyra, Lyra Extended, Lyra Carousel, RoundedRaff | Existing Home-theme family inherited through CrossInk/CrossPoint. | CrossPoint base |
| Minimal | Cover-forward minimalist Home theme. | CrossInk |
| Dashboard | Current-book cover and dense configurable reading statistics. | Duet |
| Dashboard Extended | Current-book card, chapter context, recent-cover row, stat strip, and bottom navigation. | Adapted/informed by CrossPet; Duet stats and cache integration |
| Home Stats picker | Seven Dashboard rows, two footer slots, and four strip slots can each choose from 18 values: none, book time, time left, progress, daily average, pace, sessions, average session, days reading, estimated finish, streak, reader type, this-device/all-device time, this-device/all-device books, today, and total sessions. | Duet |
| Launcher customization | Place, hide, and reorder shortcuts on Home and Apps with required escape routes preserved. | Informed by CPR-vCodex; Duet implementation |
| Default Home shortcuts | Search, Recent Books, Reading Stats, Saved Items, Favorites, Sleep, Nearby Stats Sync, If Found, and Apps. | Duet |

The complete launcher catalog contains:

- Browse Files
- Search Library
- Recent Books
- Reading Stats
- Heatmap
- Reading Profile
- Saved Items
- Favorites
- Achievements
- Dictionary
- Tetris
- If Found
- Screen Clean
- Nearby Stats Sync
- File Transfer
- OPDS
- KOReader Sync setup
- Sleep
- Read Me
- Apps
- Customize Home & Apps
- Settings

`Apps` itself appears only on Home; Settings and Browse Files remain in Apps so the compact Home menu stays focused.

## Reading Statistics

### Tracking Rules

| Feature | Details | Lineage |
| --- | --- | --- |
| Reading time | Time accumulates while a supported book is actively open, including short spans with no page turn. | CrossInk stats foundation; Duet |
| Counted sessions | A session enters session count/history only after at least one page turn; its pre-turn reading time is still retained. | Duet |
| Deep-sleep commit | Active time is flushed before deep sleep and recommit is idempotent. | Adapted through CrumBLE from aalu's reading-stats fix; Duet integration |
| Per-book record | Time, counted sessions, pages, pace, start/finish dates, completion state, last read, and estimated time left. | CrossInk/CPR-vCodex lineage; Duet expansion |
| Daily journal | Exact day totals for time, sessions, pages, completions, and recent session records. | CPR-vCodex lineage; Duet expansion |
| Attribution ledger | Per-date book attribution for time/pages, with separate labels for legacy unattributed history. | Duet |
| Session log | Exact start time, duration, pages, and book for retained sessions. | Duet |
| Stats Date | CRC-protected editable date for clockless/unreliable-clock operation; NTP can update the device clock when connected. | CPR-vCodex Sync Day concept; Duet implementation |
| Ignore Stats | Books under `/ignore_stats/` retain local progress without contributing to aggregate statistics. | CPR-vCodex lineage; Duet |
| Manual correction | Per-date, per-book time adjustments with validation and rollback; start and finish dates require a deliberate hold before editing. | CPR-vCodex lineage; Duet expansion |

### The 33 Top-Level Pages

1. **Current Book** - active or latest book/session data, progress, time left, estimated words per minute, totals, and dates.
2. **Book Progress** - progress graph and estimated completion context.
3. **Book Patterns** - per-book sessions, pace, time, and reading pattern.
4. **Trends** - Today, last 7 days, last 30 days, and current-year summaries.
5. **Activity Chart** - recent activity against the daily goal.
6. **Daily Minutes** - scrollable 90-day daily history with day detail.
7. **Monthly Calendar** - calendar view with per-day book drill-down.
8. **Heatmap** - 12-month reading-intensity view.
9. **Reading Profile** - raw seven-day 12-metric grid: reading days, goal days, known time, average reading day, sessions, average session, best day, completions, pages, current streak, longest streak, and timed/read days.
10. **Goals** - daily goal and goal-streak detail.
11. **Recent Sessions** - scrollable exact session history.
12. **Weekday Pattern** - reading distribution by weekday.
13. **Pace Trend** - a relative 30-day screen-page pace trend and direction. Historical daily records do not contain the per-book word totals needed to reconstruct WPM.
14. **Time of Day** - morning/afternoon/evening/night distribution.
15. **Monthly Trend** - reading by month.
16. **Year Line** - cumulative current-year line and page-turn total.
17. **Device Split** - per-device comparison; hidden until synced-device data exists.
18. **Session Lengths** - short/medium/long session distribution.
19. **Streak Milestones** - current and achieved streak levels.
20. **Started / Finished** - monthly starts versus completions.
21. **Reading Dates** - scrollable per-book start and finish history.
22. **Reader DNA** - separate 30-day six-axis normalized radar for Habit, Volume, Focus, Pace, Streak, and Finisher, with raw measurements and an overall score.
23. **Reader DNA Details** - the measurements and scoring context behind the radar.
24. **Reading Signature** - a plain-language summary of the reader's recent habits.
25. **Signature Metrics** - the raw measurements behind the reading signature.
26. **Fastest Reads** - completed-book ranking by elapsed reading days.
27. **Wrapped** - 12-month time, days, completions, streak, weekday, and average-session summary.
28. **Started Books** - scrollable in-progress list with estimates.
29. **Library Overview** - catalog-backed counts and completion.
30. **Reading Taste** - catalog-backed genre/spice/author patterns.
31. **Series Progress** - scrollable series completion.
32. **This Device** - device-local aggregate statistics.
33. **All Devices** - merged aggregate statistics; hidden until synced data exists.

The Reading Profile and Reader DNA are intentionally different: Reading Profile shows raw recent values; Reader DNA normalizes six longer-window dimensions into scores.

The exact page render paths are simulator-covered. The source also includes day-detail and correction subviews opened from Daily Minutes and Monthly Calendar, plus a guarded Book Dates editor opened from Reading Dates.

## Achievements

Duet exposes **108 persistent milestones**:

- **62 thresholds adapted from CPR-vCodex**: 5 books-started, 6 sessions, 24 books-finished, 7 reading-time, 5 goal-days, 5 goal-streak, 4 bookmarks, and 6 longest-session milestones.
- **46 Duet milestones**: 8 reading-days, 6 reading-streak, 7 screen-pages, 4 series-started, 4 series-completed, 4 spice-level, 4 morning-reading, 4 night-reading, 4 weekend-reading, and 1 cross-device milestone.

Unlocks persist in `/.duet/state/achievements.bin`, recover from its `.bak`, and are retroactively adopted from existing reading history if no unlock file exists. Achievement state is restart-persistent, but the unlock ledger is not currently included in `.cstats` archives; `.cstats` restores reading statistics from which many milestones can be re-derived.

Popups can list every milestone unlocked in the batch and offer **See All**. Achievement refresh work is deferred from timing-sensitive navigation paths.

## Dictionary And Reference

| Feature | Details | Lineage |
| --- | --- | --- |
| StarDict packages | Matching `.ifo`, `.idx`, uncompressed `.dict`, and optional `.syn` under `/dictionaries/<Name>/`. Compressed `.dict.dz` must be extracted first. | Adapted from CPR-vCodex |
| Multiple dictionaries | Prepare, activate, and switch between installed dictionaries. | CPR-vCodex; Duet |
| In-reader lookup | Select a word, view suggestions/definition, and return to the page. | Adapted from CPR-vCodex |
| Lookup history | Per-book looked-up words. | Adapted from CPR-vCodex |
| Hardening | Corrupt-cache recovery, yielding index builds, visible progress, low-memory preflight, and SD-font glyph preservation. | Informed by CrumBLE's SEEK-derived recovery patterns; Duet |

## Sync

| Feature | Details | Lineage |
| --- | --- | --- |
| KOReader Sync | Account setup plus in-book remote/local position comparison, Apply Remote, and Upload Local. | CrossPoint base; Duet UI integration |
| Nearby Position Sync | Two Duet devices compare the current EPUB position over ESP-NOW and move only after explicit Apply. | CrossInk; Duet shared-build integration and reliability hardening |
| Nearby Reading Stats Sync | Direct device-to-device exchange of global totals, per-book summaries/details, journal, ledger, Stats Date, device name, and retained peer snapshots. | CrossInk two-device stats-sync concept; Duet protocol and merge layer |
| Person-level aggregate | This Device and All Devices totals stay separate; calendars, streaks, profiles, and library analytics can use merged data. | Duet |
| Idempotent merge design | Repeated exchanges are designed to converge without double-counting. Repeated real-device convergence remains an alpha acceptance target. | Duet |
| Device names | X3/X4 records can be distinguished in Device Split and synced-state files. | Duet |

KOReader Sync handles a book's reading position. Nearby Stats Sync handles Duet's statistics; these are separate operations.

## Transfer And Connectivity

| Feature | Details | Lineage |
| --- | --- | --- |
| Browser file transfer | Join Wi-Fi or create a hotspot; upload, download, rename, move, and delete through the web UI. | CrossPoint base |
| WebDAV | Mount/manage the SD card through a WebDAV client. | CrossPoint base |
| Calibre Wireless | Send books with the CrossPoint Calibre device plugin. | CrossPoint base |
| OPDS | Save multiple catalogs, browse, authenticate with Basic auth, and download books. | CrossPoint base |
| Web EPUB optimizer | Optimize image-heavy EPUBs and preserve CrossInk location metadata where possible. | CrossInk |
| Font downloads/uploads | Download catalog fonts or upload `.cpfont` files from the web UI. | CrossPoint base; CPR-vCodex/CrossInk lineage |
| Firmware updates | First install/recovery by custom USB BIN; Duet-to-Duet update from one SD-root BIN; Wi-Fi update plumbing remains inherited. | CrossPoint base; Duet release safety |
| USB serial file transfer | Internal transfer service for supported tooling. | CrossPoint base |

## Sleep, Power, And Display Care

The sleep-mode enum contains 12 modes:

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

Minimal Stats is hidden on X4 in the current settings UI and remains available on X3.

| Feature | Details | Lineage |
| --- | --- | --- |
| Custom sleep images | Random BMP selection from `/.sleep` or `/sleep`, with root `sleep.bmp` fallback and cached inventory. | CrossPoint/CrossInk; Duet large-folder cache |
| Cover presentation | Fit or Crop plus None/Contrast/Inverted filter. | CrossPoint base; CrossInk |
| Page Overlay | Sleep presentation over the stored monochrome reader page. BMP overlays treat white as transparent; PNG overlays preserve the page wherever alpha is below the compositor threshold. PNG files are accepted only in Page Overlay mode. | CrossPoint/CrossInk foundation; Duet PNG compositor |
| Statistics sleep screens | Reading Stats, Minimal Stats where supported, and Dashboard. | CrossInk; Duet Dashboard |
| Quick Resume | Preserve visible content for faster return, including timeout option. | CrossPoint base |
| Locked-image cycling | A fresh Custom/Page Overlay image can be selected without fully waking. CrumBLE's original behavior used one brief tap; Duet adds Off/1/2/3 choices and defaults to 3 to reduce accidental changes. The code intends to count the wake press as the first click, but current physical testing has found that the reliable default gesture can feel like four physical presses: one initial wake press followed by three deliberate taps. | CrumBLE one-tap behavior; Duet multi-click setting, Lauren-selected three-click default, and wake-path gesture repair |
| Awake Power actions | Configurable single, double, triple, and long-press actions, plus long-press Menu and Back actions. | CrossInk controls; adapted from CrossPet multi-click detection; Duet action system |
| Ghosting care | Full-refresh options, Screen Clean, fitted/cropped images, and device-specific sleep/wake handling. | CrossPoint base; CPR-vCodex Screen Clean idea; Duet X3/X4 work |
| Session flush | Active time is committed before deep sleep. | CrumBLE/aalu lineage; Duet |

## Controls

- Side buttons: Disabled, Previous/Next, Next/Previous, or Next/Next.
- Side-button long press: Off, chapter skip, font-size change, or orientation.
- Front-button orientation awareness: Off, navigation buttons, or all buttons.
- Front-button long press: Off, chapter skip, font-size change, or orientation.
- Power single/double/triple/long press and long Menu/Back actions can map to: Sleep, page turn, bookmark, stats, mark finished, refresh, change font, Guide Dots, Bionic Reading, cycle page-turn mode, Sync Progress, File Transfer, Calibre Wireless, join network, hotspot, screenshot, Dark Mode, footnotes, Browse Files, or save clipping. Tilt is added when the X3 sensor is available.

This control system combines inherited CrossPoint/CrossInk remapping with Duet's multi-click handling and additional reader actions.

## Reliability, Recovery, And Developer Tools

| Feature | Details | Lineage |
| --- | --- | --- |
| Namespaced state | Canonical writes live under `/.duet`: hot state in `/state`, durable per-book records in `/books`, thumbnails and layouts in `/cache`, and statistics archives in `/backups/reading-stats`. Inherited `/.crossink`, `/.crosspoint`, and `/.crossink-stats-backup` files remain non-destructive migration and recovery sources. | Duet |
| Debounced persistence | Settings, progress, and other hot state avoid unnecessary synchronous SD writes. | CrossPoint base; Duet tuning |
| Complete stats archives | `.cstats` includes the recognized global, journal, ledger, session, clock, library, synced-device, and per-book stats records across both roots. | Duet |
| Archive validation | Header/path/size/entry-count checks, per-entry CRC, legacy-root mapping, staged replacement, automatic pre-restore safety export, and rollback on failure. | Duet |
| Archive verification | Non-empty content-level export/restore round trips pass both device simulators; physical X3/X4 verification is tracked for alpha.6. | Duet; simulator-verified |
| Automatic global-stats backups | Optional rolling backups under `/.duet/backups/reading-stats`, with legacy backup import support. | CPR-vCodex/CrossInk lineage; Duet |
| Cache-safe book moves | Preserve durable progress/stats while disposable layout data can rebuild. | CrossPoint base; Duet |
| Recovery-first cache cleanup | Clean Library Cache moves confirmed orphans to `.attic`; troubleshooting guidance avoids deleting both hidden roots wholesale. | Duet |
| Crash reports | SD-card crash report with faulting program counter and build identity. | CrossPoint base; Duet expansion |
| Breadcrumb telemetry | Boot phases, Home, picker, sync preparation, reader transitions, and chapter pre-index timing files. | Duet |
| Simulator | X3/X4 screen-size smoke paths, seeded stats/media fixtures, archive round trips, and whole-UI navigation checks. | CrossPoint/CrossInk simulator; Duet expansion |
| Release tooling | Neutral public build profiles, deterministic names, checksums, privacy audit, draft packaging, catalog generation, and release gates. | Duet |

Performance claims are hardware- and card-dependent. On the maintainer's large development cards, telemetry identified multi-second FAT directory scans in crowded per-book cache directories; moving hot files into `/.duet/state`, sharding thumbnails and layouts under `/.duet/cache`, caching font discovery, and desktop-prefilling covers materially reduced those paths. Those measurements are evidence for the architecture, not a universal boot-time guarantee.

## Deliberate Exclusions

Duet does not include:

- flashcards;
- a virtual pet;
- weather, Pomodoro, chess, Sudoku, Minesweeper, or 2048;
- CrossPet's Bluetooth keyboard/page-turner build;
- a dedicated sleep-image manager with preview/reordering;
- PDF rendering;
- a general web browser, RSS/news reader, audio player, or audiobook player;
- typed notes or a general-purpose productivity suite.

## Source And License Detail

- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) records direct adaptation, design lineage, exact revisions, and transitive sources.
- [FONT_SOURCES.md](FONT_SOURCES.md) records font source and redistribution status.
- [licenses/fonts/BUILTIN_FONT_SOURCES.md](licenses/fonts/BUILTIN_FONT_SOURCES.md) maps each generated built-in source family to its pinned source and bundled notice.
- [CHANGELOG.md](CHANGELOG.md) records the development history.
