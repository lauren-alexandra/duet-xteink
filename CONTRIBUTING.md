# Contributing to Duet

Thank you for helping improve Duet. It is currently created and maintained by Lauren Landau as a one-person project, and community development help is explicitly welcome. You do not need a personal invitation to investigate an issue, improve documentation, propose a focused fix, or open a pull request. The project supports both the Xteink X3 and X4, two memory-constrained devices whose behavior can differ even when they run the same source.

Feedback is a contribution too. Bug reports, feature ideas, accessibility notes, confusing workflows, documentation corrections, performance observations, and successful test reports are all useful. Not every suggestion can be implemented, but every clear, good-faith report is welcome.

## Where Help Is Especially Welcome

- Embedded C++ and ESP32-C3 memory, task, storage, and power-management work.
- X3/X4 display behavior, e-ink refresh tuning, and device-specific diagnostics.
- Library, cover-cache, EPUB-layout, chapter-indexing, and input-latency performance.
- Reproducible physical-device testing across different SD cards and library sizes.
- Documentation, contributor tooling, CI, release packaging, accessibility, localization, and privacy-safe demo fixtures.

Small, well-tested repairs are valuable. For a larger feature or architectural change, open a Discussion first so Lauren can confirm that it fits Duet's direction and help avoid duplicated work.

## Before Starting

1. Search existing issues and discussions.
2. Open a discussion before beginning a large feature or architectural change.
3. Keep changes focused and preserve compatibility with both devices unless an issue is explicitly device-specific.
4. Never include copyrighted ebooks, personal reading data, credentials, device identifiers, private fonts, or another person's logs in a report or pull request.

Alpha reports are routed through the forms and labels described in [Duet Alpha Issue Triage](docs/ISSUE_TRIAGE.md).

## Pull Requests

A pull request should include:

- A concise description of the behavior changed.
- The reason for the change.
- X3 and X4 build results.
- Simulator tests or physical-device tests performed.
- Any memory, storage, rendering, migration, or rollback risk.
- Privacy-reviewed screenshots for visible changes. Fabricate reading data. Approved real titles or covers may appear inside the Duet interface, but do not attach EPUBs, extracted cover files, personal catalogs, or real device-state files.
- Updated credit and license records when code or design is adapted from another project.

Do not combine unrelated refactors with a functional fix. On-device behavior is the final acceptance test; a successful build is not proof that a device workflow is correct. Release-bound changes use the [maintainer release runbook](docs/MAINTAINER_RELEASE_RUNBOOK.md) and a completed physical acceptance record.

## License

By submitting a contribution, you agree that it may be distributed under Duet's MIT License and that you have the right to submit it. Existing third-party notices and file-specific licenses must remain intact.

## AI-Assisted Contributions

AI assistance is permitted, but the human contributor remains responsible for reviewing the code, testing it, verifying provenance, and describing any substantial generated or adapted material in the pull request.
