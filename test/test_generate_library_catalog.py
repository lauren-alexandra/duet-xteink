from __future__ import annotations

import csv
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "generate_library_catalog.py"


class GenerateLibraryCatalogTest(unittest.TestCase):
    def generate(self, spice_values: list[str] | None) -> list[list[str]]:
        with tempfile.TemporaryDirectory(prefix="duet-catalog-") as temp:
            export_root = Path(temp)
            category = export_root / "02 By Category" / "Fiction"
            category.mkdir(parents=True)
            index = export_root / "_XTEINK-Organized-Export-Index.tsv"
            fields = [
                "export_kind",
                "export_path",
                "calibre_id",
                "title",
                "authors",
                "series",
                "primary_genre",
                "series_index",
            ]
            if spice_values is not None:
                fields.append("spice_level")

            rows = []
            for offset, title in enumerate(("Alpha", "Beta")):
                row = {
                    "export_kind": "category",
                    "export_path": str(category / f"{title}.epub"),
                    "calibre_id": str(offset + 1),
                    "title": title,
                    "authors": f"{title} Author",
                    "series": "",
                    "primary_genre": "Fiction",
                    "series_index": "",
                }
                if spice_values is not None:
                    row["spice_level"] = spice_values[offset]
                rows.append(row)

            with index.open("w", encoding="utf-8", newline="") as handle:
                writer = csv.DictWriter(handle, fieldnames=fields, delimiter="\t")
                writer.writeheader()
                writer.writerows(rows)

            output = export_root / "library_catalog.tsv"
            subprocess.run(["python3", str(SCRIPT), str(index), str(output)], cwd=ROOT, check=True, capture_output=True)
            return [line.split("\t") for line in output.read_text(encoding="utf-8").splitlines()]

    def test_missing_spice_column_emits_no_spice_dictionary(self) -> None:
        lines = self.generate(None)
        self.assertEqual(lines[0][6], "0")
        self.assertFalse(any(fields[0] == "P" for fields in lines))
        self.assertEqual([fields[5] for fields in lines if fields[0] == "B"], ["-1", "-1"])

    def test_mixed_spice_values_only_tag_supplied_books(self) -> None:
        lines = self.generate(["Spice 3 - Open Door", ""])
        self.assertEqual(lines[0][6], "1")
        self.assertEqual([fields[2] for fields in lines if fields[0] == "P"], ["Spice 3 - Open Door"])
        self.assertEqual([fields[5] for fields in lines if fields[0] == "B"], ["0", "-1"])


if __name__ == "__main__":
    unittest.main()
