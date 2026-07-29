#!/usr/bin/env bash
# Usage: ./scripts/release.sh 0.1.0-alpha.2
# Bumps both Duet version fields in platformio.ini, commits, and creates the git tag.
set -euo pipefail

VERSION="${1:-}"
if [[ -z "$VERSION" ]]; then
  echo "Usage: $0 <version>  (e.g. 1.2.7)" >&2
  exit 1
fi

if [[ ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$ ]]; then
  echo "Version must look like 0.1.0-alpha.2 or 0.1.0" >&2
  exit 1
fi

sed -i '' "s/crossink_version = .*/crossink_version = $VERSION/" platformio.ini
sed -i '' "s/crossink_public_version = .*/crossink_public_version = $VERSION/" platformio.ini
git add platformio.ini
git commit -m "Release Duet v$VERSION"
git tag "v$VERSION"
echo "Tagged v$VERSION. Review before pushing the commit and tag."
