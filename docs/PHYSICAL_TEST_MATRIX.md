# Duet Physical Test Matrix

This page separates source review, build success, simulator coverage, and behavior observed on an actual Xteink panel. A check is complete only when the listed device and exact firmware artifact passed on hardware.

## Current candidate

- Version: `v0.1.0-alpha.9`
- X3 artifact: `Duet-X3-v0.1.0-alpha.9.bin`
- X3 size: 5,960,976 bytes
- X3 SHA-256: `90c63270da45269eee80daacda4500175003a3e81091ffdd7a49b901352599c0`
- X4 artifact: `Duet-X4-v0.1.0-alpha.9.bin`
- X4 size: 5,823,888 bytes
- X4 SHA-256: `dd53a2bb234e4390efb3975cdea72bf5caec24014ac2e31fcee0ad291f5f47eb`
- Source state: public `release/alpha9` candidate; exact public-artifact acceptance remains open

Both Alpha.9 hardware targets build successfully from the same public source. The X3 image leaves 592,624 bytes of OTA app-partition headroom. The X4 image leaves 729,712 bytes. All 118 host unit tests pass, and full X3 and X4 simulator smoke tests exercise small caps, reader relayout, Home, and statistics. This is useful development evidence, but it is not a substitute for testing these exact cleanly named Alpha.9 artifacts. Simulator results do not prove physical panel refresh behavior.

## Alpha 2 Status

Alpha.2 was not physically tested. The X4 photo initially attributed to alpha.2 was later confirmed to have been taken while `alpha.1-lookahead1.ghost1` was still installed.

Code review still found an installation-path weakness worth fixing before the next flash: SD updates restart with `ESP_RST_SW`, which HalGPIO classifies as `Other`, so an `AfterFlash`-only cleanup gate is unreliable. Alpha.3 replaces that gate with a device-and-version marker.

## Alpha 9 Acceptance

| Area | X3 | X4 | Evidence needed |
| --- | --- | --- | --- |
| Install and reported version | Pending | Pending | System page shows `Duet 0.1.0-alpha.9` |
| Current FreeInk hardware detection | Pending | Pending | Correct display driver selected; no boot loop, SD Card Error, or power-latch failure on the tested hardware variant |
| Update Complete handoff | Pending | Pending | Updater exits and reaches Home without a manual reset or repeated flash |
| Font-size direction and reading-position anchor | Pending | Pending | X3 right increases and left decreases; X4 Up increases and Down decreases; both reflow without advancing the visible text, including from a chapter's first page |
| Relayout rollback | Pending | Pending | A failed complex relayout restores the prior font, size, spacing, and orientation rather than persisting the failed settings |
| Small-caps EPUB styling | Pending | Pending | CSS small caps render as compact capitals and preserve surrounding bold/italic style |
| SD-font UI fallback | Pending | Pending | Compatible extended/CJK glyphs render through the active SD font; unloading or changing the family clears the prior fallback |
| Clock Sync | Pending | Pending | X3 hardware clock and X4 software clock show the synchronized local time after leaving the sync screen |
| Canonical storage migration | Pending | Pending | `/.duet` created; active book, progress, settings, stats, achievements, covers, and catalog preserved; legacy roots untouched |
| Second boot after migration | Pending | Pending | Migration does not visibly repeat and the same active book remains selected |
| Post-install Home ghosting | Pending | Pending | No updater text or prior screen after first settled Home frame |
| Open known-good EPUB | Pending | Pending | Opens and returns to saved position |
| Sleep from Home | Pending | Pending | Confirmation, final image, no lockup |
| Sleep from open book | Pending | Pending | Time/progress saved, final image, no lockup |
| Locked sleep-image cycle | Pending | Pending | Test Off/1/2/3; record whether default 3 is three total presses or one wake press plus three taps |
| Wake to Home | Pending | Pending | Reasonable refresh count and time |
| Settled sleep-image ghosting | Pending | Pending | Photo after panel finishes updating |
| 2x2 grid across pages | Pending | Pending | Forward, backward, cursor during hydration |
| 3x3 grid across pages | Pending | Pending | Forward, backward, cursor during hydration |
| 4x4 grid across pages | Pending | Pending | Forward, backward, cursor during hydration |
| Carousel ordinary movement | Pending | Pending | Five visible covers persist and move once |
| Book open and return Home | Pending | Pending | No freeze; current book remains correct |
| Chapter preview boundary | Pending | Pending | After the temporary preview pages, Indexing completes and Next advances instead of repainting the same page |
| Current-book WPM | Pending | Pending | Enriched EPUB has `META-INF/x-locations.json`; WPM appears after measurable attributable time and progress; untouched EPUB shows a dash rather than fake WPM |
| Chapter transition | Pending | Pending | Timing and `/.duet/state/reader_timing.txt` if slow |
| Achievement notification | Pending | Pending | Dismisses promptly; controls remain responsive |
| Nearby Stats Sync | Pending | Pending | Both devices report success and figures converge |
| Nearby Position Sync | Pending | Pending | Explicit apply moves only the chosen device |
| Complete stats archive export | Pending | Pending | Non-zero file/byte count and `.cstats` file under `/.duet/backups/reading-stats` |
| Complete stats archive restore | Pending | Pending | Safety copy created; global, per-book, session, date, and synced figures restored |

## Post-Alpha.7 WPM Sync Verification

Matched private/test `v0.1.0-alpha.7.1` builds physically passed the WPM detail-sync repair on Lauren's X3 and X4. Current-book WPM displayed on both readers, and a fresh Nearby Stats Sync made the detailed current-book reading statistics and WPM converge across the two devices.

- X3 SHA-256: `287e1ae508a59b3f4717bd98570d071537b1b59ea181954a48259c32759f23b4`
- X4 SHA-256: `2bc4d7de14f732c6564fafc85250388e088e7d320615d35bae2195438412bb34`

This result cleared the hardware-verification gate for the Alpha.7.1 WPM/detail-snapshot repair. It does not retroactively mark the published Alpha.7 BINs as passed because they do not contain that final repair. Alpha.8 and later include the accepted source so testers can reproduce the behavior from matched public X3 and X4 downloads.

## Reporting a result

Record:

1. Device and exact version shown in Settings.
2. Whether the firmware was a first flash or SD-card update.
3. The test row and exact button sequence.
4. Pass, fail, or intermittent.
5. Approximate delay or visible refresh count when timing matters.
6. The smallest relevant log, photo, or video after removing private details.

Do not mark a row passed from a build log, checksum, simulator screenshot, or successful SD-card copy. Those verify different parts of the release.
