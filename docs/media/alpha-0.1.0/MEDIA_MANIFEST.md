# Alpha.6 Public Media Manifest

## Release Identity

- Duet version: `0.1.0-alpha.6`
- X3 firmware SHA-256: `0db9b7712cda235b54e064403510480a577334b8faf5eeb61fa7c2eef26f22ef`
- X4 firmware SHA-256: `9ba44d3b3a7a524173748901124b8557516605fff1aeef9a017d49e475e41d4f`
- Capture source: alpha.6 public release checkout and deterministic simulator fixture
- Native dimensions: X3 `528x792`; X4 `480x800`

## Inventory

- 43 X3 Reading Stats screenshots
- 43 X4 Reading Stats screenshots
- 8 curated X3 feature screenshots plus one X3 overview sheet
- 13 curated X4 feature/app screenshots plus one X4 overview sheet
- 4 X4 font previews
- Total: 111 PNG files

## Data Provenance

Real book titles and public-facing cover art appear only inside the simulated Duet interface. The underlying EPUBs and extracted cover files are not included. All progress, reading time, dates, sessions, pace, streaks, completion estimates, achievements, device names, and sync data are deterministic fabricated test values.

## Review

- All 111 PNG files decode successfully through ImageMagick.
- Every local image and gallery link in the README and media indexes resolves.
- X3 and X4 feature overview sheets were reviewed for hydrated covers, density, footer fit, and visible metadata.
- Representative dense statistics pages were reviewed side by side at native aspect ratio, including Current, Heatmap, Reader DNA, Wrapped, Library, Started, Devices, and Trends.
- No personal contact details, credentials, account data, device identifiers, real reading history, or filesystem paths are intentionally present.

## Evidence Boundary

These images verify deterministic simulator rendering from the release source. They do not verify physical e-ink refresh behavior, ghosting, SD-card timing, radio sync, sleep/wake, or long-session stability. Physical evidence remains tracked separately in the test matrix.
