---
title: Font Build Variants
parent: Fonts
nav_order: 1
---

# Font Build Variants

Every font family installed from Duet's SD-card font pack provides 10, 12, 14, 16, 18, and 20 pt on both X3 and X4.

The firmware also contains built-in copies of Lexend Deca and Bitter so the reader always has usable fonts when no SD-card fonts are installed. Because storage inside the firmware BIN is limited, those two built-in families offer a smaller set of sizes. The lists below apply only while using the built-in Lexend Deca or Bitter; they do not limit any family installed from the SD card.

## Variants

### X3 public profile (`x3-public`, based on `tiny`)

The X3 public BIN embeds:

- Emoji and miscellaneous-symbol support
- 4 built-in font sizes:
  - 10 pt
  - 12 pt
  - 14 pt
  - 16 pt

### X4 public profile (`x4-public`, based on `xlarge`)

The X4 public BIN embeds:

- Emoji and miscellaneous-symbol support
- 3 built-in font sizes:
  - 16 pt
  - 18 pt
  - 20 pt

Embedding all six sizes for both built-in families would make either public BIN about 7.13 MB, exceeding the 6.55 MB app partition. Keeping the complete six-size collection on the SD card preserves Duet's firmware features, translation catalogs, emoji, and symbols while allowing every SD-card family to offer the full reading range.

## Flashing A Variant

Download `Duet-X3-v<version>.bin` or `Duet-X4-v<version>.bin` from the [official releases](https://github.com/lauren-alexandra/duet-xteink/releases), or build locally with PlatformIO:

```sh
pio run -e x3-public
pio run -e x4-public
```

Flash only the BIN matching the physical device.
