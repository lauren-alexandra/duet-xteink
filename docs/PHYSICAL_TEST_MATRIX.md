# Duet Physical Test Matrix

This page separates source review, build success, simulator coverage, and behavior observed on an actual Xteink panel. A check is complete only when the listed device and exact firmware artifact passed on hardware.

## Current candidate

- Version: `v0.1.0-alpha.7`
- X3 artifact: `Duet-X3-v0.1.0-alpha.7.bin`
- X3 size: 5,941,904 bytes
- X3 SHA-256: `e27ede316b5fce4729395b7d68c18fed9be6b18507919a13b6e38c474b0629ba`
- X4 artifact: `Duet-X4-v0.1.0-alpha.7.bin`
- X4 size: 5,804,864 bytes
- X4 SHA-256: `c38ed35ccd48df2d2dda3507fece1f843eb43005b934a4d629219e10540fa583`
- Source state: release branch pending final tag and prerelease publication

Both alpha.7 hardware targets build successfully. The X3 image leaves 611,696 bytes of OTA app-partition headroom. The X4 image leaves 748,736 bytes. All 117 host unit tests pass, and the X3 and X4 simulator smoke tests pass. This is useful development evidence, but it is not a substitute for testing these exact cleanly named alpha.7 artifacts. Simulator results do not prove physical panel refresh behavior.

## Alpha 2 Status

Alpha.2 was not physically tested. The X4 photo initially attributed to alpha.2 was later confirmed to have been taken while `alpha.1-lookahead1.ghost1` was still installed.

Code review still found an installation-path weakness worth fixing before the next flash: SD updates restart with `ESP_RST_SW`, which HalGPIO classifies as `Other`, so an `AfterFlash`-only cleanup gate is unreliable. Alpha.3 replaces that gate with a device-and-version marker.

## Alpha 7 Acceptance

| Area | X3 | X4 | Evidence needed |
| --- | --- | --- | --- |
| Install and reported version | Pending | Pending | System page shows `Duet 0.1.0-alpha.7` |
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
| Current-book WPM | Pending | Pending | WPM appears after at least ten seconds and measurable book progress |
| Chapter transition | Pending | Pending | Timing and `/.duet/state/reader_timing.txt` if slow |
| Achievement notification | Pending | Pending | Dismisses promptly; controls remain responsive |
| Nearby Stats Sync | Pending | Pending | Both devices report success and figures converge |
| Nearby Position Sync | Pending | Pending | Explicit apply moves only the chosen device |
| Complete stats archive export | Pending | Pending | Non-zero file/byte count and `.cstats` file under `/.duet/backups/reading-stats` |
| Complete stats archive restore | Pending | Pending | Safety copy created; global, per-book, session, date, and synced figures restored |

## Reporting a result

Record:

1. Device and exact version shown in Settings.
2. Whether the firmware was a first flash or SD-card update.
3. The test row and exact button sequence.
4. Pass, fail, or intermittent.
5. Approximate delay or visible refresh count when timing matters.
6. The smallest relevant log, photo, or video after removing private details.

Do not mark a row passed from a build log, checksum, simulator screenshot, or successful SD-card copy. Those verify different parts of the release.
