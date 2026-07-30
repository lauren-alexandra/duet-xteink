---
title: EPUB WPM Preparation
nav_order: 4
---

# EPUB WPM Preparation

Plain EPUBs read normally in Duet. True visible words-per-minute and reference-page statistics require compatible word-location metadata at `META-INF/x-locations.json` inside the EPUB.

Run Duet's desktop enrichment step once after adding new EPUBs, before expecting WPM to populate. The tool counts words by spine item, writes the location manifest Duet understands, and skips books that already contain compatible metadata unless `--force` is supplied.

Duet does not fake WPM from Xteink screen pages per minute. Screen-page pace remains an internal signal for time-left and reading-rhythm estimates when true word locations are unavailable.

## Recommended Order

1. Keep an untouched backup of the source library.
2. Enrich the source EPUBs on the computer before copying them to both readers when practical.
3. Choose a separate backup folder outside Ready to Load and outside the SD-card book folder.
4. Copy or organize the enriched EPUBs on each device.
5. Run [Desktop Cover Prefill](COVER_PREFILL.md) after the final paths are in place.

## Dry Run First

From the Duet repository root:

```bash
python3 scripts/enrich_epub_locations.py --dry-run "/path/to/epub-or-folder"
```

The scan reports `would-write`, `skipped`, and `failed` files without changing an EPUB.

## Apply With Backups

```bash
python3 scripts/enrich_epub_locations.py --backup --backup-dir "/path/to/backup-folder" "/path/to/epub-or-folder"
```

Before rewriting each EPUB, `--backup` creates a `.duetbak` copy. `--backup-dir` keeps those copies in a separate folder instead of cluttering a device-ready shelf or SD card, and adds a stable short source identifier so duplicate EPUB filenames from different subfolders cannot collide. Existing enriched EPUBs are skipped.

## Deliberately Regenerate Existing Metadata

Use `--force` only when intentionally refreshing manifests:

```bash
python3 scripts/enrich_epub_locations.py --force --backup --backup-dir "/path/to/backup-folder" "/path/to/epub-or-folder"
```

## Confirm Success

A successful run exits with status `0`. Review the final summary and confirm `failed=0`. Updated EPUBs contain `META-INF/x-locations.json` with:

- `format` set to `x-locations`.
- `version` set to `1`.
- Positive `totalWords` and `totalLocations`.
- Spine entries containing `wordStart` and `wordCount`.

After reading measurable progress for measurable time, Current Book and Home can show WPM. Pace Trend, Reader DNA, and Reading Signature can include qualifying ledger entries for that enriched current book without opening the EPUB while the statistics page renders. A dash or omitted historical entry remains correct when Duet does not have enough attributable time, progress, or location metadata to calculate it.

The release-prep implementation was validated on a 324-EPUB shelf with separate `.duetbak` output. The first pass exposed one URL-escaped OPF path, the resolver was corrected to decode it, and the final dry run reported 324 skipped enriched books and zero failures.

## Safety

- Run `--dry-run` first.
- Use `--backup` for the first modifying pass.
- Prefer `--backup-dir` outside Ready to Load and outside the SD-card book folder. Create adjacent backups only when you explicitly want them there.
- Keep the original library backup until the enriched files open correctly.
- Do not use `--force` as routine maintenance.
- Never upload, publish, or paste EPUB contents into an issue, chat, or test report.
- Review paths before sharing terminal output because filenames may reveal personal library information.

## Have A Computer Assistant Prepare The Library

The complete copy-ready prompt for Codex, Claude CoWork, Perplexity Computer, or another local computer assistant is in [AI Library Prep Prompt](AI_LIBRARY_PREP_PROMPT.md). It covers EPUB enrichment first and model-specific cover prefill second.
