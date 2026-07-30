# Duet Open Font Pack

This optional pack contains 123 redistributable `.cpfont` families prepared for Duet on the Xteink X3 and X4. Every family includes 10, 12, 14, 16, 18, and 20 pt files.

## Install the complete pack

1. Back up the SD card.
2. Extract the ZIP on a computer.
3. Copy the folders inside `fonts/` to `/.fonts/` or `/fonts/` on the SD card.
4. Eject the card through the operating system and restart Duet.
5. Open **Settings > Reader > Font Family**.

The ZIP is large because each `.cpfont` contains rasterized glyph data for several styles and broad reading-language coverage. You do not need the complete pack to use custom fonts.

## Install only a few families

Use **Settings > Reader > Font Options > Manage Fonts** for the smaller 24-family compatibility catalog. You can also follow the upstream project link for any family in `FONT_PACK_SOURCES.tsv`, download only the source fonts you want, and convert them with Duet's checked-in `fontconvert_sdcard.py` tool.

## Contents

- `fonts/`: ready-to-copy family folders.
- `FONT_PACK_MANIFEST.tsv`: one row per `.cpfont`, including size, byte count, SHA-256, source group, source URL, revision, and license note.
- `FONT_PACK_SOURCES.tsv`: one row per family.
- `SHA256SUMS.txt`: checksum for every generated font file.
- `FONT_SOURCES.md`: the human-readable source and redistribution ledger from the Duet repository.
- `licenses/`: upstream copyright notices, license texts, and pinned project notices for the included source groups.

The pack contains only families on Duet's reviewed public allowlist. It does not scan or package arbitrary fonts found elsewhere on the maintainer's computer or SD cards.
