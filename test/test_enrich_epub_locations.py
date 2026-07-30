from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "enrich_epub_locations.py"


class EnrichEpubLocationsTest(unittest.TestCase):
    def make_epub(self, path: Path) -> None:
        container = """<?xml version="1.0" encoding="UTF-8"?>
<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""
        package = """<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0">
  <manifest>
    <item id="chapter" href="Text/Wizard%27s%20Castle.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine>
    <itemref idref="chapter"/>
  </spine>
</package>
"""
        chapter = """<!doctype html>
<html xmlns="http://www.w3.org/1999/xhtml"><body><p>One short chapter with enough words to verify the location manifest.</p></body></html>
"""
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
            archive.writestr("META-INF/container.xml", container)
            archive.writestr("OEBPS/content.opf", package)
            archive.writestr("OEBPS/Text/Wizard's Castle.xhtml", chapter)

    def test_url_escaped_spine_and_separate_backup_directory(self) -> None:
        with tempfile.TemporaryDirectory(prefix="duet-locations-") as temp:
            root = Path(temp)
            epub = root / "Wizard's Castle.epub"
            backups = root / "backups"
            self.make_epub(epub)

            result = subprocess.run(
                [
                    "python3",
                    str(SCRIPT),
                    "--backup",
                    "--backup-dir",
                    str(backups),
                    str(epub),
                ],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
            )

            self.assertIn("updated=1", result.stdout)
            backup_files = list(backups.glob("Wizard's Castle.epub.*.duetbak"))
            self.assertEqual(len(backup_files), 1)
            self.assertFalse((root / "Wizard's Castle.epub.duetbak").exists())
            with zipfile.ZipFile(epub) as archive:
                manifest = json.loads(archive.read("META-INF/x-locations.json"))
            self.assertEqual(manifest["format"], "x-locations")
            self.assertEqual(manifest["version"], 1)
            self.assertGreater(manifest["totalWords"], 0)
            self.assertGreater(manifest["totalLocations"], 0)
            self.assertEqual(manifest["spine"][0]["href"], "Text/Wizard%27s%20Castle.xhtml")

            second = subprocess.run(
                ["python3", str(SCRIPT), "--dry-run", str(epub)],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
            )
            self.assertIn("updated=0", second.stdout)
            self.assertIn("skipped=1", second.stdout)
            self.assertIn("failed=0", second.stdout)

    def test_duplicate_filenames_receive_distinct_external_backups(self) -> None:
        with tempfile.TemporaryDirectory(prefix="duet-locations-") as temp:
            root = Path(temp)
            first = root / "first" / "Same Title.epub"
            second = root / "second" / "Same Title.epub"
            backups = root / "backups"
            first.parent.mkdir()
            second.parent.mkdir()
            self.make_epub(first)
            self.make_epub(second)

            result = subprocess.run(
                [
                    "python3",
                    str(SCRIPT),
                    "--backup",
                    "--backup-dir",
                    str(backups),
                    str(root),
                ],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
            )

            self.assertIn("updated=2", result.stdout)
            backup_files = sorted(backups.glob("Same Title.epub.*.duetbak"))
            self.assertEqual(len(backup_files), 2)
            self.assertNotEqual(backup_files[0].name, backup_files[1].name)


if __name__ == "__main__":
    unittest.main()
