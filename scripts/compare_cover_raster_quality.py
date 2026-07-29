#!/usr/bin/env python3
"""Build controlled 1-bit cover comparisons at Duet's real display sizes."""

from __future__ import annotations

import argparse
import json
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path


SOURCES = (
    "illustrated",
    "photographic",
    "pale",
    "dark",
    "text-heavy",
    "gradient",
)

DIMENSIONS = {
    "x3-grid-2x2": (193, 282),
    "x3-grid-3x3": (123, 181),
    "x3-grid-4x4": (93, 140),
    "x4-grid-2x2": (196, 286),
    "x4-grid-3x3": (125, 183),
    "x4-grid-4x4": (94, 142),
    "carousel-center": (230, 338),
    "carousel-adjacent": (160, 234),
    "shared-120x180": (120, 180),
    "shared-123x180": (123, 180),
    "adaptive-small": (125, 188),
    "adaptive-x3": (296, 444),
    "adaptive-x4": (350, 525),
}

CONTACT_DIMENSIONS = (
    "x3-grid-4x4",
    "x4-grid-4x4",
    "carousel-center",
    "carousel-adjacent",
    "adaptive-x3",
    "adaptive-x4",
)


@dataclass(frozen=True)
class RasterMethod:
    slug: str
    label: str
    kind: str
    preprocess: tuple[str, ...] = ()
    diffusion: str | None = None
    dither: str | None = None
    kernel: str | None = None
    diffusion_percent: int = 100


ROUND1_METHODS = (
    RasterMethod(
        slug="01-current-fs",
        label="Current Floyd-Steinberg",
        kind="imagemagick",
        dither="FloydSteinberg",
    ),
    RasterMethod(
        slug="02-reduced-fs",
        label="Reduced-diffusion Floyd-Steinberg",
        kind="imagemagick",
        preprocess=("-sigmoidal-contrast", "1.5x50%", "-unsharp", "0x0.55+0.45+0.02"),
        diffusion="70%",
        dither="FloydSteinberg",
    ),
    RasterMethod(
        slug="03-home-atkinson",
        label="On-device Home Atkinson",
        kind="atkinson",
    ),
    RasterMethod(
        slug="04-device-reduced-fs",
        label="Device reduced-diffusion Floyd-Steinberg",
        kind="reduced-fs",
    ),
    RasterMethod(
        slug="05-riemersma",
        label="Reduced-diffusion Riemersma",
        kind="imagemagick",
        preprocess=("-sigmoidal-contrast", "1.25x50%", "-unsharp", "0x0.45+0.35+0.02"),
        diffusion="75%",
        dither="Riemersma",
    ),
)

ROUND2_METHODS = (
    RasterMethod(
        slug="01-home-atkinson",
        label="Current on-device Atkinson",
        kind="atkinson",
    ),
    RasterMethod(
        slug="02-stucki",
        label="Brightened serpentine Stucki",
        kind="kernel",
        preprocess=("-gamma", "1.15", "-unsharp", "0x0.45+0.3+0.02"),
        kernel="stucki",
        diffusion_percent=82,
    ),
    RasterMethod(
        slug="03-burkes",
        label="Brightened serpentine Burkes",
        kind="kernel",
        preprocess=("-gamma", "1.15", "-unsharp", "0x0.45+0.3+0.02"),
        kernel="burkes",
        diffusion_percent=78,
    ),
    RasterMethod(
        slug="04-sierra",
        label="Brightened serpentine Sierra",
        kind="kernel",
        preprocess=("-gamma", "1.15", "-unsharp", "0x0.45+0.3+0.02"),
        kernel="sierra",
        diffusion_percent=78,
    ),
    RasterMethod(
        slug="05-jarvis",
        label="Brightened serpentine Jarvis-Judice-Ninke",
        kind="kernel",
        preprocess=("-gamma", "1.15", "-unsharp", "0x0.45+0.3+0.02"),
        kernel="jarvis",
        diffusion_percent=82,
    ),
)

DIFFUSION_KERNELS = {
    "stucki": (
        42,
        (
            (1, 0, 8),
            (2, 0, 4),
            (-2, 1, 2),
            (-1, 1, 4),
            (0, 1, 8),
            (1, 1, 4),
            (2, 1, 2),
            (-2, 2, 1),
            (-1, 2, 2),
            (0, 2, 4),
            (1, 2, 2),
            (2, 2, 1),
        ),
    ),
    "burkes": (
        32,
        (
            (1, 0, 8),
            (2, 0, 4),
            (-2, 1, 2),
            (-1, 1, 4),
            (0, 1, 8),
            (1, 1, 4),
            (2, 1, 2),
        ),
    ),
    "sierra": (
        32,
        (
            (1, 0, 5),
            (2, 0, 3),
            (-2, 1, 2),
            (-1, 1, 4),
            (0, 1, 5),
            (1, 1, 4),
            (2, 1, 2),
            (-1, 2, 2),
            (0, 2, 3),
            (1, 2, 2),
        ),
    ),
    "jarvis": (
        48,
        (
            (1, 0, 7),
            (2, 0, 5),
            (-2, 1, 3),
            (-1, 1, 5),
            (0, 1, 7),
            (1, 1, 5),
            (2, 1, 3),
            (-2, 2, 1),
            (-1, 2, 3),
            (0, 2, 5),
            (1, 2, 3),
            (2, 2, 1),
        ),
    ),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_dir", type=Path, help="Directory containing the six named SVG cover masters")
    parser.add_argument("output_dir", type=Path, help="Comparison output directory")
    parser.add_argument("--force", action="store_true", help="Regenerate existing comparison images")
    parser.add_argument("--method-set", choices=("round1", "round2"), default="round1")
    return parser.parse_args()


def find_source(source_dir: Path, name: str) -> Path | None:
    for suffix in (".png", ".jpg", ".jpeg", ".webp", ".svg.png", ".svg"):
        candidate = source_dir / f"{name}{suffix}"
        if candidate.is_file():
            return candidate
    return None


def base_resize_command(
    magick: str,
    source: Path,
    width: int,
    height: int,
    preprocess: tuple[str, ...],
) -> list[str]:
    return [
        magick,
        str(source),
        "-auto-orient",
        "-colorspace",
        "Gray",
        "-resize",
        f"{width}x{height}^",
        "-background",
        "white",
        "-gravity",
        "center",
        "-extent",
        f"{width}x{height}",
        *preprocess,
    ]


def render_imagemagick(
    magick: str,
    source: Path,
    destination: Path,
    width: int,
    height: int,
    method: RasterMethod,
) -> None:
    command = base_resize_command(magick, source, width, height, method.preprocess)
    if method.diffusion:
        command.extend(("-define", f"dither:diffusion-amount={method.diffusion}"))
    command.extend(
        (
            "-dither",
            method.dither or "FloydSteinberg",
            "-colors",
            "2",
            "-type",
            "bilevel",
            str(destination),
        )
    )
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def atkinson_pixels(gray: bytes, width: int, height: int) -> bytes:
    if len(gray) != width * height:
        raise ValueError(f"Expected {width * height} grayscale bytes, got {len(gray)}")

    output = bytearray(width * height)
    row0 = [0] * (width + 4)
    row1 = [0] * (width + 4)
    row2 = [0] * (width + 4)

    for y in range(height):
        row_offset = y * width
        for x in range(width):
            adjusted = gray[row_offset + x] + row0[x + 2]
            adjusted = max(0, min(255, adjusted))
            quantized = 0 if adjusted < 128 else 255
            output[row_offset + x] = quantized
            error = (adjusted - quantized) >> 3
            row0[x + 3] += error
            row0[x + 4] += error
            row1[x + 1] += error
            row1[x + 2] += error
            row1[x + 3] += error
            row2[x + 2] += error
        row0, row1, row2 = row1, row2, [0] * (width + 4)

    return bytes(output)


def reduced_fs_pixels(gray: bytes, width: int, height: int) -> bytes:
    if len(gray) != width * height:
        raise ValueError(f"Expected {width * height} grayscale bytes, got {len(gray)}")

    output = bytearray(width * height)
    current = [0] * (width + 2)
    following = [0] * (width + 2)

    for y in range(height):
        row_offset = y * width
        for x in range(width):
            source = gray[row_offset + x]
            source = max(0, min(255, ((source - 128) * 115) // 100 + 128))
            adjusted = max(0, min(255, source + current[x + 1]))
            quantized = 0 if adjusted < 128 else 255
            output[row_offset + x] = quantized
            error = adjusted - quantized
            current[x + 2] += (error * 78) >> 8
            following[x] += (error * 34) >> 8
            following[x + 1] += (error * 56) >> 8
            following[x + 2] += (error * 11) >> 8
        current, following = following, [0] * (width + 2)

    return bytes(output)


def kernel_diffusion_pixels(
    gray: bytes,
    width: int,
    height: int,
    kernel_name: str,
    diffusion_percent: int,
) -> bytes:
    if len(gray) != width * height:
        raise ValueError(f"Expected {width * height} grayscale bytes, got {len(gray)}")
    if kernel_name not in DIFFUSION_KERNELS:
        raise ValueError(f"Unknown diffusion kernel: {kernel_name}")

    denominator, kernel = DIFFUSION_KERNELS[kernel_name]
    max_row_offset = max(dy for _dx, dy, _weight in kernel)
    errors = [[0] * width for _ in range(max_row_offset + 1)]
    output = bytearray(width * height)

    for y in range(height):
        reverse = (y & 1) != 0
        x_values = range(width - 1, -1, -1) if reverse else range(width)
        row_offset = y * width
        for x in x_values:
            adjusted = max(0, min(255, gray[row_offset + x] + errors[0][x]))
            quantized = 0 if adjusted < 128 else 255
            output[row_offset + x] = quantized
            error = adjusted - quantized
            for dx, dy, weight in kernel:
                target_x = x - dx if reverse else x + dx
                if target_x < 0 or target_x >= width or y + dy >= height:
                    continue
                numerator = error * weight * diffusion_percent
                errors[dy][target_x] += int(numerator / (denominator * 100))
        errors.pop(0)
        errors.append([0] * width)

    return bytes(output)


def render_python_dither(
    magick: str,
    source: Path,
    destination: Path,
    width: int,
    height: int,
    method: RasterMethod,
) -> None:
    gray_command = base_resize_command(magick, source, width, height, method.preprocess)
    gray_command.extend(("-depth", "8", "gray:-"))
    gray = subprocess.run(gray_command, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
    if method.kind == "atkinson":
        pixels = atkinson_pixels(gray, width, height)
    elif method.kind == "reduced-fs":
        pixels = reduced_fs_pixels(gray, width, height)
    elif method.kind == "kernel":
        if not method.kernel:
            raise ValueError(f"{method.slug} has no diffusion kernel")
        pixels = kernel_diffusion_pixels(
            gray,
            width,
            height,
            method.kernel,
            method.diffusion_percent,
        )
    else:
        raise ValueError(f"Unsupported Python dither kind: {method.kind}")
    pgm = f"P5\n{width} {height}\n255\n".encode("ascii") + pixels
    subprocess.run(
        [magick, "pgm:-", "-type", "bilevel", str(destination)],
        input=pgm,
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )


def render_candidate(
    magick: str,
    source: Path,
    destination: Path,
    width: int,
    height: int,
    method: RasterMethod,
) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    if method.kind in {"atkinson", "reduced-fs", "kernel"}:
        render_python_dither(magick, source, destination, width, height, method)
    else:
        render_imagemagick(magick, source, destination, width, height, method)


def make_contact_sheet(
    magick: str,
    images: list[Path],
    destination: Path,
    width: int,
    height: int,
    scale: int,
    methods: tuple[RasterMethod, ...],
) -> None:
    tile_width = width * scale
    tile_height = height * scale
    command = [magick, "montage"]
    for index, image in enumerate(images):
        method = methods[index % len(methods)]
        short_label = method.slug.split("-", 1)[-1].replace("-", " ").title()
        command.extend(("(", str(image), "-set", "label", short_label, ")"))
    command.extend(
        (
            "-font",
            "/System/Library/Fonts/Helvetica.ttc",
            "-pointsize",
            "9" if scale == 1 else "16",
            "-filter",
            "point",
            "-tile",
            f"{len(methods)}x{len(SOURCES)}",
            "-geometry",
            f"{tile_width}x{tile_height}!+12+12",
            "-background",
            "white",
            "-quality",
            "95",
            str(destination),
        )
    )
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)


def write_report(output_dir: Path, methods: tuple[RasterMethod, ...], method_set: str) -> None:
    report = {
        "sources": list(SOURCES),
        "source_license": "Standard Ebooks cover art; public domain or CC0",
        "dimensions": {name: list(size) for name, size in DIMENSIONS.items()},
        "method_set": method_set,
        "methods": [
            {
                "slug": method.slug,
                "label": method.label,
                "kind": method.kind,
                "preprocess": list(method.preprocess),
                "diffusion": method.diffusion,
                "dither": method.dither,
            }
            for method in methods
        ],
        "sheet_layout": "Rows are source types in SOURCES order; columns are methods in manifest order.",
    }
    (output_dir / "comparison-manifest.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    source_dir = args.source_dir.expanduser().resolve()
    output_dir = args.output_dir.expanduser().resolve()
    magick = shutil.which("magick")
    if not magick:
        raise SystemExit("ImageMagick's `magick` command is required")
    methods = ROUND1_METHODS if args.method_set == "round1" else ROUND2_METHODS

    source_paths = {name: find_source(source_dir, name) for name in SOURCES}
    missing = [name for name, path in source_paths.items() if path is None]
    if missing:
        raise SystemExit(f"Missing cover masters: {', '.join(missing)}")

    output_dir.mkdir(parents=True, exist_ok=True)
    for dimension_name, (width, height) in DIMENSIONS.items():
        dimension_dir = output_dir / dimension_name
        for source_name in SOURCES:
            source = source_paths[source_name]
            assert source is not None
            for method in methods:
                destination = dimension_dir / f"{source_name}__{method.slug}.png"
                if destination.is_file() and not args.force:
                    continue
                render_candidate(magick, source, destination, width, height, method)

        if dimension_name in CONTACT_DIMENSIONS:
            images = [
                dimension_dir / f"{source_name}__{method.slug}.png"
                for source_name in SOURCES
                for method in methods
            ]
            make_contact_sheet(
                magick,
                images,
                output_dir / f"{dimension_name}__actual-size.png",
                width,
                height,
                1,
                methods,
            )
            make_contact_sheet(
                magick,
                images,
                output_dir / f"{dimension_name}__pixel-3x.png",
                width,
                height,
                3,
                methods,
            )

    write_report(output_dir, methods, args.method_set)
    print(f"Cover comparison written to {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
