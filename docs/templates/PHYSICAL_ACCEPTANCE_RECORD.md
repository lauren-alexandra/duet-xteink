# Duet Physical Acceptance Record

## Candidate

- Version:
- Commit:
- Date:
- Maintainer:
- X3 BIN:
- X3 SHA-256:
- X4 BIN:
- X4 SHA-256:
- Previous accepted version:
- Installation path:

## Test Cards

| Device | SD card | Filesystem | Free space | Books | Prefill  |
| ------ | ------- | ---------- | ---------: | ----: | -------- |
| X3     |         |            |            |       | Yes / No |
| X4     |         |            |            |       | Yes / No |

## Core Acceptance

| Test                               | X3  | X4  | Evidence or note |
| ---------------------------------- | --- | --- | ---------------- |
| Install/update and version display |     |     |                  |
| Cold boot                          |     |     |                  |
| Home loads correct active book     |     |     |                  |
| Open current book                  |     |     |                  |
| Page turn responsiveness           |     |     |                  |
| Return from reader to Home         |     |     |                  |
| Sleep message and transition       |     |     |                  |
| Wake and resume                    |     |     |                  |
| Settled-screen ghosting            |     |     |                  |
| Recovery path available            |     |     |                  |

## Library Acceptance

| Test                             | X3  | X4  | Evidence or note |
| -------------------------------- | --- | --- | ---------------- |
| List/folder navigation           |     |     |                  |
| 2x2 grid                         |     |     |                  |
| 3x3 grid                         |     |     |                  |
| 4x4 grid                         |     |     |                  |
| Carousel                         |     |     |                  |
| Cursor moves during hydration    |     |     |                  |
| Forward page lookahead           |     |     |                  |
| Backward page lookahead          |     |     |                  |
| Covers persist during navigation |     |     |                  |
| Search                           |     |     |                  |
| More Info                        |     |     |                  |

## Reader And Data Acceptance

| Test | X3 | X4 | Evidence or note |
| --- | --- | --- | --- |
| Chapter transition |  |  |  |
| Pre-index cancellation |  |  |  |
| Book progress persists |  |  |  |
| Active-book selection persists |  |  |  |
| Stats pages |  |  |  |
| Achievement unlock |  |  |  |
| Nearby stats sync |  |  |  |
| Nearby position sync |  |  |  |
| Repeated sync is idempotent |  |  |  |
| Complete stats export is non-empty |  |  | File count, bytes, archive name |
| Controlled post-export change |  |  | Disposable book and one page turn |
| Complete stats restore |  |  | Global, book, session, date, sync figures |
| Pre-restore safety copy created |  |  | Safety archive name |
| Crash report reviewed |  |  |  |

## Timings

| Measurement               |  X3 |  X4 |
| ------------------------- | --: | --: |
| Cold boot to usable Home  |     |     |
| Home to first reader page |     |     |
| Reader to usable Home     |     |     |
| First grid placeholder    |     |     |
| First grid cover          |     |     |
| Full grid page            |     |     |
| First carousel paint      |     |     |
| Carousel next/previous    |     |     |
| New chapter usable        |     |     |
| Stats library page        |     |     |
| Two-device sync           |     |     |

## Known Issues

-

## Decision

- X3: Pass / Pass with known issue / Fail
- X4: Pass / Pass with known issue / Fail
- Publication: Approved / Not approved
- Decision notes:

This record certifies only the commit and hashes written above. A rebuilt or replaced BIN requires a new record.
