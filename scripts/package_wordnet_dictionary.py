#!/usr/bin/env python3
"""Package the reviewed WordNet 3.0 StarDict files as a Duet release asset."""

from __future__ import annotations

import argparse
import hashlib
import json
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LICENSE_PATH = ROOT / "licenses" / "dictionaries" / "WordNet-3.0" / "LICENSE.txt"
EXPECTED_FILES = {
    "princeton-wordnet-3.0.dict": "b908b3905956513359a0ccd6afe5e3558fa4f68212a2d321faac8aef560f3fa4",
    "princeton-wordnet-3.0.idx": "f4e6a99d432efdd5176f1f04b3280ccc64574e50e5437913ae0b348311fa19c3",
    "princeton-wordnet-3.0.ifo": "6bd7317e927362b079b41f879bf7aa52ff9bf233e04052c8482d864eb227148e",
    "princeton-wordnet-3.0.syn": "0b504cb52060c0bf3aa1b13bd4aa18084629ea5fcc33c7994d060cf207d00f0a",
}
ARCHIVE_NAME = "Duet-WordNet-3.0-StarDict.zip"
ARCHIVE_PREFIX = "dictionaries/English"
FIXED_ZIP_TIMESTAMP = (2026, 7, 29, 0, 0, 0)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def add_bytes(archive: zipfile.ZipFile, name: str, data: bytes) -> None:
    info = zipfile.ZipInfo(name, FIXED_ZIP_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    archive.writestr(info, data)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", required=True, type=Path, help="Directory containing the reviewed StarDict files")
    parser.add_argument("--output-dir", type=Path, default=ROOT / ".pio" / "build", help="Directory that receives the ZIP")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source_dir = args.source_dir.expanduser().resolve()
    output_dir = args.output_dir.expanduser().resolve()
    if not source_dir.is_dir():
        raise FileNotFoundError(f"Dictionary source directory not found: {source_dir}")
    if not LICENSE_PATH.is_file():
        raise FileNotFoundError(f"WordNet license not found: {LICENSE_PATH}")

    verified: dict[str, str] = {}
    for filename, expected_hash in EXPECTED_FILES.items():
        path = source_dir / filename
        if not path.is_file():
            raise FileNotFoundError(f"Required dictionary file missing: {path}")
        actual_hash = sha256(path)
        if actual_hash != expected_hash:
            raise ValueError(f"SHA-256 mismatch for {filename}: expected {expected_hash}, got {actual_hash}")
        verified[filename] = actual_hash

    output_dir.mkdir(parents=True, exist_ok=True)
    archive_path = output_dir / ARCHIVE_NAME
    if archive_path.exists():
        raise FileExistsError(f"Refusing to overwrite existing dictionary archive: {archive_path}")

    checksum_text = "\n".join(f"{digest}  {filename}" for filename, digest in sorted(verified.items())) + "\n"
    readme_text = """Duet WordNet 3.0 dictionary pack

1. Copy the top-level dictionaries folder to the root of the Xteink SD card.
2. Eject the card cleanly and start Duet.
3. Open Apps > Dictionary, select WordNet 3.0, and let preparation finish.
4. In a book, press Confirm and choose Dictionary to select and look up a word.

The dictionary must remain at /dictionaries/English/ with all four StarDict files together.
WordNet 3.0 is redistributed under the included WORDNET-LICENSE.txt.
Full instructions: https://lauren-alexandra.github.io/duet-xteink/DICTIONARY_SETUP.html
"""

    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for filename in sorted(EXPECTED_FILES):
            add_bytes(archive, f"{ARCHIVE_PREFIX}/{filename}", (source_dir / filename).read_bytes())
        add_bytes(archive, f"{ARCHIVE_PREFIX}/WORDNET-LICENSE.txt", LICENSE_PATH.read_bytes())
        add_bytes(archive, "README-FIRST.txt", readme_text.encode("utf-8"))
        add_bytes(archive, "SHA256SUMS.txt", checksum_text.encode("ascii"))

    result = {
        "archive": str(archive_path),
        "archive_sha256": sha256(archive_path),
        "files": verified,
    }
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
