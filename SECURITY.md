# Security Policy

## Reporting a Security Issue

Please use GitHub's private security-advisory feature for vulnerabilities that could expose credentials, corrupt an SD card, bypass an update check, or affect network services. Do not open a public issue containing a proof of concept, credentials, private network details, or another person's device data.

For ordinary crashes, freezes, rendering problems, or performance regressions, use the public bug-report template.

## Sensitive Data

Never upload:

- Wi-Fi, OPDS, KOReader Sync, or Calibre credentials.
- An SD-card image or complete `.crossink` / `.crosspoint` directory.
- Copyrighted ebooks, dictionaries, extracted cover files, or personal sleep screens. A minimal issue screenshot may show a cover as part of the Duet interface after the reporter reviews the frame for private data.
- Personal contact files, reading guides, library catalogs, or tracker data.
- Device MAC addresses, IP addresses, or serial identifiers without redaction.

Logs may contain filenames or paths. Review and redact them before attaching them to a public issue.

## Supported Versions

During the alpha period, only the most recent tagged prerelease receives security and reliability fixes. Testers should include the exact release tag and device model in every report.
