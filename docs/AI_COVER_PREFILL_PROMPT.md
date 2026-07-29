# AI Cover Prefill Prompt

This prompt is suitable for Claude CoWork, Codex, Perplexity Computer, or another assistant that can inspect local files and run terminal commands. It is also included directly in the [Desktop Cover Prefill](COVER_PREFILL.md) guide.

```text
Help me prefill Duet book-cover thumbnails on my mounted Xteink SD card.

Before changing anything:
1. Locate the Duet source checkout and read docs/COVER_PREFILL.md plus any repository agent instructions.
2. Identify the mounted card and confirm whether it belongs to an Xteink X3 or X4. Do not infer the model from card capacity alone. If the model is ambiguous, stop and ask me.
3. Confirm the card is backed up. Do not delete, move, rename, or reorganize books or any reader data.
4. Confirm Python 3 and ImageMagick's `magick` command are available.

Then:
5. From the Duet repository root, run scripts/prefill_cover_thumbnails.py against the mounted card with the correct --device x3 or --device x4 argument.
6. Keep all SD-card work in this desktop process. Do not modify firmware code or start device-side background work.
7. Preserve every valid existing thumbnail. Do not use --force unless I explicitly request a complete rebuild.
8. If a book has no usable embedded cover, report it. Use --cover-override MATCH=IMAGE_PATH only when I provide or approve the image.
9. Never copy .duet, .crossink, .crosspoint, reading stats, settings, or caches from one device's card to the other. Run the prefill independently for each card.

Before saying it is finished:
10. Verify the script exited successfully.
11. Read /.duet/state/desktop_cover_prefill.json on the card.
12. Confirm failed_books is empty and that generated + valid_existing equals book_count * 12.
13. Confirm the manifest's device value matches the physical reader.
14. Report the mounted card path, device model, EPUB-path count, generated thumbnail count, preserved thumbnail count, failures, and manifest path.
15. Do not eject the card unless I ask you to.
```
