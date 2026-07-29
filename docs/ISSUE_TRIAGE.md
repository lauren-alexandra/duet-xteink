# Duet Alpha Issue Triage

The alpha needs reports about successful sessions as much as bug reports. Successes show which device, SD-card, and library combinations are already stable.

## Intake

Use:

- **Alpha Test Report** for a complete successful, mixed, or failed test session;
- **Alpha Bug Report** for one reproducible defect;
- **Discussions** for questions, ideas, setup help, and behavior that has not yet been reproduced;
- **Private vulnerability reporting** for security issues.

Do not ask testers to upload EPUBs, extracted covers, full SD-card images, credentials, personal catalogs, or unredacted device-state archives.

## Initial Labels

Create these labels before inviting testers:

| Label              | Purpose                                            |
| ------------------ | -------------------------------------------------- |
| `alpha`            | Report applies to an alpha build                   |
| `bug`              | One reproducible defect                            |
| `needs-triage`     | Maintainer has not yet reproduced or classified it |
| `needs-info`       | A specific missing detail blocks reproduction      |
| `confirmed`        | Reproduced or supported by sufficient evidence     |
| `device:x3`        | Xteink X3                                          |
| `device:x4`        | Xteink X4                                          |
| `area:reader`      | Book opening, layout, page turns, chapters         |
| `area:library`     | Lists, grids, carousel, search, More Info          |
| `area:stats`       | Reading data, dashboards, charts, achievements     |
| `area:sync`        | Nearby stats or position sync                      |
| `area:sleep`       | Sleep, wake, lock images, ghosting                 |
| `area:fonts`       | Font picker, sizing, styles, cache                 |
| `area:dictionary`  | StarDict install, index, lookup                    |
| `area:install`     | Flashing, update, recovery, version display        |
| `performance`      | Lag, load time, memory pressure, watchdog          |
| `data-integrity`   | Incorrect, missing, duplicated, or damaged state   |
| `crash`            | Crash, reboot, boot loop, or watchdog reset        |
| `privacy`          | Report or asset needs privacy review               |
| `good-first-issue` | Small, isolated, documented contribution           |
| `upstream`         | Root cause or fix belongs in an upstream project   |
| `duplicate`        | Already tracked elsewhere                          |
| `wont-fix`         | Deliberate project decision with explanation       |

Keep priority separate from popularity:

| Label               | Meaning                                                |
| ------------------- | ------------------------------------------------------ |
| `priority:critical` | Data loss, boot loop, unrecoverable device, security   |
| `priority:high`     | Frequent crash or core reading workflow blocked        |
| `priority:medium`   | Material defect with a workable recovery or workaround |
| `priority:low`      | Cosmetic issue, small inconvenience, or future polish  |

## Triage Order

1. Protect the tester: identify recovery or data-loss risk first.
2. Confirm device, exact Duet version, installation path, and BIN hash.
3. Separate X3-only, X4-only, and shared behavior.
4. Ask for the smallest privacy-safe evidence that can answer the next diagnostic question.
5. Reproduce against the same version before changing unrelated code.
6. Record whether the issue is new, inherited, or adapted-code behavior.
7. Link duplicates to one canonical issue.
8. Close only with a reason and the version containing the fix.

## Useful Performance Evidence

For lag or freezing, ask for:

- folder size and library view;
- first visit or warmed cache;
- cursor responsiveness before covers appear;
- seconds to first placeholder, first cover, complete page, and next input;
- whether moving forward and backward differ;
- free-space and SD-card type;
- any Duet timing log specifically requested by the maintainer.

“Slow” is valid user feedback. Timings help locate the cause; they are not a requirement for believing the tester.

## Maintainer Response Shape

A useful first response says:

1. whether the risk requires stopping use or restoring a prior build;
2. what is already known;
3. the one next piece of evidence needed;
4. whether the issue is confirmed, under investigation, or awaiting details.

Avoid claiming a fix from build or simulator success. Close a device defect only after the relevant physical acceptance has passed.
