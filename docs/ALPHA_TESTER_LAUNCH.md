# Reddit Soft-Launch Draft

## Title

Duet: looking for Xteink X3/X4 alpha testers for a reading-focused CrossInk fork

## Post

Hi! I've been building **Duet**, an open-source CrossInk fork for the Xteink X3 and X4, entirely as a one-person project. I am its creator, designer, maintainer, physical-device tester, documentation department, and person currently asking two tiny e-readers to do an unreasonable number of things. I think it is ready for a small, honest alpha test rather than a grand “everything is finished” launch.

Duet is for people who want these tiny readers to feel like a real personal library: cover grids and a carousel, smart title/author/series search, book descriptions, custom dashboards, a much larger reading-stats system, custom fonts and dictionaries, bookmarks and clippings, configurable reader controls, sleep screens, achievements, and direct X3/X4 stats and position sync.

The current source-verified inventory includes eight Home themes, 6 book-browser layouts, 33 top-level statistics pages, 22 launcher destinations, 12 sleep modes, and 108 persistent achievement milestones. The complete public story is in `docs/DUET_FULL_FEATURE_TOUR.md`, the canonical technical inventory is in `FEATURES.md`, and `THIRD_PARTY_NOTICES.md` records the exact audited upstream revisions and direct versus design-only adaptation.

The project is open source under MIT. Forks are welcome. The official source and releases are:

https://github.com/lauren-alexandra/duet-xteink

The current tester build is **v0.1.0-alpha.6** with separate, clearly named X3 and X4 BINs. Please only test if you are comfortable backing up an SD card, keeping a rollback BIN, and collecting a small log if something goes wrong.

I genuinely want all the useful feedback I can get: bugs, confusing button behavior, features you wish existed, accessibility needs, documentation that does not make sense, performance differences between cards or devices, visual details that feel off, and things Duet already does especially well. You do not need to be a firmware developer to notice something valuable. If you are a developer and would like to help investigate, fix, document, test, or improve any part of Duet, I would genuinely love that too. Focused pull requests are welcome, and the contributor guide identifies the areas where another pair of technical eyes would help most.

### What I most need tested

- Grid and carousel navigation while covers are still loading.
- Forward and backward grid-page changes after neighboring pages prehydrate.
- Large libraries, slow cards, and folders with hundreds or thousands of book paths.
- Chapter changes and background next-chapter indexing.
- Sleep/wake, book resume, and repeated open/close use over several days.
- Nearby X3/X4 stats sync and explicit position sync in both directions.
- Font styles/sizes, dictionary lookup, More Info, search, and all stats pages.

### Known rough edges

- Building covers on the reader can still be slow in huge libraries.
- Complex EPUB chapters may visibly index when background work cannot finish.
- X3 and X4 can behave differently under the same library load.
- Alpha.6 writes new state under `/.duet` and imports inherited `/.crossink`, `/.crosspoint`, and `/.crossink-stats-backup` data without deleting the original sources. The first-boot migration and second-boot preservation checks are part of the acceptance route.
- Complete stats archives now use the canonical namespace while preserving legacy-path recovery. Non-empty content-level round trips pass both simulators, but physical X3/X4 export and restore remain part of the alpha acceptance matrix.
- Alpha.6 includes the device/version-gated X4 post-install cleanup introduced after alpha.2. The current path still needs broader physical testing after SD-card updates, from Home and inside a book, and across repeated sleep/wake cycles.
- Current-book speed is shown as estimated WPM. Historical aggregate trend records still use relative screen-page pace internally because older journal entries do not contain the word totals needed to reconstruct WPM.
- A damaged SD-card filesystem can imitate firmware bugs because the reader's books and state live on the card. The tester guide explains how to back up and verify a card before deleting caches or reformatting.
- This is exactly the stage where a successful simulator/build is not enough; I need evidence from more physical readers and SD cards.

For large or multiply organized libraries, there is an optional desktop cover prefill. It extracts each EPUB's real cover once on your computer and writes the exact X3/X4 grid and carousel thumbnails Duet will request. The pass is incremental, and the firmware still generates covers for genuinely new books. The repo includes exact commands plus a ready-to-paste prompt for Codex, Claude CoWork, Perplexity Computer, or another local computer assistant.

Please do not post ebooks, complete SD-card archives, credentials, or raw personal reading history. A good report needs the device, exact version, card details, folder size, view and sort mode, prefill state, button sequence, expected/actual result, and the smallest relevant timing or crash log.

The release includes a short ordered alpha.6 acceptance route as well as the full device matrix. Neither X3 nor X4 will be marked physically accepted from simulator results alone.

This project exists because CrossPoint Reader, CrossInk, and several other community forks shared excellent work. The README and third-party notices map the feature lineage in detail, including CrumBLE, CPR-vCodex, CrossInk Carousel, CrossPet, Biscuit, SEEK, and others.

I am currently the only maintainer, so I will keep the first tester group intentionally small. That does not mean I want to do every piece alone forever: developers who want to help with embedded C++, performance, X3/X4 hardware behavior, documentation, tooling, accessibility, or release QA are very welcome. I will keep alpha issues and release notes public and publish frequent, device-matched updates as fixes are verified. I would love a few patient testers who enjoy both reading and pressing suspicious combinations of buttons. I cannot promise to implement every idea or merge every patch, but I do want each thoughtful report and contribution to have a clear public place to land.

## Reddit And GitHub Media Story

Use `docs/DUET_FULL_FEATURE_TOUR.md` as the complete feature and lineage tour. The README is the project front page, and the Reddit post is the shorter invitation rather than a competing description of Duet.

Publish the media in this order:

1. **Hero pair:** X3 and X4 together on two different Duet Home themes.
2. **Library:** a fully hydrated cover grid beside the five-cover carousel, followed by a short unsped clip showing input during hydration.
3. **Two readers, one history:** Nearby Sync on both devices with fabricated reader names and statistics.
4. **Statistics lab:** Reader DNA, heatmap, timeline, and one dense overview page using the fabricated public fixture.
5. **Reading tools:** the in-reader quick overlay, dictionary lookup, and More Info.
6. **Personalization:** Home Stats selection, font A/B comparison, and an achievement screen.
7. **Performance:** a short real-time book-open, chapter-transition, and return-to-Home sequence.
8. **Identity and trust:** the System page with the exact Duet version, plus visible links to installation, recovery, known issues, and the complete credits chain.

The optional 130-family development font collection may be shown as a separate SD-card system, but it must not be described as bundled with the initial firmware-only alpha. The public font pack remains a later, separately reviewed and licensed download.

GitHub should carry the complete feature and lineage story: Nearby Sync, the statistics lab, Home customization, library views and search, dictionaries, fonts, achievements, performance work, Clean Library Cache, desktop cover prefill, installation/recovery, and detailed credit to CrossPoint Reader, CrossInk, CrossInk Carousel, CrumBLE, CPR-vCodex, CrossPet, Biscuit, SEEK, and aalu's reading-stats work.

Invite broad feedback in the Reddit post and README, then route it cleanly: GitHub Discussions for questions, ideas, UX impressions, accessibility, documentation, and visual feedback; Alpha Test Reports for complete or mixed test sessions; Alpha Bug Reports for one reproducible defect.

## Suggested First Comment

Current test notes, install/recovery steps, cover-prefill instructions, useful logs, and privacy guidance are all linked from the release. Please mention whether you have an X3, X4, or both when volunteering.

```text
Device: X3 / X4 / both
Current firmware:
Approximate library size:
Largest folder:
SD card brand and capacity:
Comfortable backing up, flashing, and rolling back: yes / no
Can collect privacy-reviewed logs and an occasional photo or short video: yes / no
Most interested in testing:
```

## First-Wave Plan

Start with roughly six to ten testers rather than distributing the candidate as if it were stable. Aim for:

- At least two X3-only testers.
- At least two X4-only testers.
- At least two people with both devices who can test sync in both directions.
- A mix of small libraries, several-hundred-book libraries, and at least one library with a very large folder.
- A mix of SD-card brands and capacities.
- At least one experienced CrossPoint/CrossInk user and one careful newer user.

Distribute builds through a clearly marked GitHub prerelease, not individual DM attachments. Keep the previous known-good BIN, checksums, backup steps, recovery instructions, known issues, and report form beside the download. Collect technical reports in GitHub Issues so fixes and credit remain public; use the Reddit thread for announcements, quick questions, and directing people to the canonical project. Use **Alpha Test Report** for successful or mixed test sessions and **Alpha Bug Report** for a reproducible defect. Questions and feature ideas belong in GitHub Discussions.
