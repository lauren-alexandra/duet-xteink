# Public Screenshot and Demo Plan

Public media should show Duet clearly without publishing Lauren's library, reading history, contact details, credentials, or device identifiers.

## Demo Data

Use a privacy-safe hybrid fixture:

- Real book titles and cover art from Lauren's library may appear in finished product screenshots as part of the Duet interface. Do not publish or package the underlying EPUBs or extracted standalone cover files.
- Use invented progress, sessions, streaks, achievements, device names, and sync results.
- Fictional contact, network, sync, and server values.

Do not copy a real card's catalogs, statistics, settings, credentials, or device-state files into the public repository.

## Core Screenshot Set

1. Home dashboard on X3.
2. Dashboard Extended on X4.
3. Library list plus 2x2, 3x3, and 4x4 cover grids.
4. Five-cover carousel with a selected title and author.
5. Smart search autocomplete and results.
6. Book Info with cover, description, series, and reading status.
7. Reader page with the quick overlay.
8. Font picker and matched A/B comparison.
9. Dictionary lookup and looked-up-word history.
10. Every Reading Stats page, including overview, heatmap, streaks, Reader DNA, device split, timelines, and achievements.
11. Nearby Sync and KOReader Sync confirmation screens without account data.
12. Sleep screen and the Duet boot mark.
13. File-transfer web portal.
14. X3 and X4 together, showing the same book or synced statistics.

## Minimum Alpha Launch Set

The soft launch does not need all fourteen scenes. Publish these first:

1. X3 and X4 together on two different Duet home themes.
2. One 3x3 grid and one five-cover carousel with fully hydrated covers.
3. A short unsped clip showing the picker moving while covers load.
4. The reader quick overlay.
5. Two contrasting statistics pages, including the heatmap or Reader DNA.
6. Nearby Sync showing both device names with fictional or generic labels.
7. The System page showing the exact Duet alpha version.

Hold back any screen that still misstates data, stalls during the recording, or contains real library or account details. A smaller honest set is better than an exhaustive set from an older build.

## Short Videos

- Move through a grid immediately while covers continue loading.
- Move through the carousel as adjacent covers hydrate.
- Open a book, turn pages, cross a pre-indexed chapter boundary, and return home.
- Sync progress or statistics between X3 and X4 with confirmation visible.
- While the reader is asleep, record the locked sleep-image cycle gesture unsped from the first physical press through the newly settled image. Capture X3 and X4 separately, identify the configured click count, and preserve the observed one-wake-plus-three-taps behavior if that is what the device actually requires.

Keep each clip focused and short enough that viewers can see responsiveness without an edited speed-up.

## Capture Rules

- Capture simulator images at native X3 and X4 dimensions.
- Include several real-device photographs to show actual e-ink rendering.
- Use full refresh before final still images.
- Avoid glare, heavy perspective distortion, and hands covering controls.
- Do not show a computer desktop, phone notification, Wi-Fi name, IP address, MAC address, username, email address, phone number, or filesystem path.
- Run OCR and metadata checks before publication.
- Record the firmware tag used for every image and video.
- Keep a media manifest that states whether each statistic is fabricated and whether any visible title or cover is real.
- Start from the reusable [Public Media Manifest](templates/MEDIA_MANIFEST.md) so every candidate has an explicit version, provenance, OCR, metadata, and approval record.
- Verify the System version in the same capture session as the screenshots.
- Keep original unedited captures alongside any cropped public copies.

## Suggested Repository Layout

```text
docs/media/
|-- alpha-0.1.0/
|   |-- x3/
|   |-- x4/
|   |-- web/
|   `-- video/
`-- README.md
```

Only approved, privacy-checked media belongs in the public repository.

## Publication Package

The public repository contains the reviewed native-resolution simulator gallery under `docs/media/alpha-0.1.0/`. The private media workspace keeps the original BMP captures, contact-sheet working files, and earlier alpha.3 and alpha.5 references outside the release. The final media manifest must record the alpha.6 source revision and both firmware hashes before the gallery is described as release-matched evidence.

## Current Candidate Coverage

The public alpha.6 gallery now contains 43 fresh Reading Stats captures for each device: all 33 top-level pages plus 10 useful alternate and detail states. It also includes fresh X3/X4 Dashboard, Reading Home, hydrated grid, carousel, search, More Info, reader overlay, dictionary, achievement, utility, and font captures generated from the current source. Alpha.3 and alpha.5 captures remain private reference material and are not represented as current alpha.6 evidence.

Real book titles and rendered cover art appear in the library, dashboard, search, More Info, and reader scenes. All reading statistics, progress, sessions, streaks, achievements, sync results, and device names are fabricated.

Before publication, record the final alpha.6 source revision and firmware hashes in the public media manifest, capture the System/version page from the exact final build, rerun OCR and metadata checks, and confirm that the checked-in images still match the final simulator output. Earlier preflight media remains segregated and is not publication evidence.

Real-device photos, responsiveness clips, boot and sleep frames, a physical Nearby Sync clip, and the X3/X4 together shot remain physical-device work.

## Final Capture Status

| Scene | Status | Next action |
| --- | --- | --- |
| X3 Dashboard | Fresh alpha.6 simulator capture reviewed | Included in public gallery |
| X4 Dashboard Extended | Fresh alpha.6 simulator capture reviewed | Included in public gallery |
| 2x2, 3x3, and 4x4 grids | Fresh hydrated alpha.6 captures reviewed | Included in public gallery |
| Five-cover carousel | Fresh hydrated alpha.6 capture reviewed | Included in public gallery |
| Search autocomplete/results | Fresh alpha.6 capture reviewed | Included in public gallery |
| Book Info | Fresh hydrated alpha.6 capture reviewed | Included in public gallery |
| Reader quick overlay | Fresh alpha.6 capture reviewed | Included in public gallery |
| Font picker/comparison | Four-family alpha.6 set reviewed | Included in public gallery |
| Dictionary/history | Fresh alpha.6 captures reviewed | Included in public gallery |
| All Reading Stats pages | 43 fresh alpha.6 frames per device reviewed | Included in complete X3/X4 gallery |
| Nearby and KOReader Sync | Fresh public-data capture still required | Capture from final alpha.6 build |
| Sleep and boot | Outstanding | Capture clean X3 and X4 device frames |
| System/version | Alpha.5 source capture is obsolete | Recapture from alpha.6 |
| Web portal | Data-safe alpha.5 source capture reviewed | Compare with alpha.6 and approve |
| X3 and X4 together | Outstanding | Photograph after final physical acceptance |
