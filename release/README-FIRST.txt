DUET v@DUET_VERSION@ - READ THIS FIRST

Duet is alpha firmware for the Xteink X3 and X4. It is an independent, one-person project created and maintained by Lauren Landau, with upstream work credited in the included notices.

USE THE CORRECT FILE

  Xteink X3: Duet-X3-v@DUET_VERSION@.bin
  Xteink X4: Duet-X4-v@DUET_VERSION@.bin

Never flash the X3 file onto an X4 or the X4 file onto an X3.

BEFORE FLASHING

1. Back up the complete SD card.
2. Keep the last firmware BIN that worked on your reader.
3. Verify the downloaded BIN against SHA256SUMS.txt.
4. Preserve .duet, .crossink, .crosspoint, books, fonts, dictionaries, and sleep images.
5. Put exactly one firmware BIN at the SD-card root.

INSTALL

First install or recovery:
Use the CrossPoint web installer's Custom .bin option.

Upgrade from Duet:
Settings > System > SD Firmware Update.

After flashing, verify Settings > System shows:

  Duet @DUET_VERSION@

Open one known-good book and test sleep/wake before a long reading session.

LARGE LIBRARIES

Plain EPUBs read normally, but true visible WPM and reference-page statistics require Duet word-location metadata. Prepare new EPUBs before cover prefill:

  python3 scripts/enrich_epub_locations.py --dry-run "/path/to/epub-or-folder"
  python3 scripts/enrich_epub_locations.py --backup --backup-dir "/path/to/backup-folder" "/path/to/epub-or-folder"

The tool skips already enriched EPUBs. Use --force only when deliberately regenerating existing metadata. Keep the backup folder outside Ready to Load and outside the SD-card book folder so .duetbak files do not clutter the device library.

Duet can create missing covers on-device. For a large or multiply organized library, run the desktop cover prefill once after loading books:

  python3 scripts/prefill_cover_thumbnails.py "/Volumes/XTeink X3" --device x3
  python3 scripts/prefill_cover_thumbnails.py "/Volumes/XTeink X4" --device x4

Run only the command matching the card. Never copy .duet, .crossink, or .crosspoint from one reader to the other. Confirm failed_books is empty in:

  /.duet/state/desktop_cover_prefill.json

After a macOS copy, remove only confirmed AppleDouble sidecars named ._* from the card's book folders. A file such as ._Book.epub is metadata, not a second book, but its suffix can double the apparent library count.

ALPHA NOTES

- First-time cover generation may be slow without desktop prefill.
- Complex EPUB chapters may visibly index.
- X3 and X4 must be tested separately.
- Keep both readers on Nearby Stats Sync until both report success.
- Complete .cstats export/restore passes non-empty, content-level X3 and X4 simulator round trips. Physical-card restore remains an alpha acceptance item, so keep a normal SD-card backup and treat it as the primary recovery copy until that check is complete.
- Filesystem damage can imitate firmware crashes. Back up and verify a card before deleting hidden state or reformatting it.

RECOVERY

If an update causes a boot loop or prevents opening/sleeping:

1. Remove the SD card and boot once.
2. If needed, flash the last known-good BIN through the web installer.
3. Do not delete hidden state folders while diagnosing.

Official source and releases:
https://github.com/lauren-alexandra/duet-xteink

Full alpha guidance:
docs/ALPHA_TESTING.md

Complete feature and lineage catalog:
FEATURES.md

User guide:
USER_GUIDE.md

Fast alpha.7 device acceptance route:
docs/ALPHA7_ACCEPTANCE_QUICKSTART.md

Installation and recovery:
docs/installation.md

Cover prefill:
docs/COVER_PREFILL.md

EPUB WPM preparation:
docs/EPUB_LOCATION_ENRICHMENT.md

Combined computer-assistant library-prep prompt:
docs/AI_LIBRARY_PREP_PROMPT.md

Troubleshooting:
docs/troubleshooting.md

Credits and licenses:
AUTHORS.md
THIRD_PARTY_NOTICES.md
LICENSE

REPORT RESULTS

Complete successful, mixed, or failed alpha session:
https://github.com/lauren-alexandra/duet-xteink/issues/new?template=alpha_test_report.yml

One reproducible defect:
https://github.com/lauren-alexandra/duet-xteink/issues/new?template=bug_report.yml

Questions and ideas:
https://github.com/lauren-alexandra/duet-xteink/discussions
