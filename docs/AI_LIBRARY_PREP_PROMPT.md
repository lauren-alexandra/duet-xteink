# AI Library Prep Prompt

This prompt is suitable for Codex, Claude CoWork, Perplexity Computer, or another assistant that can inspect local files and run terminal commands. It performs Duet's two computer-side library preparation steps in the correct order: EPUB word-location enrichment, then model-specific cover prefill.

```text
Help me prepare my EPUB library for Duet on my Xteink reader.

Before changing anything:
1. Locate the Duet source checkout and read docs/EPUB_LOCATION_ENRICHMENT.md, docs/COVER_PREFILL.md, and any repository agent instructions.
2. Identify the source EPUB folder or mounted reader card I want prepared. If a mounted card is involved, confirm whether it belongs to an Xteink X3 or X4. Do not infer the model from card capacity alone.
3. Confirm there is a separate backup of the EPUBs or complete SD card. Do not delete, move, rename, reorganize, or expose any books or reader data.
4. Confirm Python 3 is available. For cover prefill, also confirm ImageMagick's magick command is available.

EPUB word-location enrichment:
5. From the Duet repository root, run:
   python3 scripts/enrich_epub_locations.py --dry-run "/path/to/epub-or-folder"
6. Report the dry-run counts for would-write, skipped, and failed without quoting or exposing EPUB contents.
7. If the dry run has failures, stop and explain them before modifying anything.
8. Choose a separate backup folder outside Ready to Load and outside any SD-card book folder, confirm it has enough free space, then run:
   python3 scripts/enrich_epub_locations.py --backup --backup-dir "/path/to/backup-folder" "/path/to/epub-or-folder"
9. Do not use --force unless I explicitly ask to regenerate existing x-locations manifests. If I do, use:
   python3 scripts/enrich_epub_locations.py --force --backup --backup-dir "/path/to/backup-folder" "/path/to/epub-or-folder"
10. Verify the modifying run exits successfully with failed=0. Plain EPUBs remain readable, while META-INF/x-locations.json enables true visible WPM and reference-page statistics. Do not substitute screen pages/minute and call it WPM.

Cover prefill for a mounted reader card:
11. If preparing a mounted card, run scripts/prefill_cover_thumbnails.py against that card with the correct --device x3 or --device x4 argument.
12. Preserve every valid existing thumbnail. Do not use --force unless I explicitly request a complete rebuild.
13. If a book has no usable embedded cover, report it. Use --cover-override MATCH=IMAGE_PATH only when I provide or approve the image.
14. Never copy .duet, .crossink, .crosspoint, reading stats, settings, or caches from one device's card to the other. Run the prefill independently for each card.
15. Read /.duet/state/desktop_cover_prefill.json and confirm failed_books is empty, generated + valid_existing equals book_count * 12, and the manifest device matches the physical reader.
16. On macOS, check the copied book folders for AppleDouble files whose basenames begin `._`, especially `._*.epub`. Confirm they are metadata sidecars rather than real books, remove only those sidecars, and verify the visible EPUB count again.

Before saying it is finished:
17. Report the EPUB enrichment counts, separate backup location, cover-prefill counts if run, mounted card path, device model, AppleDouble cleanup count, failures, and manifest path.
18. Do not upload, print, summarize, or otherwise expose EPUB text, cover files, personal catalogs, reading history, or credentials.
19. Do not eject the card unless I ask you to.
```
