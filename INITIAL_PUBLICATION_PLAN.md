# Initial Public Repository Plan

## Recommendation

Publish Duet from a new, reviewed root commit rather than pushing the current 78-commit development history.

This is an attribution-preserving clean publication, not an attempt to hide Duet's lineage. The public tree keeps the MIT license, original copyright notices, project authorship, pinned upstream sources, and feature-level credit. The private development history remains archived for provenance and recovery.

## Why

The current local history:

- contains 78 commits;
- uses a personal Gmail address in 76 commit author records;
- begins at an internal `Repair11` snapshot rather than the original upstream project's history;
- contains internal Repair numbers, device debugging, handoff notes, and iterative branding experiments; and
- has no configured remote in this isolated release-prep repository.

Publishing that history would expose personal contact information without providing a complete upstream lineage. A reviewed root commit is both clearer for users and safer for the maintainer.

## What Preserves Credit

The public root commit must include:

- `AUTHORS.md`
- `PROJECT_IDENTITY.md`
- `LICENSE`
- `NOTICE`
- `THIRD_PARTY_NOTICES.md`
- `FONT_SOURCES.md`
- every redistributed third-party license under `licenses/`
- source links and pinned revisions for adapted firmware features

The initial commit author should be:

```text
Lauren Landau <284394660+lauren-alexandra@users.noreply.github.com>
```

The canonical public repository should remain:

```text
https://github.com/lauren-alexandra/duet-xteink
```

## Publication Sequence

1. Finish the stats export/restore repair and the remaining X3/X4 acceptance matrix.
2. Confirm the approved 39-file copy-suffix cleanup remains reflected in the reviewed tree.
3. Complete the public font-pack decision or clearly defer it to a later, separately licensed release.
4. Regenerate final screenshots and verify their data policy.
5. Run the public audit, tests, simulator checks, and both public builds.
6. Create a private Git bundle of the full development repository outside the public tree.
7. Record the bundle's SHA-256 and keep it with the private release-prep archive.
8. Export only the reviewed working tree into a new repository or orphan `main` branch.
9. Configure the GitHub noreply author identity locally.
10. Stage files from an explicit reviewed manifest, never `git add .`.
11. Create one root commit for the first public alpha.
12. Add the Duet repository as `origin` and the relevant CrossInk repository as read-only `upstream`.
13. Push only after the GitHub repository settings and branch protections are reviewed.
14. Build the prerelease from that exact public commit and verify artifact checksums against the release page.

## Suggested Root Commit

```text
Initial public alpha: Duet for Xteink X3 and X4

Publish the reviewed Duet firmware, documentation, attribution, build
automation, tester guidance, and device-specific release targets.
```

The public semantic version should use the actual accepted release candidate, not an internal Repair number.

## GitHub Settings Before First Push

- Default branch: `main`
- Issues: enabled
- Discussions: optional but useful for tester questions
- Wiki: disabled unless there is a real maintenance plan
- Releases: enabled
- Require pull requests for `main`
- Require the public CI workflow to pass
- Block force pushes and branch deletion on `main`
- Enable private vulnerability reporting
- Use the issue forms included in `.github/ISSUE_TEMPLATE/`
- Mark the first tester release as a prerelease

## Explicit Non-Actions

This plan does not:

- delete any files;
- rewrite or discard the private development repository;
- create a commit;
- add a remote;
- push to GitHub;
- publish firmware; or
- stage either SD card.
