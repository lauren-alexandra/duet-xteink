---
title: Font Sources
parent: Fonts
nav_order: 2
---

# Font Sources

Duet's font work draws from openly licensed type projects, the inherited CrossInk catalog, and e-reader-oriented derivatives. The canonical family-by-family source and redistribution ledger is maintained in [FONT_SOURCES.md on GitHub](https://github.com/lauren-alexandra/duet-xteink/blob/main/FONT_SOURCES.md).

## Main Source Collections

- [CrossInk Fonts](https://github.com/uxjulia/crossink-fonts) supplies the current 24-family compatibility catalog used by Alpha.7's on-device downloader.
- [nicoverbruggen/ebook-fonts](https://github.com/nicoverbruggen/ebook-fonts) supplies the NV e-reader-optimized families plus Cartisse, Libron, Readerly, and Sourcerer.
- [nicoverbruggen/readerly](https://github.com/nicoverbruggen/readerly) supplies Readerly's source and license history.
- [Google Fonts](https://github.com/google/fonts), [SIL](https://software.sil.org/fonts/), [CTAN](https://ctan.org/), and the [TeX Gyre project](https://www.gust.org.pl/projects/e-foundry/tex-gyre) supply additional open families recorded in the ledger.

## Redistribution

A public Duet font archive must include only redistributable generated families, map every `.cpfont` file to its source face, preserve the applicable copyright and license notices, and publish checksums for the generated files. Firmware and font archives remain separate because the full SD-card collection is far larger than the X3/X4 application partition.

The Alpha.7 firmware release does not currently include an expanded Duet font archive. Use the [Fonts](sd-card-fonts.md) page for the exact downloads presently available.
