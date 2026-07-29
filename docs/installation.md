---
title: Installation
nav_order: 14
---

# Installation

Duet releases contain a separate firmware file for each reader:

- `Duet-X3-v<version>.bin` for the Xteink X3
- `Duet-X4-v<version>.bin` for the Xteink X4

Do not flash the X3 file onto an X4 or the X4 file onto an X3. Back up the entire SD card before the first alpha installation. Preserve `/.duet`, `/.crossink`, `/.crosspoint`, `/.crossink-stats-backup`, `/fonts`, `/dictionaries`, and your book folders during every upgrade.

## Web Installer

Use this route for the first Duet installation or recovery:

1. Download the correct `Duet-X3-...bin` or `Duet-X4-...bin` from the [official Duet releases](https://github.com/lauren-alexandra/duet-xteink/releases).
2. Connect your Xteink X4 or X3 to your computer via USB-C and wake/unlock the device.
3. Go to <https://crosspointreader.com/#flash-tools> and choose your device.
4. Select **Custom .bin** from the options.
5. Choose the Duet `.bin` file you downloaded and click **Flash**.

To revert back to the official firmware, flash the latest official firmware from <https://crosspointreader.com/#flash-tools>.

## SD Card Upgrade

Once Duet is installed:

1. Keep a copy of the currently working BIN as a rollback file.
2. Put exactly one new Duet firmware BIN at the SD card root.
3. On the reader, open **Settings > System > SD Firmware Update**.
4. Confirm the device name and version, then let the reader restart.
5. Verify **Settings > System** shows the expected Duet version before removing the rollback copy from your computer.

## Command Line

These instructions are for macOS and Linux. Windows users should use the web installer.

Install `esptool`:

```sh
pip3 install esptool
```

Download the correct Duet BIN from the [official releases](https://github.com/lauren-alexandra/duet-xteink/releases), then connect your device with USB-C.

Find the device port:

```sh
# Linux
dmesg | grep tty

# macOS
ls /dev/cu.*
```

Flash the firmware:

```sh
# Linux
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/firmware.bin

# macOS
esptool.py --chip esp32c3 --port /dev/cu.usbmodem2101 --baud 921600 write_flash 0x10000 /path/to/firmware.bin
```

Replace the port and firmware path with your actual values.

## Alpha Recovery

If the reader boot-loops, cannot open books, or cannot sleep after an update, remove the SD card and boot once. If the problem continues, flash the last known-good BIN through the web installer. Do not delete the SD card's hidden state folders while diagnosing; they contain progress, settings, statistics, bookmarks, and caches that can help recover both the data and the cause.
