# Troubleshooting

- [Firmware update or recovery](#firmware-update-or-recovery)
- [SD-card filesystem errors](#sd-card-filesystem-errors)
- [Slow or missing library covers](#slow-or-missing-library-covers)
- [Cannot see the device on the network](#cannot-see-the-device-on-the-network)
- [Connection drops or times out](#connection-drops-or-times-out)
- [Upload fails](#upload-fails)
- [Saved password not working](#saved-password-not-working)

## Firmware Update or Recovery

If an update causes a boot loop, prevents books from opening, or breaks sleep/wake:

1. Remove the SD card and boot once.
2. If the problem continues, flash the last known-good BIN through the CrossPoint web installer's **Custom .bin** option.
3. Confirm the installed version in **Settings > System**.
4. Do not delete hidden state folders while diagnosing. They contain settings, progress, statistics, bookmarks, and evidence that may identify the cause.

Use only the BIN for the physical model. X3 and X4 firmware files are not interchangeable.

## SD-Card Filesystem Errors

Duet keeps most persistent data on the SD card. Filesystem damage can therefore look like a firmware defect: unrelated screens may slow down or crash, caches may disappear, and behavior may change after reboot.

Back up the complete card before any repair. Do not begin by deleting `/.duet`, `/.crossink`, `/.crosspoint`, books, or statistics.

On macOS, run a read-only check first:

```bash
diskutil verifyVolume "/Volumes/NAME OF CARD"
```

If it reports allocation-bitmap errors, overlapping clusters, or another repairable problem:

1. Keep the full backup.
2. Use Disk Utility First Aid or:

   ```bash
   diskutil repairVolume "/Volumes/NAME OF CARD"
   ```

3. Run the read-only verification again.
4. Confirm books and hidden state folders are still present.
5. Eject the card through macOS before removing it.

Windows and Linux testers should use their operating system's standard filesystem verification tool while the card is not being written. Include the result in a bug report. Do not reformat a card merely because Duet crashed.

## Slow or Missing Library Covers

Cover images are cached by exact book path and device-specific size. A large folder can take time on its first visit, especially on the X3 or a slower card.

1. Let one small folder finish hydrating, leave it, and return. Existing valid thumbnails should persist.
2. For a large or multiply organized library, run [Desktop Cover Prefill](COVER_PREFILL.md).
3. Check `/.duet/state/desktop_cover_prefill.json`; `failed_books` should be empty.
4. Do not copy thumbnail or hidden-state folders between X3 and X4 cards.
5. If the picker stops moving, collect `/.duet/state/picker_timing.txt`, `/.duet/state/picker_hb.txt`, and `/crash_report.txt` if present before clearing anything.

## Cannot See the Device on the Network

**Problem:** Browser shows "Cannot connect" or "Site can't be reached"

**Solutions:**

1. Verify both devices are on the correct network
   - Check your computer/phone Wi-Fi settings
   - In **Join Network** mode, your computer/phone and Duet must be on the same Wi-Fi network
   - In **Create Hotspot** mode, your computer/phone must be connected to the `CrossPoint-Reader` hotspot
2. Double-check the IP address
   - Make sure you typed it correctly
   - Include `http://` at the beginning
   - Try the displayed IP address if `http://crosspoint.local/` does not resolve
3. Try disabling VPN if you're using one
4. Some networks have "client isolation" enabled - use Create Hotspot mode or check with your network administrator

## Connection Drops or Times Out

**Problem:** Wi-Fi connection is unstable

**Solutions:**

1. Move closer to the Wi-Fi router, or use Create Hotspot mode for a direct connection
2. Check signal strength on the device (should be at least `||` or better)
3. Avoid interference from other devices
4. Try a different Wi-Fi network if available

## Upload Fails

**Problem:** File upload doesn't complete or shows an error

**Solutions:**

1. Check that the SD card has enough free space
2. Check that the filename is valid for the SD card filesystem
3. Try uploading a smaller file first to test
4. Refresh the browser page and try again
5. If WebSocket upload fails repeatedly, refresh the page and retry with the HTTP fallback path

## Saved Password Not Working

**Problem:** Device fails to connect with saved credentials

**Solutions:**

1. When connection fails, you'll be prompted to "Forget Network"
2. Select **Yes** to remove the saved password
3. Reconnect and enter the password again
4. Choose to save the new password
