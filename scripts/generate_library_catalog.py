#!/usr/bin/env python3
"""Build the compact Duet metadata catalog copied to the SD card."""

from __future__ import annotations

import argparse
import csv
import html
import re
from pathlib import Path


CATALOG_VERSION = 2


def clean(value: str) -> str:
    return " ".join((value or "").replace("\t", " ").replace("\r", " ").replace("\n", " ").split())


def condense_description(value: str, *, max_words: int = 72) -> str:
    text = html.unescape(value or "")
    text = re.sub(r"</(?:p|div|li|h\d)>", ". ", text, flags=re.I)
    text = re.sub(r"<br\s*/?>", ". ", text, flags=re.I)
    text = re.sub(r"<[^>]+>", " ", text)
    text = html.unescape(text)
    text = text.replace("\u2018", "'").replace("\u2019", "'")
    text = text.replace("\u201c", '"').replace("\u201d", '"')
    text = text.replace("\u2013", " - ").replace("\u2014", " - ")
    text = re.sub(r"\s+", " ", text).strip(" .")
    text = re.split(
        r"\b(?:Praise for|About the Author|The series includes|Also by)\b",
        text,
        maxsplit=1,
        flags=re.I,
    )[0].strip()
    sentences = re.split(r"(?<=[.!?])\s+(?=[A-Z0-9\"'])", text)
    skip = re.compile(
        r"bestsell|booktok sensation|major motion picture|reader review|praise for|"
        r"^named (?:one of|a)|^as featured in|^perfect for fans|^from .* author|"
        r"^experience the|award-winning author|author of the",
        flags=re.I,
    )
    useful = [sentence.strip() for sentence in sentences if sentence.strip() and not skip.search(sentence)]
    if not useful and text:
        useful = [text]

    chosen: list[str] = []
    word_count = 0
    for sentence in useful:
        words = sentence.split()
        if chosen and (word_count + len(words) > max_words or len(chosen) >= 3):
            break
        if not chosen and len(words) > max_words:
            return clean(" ".join(words[:max_words]).rstrip(" ,;:") + "...")
        chosen.append(sentence)
        word_count += len(words)
        if word_count >= 48:
            break
    result = " ".join(chosen).strip()
    if len(result.split()) > max_words:
        result = " ".join(result.split()[:max_words]).rstrip(" ,;:") + "..."
    return clean(result)


def load_descriptions(paths: list[Path]) -> dict[int, str]:
    descriptions: dict[int, str] = {}
    for path in paths:
        with path.expanduser().resolve().open("r", encoding="utf-8-sig", newline="") as source:
            for row in csv.DictReader(source, delimiter="\t"):
                calibre_id = int(row["calibre_id"])
                descriptions[calibre_id] = condense_description(row.get("description", ""))
    return descriptions


def dictionary(values: list[str]) -> tuple[dict[str, int], list[str]]:
    ordered = sorted({value for value in values if value}, key=str.casefold)
    return {value: index for index, value in enumerate(ordered)}, ordered


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("index", type=Path, help="_XTEINK-Organized-Export-Index.tsv")
    parser.add_argument("output", type=Path, help="output library_catalog.tsv")
    parser.add_argument(
        "--descriptions",
        action="append",
        default=[],
        type=Path,
        help="description TSV; may be repeated, with later files overriding earlier ones",
    )
    args = parser.parse_args()

    index_path = args.index.expanduser().resolve()
    export_root = index_path.parent.resolve()
    category_root = export_root / "02 By Category"
    descriptions = load_descriptions(args.descriptions)

    with index_path.open("r", encoding="utf-8-sig", newline="") as source:
        rows = []
        for row in csv.DictReader(source, delimiter="\t"):
            if row.get("export_kind") != "category":
                continue
            export_path = Path(row["export_path"]).resolve()
            try:
                relative_path = export_path.relative_to(category_root)
            except ValueError as exc:
                raise SystemExit(f"Category export is outside {category_root}: {export_path}") from exc
            rows.append(
                {
                    "calibre_id": int(row["calibre_id"]),
                    "title": clean(row.get("title", "")) or "Untitled",
                    "author": clean(row.get("authors", "")) or "Unknown Author",
                    "series": clean(row.get("series", "")),
                    "genre": clean(row.get("primary_genre", "")) or "Uncategorized",
                    "spice": clean(row.get("spice_level", "")),
                    "series_index": clean(row.get("series_index", "")) or "0",
                    "device_path": "/Books/" + relative_path.as_posix(),
                    "description": descriptions.get(int(row["calibre_id"]), ""),
                }
            )

    rows.sort(key=lambda row: row["calibre_id"])
    author_ids, authors = dictionary([row["author"] for row in rows])
    series_ids, series = dictionary([row["series"] for row in rows])
    genre_ids, genres = dictionary([row["genre"] for row in rows])
    spice_ids, spices = dictionary([row["spice"] for row in rows])

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="\n") as output:
        output.write(
            f"M\t{CATALOG_VERSION}\t{len(rows)}\t{len(authors)}\t{len(series)}\t{len(genres)}\t{len(spices)}\n"
        )
        for code, names in (("A", authors), ("S", series), ("G", genres), ("P", spices)):
            for index, name in enumerate(names):
                output.write(f"{code}\t{index}\t{name}\n")
        for row in rows:
            series_id = series_ids[row["series"]] if row["series"] else -1
            spice_id = spice_ids[row["spice"]] if row["spice"] else -1
            output.write(
                "B\t{calibre_id}\t{author_id}\t{series_id}\t{genre_id}\t{spice_id}\t{series_index}\t{device_path}"
                "\t{title}\t{description}\n".format(
                    calibre_id=row["calibre_id"],
                    author_id=author_ids[row["author"]],
                    series_id=series_id,
                    genre_id=genre_ids[row["genre"]],
                    spice_id=spice_id,
                    series_index=row["series_index"],
                    device_path=row["device_path"],
                    title=row["title"],
                    description=row["description"],
                )
            )

    print(
        f"Wrote {len(rows)} books, {len(authors)} authors, {len(series)} series, "
        f"{len(genres)} genres, {len(spices)} spice levels, and "
        f"{sum(bool(row['description']) for row in rows)} descriptions to {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
