# Alpha.7 Public Media Manifest

## Release Identity

- Duet version: `0.1.0-alpha.7`
- X3 firmware SHA-256: `ad0bccbde59b2d64119279623a785c97124dbf6a72c7cfecce58f748128f1961`
- X4 firmware SHA-256: `8a38294c547a1f947c7f810acf083b6b7e077e5f8a46e946e867b3e13a8172f2`
- Capture source: alpha.7 release-candidate checkout, deterministic statistics fixture, and a private mixed-library input set for the gallery
- Representative simulator dimensions: X4 `480x800`

## Inventory

- 43 representative Reading Stats screenshots: all 33 top-level pages plus 10 detail or alternate states
- 21 feature and app screenshots plus one feature overview sheet
- 4 font-picker comparisons
- Total: 69 PNG files

## Data Provenance

The gallery uses recognizable books from Lauren's library inside the simulated Duet interface. The underlying EPUBs and extracted cover files are not included. All progress, reading time, dates, sessions, pace, streaks, completion estimates, achievements, device names, and sync data are deterministic fabricated test values.

## Review

- All 69 PNG files decode successfully through ImageMagick.
- Every local image and gallery link in the README and media indexes resolves.
- The representative feature overview was reviewed for hydrated covers, density, footer fit, and visible metadata.
- Representative dense statistics pages were reviewed at native aspect ratio, including Current, Recent Sessions, Calendar, Heatmap, Reader DNA, Wrapped, Library, Started, Devices, Trends, and the populated WPM Pace chart. The fabricated history visibly varies session lengths, page counts, start times, daily totals, and calendar gaps.
- The four font-picker captures show eight distinct families by pairing a different current font with each preview font.
- No personal contact details, credentials, account data, device identifiers, real reading history, or filesystem paths are intentionally present.

## Evidence Boundary

Duet uses the same interface and feature code on X3 and X4, so shared screens are published once at representative X4 resolution instead of as duplicate device sets. Both simulator targets remain part of release regression testing. These images verify deterministic simulator rendering from the release source; they do not verify physical e-ink refresh behavior, ghosting, SD-card timing, radio sync, sleep/wake, or long-session stability. Physical evidence remains tracked separately in the test matrix.
