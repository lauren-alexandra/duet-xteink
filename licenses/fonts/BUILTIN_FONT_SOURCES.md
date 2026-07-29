# Built-in font sources

## Scope

Duet firmware contains generated C headers under
`lib/EpdFont/builtinFonts/`. The conversion commands preserved in those headers
identify eight source-family directories. This ledger maps each family to its
audited upstream revision and bundled license text.

The generated headers were inherited from Duet's recorded CrossInk baseline:

`9c7315f495186185ff34ec5ddb485ebf18d3fc17`

## Source ledger

| Family | Firmware role | Audited source | Revision | Bundled notice |
| --- | --- | --- | --- | --- |
| Bitter | Reader glyphs | [google/fonts](https://github.com/google/fonts/tree/main/ofl/bitter) | `9fab8b6cc7b2f20376914fd765d918c698c66d75` | `Bitter/OFL.txt` |
| ChareInk7 | Reader and fallback glyphs | [uxjulia/crossink-fonts](https://github.com/uxjulia/crossink-fonts/tree/main/fonts/ChareInk7) | `5cf3e09ff82ef5286a10d1d8e87617316d233e95` | `ChareInk7/OFL.txt` |
| IBM Plex Sans Hebrew | Hebrew UI fallback glyphs | [google/fonts](https://github.com/google/fonts/tree/main/ofl/ibmplexsanshebrew) | `9fab8b6cc7b2f20376914fd765d918c698c66d75` | `IBMPlexSansHebrew/OFL.txt` |
| Inter | UI glyphs | [google/fonts](https://github.com/google/fonts/tree/main/ofl/inter) | `9fab8b6cc7b2f20376914fd765d918c698c66d75` | `Inter/OFL.txt` |
| Lexend Deca | Reader glyphs | [uxjulia/crossink-fonts](https://github.com/uxjulia/crossink-fonts/tree/main/fonts/LexendDeca) and [google/fonts](https://github.com/google/fonts/tree/main/ofl/lexenddeca) | `5cf3e09ff82ef5286a10d1d8e87617316d233e95` and `9fab8b6cc7b2f20376914fd765d918c698c66d75` | `LexendDeca/OFL-crossink-fonts.txt`, `LexendDeca/OFL-google-fonts.txt` |
| Noto Emoji | Emoji fallback glyphs | [notofonts/noto-emoji](https://github.com/notofonts/noto-emoji) | `8998f5dd683424a73e2314a8c1f1e359c19e8742` | `NotoEmoji/OFL.txt` |
| Noto Sans CJK SC | Simplified Chinese fallback glyphs | [notofonts/noto-cjk](https://github.com/notofonts/noto-cjk) | `f8d157532fbfaeda587e826d4cd5b21a49186f7c` | `NotoSansCJK/OFL.txt` |
| Noto Sans Symbols | Symbol fallback glyphs | [notofonts/noto-fonts](https://github.com/notofonts/noto-fonts) | `ffebf8c1ee449e544955a7e813c54f9b73848eac` | `NotoSymbols/OFL.txt` |

## Reserved Font Names

The ChareInk7 OFL notice reserves the names "Charis" and "SIL." The firmware
uses the distinct ChareInk7 derivative name. Any future regeneration or public
SD-card font pack still needs a family-by-family Reserved Font Name review.

This file records source and license provenance. It is not legal advice.
