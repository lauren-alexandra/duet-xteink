# Third-Party Notices

This development branch is based on [CrossInk](https://github.com/uxjulia/CrossInk), which is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader). Duet's audited CrossInk 1.4.0 baseline is revision `b7f6708f96d05e5851f8bcfaaf57bc0e91dc0567`.

The source projects below remain credited for code, architecture, algorithms, or UI designs adapted into this branch. Descriptions distinguish direct code adaptation from broader design lineage.

## CrossPoint Reader

Source: [crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)

The recorded CrossPoint parent of Duet's CrossInk baseline is revision `6e8dbd7f239eb562daa5de81362c0025ce833dd8`. GitHub preserves that exact CrossPoint-authored parent in the CrossInk fork's merge graph even though the same commit URL is no longer exposed from the current CrossPoint repository.

CrossPoint Reader supplies the firmware foundation: device and display abstractions, EPUB/XTC/XTCH/TXT readers, file browser, persistent reading position, chapters, footnotes, bookmarks/clippings foundations, settings, storage, Wi-Fi/web transfer, WebDAV, Calibre Wireless, OPDS, KOReader Sync, sleep/wake, localization, the simulator, and build system. Duet retains CrossPoint's copyright and MIT license while extending those systems.

## CrossInk

Source: [uxjulia/CrossInk](https://github.com/uxjulia/CrossInk)

Duet's audited CrossInk 1.4.0 baseline is `b7f6708f96d05e5851f8bcfaaf57bc0e91dc0567`. Its firmware source release is `0b80e26212925a2fdf6ab92bad68957fdfaf958f`, which descends from the recorded CrossPoint synchronization merge at `9c7315f495186185ff34ec5ddb485ebf18d3fc17`.

CrossInk is Duet's direct firmware base. In addition to the inherited CrossPoint foundation, its recorded feature set includes the Lexend Deca/Bitter/ChareInk font work and fallback glyphs, adjusted reader sizes, Minimal Home, additional EPUB styling and table handling, Bionic Reading, Guide Dots, forced indents, expanded button controls, favorite/custom sleep images, finished/read-folder behavior, in-book options, foundational reading statistics and two-device statistics sync, automatic page turn, Recent Books cover view, Nearby Position Sync, and the web EPUB optimizer. Nearby Position Sync was introduced by Julia Nguyen in CrossInk commit `3837ea4fe86caaa9370ea785e2086c0c3f2b9dbf` and shipped in CrossInk 1.4.0. Duet extends or replaces portions of those paths as recorded in [FEATURES.md](FEATURES.md).

## CrossInk Carousel

Source: [chintanvajariya/CrossInk-Carousel](https://github.com/chintanvajariya/CrossInk-Carousel)

Adapted or informed work:

- The home-carousel engine reused and substantially extended for Duet's five-cover library carousel.
- Cover-first navigation, centered selection, and adjacent-cover presentation in `src/activities/home/RecentBooksActivity.*`.

Audited revision: `bf2076d7acc5a5993d11a2d3246c943c26c44a1d`

## CrossPoint Flow

Source: [ideo2004-afk/crosspoint-reader-lua](https://github.com/ideo2004-afk/crosspoint-reader-lua)

CrossPoint Flow informed the cover-forward browsing direction and interaction model. Duet does not claim its Lua implementation as original Duet code. The audited reference revision is `6bc3bcd92f9c8db991ac59a262df3ac00948bae1`.

## CrumBLE

Source: [imshentastic/CrumBLE](https://github.com/imshentastic/CrumBLE)

Audited revision: `a4c5507d14041a92389903675fb5edf94e39c8a8`

Adapted or informed work:

- The selectable 2x2, 3x3, and 4x4 bookshelf geometry in `src/activities/home/FileBrowserActivity.cpp`.
- Selected low-memory and corrupt-cache hardening ideas for dictionary lookup.
- CrumBLE's original opt-in **Tap Power While Asleep to Cycle** behavior, which used one brief Power tap to choose a fresh sleep image and return to deep sleep, plus large-folder caching concepts. Duet adapts that interaction into configurable Off/1/2/3 click choices and defaults to three at Lauren's direction to reduce accidental cycling; Duet's multi-click wake-path implementation is not attributed to CrumBLE.
- Deep-sleep reading-time commit behavior and idempotent session preservation.

CrumBLE credits its dictionary base to [SEEK Reader](https://github.com/seek-reader/seek) and its reading-time fix to [aalu's CrossPoint fork](https://github.com/aaludon/crosspoint-reader-aalu). Those transitive origins are acknowledged here; this branch retains its more capable CPR-vCodex-derived dictionary UI and selectively ports hardening rather than replacing it with CrumBLE's full implementation.

## CPR-vCodex

Source: [franssjz/cpr-vcodex](https://github.com/franssjz/cpr-vcodex)

Audited revision: `0111c4811bbc4ec95cbbe7577212ec4eea5b8cd5`

Adapted or informed work:

- The StarDict index, lookup, word-selection, history, suggestion, and definition paths in `src/DictionaryStore.*` and `src/activities/reader/Dictionary*Activity.*`.
- The first 62 persistent achievement milestones. This branch adds 46 original milestones for 108 total.
- Reading-stat, Apps, Favorites, If Found, and Screen Clean ideas where the local implementation builds on or substantially redesigns the upstream flow.

## Biscuit

Source: [yattsu/biscuit](https://github.com/yattsu/biscuit)

Audited revision: `483ac2951bc98b71bafacb18adbcac99da04bbe0`

`src/activities/apps/TetrisActivity.*` is adapted from Biscuit's Tetris implementation with CrossInk navigation, layout, and e-ink refresh changes.

## CrossPet

Source: [trilwu/crosspet](https://github.com/trilwu/crosspet)

Audited revision: `be9b86d1d2a3196144b01f27b78417cf62a122c8`

Adapted or informed work:

- The Dashboard Extended card layout, recent-cover row, stat strip, bottom navigation flow, and Tools icon in `src/components/themes/reading_home/*`, `src/activities/home/HomeActivity.*`, and `src/components/icons/tools.h`.
- The awake multi-click Power-button detector, substantially rewritten for the local action system and X3/X4 behavior.

This branch uses its own bounded generated-cover cache instead of CrossPet's full-frame cache to remain within the X3 memory envelope. CrossPet's virtual pet and additional games and productivity apps are not included.

## Font Projects

Fonts are licensed independently of the firmware. Source repositories, revisions, and release packaging requirements are in [FONT_SOURCES.md](FONT_SOURCES.md). A public release must also include the exact license and copyright notice for every redistributed font file.

## WordNet 3.0

Source: [WordNet](https://wordnet.princeton.edu/)

The optional `Duet-WordNet-3.0-StarDict.zip` release asset is an uncompressed StarDict 2.4.2 conversion of WordNet 3.0 for Duet's offline dictionary reader. It is not embedded in either firmware BIN. The archive carries the original WordNet license beside the dictionary files, and the source record and reviewed file hashes are preserved under `licenses/dictionaries/WordNet-3.0/`.

## Shared MIT License

CrossPoint Reader, CrossInk, CrossInk Carousel, CrossPoint Flow, CrumBLE, CPR-vCodex, Biscuit, and CrossPet use the following MIT license text and preserve Dave Allie's copyright notice:

MIT License

Copyright (c) 2025 Dave Allie

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
