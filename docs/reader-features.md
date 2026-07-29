---
title: Reader Features
nav_order: 17
---

# Reader Features

This page covers a subset of Duet reader features that go beyond basic page turning. It is not a complete list of every reader setting or action. For the source-verified inventory, see the [Duet Feature and Lineage Catalog](../FEATURES.md).

The sections here focus on larger Duet-specific reader features. Small fixes, implementation details, and features that only arrived from upstream CrossPoint are intentionally left out.

## In-book Reader Options

Reader settings have two scopes:

- **Global defaults:** Open **Settings -> Reader** from Home or Apps. These settings apply to EPUBs that do not have a saved per-book override.
- **Current EPUB:** Open the EPUB, press **Confirm**, and select **Reader Options**. Changes made through this in-book screen automatically save an override for the open EPUB.

Open the reader menu and select **Reader Options** to adjust settings such as:

- Font family
- Font size
- Line spacing
- Margins
- Alignment
- Image rendering
- Bionic Reading / Guide Dots
- Dark Reader Mode

Changes take effect immediately. There is no separate Per Book/Global switch; the screen from which you open the setting determines its scope. A book with an override keeps it when the global defaults change. To return it to the global defaults, hold **Confirm** on that book in Browse Files or Recent Books and choose **Reset Book Reader Settings**.

For books that are slow to index or fail because of complex publisher styling, see [EPUB Render Modes](./epub-render-modes.md).

## Font Sizes And Downloadable Font Ranges

The public X3 build includes 10, 12, 14, and 16 pt for both selectable built-in families. The public X4 build includes 16, 18, and 20 pt. SD-card families expose the point sizes actually installed for that family.

The reader can also use SD-card font packs with selectable font-size ranges. This lets you keep the installed firmware smaller while still using extra sizes or custom fonts from the SD card.

Related docs:

- [SD Card Fonts](./sd-card-fonts.md)
- [Font Build Variants](./font-build-variants.md)

## Dark Reader Mode

Dark Reader Mode reverses the reader colors so text is shown light-on-dark.

Toggle it from **Reader settings**.

Dark Reader Mode can also be assigned to shortcut actions, so it can be switched without opening the full settings menu.

## Line Spacing

Duet supports adjustable reader line spacing from compact to wide spacing.

Use this when a book feels visually cramped, or when larger fonts need more vertical room to stay comfortable.

## Guide Dots

Guide Dots adds small dots between words. The idea comes from speed-reading guidance where focusing on the space between words can help peripheral vision pick up more of the surrounding text.

Toggle it from **Reader settings**.

## Force Paragraph Indents

Some books do not define paragraph indents in a way the firmware understands, which can make the page look like one large wall of text.

Force Paragraph Indents adds an indent at each new paragraph regardless of how the book is formatted.

This works when **Reader Paragraph Alignment** is set to:

- Left
- Justify
- Book's Style

Toggle it from **Reader settings**.

## Auto Page Turn

Auto Page Turn can advance pages on a timer while reading.

Duet adds a custom interval picker, so the interval is not limited to the built-in presets. The reader can also remember a different Auto Page Turn interval per book.

Open the reader menu and select **Auto Page Turn** to configure it.

## Time Left

Duet can show estimated time left in the current chapter or book.

The estimate is based on your recent forward-page reading pace. Non-linear jumps such as chapter skips, bookmark jumps, and footnote navigation are handled separately so they do not immediately distort the normal reading estimate.

Use **Reset Reading Pace** if the estimate was trained by unusual reading behavior and you want it to learn again from fresh page turns.

## Bookmarks

Duet supports EPUB bookmarks from the reader.

You can:

- Add a bookmark while reading
- See bookmark indicators in the reader
- Open a bookmark list
- Jump back to saved locations
- Delete individual bookmarks

## Clippings And Highlights

Duet supports EPUB text clippings from the reader. Use **Create Clipping** from the reader menu, select text on the current page, and save it.

A saved clipping is used in three ways:

- It appears as a highlight in the reader
- It appears in the in-app clipping list for that book
- It is appended to `/My Clippings.txt` on the SD card in a Kindle-style text format

The in-app clipping list is stored separately from the text export. Deleting a clipping from Duet removes the saved clipping and highlight from the device UI, but it does not rewrite old entries that were already appended to `/My Clippings.txt`.

For storage paths and binary format details, see [Data Cache](./data-cache.md) and [File Formats](./file-formats.md).

## Reading Stats

Duet tracks per-book reading stats automatically and aggregates them into global stats.

Tracked stats include:

- Total reading time
- Number of sessions
- Pages turned
- Average session time
- All-time reading stats, including total books read

Duet expands this into 33 top-level pages on both devices, including current-book views, sessions, pace, streak and time charts, Reading Profile, Reader DNA, Reading Signature, calendars, heatmaps, library analytics, and separate This Device and All Devices totals. It also supports editable stat dates, idle-time filtering, guarded corrections, reset controls, and rolling global-stat backups. See the exact page list in the [feature catalog](../FEATURES.md#the-33-top-level-pages).

Reading stats can also be used as a sleep screen. Minimal Stats is available on X3 and hidden on X4 in the current settings UI.

For two-device syncing, see [Reading Stats Sync](./reading-stats-sync.md).

## Nearby Position Sync

Duet can copy the current EPUB position from one nearby Duet reader to another over ESP-NOW. Open the same EPUB on both readers, choose **Nearby Position Sync** from the in-book menu on both devices, and press **Share** on the reader that is already at the correct page.

The receiving reader shows the incoming position and only applies it after you confirm it.

For details and troubleshooting, see [Nearby Position Sync](./nearby-position-sync.md).

## Finished Books And Read Folder

You can manually mark a book as finished from the in-book menu.

At 99% book progress, Duet also shows a popup asking whether to mark the book as finished.

If **Move finished books to Read folder** is enabled, books marked as finished are moved to `/Read/` on the SD card.

Marking books as finished also contributes to the total **Books Read** reading stat.

The file browser can also mark books as finished without opening them first.

## Reader Controls And Shortcuts

Duet adds reader-focused control options beyond the default button mappings.

Examples include:

- Reader-only front-button actions
- Front and side button mappings that respect the current orientation
- X3 tilt shortcuts
- Power-button reader shortcut actions
- Quick access to Controls from the in-reader menu
- Side-button shortcuts for changing font size or font family

For the full controls reference, see [Controls](./controls.md).
