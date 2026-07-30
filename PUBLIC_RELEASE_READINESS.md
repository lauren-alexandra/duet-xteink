# Public Release Readiness

## Current Status

Duet v0.1.0-alpha.7 is published as a GitHub prerelease for the Xteink X3 and X4. It is an intentionally early alpha: source review, automated tests, simulator coverage, reproducible packaging, checksums, recovery instructions, and maintainer-device work provide a responsible starting point, while broader physical testing across readers, SD cards, libraries, sleep/wake patterns, and long sessions remains open.

The canonical download is the [Alpha.7 release](https://github.com/lauren-alexandra/duet-xteink/releases/tag/v0.1.0-alpha.7). Testers should never use firmware files sent as unverified attachments.

## Verified Published Assets

The published standalone BINs, firmware ZIP, and `SHA256SUMS.txt` were downloaded from GitHub and compared on July 29, 2026. The standalone and packaged firmware bytes match:

| Asset | Size | SHA-256 |
| --- | ---: | --- |
| `Duet-X3-v0.1.0-alpha.7.bin` | 5,941,904 bytes | `ad0bccbde59b2d64119279623a785c97124dbf6a72c7cfecce58f748128f1961` |
| `Duet-X4-v0.1.0-alpha.7.bin` | 5,804,864 bytes | `8a38294c547a1f947c7f810acf083b6b7e077e5f8a46e946e867b3e13a8172f2` |
| `Duet-v0.1.0-alpha.7-firmware.zip` | 15,783,047 bytes | `41263c6421aee3aa20598c7ed0dba13640bb33f0fb39e224e471428ca71aac2d` |
| `Duet-Open-Font-Pack-v1.zip` | 366,108,920 bytes | `1d0b100683e05a3c2f9ad6e1fbf6f24c2c3674c3ee096ddfa32c383ccbf85c7c` |
| `Duet-WordNet-3.0-StarDict.zip` | 8,857,151 bytes | `19f6840ee91881cd303bcedc29c81777da1756ad73a09b114d3226fcf01ed80a` |
| `SHA256SUMS.txt` | 204 bytes | `81809e82d8d4093c50563f46095bcc6ab0df1d372296554b139af820d355a447` |

The firmware checksum file covers the X3 and X4 BINs. The release page records the separate font and dictionary asset digests.

## Published Extras

- The optional Duet Open Font Pack contains 123 reviewed open-source families and 738 validated `.cpfont` files: 10, 12, 14, 16, 18, and 20 pt for both X3 and X4. It includes source links, pinned revisions, license notices, and a per-file checksum manifest.
- The optional WordNet 3.0 StarDict ZIP is ready to copy under `/dictionaries/` and retains its original notice.
- The public gallery contains 69 reviewed images: 43 statistics screens, 22 feature/app screens, and four font comparisons. Shared X3/X4 interfaces are shown once rather than duplicated.

## Current Unreleased Source

The current unreleased source contains changes that must not be represented as part of the published Alpha.7 BINs until a new matched X3/X4 release is built:

- Nearby Reading Stats Sync protocol v6, including milestone-wise achievement-ledger exchange and `.cstats` preservation.
- `scripts/enrich_epub_locations.py` for true WPM/reference-page metadata, including dry-run, skip-existing, forced refresh, URL-escaped OPF path support, and collision-safe separate `.duetbak` backup folders.
- Safe current-book WPM seeding for Pace Trend, Reader DNA Details, Reading Signature, and Signature Details without reopening catalogs, progress files, or EPUBs during statistics rendering.
- Input-first Home updates that keep persisted progress visible immediately and defer current-book word-count and chapter-title work until a short idle window.
- Optional spice/heat metadata: absent values are omitted from More Info, Reading Taste collapses to genre and author, and spice achievements remain inactive.
- Public font-pack generation and license packaging.
- Expanded CI compilation and catalog tests for the new desktop helpers.

Both readers must run the same Nearby Sync protocol. Do not test protocol v6 against an Alpha.7 protocol-v5 reader.

## Automated Gates

The current release-prep source passes:

- 117 host C++ tests.
- Four Python desktop-helper tests covering no-spice and mixed-spice catalogs, URL-escaped EPUB spine paths, collision-safe separate backups, and skip-existing behavior.
- X3 and X4 simulator builds.
- Python compilation for public release, catalog, WPM-enrichment, font-pack, and dictionary scripts.
- The public-release privacy and required-file audit.
- `git diff --check`.

GitHub CI must rerun these gates after the final commit. Local success does not replace CI, and neither local nor CI success proves physical e-ink behavior.

## Physical Acceptance Still Open

The purpose of the public alpha is to broaden the device evidence. High-value physical testing includes:

- First installation, Duet-to-Duet update, rollback, and reported version.
- Cold boot, warm boot, Home, book open, page turn, Home return, sleep, wake, and repeated resume.
- X3 and X4 settled-screen ghosting and visible refresh count.
- List, 2x2, 3x3, 4x4, and carousel navigation before and after cover hydration.
- Forward and backward grid-page prefetch and carousel lookahead.
- Search, matching More Info, dictionary lookup, bookmarks, clippings, fonts, and reader options.
- Complex chapters, guarded pre-indexing, cancellation, and low-memory fallback.
- Nearby Position Sync and Nearby Reading Stats Sync in both directions.
- Complete statistics navigation, achievement notifications, `.cstats` export/restore, and active-book consistency.
- Large libraries, folders above 1,000 paths, slow or nearly full cards, and multiple filesystem/card brands.
- Locked sleep-image cycling at Off, 1, 2, and 3 clicks.

Use [Alpha Testing](docs/ALPHA_TESTING.md), [Alpha.7 Acceptance Quickstart](docs/ALPHA7_ACCEPTANCE_QUICKSTART.md), and the [Physical Test Matrix](docs/PHYSICAL_TEST_MATRIX.md). A successful build, simulator capture, SD-card copy, or single maintainer-device session must not be recorded as universal physical acceptance.

## Release Package Contract

Each firmware ZIP must contain:

- Device-explicit X3 and X4 BINs from the same reviewed commit.
- `SHA256SUMS.txt`.
- `README-FIRST.txt`.
- README, feature catalog, user guide, changelog, authorship, identity, license, notices, and font-source ledger.
- Installation, recovery, alpha-testing, physical-test, dictionary, font, cover-prefill, EPUB WPM-prep, and combined computer-assistant library-prep documentation.
- `prefill_cover_thumbnails.py`, `enrich_epub_locations.py`, `generate_library_catalog.py`, and the other reviewed desktop helpers.
- Reviewed public media and built-in font notices.

Optional font and dictionary ZIPs remain separate SD-card assets, not firmware files.

## Privacy And Redistribution Boundaries

Never publish:

- `/if_found.txt`, real contact details, Wi-Fi credentials, or KOReader credentials.
- EPUBs, extracted cover source files, personal sleep-screen source files, reading guides, trackers, Calibre databases, or private catalog exports.
- Real reading history, progress, bookmarks, favorites, achievements, caches, backups, crash logs, or copied `/.duet`, `/.crossink`, or `/.crosspoint` state.
- Fonts, dictionaries, or other assets without reviewed redistribution rights and notices.

Approved product screenshots may show Lauren's book covers as part of the Duet interface. Statistics screenshots must use fabricated data. Every shared log, terminal excerpt, image, and manifest still requires a privacy review.

## Publication Gate

A new Duet alpha is ready to publish only when:

1. The active firmware source is finalized and merged.
2. X3 and X4 builds come from the same commit and have distinct versioned filenames.
3. Host tests, helper tests, simulators, static analysis, formatting, public audit, and both public builds pass in CI.
4. Release notes distinguish shipped behavior from source-only or planned behavior.
5. Firmware, font, dictionary, and package hashes are verified after download.
6. Installation, rollback, known issues, physical-test limits, privacy guidance, lineage, and licenses are visible beside the downloads.
7. The exact candidate receives at least the maintainer's X3/X4 smoke route before the tester announcement.

A stable release requires broader completion of the physical matrix. An alpha can publish with open physical rows only when those limits are stated plainly and a known-good rollback path remains available.
