# Duet Alpha Screenshot Gallery

Duet uses the same interface and feature code on X3 and X4, so this gallery shows one representative X4 capture for each shared screen instead of publishing duplicate device sets. The gallery uses recognizable books from Lauren's library so the interface looks like a real, varied reader. Every reading statistic, progress value, date, achievement state, device name, and sync value is deterministic fabricated test data. No EPUB, extracted cover asset, personal catalog, credential, contact detail, or real device-state file is included.

## Feature Overview

[![Duet feature overview](x4/feature-overview.png)](x4/feature-overview.png)

## Home And Navigation

| Dashboard | Reading Home | Home Menu |
| --- | --- | --- |
| [![Duet Dashboard](x4/dashboard.png)](x4/dashboard.png) | [![Duet Reading Home](x4/reading-home.png)](x4/reading-home.png) | [![Duet Home menu](x4/home-menu.png)](x4/home-menu.png) |

## Cover Libraries

| 2x2 Grid | 3x3 Grid | 4x4 Grid |
| --- | --- | --- |
| [![Duet 2x2 cover grid](x4/grid-2x2.png)](x4/grid-2x2.png) | [![Duet 3x3 cover grid](x4/grid-3x3.png)](x4/grid-3x3.png) | [![Duet 4x4 cover grid](x4/grid-4x4.png)](x4/grid-4x4.png) |

| Carousel | Scrolling Title |
| --- | --- |
| [![Duet carousel](x4/carousel.png)](x4/carousel.png) | [![Duet carousel title scroll](x4/carousel-title-scroll.png)](x4/carousel-title-scroll.png) |

## Search And Book Information

| Autocomplete | Filled Suggestion | Results | More Info For The Selected Result |
| --- | --- | --- | --- |
| [![Duet smart search autocomplete](x4/search-autocomplete.png)](x4/search-autocomplete.png) | [![Duet filled search suggestion](x4/search-autocomplete-filled.png)](x4/search-autocomplete-filled.png) | [![Duet search results](x4/search-results.png)](x4/search-results.png) | [![Duet More Info for the selected result](x4/more-info.png)](x4/more-info.png) |

## Reading Tools And Apps

| Reader Menu | Book Info |
| --- | --- |
| [![Duet reader quick menu](x4/reader-quick-menu.png)](x4/reader-quick-menu.png) | [![Duet Book Info from the reader](x4/reader-book-info.png)](x4/reader-book-info.png) |

| Dictionary List | Definition |
| --- | --- |
| [![Duet dictionary list](x4/dictionary-list.png)](x4/dictionary-list.png) | [![Duet dictionary definition](x4/dictionary.png)](x4/dictionary.png) |

| Apps | Favorites |
| --- | --- |
| [![Duet Apps](x4/apps.png)](x4/apps.png) | [![Duet Favorites](x4/favorites.png)](x4/favorites.png) |

| Achievements | Completed Achievements | Tetris |
| --- | --- | --- |
| [![Duet achievements in progress](x4/achievements.png)](x4/achievements.png) | [![Duet completed achievements](x4/achievements-completed.png)](x4/achievements-completed.png) | [![Duet Tetris](x4/tetris.png)](x4/tetris.png) |

## Font Variety

Each picker capture compares a different current family with a different preview family, so the four examples showcase eight fonts rather than repeating the preview as the current selection.

| NV Newsreader / Bookerly | NV Garamond / NV Bitter | Atkinson Hyperlegible Next / OpenDyslexic | Cormorant Garamond / Great Vibes |
| --- | --- | --- | --- |
| [![NV Newsreader current and Bookerly preview](fonts/bookerly.png)](fonts/bookerly.png) | [![NV Garamond current and NV Bitter preview](fonts/nv-bitter.png)](fonts/nv-bitter.png) | [![Atkinson Hyperlegible Next current and OpenDyslexic preview](fonts/opendyslexic.png)](fonts/opendyslexic.png) | [![Cormorant Garamond current and Great Vibes preview](fonts/great-vibes.png)](fonts/great-vibes.png) |

## Complete Shared Reading Stats Gallery

The [complete Reading Stats gallery](stats/README.md) contains all 33 top-level pages and 10 alternate or detail states once. The same pages and controls are available on X3 and X4.

| Reading Heatmap | Reader DNA | All-Device Stats |
| --- | --- | --- |
| [![Reading heatmap](stats/x4/smoke-stats-heatmap.png)](stats/x4/smoke-stats-heatmap.png) | [![Reader DNA](stats/x4/smoke-stats-reader-dna.png)](stats/x4/smoke-stats-reader-dna.png) | [![All-device statistics](stats/x4/smoke-stats-all-devices.png)](stats/x4/smoke-stats-all-devices.png) |

## Evidence Boundary

Simulator screenshots prove rendering and deterministic UI coverage. They do not prove physical-device timing, SD-card behavior, panel refresh quality, sleep/wake behavior, radio sync, or long-session stability. Those remain part of the [physical test matrix](../../PHYSICAL_TEST_MATRIX.md) and alpha tester program.

## Still To Capture On Physical Devices

The gallery is intentionally honest about what the simulator cannot prove. The remaining public media set needs:

- Nearby Position Sync and Nearby Reading Stats Sync on two readers.
- KOReader Sync without account details.
- Clean X3 and X4 boot and sleep frames.
- The System page showing the exact public Duet version.
- The file-transfer web portal after an alpha.7 comparison and privacy pass.
- An X3 and X4 together photo.
- Short unsped clips of grid navigation, carousel hydration, book open/close, chapter pre-indexing, and two-device sync.
