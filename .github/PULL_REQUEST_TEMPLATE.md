## Summary

What behavior changes, and why is it needed?

## Scope

List the important files or subsystems touched. Note whether the change is shared by X3 and X4 or intentionally device-specific.

## Verification

| Check                    | X3      | X4      | Simulator |
| ------------------------ | ------- | ------- | --------- |
| Builds                   | Not run | Not run | Not run   |
| Relevant workflow tested | Not run | Not run | Not run   |
| Regression checks        | Not run | Not run | Not run   |

Include exact commands and physical-device steps. A successful build alone is not physical acceptance.

## Device And Data Risk

- Memory or allocation impact:
- SD-card reads/writes or task ownership:
- Cache or persisted-format changes:
- Migration behavior:
- Recovery or rollback path:

## Visual Evidence

Attach privacy-reviewed screenshots for visible changes. Approved real titles or covers may appear inside the Duet interface, but do not attach EPUBs, extracted cover files, personal catalogs, credentials, device identifiers, or real reading-state files.

## Provenance

Identify any code, design, algorithm, font, or asset adapted from another project. Include its URL, license, pinned revision, and the notice files updated by this pull request.

## Checklist

- [ ] The change is focused and unrelated refactors are excluded.
- [ ] X3 and X4 behavior was considered.
- [ ] Tests, docs, and release notes were updated where needed.
- [ ] Compatibility with existing `/.crossink` and `/.crosspoint` state was considered.
- [ ] No ebooks, credentials, private fonts, personal data, or device-state files are included.
- [ ] Third-party licenses and credits are complete.
- [ ] Remaining physical-test gaps are stated plainly.
