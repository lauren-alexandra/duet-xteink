---
title: Controls
nav_order: 13
---

# Controls

The Controls menu lets you customize front buttons, side buttons, and reader shortcuts.

## Settings Menu Layout

### Power Button

- Single-press action
- Double-press action
- Triple-press action
- Long-press action

Defaults:

- Single press: Ignore
- Double press: Refresh Screen
- Triple press: Toggle Bookmark
- Long press: Sleep

### Front Buttons

- Remap front buttons
- Remap front buttons while reading
- Orientation aware
- Long-press behavior (in-reader only)
- Long-press back action (in-reader only)
- Long-press menu action (in-reader only)

Note: Even though some actions assigned to the front buttons could be used globally, they are restricted to apply within the reader only due to the dynamic nature of the front buttons (they can mean different things based on the screen you're on).

### Side Buttons

- Layout
- Orientation aware
- Long-press action

## Side Button Long-press Action

When set to `Change Font Size`, hold a side button for about 2 seconds:

- Up increases font size
- Down decreases font size

When set to `Orientation Change`, hold a side button for about 2 seconds:

- Up cycles through the orientations in the following order: `Landscape CCW` -> `Inverted` -> `Landscape CW` -> `Portrait`
- Down cycles through the orientations in the following order: `Landscape CW` -> `Inverted` -> `Landscape CCW` -> `Portrait`

## Power, Back, and Menu Button Actions

Defaults:

- Single-press Power Button Action: Ignore
- Double-press Power Button Action: Refresh Screen
- Triple-press Power Button Action: Toggle Bookmark
- Long-press Power Button Action: Sleep
- Long-press Back Button Action: Browse Files
- Long Press Menu Button Action: Ignore

Available actions include:

- Ignore
- Sleep
- Page Turn
- Refresh Screen
- Change Font
- Guide Dots
- Bionic Reading
- Toggle Bookmark
- Sync Progress
- Mark as Finished
- Reading Stats
- Take Screenshot
- Auto Page Turn Interval
- File Transfer
- Calibre Wireless
- Join a Network
- Create Hotspot
- Tilt Page Turn (X3 only)
- Footnotes
- Dark Mode
- Browse Files
- Save Clipping

On X3, Tilt Page Turn is also offered when the sensor is available. Some actions only do useful work while a supported book is open.

## Locked Sleep-Image Cycling

The awake Power actions above are separate from **Settings -> Display -> Tap Power While Asleep to Cycle**. CrumBLE introduced this as an opt-in one-tap action: a brief Power tap selected another sleep image and returned the device to deep sleep instead of waking it. Duet expands that behavior to Off, 1, 2, or 3 brief clicks and defaults to 3 at Lauren's direction because one tap made accidental image changes too easy.

The setting is labeled **3 clicks**, and Duet's code intends to count the press that starts the sleeping device's wake path as click one. Current physical testing has nevertheless found that the reliable default gesture can feel like four physical presses: one initial press to enter the wake-detection path, followed by the three deliberate taps. This is a known alpha input quirk rather than a fourth selectable setting. A deliberate hold continues into a normal wake.

## Footnote Shortcut

When a shortcut is mapped to Footnotes, the shortcut opens the footnotes submenu while reading. If the current page has only one footnote, Duet opens that referenced page directly.

The **Quick-return from Footnotes** setting controls whether the Power button acts like Back after opening a footnote page, making it faster to return to the original reading position.
