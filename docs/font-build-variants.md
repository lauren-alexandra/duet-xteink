---
title: Font Build Variants
parent: Fonts
nav_order: 1
---

# Font Build Variants

Duet's standard six-size SD-card font families provide 10, 12, 14, 16, 18, and 20 pt on both X3 and X4. The smaller lists below describe only the emergency Lexend Deca and Bitter fallbacks embedded inside each firmware BIN. They do not limit the sizes available after an SD-card font family is installed.

## Variants

### X3 public profile (`x3-public`, based on `tiny`)

The X3 public BIN embeds:

- Emoji and miscellaneous-symbol support
- 4 fallback font sizes:
  - 10 pt
  - 12 pt
  - 14 pt
  - 16 pt

### X4 public profile (`x4-public`, based on `xlarge`)

The X4 public BIN embeds:

- Emoji and miscellaneous-symbol support
- 3 fallback font sizes:
  - 16 pt
  - 18 pt
  - 20 pt

Embedding all six sizes for both fallback families would make either public BIN about 7.13 MB, exceeding the 6.55 MB app partition. Keeping the full reading range on the SD card preserves the firmware features, translation catalogs, emoji, and symbols while allowing every installed family to offer all six sizes.

## Flashing A Variant

Download `Duet-X3-v<version>.bin` or `Duet-X4-v<version>.bin` from the [official releases](https://github.com/lauren-alexandra/duet-xteink/releases), or build locally with PlatformIO:

```sh
pio run -e x3-public
pio run -e x4-public
```

Flash only the BIN matching the physical device.
