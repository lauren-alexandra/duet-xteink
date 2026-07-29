# Duet logo — design brief (for Codex or any designer)

## What Duet is

"Duet" is the working name of Lauren's CrossInk-fork firmware for the Xteink X3/X4 e-readers. The logo must be a single-color (1-bit friendly) mark that works at 120x120 (boot screen), 48x48 (loading icon), and ~52x80 (web portal), plus an SVG master for the README.

## Approved concept (do not depart from it)

An **eighth note whose notehead is an ink drop** — an homage to CrossInk's ink-drop logo (see `web/assets/logo.png` history or a device boot screen). Approved elements, all required:

- Solid black ink-drop as the notehead (classic teardrop, point up).
- Slim stem and a graceful single flag (elegant, not bulbous).
- A small white shine inside the drop: a thin arc following the inner lower-left belly (exactly like CrossInk's own drop shine).
- Two companion "duet dots" on a steep diagonal, tucked close to the note: an OUTLINE (ring) dot upper-left, a SOLID dot mid-right. The solid dot sits "up and in" — roughly beside the drop/stem junction, not at the bottom corner.

## Reference material

- Current best master: `branding/duet-mark.svg` (and `branding/duet-logo.svg` for the DUET wordmark lockup).
- Lauren's hand sketch of the note posture she likes: `~/Downloads/IMG_0547.HEIC` (drop-as-notehead, stem from the drop's tip, petite flag flick).
- CrossInk boot screen reference: `~/Downloads/IMG_0546.heic`.

## Deliverables and formats

1. `src/images/Logo120.h` — 120x120, 1 bit/pixel, MSB-first rows, bit 0 = ink, exactly 1800 bytes, `static const uint8_t Logo120[]` with the existing static_assert kept.
2. `src/images/LoadingIcon.h` — 48x48, same packing, with `LOADINGICON_WIDTH/HEIGHT` defines (existing file shows the shape).
3. `web/assets/logo.png` — ~52x80 grayscale PNG, anti-aliased, white background.
4. `branding/duet-mark.svg` + `branding/duet-logo.svg` updated as masters.

## Rendering guidance (hard-won lesson)

Do NOT hand-rasterize curves with ad-hoc math — quality collapses. Render the SVG with a real engine (e.g. `qlmanage -t -s 1200 -o <dir> branding/duet-mark.svg` on macOS) and downsample with box filtering; threshold at 50% for the 1-bit headers. Verify visually at all three sizes before shipping.

## Approval

Lauren approves by eye. Show large + boot-size + inverted-on-black previews before baking anything into the firmware headers.
