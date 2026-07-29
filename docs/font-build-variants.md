---
title: Font Build Variants
nav_order: 12
---

# Font Build Variants

Duet ships device-specific public font profiles because the ESP32-C3 has limited flash and RAM. Both public profiles preserve emoji and miscellaneous symbol support; they differ in the built-in reader sizes included. SD-card font families expose the sizes actually installed for that family.

## Variants

### X3 public profile (`x3-public`, based on `tiny`)

The X3 public BIN includes:

- Emoji and miscellaneous-symbol support
- 4 font sizes:
  - 10 pt
  - 12 pt
  - 14 pt
  - 16 pt

### X4 public profile (`x4-public`, based on `xlarge`)

The 8, 9, 10, 12, and 14 pt built-ins are removed to make room for the larger reader sizes while preserving emoji and symbols.

- Emoji and miscellaneous-symbol support
- 3 font sizes:
  - 16 pt
  - 18 pt
  - 20 pt

## Flashing A Variant

Download `Duet-X3-v<version>.bin` or `Duet-X4-v<version>.bin` from the [official releases](https://github.com/lauren-alexandra/duet-xteink/releases), or build locally with PlatformIO:

```sh
pio run -e x3-public
pio run -e x4-public
```

Flash only the BIN matching the physical device.
