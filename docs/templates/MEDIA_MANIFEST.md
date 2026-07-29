# Duet Public Media Manifest

Create one row for every image or video considered for publication. A file is approved only after its visible content, metadata, source build, and data provenance have all been checked.

## Capture Session

- Duet version:
- Commit:
- Capture date:
- Reviewer:
- X3 artifact and SHA-256:
- X4 artifact and SHA-256:
- Simulator fixture revision:

## Files

| File | Device | Screen | Data | Covers | Version visible | OCR checked | Metadata checked | Approved |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
|  | X3 / X4 / simulator / web |  | Fabricated / none | Real in UI / fixture / none | Yes / paired capture | Yes / No | Yes / No | Yes / No |

## Required Notes

For each approved file, record:

- Whether every statistic, streak, session, achievement, device name, and sync result is fabricated.
- Whether real book titles or covers are visible only as part of the Duet interface.
- Whether the source EPUB, extracted cover, personal catalog, and device state remain outside the public repository and release package.
- Any crop, rotation, contrast correction, or redaction applied.
- For video, whether playback is real time and unsped.
- For real-device media, whether the frame was captured after the e-ink panel fully settled.

## Rejection Reasons

Reject or recapture media that contains:

- A real reading statistic or sync/device identity.
- Contact information, credentials, network names, addresses, computer paths, or notifications.
- The wrong Duet version or an unidentified artifact.
- A stale layout from before the final stats revision.
- A loading, ghosted, distorted, or misleading state presented as final behavior.
- Embedded metadata that has not been reviewed.
