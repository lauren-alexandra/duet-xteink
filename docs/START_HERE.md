---
title: Start Here
nav_order: 1
---

# Start here

Duet is early-alpha firmware for the Xteink X3 and X4. The current tester release is `v0.1.0-alpha.7`. Start by identifying your device, backing up the complete SD card, and keeping a known-good firmware BIN available for recovery. X3 and X4 firmware files are not interchangeable.

## First Duet installation

1. Read the [current alpha notes and known issues](ALPHA_TESTING.md).
2. Back up the complete SD card, including hidden folders.
3. Download the [X3 firmware BIN](https://github.com/lauren-alexandra/duet-xteink/releases/download/v0.1.0-alpha.7/Duet-X3-v0.1.0-alpha.7.bin) or [X4 firmware BIN](https://github.com/lauren-alexandra/duet-xteink/releases/download/v0.1.0-alpha.7/Duet-X4-v0.1.0-alpha.7.bin). The [complete alpha.7 release page](https://github.com/lauren-alexandra/duet-xteink/releases/tag/v0.1.0-alpha.7) also provides the combined firmware ZIP and SHA-256 checksums.
4. Wake and unlock the reader, connect it by USB-C, and open the [CrossPoint web installer](https://crosspointreader.com/#flash-tools).
5. Choose the correct device, select **Custom .bin**, and flash the Duet BIN.
6. After the restart, open **Settings > System** and confirm the displayed Duet version.
7. Keep the backup and rollback BIN until the reader has passed sleep, wake, book-open, and page-turn checks.

The [full installation guide](installation.md) includes command-line flashing and recovery details.

## Updating an existing Duet installation

1. Back up the complete SD card and keep the currently working BIN.
2. Put exactly one new Duet BIN at the SD-card root.
3. On the reader, open **Settings > System > SD Firmware Update**.
4. Confirm the model and version, then allow the reader to restart.
5. Verify the new version in **Settings > System** before removing the rollback copy from your computer.

Duet writes new state under `/.duet` and imports inherited CrossPoint/CrossInk state without deleting it. Read the [storage migration guide](DUET_STORAGE_NAMESPACE_MIGRATION.md) before manually moving, renaming, or deleting hidden state folders.

## Loading books and building the library

Copy supported books to the SD card in the folder structure you want to browse. Duet reads EPUB, XTC, XTCH, TXT, and Markdown files. The [User Guide](../USER_GUIDE.md) covers the reader, library views, search, More Info, bookmarks, clippings, statistics, sleep modes, and settings.

For large or multiply organized libraries, run [Desktop Cover Prefill](COVER_PREFILL.md) after loading books. It creates the exact X3/X4 grid and carousel thumbnails Duet requests, which avoids making the reader generate every cover during first browsing. The complete ready-to-paste prompt for Codex, Claude CoWork, Perplexity Computer, or another local assistant is included directly on that page and also available as a [standalone prompt](AI_COVER_PREFILL_PROMPT.md).

More Info descriptions and library categories use an optional Calibre-generated `/.duet/state/library_catalog.tsv`. Copying an EPUB to the SD card does not generate a description automatically; books remain readable without the catalog, but More Info shows only the metadata Duet can resolve locally. Fonts go under `/fonts` or `/.fonts`; StarDict dictionaries go under `/dictionaries/<Name>/`. Fonts can be installed on the reader through **Settings > Reader > Font Options > Manage Fonts**. The Alpha.7 on-device downloader currently uses CrossInk's credited compatibility catalog; see [SD-card fonts](sd-card-fonts.md) for the exact public-pack status and manual installation options. For offline lookup, download the ready-to-copy WordNet 3.0 release asset and follow [Dictionary Setup](DICTIONARY_SETUP.md). [Reader Features](reader-features.md) and the [User Guide](../USER_GUIDE.md) cover the rest of the library and reader structure.

## Using an X3 and X4 together

Run the same Duet release on both devices. [Nearby Position Sync](nearby-position-sync.md) compares the current book position and moves only after confirmation. [Nearby Reading Stats Sync](reading-stats-sync.md) exchanges reading totals, per-book statistics, sessions, journals, dates, and retained device snapshots. KOReader Sync remains available for remote book-position synchronization.

Position sync requires the same book identity on both readers. Statistics and achievements have separate persistence rules, so read the sync guides before assuming every screen or unlock notification will be identical.

## Testing the alpha

Use the short [Alpha.7 Acceptance Quickstart](ALPHA7_ACCEPTANCE_QUICKSTART.md) for an ordered physical-device pass. The broader [Alpha Testing guide](ALPHA_TESTING.md) lists known issues, high-value test areas, useful logs, privacy rules, and a copy-ready report format.

Use **Alpha Test Report** for a successful or mixed test session and **Alpha Bug Report** for one reproducible defect. Do not upload ebooks, full SD-card archives, credentials, personal catalogs, contact files, or raw reading history. Review logs before posting because book paths and filenames may be personal.

## Recovery

If an update causes a boot loop, prevents books from opening, or breaks sleep/wake, remove the SD card and boot once. If the problem continues, flash the last known-good BIN through the web installer's **Custom .bin** option. Do not delete `/.duet`, `/.crossink`, `/.crosspoint`, or statistics folders while diagnosing; they contain both recoverable state and useful evidence.

The [Troubleshooting guide](troubleshooting.md) covers firmware recovery, SD-card filesystem checks, slow or missing covers, networking, and file transfer.

## Where to go next

| What you need | Guide |
| --- | --- |
| Every user-facing feature | [Complete Feature Tour](DUET_FULL_FEATURE_TOUR.md) |
| Source-verified feature counts and lineage | [Feature and Lineage Catalog](../FEATURES.md) |
| Everyday controls and settings | [User Guide](../USER_GUIDE.md) |
| Installation and recovery | [Installation](installation.md) and [Troubleshooting](troubleshooting.md) |
| Current alpha risks and test targets | [Alpha Testing](ALPHA_TESTING.md) |
| Large-library cover preparation | [Desktop Cover Prefill](COVER_PREFILL.md) |
| Installing and using a dictionary | [Dictionary Setup](DICTIONARY_SETUP.md) |
| Installing or converting fonts | [SD-card fonts](sd-card-fonts.md) |
| All current screenshots | [Alpha.7 Media Gallery](media/alpha-0.1.0/README.md) |
| Contributing code or documentation | [Contributing to Duet](../CONTRIBUTING.md) |
| Complete credit and audited upstream revisions | [Third-Party Notices](../THIRD_PARTY_NOTICES.md) |
