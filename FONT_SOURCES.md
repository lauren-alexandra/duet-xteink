# Font Sources And Redistribution

## Packaging rule

The initial public alpha is a firmware-only package. It includes the notices for the fonts already baked into the X3 and X4 BINs, but it does not redistribute Lauren's private SD-card font collection.

A later optional font pack may include a ready-to-copy SD-card font folder. Those `.cpfont` files are separate release assets, not bytes embedded in the firmware BIN.

Only fonts with confirmed redistribution rights, complete copyright notices, and the applicable license text may enter the public bundle. This is a source and packaging audit, not legal advice.

The canonical personal pack currently contains 130 families and 780 generated `.cpfont` files at 10, 12, 14, 16, 18, and 20 pt. It is about 643 MB uncompressed. The public font folder must be generated from reviewed manifests rather than copied from that personal folder.

## Built-in firmware fonts

Duet's generated firmware headers contain glyphs from eight source families:

- Selectable reader families: Bitter and Lexend Deca.
- Reader fallback glyphs: ChareInk7. It is not a third selectable built-in family in the public X3/X4 builds.
- UI family: Inter, with IBM Plex Sans Hebrew fallback glyphs.
- Symbol and language fallbacks: Noto Emoji, Noto Sans Symbols, and Noto Sans CJK SC.

The pinned source revisions, generated-header roles, and bundled notice paths are recorded in [`licenses/fonts/BUILTIN_FONT_SOURCES.md`](licenses/fonts/BUILTIN_FONT_SOURCES.md). The firmware release archive carries that ledger and all corresponding license texts under `licenses/fonts/`.

These generated headers were inherited from Duet's recorded CrossInk baseline, revision `9c7315f495186185ff34ec5ddb485ebf18d3fc17`. ChareInk7 is already distributed under its distinct derivative name; any future regeneration still requires a Reserved Font Name check.

## Current CrossInk SD Manifest

`lib/EpdFont/scripts/sd-fonts.yaml` is the source of truth for the standard CrossInk SD-font generator. It currently defines these 24 families:

- Serif: Alegreya, Bitter, ChareInk, Gentium Book Plus, IBM Plex Serif, Literata, Lora, Merriweather, Noto Serif Extended, Source Serif 4, Tinos, Domitian, Libre Baskerville, and Vollkorn.
- Sans serif: IBM Plex Sans, Inter, Lexend Deca, Noto Sans Extended, and Source Sans 3.
- Mono/typewriter: IBM Plex Mono and Source Code Pro.
- Accessibility: OpenDyslexic, Atkinson Hyperlegible Next, and Lexica Ultralegible.

The manifest contains the exact source URL, variable-font axis values, glyph intervals, generated sizes, and selected faces for each family. Its sources are primarily Google Fonts, the original font project repositories, CTAN, and `uxjulia/crossink-fonts` revision `5cf3e09ff82ef5286a10d1d8e87617316d233e95`.

The CrossInk fonts repository includes OFL notices for ChareInk7, Lexend Deca, Noto Sans, and OpenDyslexic. Those notices must accompany generated files.

## E-Reader Optimized Collection

The public bundle may include the 37-family collection from [nicoverbruggen/ebook-fonts](https://github.com/nicoverbruggen/ebook-fonts), pinned for this audit at revision `88d65d5e40ca29aca5d9ec790fd0c3b43508a88d`:

- Cartisse, Libron, Readerly, and Sourcerer.
- NV Adelph, NV Ancizar Sans, NV Ancizar Serif, NV Basker, NV Bitter, NV Cardo, NV Castoro, NV Charis, NV Charter, NV Clara, NV Cooper, NV Disleksio, NV Elstob, NV Erewhon, NV Garamond, NV Gentium, NV Georsio, NV Jost, NV Junius, NV Kierkegaard, NV Legible Next, NV Libertinus, NV Literata, NV Lore, NV Membo, NV Newsreader, NV NinePoint, NV Palatium, NV Plex Serif, NV Scarlet, NV Source Serif, NV Technical, and NV ZillaSlab.

The repository documents the original source and modification history for each family. Most are SIL OFL 1.1. NV Charter uses the Bitstream Charter license, and NV Adelph uses the Open Inclusive Font License. The original notice for each family must be copied into the release's `licenses/fonts/` directory.

[Readerly](https://github.com/nicoverbruggen/readerly), pinned at revision `8f72824a9d1b3a182fa1aa1e8c3b9fd180c43a73`, is based on Newsreader and is licensed under SIL OFL 1.1. Readerly changes metrics and glyph details toward an e-reader-oriented result; it does not redistribute Bookerly outlines.

The generated XTEINK set uses the optimized NV variant in place of a redundant ordinary family where appropriate: NV Bitter, NV Garamond, NV Literata, NV Plex Serif, NV Source Serif, and NV Newsreader. Distinct choices such as Readerly, Libron, Sourcerer, and NV Palatium remain separate.

## Supplemental Open Families

These additional families were built from official open sources and can be considered for the public bundle after their exact notices are copied beside the generated output:

| Families | Source | License family |
| --- | --- | --- |
| Antykwa Torunska | [CTAN antt](https://ctan.org/pkg/antt) | GUST Font License |
| Besley, Petrona, Piazzolla, Poltawski Nowy, Science Gothic | [Google Fonts](https://github.com/google/fonts) | SIL OFL 1.1 |
| Cochineal | [CTAN Cochineal](https://ctan.org/pkg/cochineal) | SIL OFL 1.1 |
| Coelacanth 0.3.0 | [Coelacanth project](https://gitlab.com/Fuzzypeg/coelacanth) | SIL OFL 1.1 |
| ETbb | [CTAN ETbb](https://ctan.org/pkg/etbb) | MIT-style font notice included by the project |
| Gentium | [SIL Gentium](https://software.sil.org/gentium/) | SIL OFL 1.1 |
| KpRoman | [CTAN kpfonts-otf](https://ctan.org/pkg/kpfonts-otf) | SIL OFL 1.1 for fonts |
| TeX Gyre Adventor, Bonum, Cursor, Heros, Heros Condensed, Pagella, Schola, Termes | [TeX Gyre](https://www.gust.org.pl/projects/e-foundry/tex-gyre) | GUST Font License |
| Ysabeau | [CatharsisFonts/Ysabeau](https://github.com/CatharsisFonts/Ysabeau) | SIL OFL 1.1 |

Optimized equivalents already supplied by the NV collection should replace duplicate ordinary builds: Ancizar Serif, Castoro, Erewhon, fbb, Kierkegaard, Literata, and Zilla Slab. Alegreya, Merriweather, Noto Serif Extended, Roboto Slab, and Vollkorn are already represented elsewhere in the canonical source set.

## Private-Only Families

These legitimately owned or operating-system-provided faces may remain in the private 130-family pack, but must not be included in a public repository, release ZIP, font-only ZIP, screenshot source bundle, or automated font build:

- Apple Chancery
- Arial Rounded
- Bookerly
- Bradley Hand
- Georgia
- Herculanum
- Skia

Caecilia is not included and must not be added without a source license that expressly permits redistribution.

## Generated `.cpfont` Files

The XTEINK build process rasterizes selected source faces into device-specific `.cpfont` files, subsets glyph coverage, extracts static instances from variable fonts, and may synthesize missing bold or italic faces. Public output therefore needs its own generated-asset manifest containing:

- Display family and generated filename.
- Upstream project URL and pinned revision or package version.
- Source font filename and face.
- Generated point size and glyph intervals.
- Whether the face is real, variable-font-instanced, or synthetic.
- SHA-256 of every generated `.cpfont` file.
- Copyright and license filename.
- Reserved Font Name decision and renamed derivative name where required.

The generator configuration and conversion scripts should be public so the font bundle is reproducible.

## Publication Gate

The font package is ready only when all included families are generated from the public manifests, every output maps to a source and license, any Reserved Font Name requirements are satisfied, the private-only list is absent, the family/size/style inventory validates, and the release ZIP includes the license directory plus a checksum manifest.
