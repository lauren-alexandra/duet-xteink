# Public Release Readiness

## Current Status

Duet v0.1.0-alpha.6 is in final prerelease preparation. The latest shared X3/X4 device code has been reconciled into the public branch, the canonical `/.duet` migration is implemented, public identity and lineage are documented, privacy-safe simulator media exists, and deterministic build, audit, checksum, and packaging tools are present. The remaining publication work is mechanical and verifiable: run the final tests and public X3/X4 builds from one commit, package and hash the exact artifacts, create the canonical public repository, and publish the GitHub prerelease. The physical matrix intentionally remains open after publication because the purpose of this early alpha is to widen hardware, SD-card, and library testing; simulator or maintainer-device success is not presented as universal acceptance.

## Completed Audit Work

- Created a rollback archive of the current dirty working tree before release documentation changes.
- Audited source files for personal names, phone numbers, email addresses, reading history, books, tracker data, and stored credentials. No personal values were found in the source tree.
- Mapped major imported and adapted firmware features to upstream projects and pinned revisions.
- Reconciled the public README, user guide, feature catalog, controls, storage, font, statistics, sync, sleep, and release documents against the current source. The canonical catalog now distinguishes implemented behavior, device-specific behavior, optional SD assets, deliberate exclusions, and physical acceptance still pending.
- Corrected the documented alpha.4 storage model to match the reviewed builds: frequently opened state lives under `/.crossink`, while path-keyed per-book progress, statistics, covers, and render caches remain under `/.crosspoint`.
- Implemented the [Duet Storage Namespace Migration](docs/DUET_STORAGE_NAMESPACE_MIGRATION.md): `/.duet` is the canonical namespace, while inherited `/.crossink`, `/.crosspoint`, and `/.crossink-stats-backup` directories remain non-destructive import and recovery sources.
- Verified the current branch supports both X3 and X4 build targets.
- Separated the public font plan from the private 130-family personal pack.
- Added a feature catalog, third-party notices, and font source ledger.
- Selected **Duet** as the public project name and `lauren-alexandra/duet-xteink` as the canonical repository identity.
- Reset public versioning to semantic alpha releases; the clean current candidate is `v0.1.0-alpha.6`, while internal Repair, WPM, and indexing labels remain development provenance rather than public release versions.
- Adopted device-explicit firmware artifact names: `Duet-X3-v<version>.bin` and `Duet-X4-v<version>.bin`.
- Added an incremental desktop cover-prefill workflow and guarded computer-assistant prompt for large or multiply organized libraries.
- Rebranded the boot/version display, Settings footer, default device names, simulator, crash text, web portal, translations, updater, release workflows, release catalog, and public documentation as Duet.
- Preserved `/.crossink`, `/.crosspoint`, `/.crossink-stats-backup`, legacy setting names, render-mode IDs, and internal compatibility symbols as migration and recovery inputs so existing device state survives the public rename and canonical `/.duet` move.
- Added alpha release notes, a Reddit soft-launch draft, exact installation and recovery instructions, a screenshot plan, a complete public feature tour, and `release/README-FIRST.txt`.
- Added backup-first SD-card verification guidance so filesystem damage is not mistaken for a firmware defect or "fixed" by deleting recoverable state.
- Added an automated public-release audit for version drift, required attribution files, local computer paths, phone-number-shaped text, ebooks outside approved test fixtures, and accidentally tracked device-state files.
- Added automation for a self-contained firmware archive with both device builds, checksums, tester instructions, installation/recovery help, the complete feature catalog and user guide, cover-prefill guidance, authorship, notices, and licenses.
- Created a reviewed simulator-media candidate set that permits real titles and covers only inside interface screenshots while fabricating all reading data.
- Added simulator-only real-cover fixtures for Dashboard and More Info plus real-title search autocomplete without importing either device's catalog or state files.
- Refreshed and integrity-tested the alpha.3 draft ZIP before the archive repair; alpha.4 now has clean replacement device builds.
- Audited all 39 untracked copy-suffix files. Every copy is byte-for-byte identical to the corresponding file in `HEAD`; none contains unique work. The evidence and recommended cleanup are recorded in `REPOSITORY_HYGIENE_REVIEW.md`.
- Removed all 39 audited copy-suffix files after explicit approval. This also removed the duplicate `Ssd1677Driver 2.cpp` that was confirmed to break both public builds at link time.
- Audited the 78-commit local history and documented a privacy-safe initial publication strategy in `INITIAL_PUBLICATION_PLAN.md`.
- Bundled the license and source ledger for all eight font families represented in the generated firmware headers.
- Redirected OTA, issue, documentation, and release automation to the canonical Duet repository. Removed the inherited upstream Pages `CNAME`.
- Gated font publishing until Duet-owned hosting and a reviewed redistributable font pack are configured; alpha downloads continue using the credited upstream compatibility endpoint without publishing into it.
- Added separate GitHub forms for complete alpha test sessions and reproducible defects, plus a privacy-aware triage and label guide.
- Added a maintainer release runbook and per-candidate physical acceptance record with exact X3/X4 hashes, timings, and publication decisions.
- Hardened release automation with version/release-note validation, workflow concurrency, timeouts, RC checksums, and a post-publication catalog pull request that respects branch protection.
- Moved pull-request firmware builds onto GitHub-hosted runners and made CI compile both public X3/X4 targets plus the privacy/release audit, so outside contributors receive the same basic build gate without access to a private runner.
- Completed clean local `x3-public` and `x4-public` builds after the approved duplicate cleanup. The X3 image has 662,592 bytes and the X4 image has 799,664 bytes free in the OTA app partition.
- Packaged those exact binaries into a fresh draft firmware ZIP and verified both SHA-256 entries from the package manifest.
- Added the full rich dummy-stats fixture specification and every-page capture inventory so the gallery can be regenerated after the stats UI stabilizes.
- Repaired complete `.cstats` export/restore across the split storage model: durable root/session/sync data comes from `/.crossink`, while per-book `stats*.bin` files remain in `/.crosspoint`. Legacy archive root paths are restored into current locations.
- Passed non-empty content-level `.cstats` round trips in both X3 and X4 simulators, including global totals, Stats Date, session history, synced ledger/date/name data, per-book statistics, safety export, and removal of post-backup files.
- Completed clean alpha.4 `x3-public` and `x4-public` builds. The X3 image has 661,472 bytes and the X4 image has 798,560 bytes free in the OTA app partition.
- Packaged those exact alpha.4 binaries into `Duet-v0.1.0-alpha.4-DRAFT-firmware.zip`; the ZIP integrity test and both packaged SHA-256 checks passed.
- Added a short alpha.4 device-acceptance route, expanded the reusable physical record for complete-stats restore evidence, and added a privacy/version manifest for final screenshots and clips.
- Generated and reviewed 111 fresh alpha.6 simulator PNGs: 43 Reading Stats states per device, 21 curated feature and app captures, and four font previews. Every checked-in image decodes, every gallery link resolves, and the repository media set is 1.4 MB.
- Completed the final alpha.6 host, simulator, static-analysis, privacy, formatting, and public-build gates. All 117 host tests pass; both X3 and X4 simulator smokes pass; cppcheck reports zero high and zero medium findings; and the release audit passes.
- Built the final alpha.6 X3 image at 5,937,136 bytes with 616,464 bytes of OTA app-partition headroom and the X4 image at 5,800,048 bytes with 753,552 bytes of headroom.

## Remaining Publication Checks

- Package the exact final X3/X4 binaries and verify the ZIP plus `SHA256SUMS.txt`.
- Use the reviewed public-history strategy in `INITIAL_PUBLICATION_PLAN.md`; the private development history contains personal author metadata and remains preserved separately rather than becoming the public lineage.
- Create `lauren-alexandra/duet-xteink`, publish the source, tag `v0.1.0-alpha.6`, and attach the verified firmware package as a GitHub prerelease.
- Capture the remaining real-device System/version, sleep, sync, and paired-reader media during physical alpha acceptance. The checked-in simulator gallery is current alpha.6 media.
- Continue the open X3/X4 physical matrix through tester reports after publication. Exact physical acceptance is a stable-release gate, not a reason to disguise the alpha as finished.

## Deferred Beyond The Initial Alpha

- The optional public SD-card font bundle is a later, separately licensed release. The initial alpha is firmware-only and already carries the source ledger and licenses for the eight families represented in its generated font headers. Do not copy the private 130-family folder wholesale; generation, checksums, Reserved Font Name review, and per-family notices remain mandatory before any public font ZIP.

## Release Package

The initial alpha can ship as a firmware-only archive while the separately licensed public font pack is reviewed:

```text
Duet-v0.1.0-alpha.6-firmware.zip
|-- README-FIRST.txt
|-- README.md
|-- FEATURES.md
|-- USER_GUIDE.md
|-- CHANGELOG.md
|-- PUBLIC_RELEASE_READINESS.md
|-- firmware/
|   |-- Duet-X3-v0.1.0-alpha.6.bin
|   `-- Duet-X4-v0.1.0-alpha.6.bin
|-- docs/
|   |-- ALPHA6_ACCEPTANCE_QUICKSTART.md
|   |-- ALPHA_TESTING.md
|   |-- AI_COVER_PREFILL_PROMPT.md
|   |-- COVER_PREFILL.md
|   |-- DUET_FULL_FEATURE_TOUR.md
|   |-- ISSUE_TRIAGE.md
|   |-- MAINTAINER_RELEASE_RUNBOOK.md
|   |-- media/
|   |   `-- alpha-0.1.0/
|   |       |-- README.md
|   |       |-- x3/
|   |       |-- x4/
|   |       |-- fonts/
|   |       `-- stats/
|   |-- font-build-variants.md
|   |-- nearby-position-sync.md
|   |-- PHYSICAL_TEST_MATRIX.md
|   |-- SCREENSHOT_PLAN.md
|   |-- STATS_MEDIA_FIXTURE.md
|   |-- controls.md
|   |-- data-cache.md
|   |-- epub-render-modes.md
|   |-- file-formats.md
|   |-- i18n.md
|   |-- reader-features.md
|   |-- reading-stats-sync.md
|   |-- sd-card-fonts.md
|   |-- simulator.md
|   |-- templates/
|   |   |-- MEDIA_MANIFEST.md
|   |   `-- PHYSICAL_ACCEPTANCE_RECORD.md
|   |-- installation.md
|   |-- troubleshooting.md
|   |-- webserver-endpoints.md
|   `-- webserver.md
|-- RELEASE_NOTES.md
|-- AUTHORS.md
|-- PROJECT_IDENTITY.md
|-- LICENSE
|-- NOTICE
|-- THIRD_PARTY_NOTICES.md
|-- FONT_SOURCES.md
|-- licenses/
|   `-- fonts/
|       |-- BUILTIN_FONT_SOURCES.md
|       `-- [built-in family license folders]
`-- SHA256SUMS.txt
```

The later optional font archive may add a reviewed `/sd-card/fonts` folder and the notices for every included `.cpfont`. SD fonts remain separate assets rather than part of the firmware image. The current private font folder is about 643 MB uncompressed, whereas the device firmware partition has less than 1 MB of headroom in the latest X3 build.

Separate BIN-only downloads may also be offered, but the initial alpha's primary download is the firmware package shown above.

## Private Data Exclusions

Never include any of the following in the public repository or release assets:

- `/if_found.txt`, personal names, phone numbers, or email addresses.
- Wi-Fi or KOReader Sync credentials and account configuration.
- EPUBs, extracted standalone book-cover files, personal sleep screens, or dictionaries without a redistribution review. Manually approved product screenshots may show covers as part of the Duet interface, but the source cover files and ebooks must remain outside the repository and package.
- Reading guide files, tracker workbooks, Calibre catalogs, or library TSVs.
- Reading progress, stats, bookmarks, favorites, achievements, caches, backups, or any `/.duet`, `/.crossink`, or `/.crosspoint` state copied from a real device.
- Personal-only commercial or operating-system fonts listed in [FONT_SOURCES.md](FONT_SOURCES.md).

Example configuration and contact files must contain unmistakably fictional placeholder values.

## Build Matrix

Both supported device builds must come from the same reviewed commit.

| Target | Device | Required checks |
| --- | --- | --- |
| `x3-public` | XTEINK X3 | Size/headroom, boot, book open, reader, stats, fonts, dictionary, grid, carousel, search, sync, sleep |
| `x4-public` | XTEINK X4 | Size/headroom, boot, book open, reader, stats, fonts, dictionary, grid, carousel, search, sync, sleep |

## Test Matrix

- Clean installation and upgrade over a current CrossInk build.
- Cold boot, warm boot, sleep, wake, and repeated open/resume cycles.
- Position, stats, bookmarks, favorites, achievements, and settings preserved.
- EPUB, XTC, XTCH, TXT, and Markdown smoke tests.
- Difficult and image-heavy EPUB low-memory fallbacks.
- Every Home theme and configurable Home/Apps route.
- List, 2x2, 3x3, 4x4, and carousel library navigation before and after covers hydrate.
- Search by middle title word, title prefix, author, and series.
- More Info, Book Info, chapter selection, dictionary, bookmarks, and clippings.
- All stats tabs, long lists, dates, streaks, profile, heatmap, and restore.
- Font categories, A/B comparison, every built-in size for that device, one six-size SD-card family, real and synthetic styles, OpenDyslexic, and low-memory font release.
- KOReader Sync and sync-to-furthest in both X3-to-X4 directions.
- Sleep images, clean refresh, locked click-cycle settings, and no wake loop.
- Factory/recovery path using a known-good firmware artifact.

## Release Assets To Produce

- Source tag and release commit.
- X3 and X4 BINs.
- Firmware ZIP with X3/X4 BINs, built-in font notices, and tester documents.
- Optional reviewed font-only ZIP in a later release.
- Per-file SHA-256 manifest.
- Installation, upgrade, rollback, and data-backup instructions.
- Feature catalog and release notes.
- Third-party firmware and font notices.
- X3 and X4 screenshots.
- Known limitations and physical-device test results.

## Final Gate

The alpha.6 prerelease is ready to publish when the source tree is free of personal data, every redistributed asset has a bundled license and source record, both BINs and the package are produced from one public commit, checksums verify, release automation passes, and open hardware risks are stated plainly. A stable release requires the complete X3 and X4 physical acceptance matrix; the early alpha deliberately publishes before that gate so more devices can generate the evidence.
