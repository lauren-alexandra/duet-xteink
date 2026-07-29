# Repository Hygiene Review

## Scope

This review covers the untracked files whose names contain a copy suffix such as `README 2.md` or `ci 2.yml`. These files must not be included through a blanket `git add .`.

## Result

All 39 copy-suffix files are byte-for-byte identical to the corresponding file in the repository's current `HEAD` commit.

The canonical files without ` 2` have since been edited for Duet. The copies therefore contain no unique work and preserve only the older pre-edit state already available from Git.

The source copy is also an active build blocker. A public X3 build on July 23, 2026 compiled both `Ssd1677Driver.cpp` and `Ssd1677Driver 2.cpp`, then failed at link time with multiple definitions of the driver constructor, display, refresh, grayscale, RAM, and sleep methods. The X4 build began compiling the same duplicate before the redundant run was stopped. This is deterministic repository behavior, not a device-specific firmware defect.

Approved disposition:

- Keep every canonical file without the ` 2` suffix.
- Remove all 39 copy-suffix files before the initial public commit.
- Do not restore any canonical file from its copy.
- Run the public-release audit and `git diff --check` again afterward.
- Re-run both `x3-public` and `x4-public`; the prior dual build cannot pass while the duplicate driver source remains.

Lauren approved the removal after reviewing this evidence. All 39 copy-suffix files were removed on July 23, 2026. The canonical files and Git history were left intact.

## Reviewed Files

### GitHub configuration

- `.github/ISSUE_TEMPLATE/bug_report 2.yml`
- `.github/workflows/ci 2.yml`
- `.github/workflows/release 2.yml`
- `.github/workflows/release-fonts 2.yml`
- `.github/workflows/release_candidate 2.yml`

### Repository documents

- `CHANGELOG 2.md`
- `FEATURES 2.md`
- `GOVERNANCE 2.md`
- `LICENSE 2`
- `PRODUCT 2.md`
- `PUBLIC_RELEASE_READINESS 2.md`
- `README 2.md`
- `SCOPE 2.md`
- `THIRD_PARTY_NOTICES 2.md`
- `USER_GUIDE 2.md`

### Documentation

- `docs/_config 2.yml`
- `docs/catalog 2`
- `docs/controls 2.md`
- `docs/data-cache 2.md`
- `docs/epub-render-modes 2.md`
- `docs/file-formats 2.md`
- `docs/font-build-variants 2.md`
- `docs/hyphenation-trie-format 2.md`
- `docs/i18n 2.md`
- `docs/index 2.md`
- `docs/installation 2.md`
- `docs/nearby-position-sync 2.md`
- `docs/reader-features 2.md`
- `docs/reading-stats-sync 2.md`
- `docs/sd-card-fonts 2.md`
- `docs/simulator 2.md`
- `docs/troubleshooting 2.md`
- `docs/webserver 2.md`
- `docs/webserver-endpoints 2.md`
- `docs/contributing/README 2.md`
- `docs/contributing/architecture 2.md`
- `docs/contributing/getting-started 2.md`
- `docs/contributing/testing-debugging 2.md`

### Source

- `freeink-sdk/libs/display/FreeInkDisplay/src/driver/Ssd1677Driver 2.cpp`

## Verification Method

For each untracked copy:

1. Derive the canonical path by removing ` 2` before the extension.
2. Confirm the canonical path exists in `HEAD`.
3. Compare the untracked file byte-for-byte with `git show HEAD:<canonical>`.
4. Record `MATCHES_HEAD` only when the comparison succeeds.

Result: `39 MATCHES_HEAD`, `0 DIFFERS_FROM_HEAD`, and `0 NO_HEAD_FILE`.
