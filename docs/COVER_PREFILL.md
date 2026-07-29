# Desktop Cover Prefill

Duet can generate missing book thumbnails on the reader, but a large or multiply organized library can make that first browse unnecessarily slow. Run the desktop prefill once after loading books to create the exact X3 or X4 grid and carousel images before the card returns to the device.

The firmware still generates thumbnails for genuinely new books later. The desktop pass is an accelerator, not a permanent dependency.

## What It Creates

For every EPUB path under `/Books`, the script creates twelve exact 1-bit BMP targets in `/.duet/cache/thumbs`:

- Three model-specific grid sizes for 2x2, 3x3, and 4x4 views.
- Two library-carousel sizes for selected and adjacent covers.
- Two shared card/detail sizes.
- Two Home-carousel sizes.
- Three adaptive Fit targets used by larger cover surfaces.

The cache key includes the complete book path. A book copied into All Books, an author folder, a category folder, and a spice folder therefore needs a separate twelve-image set for each path. The final `book_count` is a count of EPUB paths, not necessarily distinct titles.

## Requirements

- macOS or another computer with Python 3.
- A local Duet source checkout.
- [ImageMagick](https://imagemagick.org/) with the `magick` command available.
- The X3 or X4 SD card mounted and backed up.

On macOS with Homebrew:

```bash
brew install imagemagick
```

## Run It

From the Duet repository root, use the command matching the card. Confirm the mounted volume name first; do not guess the model from card capacity.

X3:

```bash
python3 scripts/prefill_cover_thumbnails.py "/Volumes/XTeink X3" --device x3
```

X4:

```bash
python3 scripts/prefill_cover_thumbnails.py "/Volumes/XTeink X4" --device x4
```

Run the command separately for each card. X3 and X4 use different grid dimensions, and their private device state must never be cloned between cards.

## Confirm Success

A successful run exits with status `0` and ends with a JSON report. Confirm:

- `failed_books` is `[]`.
- `device` matches the physical reader.
- `book_count` matches the number of EPUB paths scanned.
- `generated + valid_existing` equals `book_count * 12`.

The same report is saved on the card at:

```text
/.duet/state/desktop_cover_prefill.json
```

Rerunning the command is safe and incremental. Existing valid exact-size thumbnails are preserved unless `--force` is supplied.

## Coverless or Malformed EPUBs

If an EPUB has no usable embedded cover, supply a local image for every card path containing a stable match:

```bash
python3 scripts/prefill_cover_thumbnails.py "/Volumes/XTeink X3" --device x3 \
  --cover-override "No-Cover Example=/absolute/path/to/cover.jpg"
```

`--cover-override` may be repeated. It changes only generated cache images; it does not modify the EPUB.

## Safety Rules

- Back up the card before the first run.
- Never delete `/.duet/cache/thumbs` merely to rerun the script.
- Never copy the complete `/.duet`, `/.crossink`, or `/.crosspoint` directory from one reader to another.
- Never run the X3 command against an X4 card or vice versa.
- Leave books, reading history, settings, fonts, dictionaries, and sleep screens untouched.
- Review `failed_books` before ejecting the card.

For a ready-to-paste computer-assistant request, see [AI Cover Prefill Prompt](AI_COVER_PREFILL_PROMPT.md).
