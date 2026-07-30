# Duet Maintainer Release Runbook

This is the maintainer-side path from a reviewed source tree to a public Duet alpha. It deliberately separates four different facts:

1. the source builds;
2. the simulator passes;
3. the firmware runs correctly on a physical X3 and X4;
4. the release is approved for publication.

None of those facts implies the next one.

## One-Time Public Repository Setup

Before the first public push:

1. Finish the review in `PUBLIC_RELEASE_READINESS.md`.
2. Explicitly approve the copy-suffix cleanup recorded in `REPOSITORY_HYGIENE_REVIEW.md`.
3. Follow `INITIAL_PUBLICATION_PLAN.md` to create the reviewed public root commit under the maintainer's GitHub noreply identity.
4. Preserve the private development history as a verified Git bundle outside the public repository.
5. Create the maintainer-owned GitHub repository.
6. Enable Discussions, private vulnerability reporting, and branch protection.
7. Create the labels listed in `docs/ISSUE_TRIAGE.md`.
8. Confirm the repository URLs in the README, issue forms, and release workflows point to the canonical repository.

Do not publish the private working history as the initial public history. It contains personal author email addresses and begins partway through the project's internal repair series.

## Candidate Naming

Duet uses semantic prerelease versions:

- `0.1.0-alpha.8` for the current public-alpha candidate;
- `0.1.0-alpha.9` for the next public alpha with changed firmware;
- `0.1.0-alpha.8-rc.1` for an optional release candidate.

Any firmware change after a candidate has been flashed must receive a distinguishable version. Do not silently replace a BIN while keeping the same version string.

The device filenames must be:

```text
Duet-X3-v<VERSION>.bin
Duet-X4-v<VERSION>.bin
```

## Local Preflight

Run from the repository root:

```bash
git status --short
git diff --check
python3 -m py_compile scripts/audit_public_release.py scripts/enrich_epub_locations.py scripts/generate_library_catalog.py scripts/generate_release_catalog.py scripts/package_release.py scripts/package_public_font_pack.py scripts/package_wordnet_dictionary.py
python3 -m unittest discover -s test -p "test_*.py"
python3 scripts/audit_public_release.py
python3 scripts/run_simulator_smoke_test.py --device x3
python3 scripts/run_simulator_smoke_test.py --device x4
pio run -e x3-public -e x4-public
```

Then verify:

- both BIN names contain the intended version;
- the firmware size checks pass;
- the simulator used fabricated reading data;
- no EPUB, extracted cover, personal catalog, device state, credential, or unreviewed font file entered the public tree;
- changed third-party code or design lineage is recorded in `THIRD_PARTY_NOTICES.md`;
- the matching release note exists at `docs/releases/v<VERSION>.md`.

## Package The Candidate

For an unaccepted physical-test package, use `--draft`:

```bash
python3 scripts/package_release.py \
  --version <VERSION> \
  --x3 .pio/build/x3-public/Duet-X3-v<VERSION>.bin \
  --x4 .pio/build/x4-public/Duet-X4-v<VERSION>.bin \
  --output-dir .pio/build \
  --draft
```

This produces:

- the X3 and X4 BINs;
- `SHA256SUMS.txt`;
- a firmware-only ZIP;
- installation, testing, troubleshooting, attribution, and license files.

Build the optional WordNet 3.0 dictionary asset from the reviewed StarDict source directory:

```bash
python3 scripts/package_wordnet_dictionary.py \
  --source-dir <WORDNET-STARDICT-DIR> \
  --output-dir .pio/build
```

The script accepts only the reviewed `.ifo`, `.idx`, `.dict`, and `.syn` hashes, adds the original WordNet license and copy instructions, and creates `Duet-WordNet-3.0-StarDict.zip`. Test the ZIP before publication:

```bash
unzip -t .pio/build/Duet-WordNet-3.0-StarDict.zip
```

Build the optional Duet Open Font Pack from the reviewed `.cpfont` root and positive allowlist:

```bash
python3 scripts/package_public_font_pack.py \
  --font-root "<REVIEWED-CPFONTS-ROOT>" \
  --config release/public-font-pack.json \
  --output .pio/build/Duet-Open-Font-Pack-v1.zip
unzip -t .pio/build/Duet-Open-Font-Pack-v1.zip
```

The packager must report 123 families, 738 files, sizes 10/12/14/16/18/20, and `.cpfont` version 4. Compare its printed SHA-256 with the digest shown by GitHub after upload.

Verify the hashes before either BIN reaches a device:

```bash
(cd .pio/build/Duet-v<VERSION> && shasum -a 256 -c SHA256SUMS.txt)
```

## Physical Acceptance

Copy `.github/maintainer/PHYSICAL_ACCEPTANCE_RECORD.md` for the exact candidate. Test the same commit and hashes on both devices.

Minimum acceptance:

- clean install/update and version display;
- boot, Home, book open, page turn, Home return, sleep, and wake;
- 2x2, 3x3, and 4x4 grids plus carousel;
- input while first-use covers are hydrating;
- forward and backward page lookahead;
- book More Info and search;
- chapter transition and pre-index cancellation;
- stats pages, achievements, and active-book selection;
- Nearby stats sync and explicit position sync;
- X4 settled-screen ghosting;
- crash report inspection after the run.

A result may be `pass`, `pass with known issue`, or `fail`. Record actual timings and attach privacy-reviewed photos where visual behavior matters.

Do not mark a release accepted while either device has an unexplained crash, boot loop, data-loss path, incompatible sync result, or inaccessible recovery path.

## Final Media

After the accepted firmware is frozen:

1. regenerate every simulator stats page using the rich dummy fixture;
2. review all labels, clipping, navigation, and values;
3. capture native X3 and X4 screenshots;
4. take the approved real-device photos and unsped videos;
5. remove metadata from public images;
6. verify no personal data or unpublished device files are present.

Draft media made against an earlier UI is planning material, not release media.

## GitHub Prerelease

For the first alpha, prefer a reviewed manual dispatch:

1. Merge the accepted source to the protected default branch.
2. Run **Build Duet Release** with the exact `<VERSION>`.
3. Confirm the workflow audit, X3 build, X4 build, package, checksums, and catalog generation all succeeded.
4. Download the workflow artifacts and compare their SHA-256 hashes with the physically accepted BINs.
5. Review the generated draft GitHub release.
6. Upload the reviewed optional dictionary and font assets to the draft with `gh release upload v<VERSION> .pio/build/Duet-WordNet-3.0-StarDict.zip .pio/build/Duet-Open-Font-Pack-v1.zip`.
7. Verify the release is marked **prerelease**.
8. Confirm the release lists the separate firmware, font-pack, and dictionary digests.
9. Review release notes, known issues, recovery instructions, credits, and attached files.
10. Publish only after the hashes match the accepted record.

The release workflow creates a draft. A maintainer must still perform the acceptance and hash comparison before publication. The public update catalog is attached to the draft but is not written to the website source until the GitHub release is actually published. Publication opens a catalog-update pull request so branch protection and human review remain intact.

## After Publication

1. Install the published BINs on one X3 and one X4 as a download-path sanity check.
2. Confirm the release page links, firmware SHA-256 file, font-pack digest, and dictionary digest.
3. Review and merge the post-publication catalog pull request.
4. Open the tester announcement only after downloads are verified.
5. Triage reports using `docs/ISSUE_TRIAGE.md`.
6. Keep unresolved known issues visible in the release notes.

## Rollback And Hotfixes

Keep the last accepted X3 and X4 BINs available.

If a candidate causes crashes, boot loops, data loss, or recovery trouble:

1. unpublish or clearly mark the affected release;
2. pin a warning in the release notes and tester discussion;
3. tell testers which prior version and recovery path to use;
4. preserve crash logs and the failing BIN hash;
5. fix on a new version number;
6. repeat simulator and physical acceptance from the beginning.

Never overwrite a published asset with different bytes under the same filename.
