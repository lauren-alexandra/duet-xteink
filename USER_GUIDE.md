# Duet User Guide

Welcome to the **Duet** firmware. This guide outlines the hardware controls, navigation, and reading features of the device.

- [Duet User Guide](#Duet-user-guide)
  - [1. Hardware Overview](#1-hardware-overview)
    - [Button Layout](#button-layout)
    - [Taking a Screenshot](#taking-a-screenshot)
  - [2. Power \& Startup](#2-power--startup)
    - [Power On / Off](#power-on--off)
    - [First Launch](#first-launch)
  - [3. Screens](#3-screens)
    - [3.1 Home Screen](#31-home-screen)
    - [3.2 Reading Mode](#32-reading-mode)
    - [3.3 Browse Files Screen](#33-browse-files-screen)
    - [3.4 Recent Books Screen](#34-recent-books-screen)
    - [3.5 File Transfer Screen](#35-file-transfer-screen)
    - [3.5.1 Calibre Wireless Transfers](#351-calibre-wireless-transfers)
      - [Installing the Plugin in Calibre](#installing-the-plugin-in-calibre)
      - [Configuring the CrossPoint Plugin in Calibre](#configuring-the-crosspoint-plugin-in-calibre)
      - [Uploading Books](#uploading-books)
      - [Removing a Book](#removing-a-book)
    - [3.6 Settings](#36-settings)
      - [3.6.1 Display](#361-display)
      - [3.6.2 Reader](#362-reader)
      - [3.6.3 Controls](#363-controls)
      - [3.6.4 System](#364-system)
      - [3.6.5 OPDS Servers (Multiple Libraries)](#365-opds-servers-multiple-libraries)
      - [3.6.6 Web Settings (Wi-Fi + OPDS)](#366-web-settings-wi-fi--opds)
      - [3.6.7 KOReader Sync Quick Setup](#367-koreader-sync-quick-setup)
        - [Option A: Free Public Server (`sync.koreader.rocks`)](#option-a-free-public-server-synckoreaderrocks)
        - [Option B: Self-Hosted Server (Docker Compose)](#option-b-self-hosted-server-docker-compose)
    - [3.7 Sleep Screen](#37-sleep-screen)
      - [Cover settings](#cover-settings)
      - [Custom images](#custom-images)
    - [3.8 Custom Fonts (SD Card)](#38-custom-fonts-sd-card)
    - [3.9 Library Views, Search, and More Info](#39-library-views-search-and-more-info)
    - [3.10 Reading Statistics](#310-reading-statistics)
    - [3.11 Nearby Sync](#311-nearby-sync)
    - [3.12 Apps and Utilities](#312-apps-and-utilities)
  - [4. Reading Mode](#4-reading-mode)
    - [Page Turning](#page-turning)
    - [Chapter Navigation](#chapter-navigation)
    - [Auto Page Turn](#auto-page-turn)
    - [Tilt Page Turn (X3 only)](#tilt-page-turn-x3-only)
    - [Footnote Navigation](#footnote-navigation)
    - [System Navigation](#system-navigation)
    - [Supported Languages](#supported-languages)
  - [5. Reader Menu](#5-reader-menu)
    - [5.1 Chapter Selection](#51-chapter-selection)
    - [5.2 Bookmarks](#52-bookmarks)
  - [6. Current Limitations & Roadmap](#6-current-limitations--roadmap)
  - [7. Troubleshooting Issues & Escaping Bootloop](#7-troubleshooting-issues--escaping-bootloop)

## 1. Hardware Overview

Duet uses logical **Back**, **Confirm**, **Left**, and **Right** actions plus the Power and side page-turn buttons. The physical placement differs between the X3 and X4, and the on-screen bottom hints show the actions available in the current view. Front and side mappings can be customized.

### Button Layout

| Group | Logical use |
| --- | --- |
| **Front buttons** | Back, Confirm, Left, and Right according to the active mapping and screen hints |
| **Side buttons** | Previous/next navigation by default; order and long-press action are configurable |
| **Power** | Wake/sleep plus configurable awake single/double/triple/long gestures |
| **Reset** | Hardware recovery when the firmware is unresponsive |

Button layout can be customized in the **[Controls Settings](#363-controls)**.

### Taking a Screenshot

When the Power Button and Volume Down button are pressed at the same time, it will take a screenshot and save it in the folder `screenshots/`.

Alternatively, while reading a book, press the **Confirm** button to open the reader menu and select **Take screenshot**.

---

## 2. Power & Startup

### Power On / Off

To turn the device on or off, **press and hold the Power button for approximately half a second**. In the **[Controls Settings](#363-controls)** you can configure the power button to turn the device off with a short press instead of a long one.

To reboot the device (for example after a firmware update or if it's frozen), press and release the Reset button, and then quickly press and hold the Power button for a few seconds.

### First Launch

Upon turning the device on for the first time, you will be placed on the **[Home](#31-home-screen)** screen.

> [!NOTE] On subsequent restarts, the firmware will automatically reopen the last book you were reading.

---

## 3. Screens

### 3.1 Home Screen

The Home screen is the main entry point to Duet. It keeps the active/latest book available for **[Reading Mode](#4-reading-mode)** and can expose shortcuts for Search, Recent Books, Reading Stats, Saved Items, Favorites, Sleep, Nearby Stats Sync, If Found, Apps, and other tools. Use **Customize Home & Apps** to place, hide, and reorder shortcuts; Duet preserves required escape routes.

Eight Home themes are available: Classic, Minimal, Dashboard, Dashboard Extended, Lyra, Lyra Extended, Lyra Carousel, and RoundedRaff. Dashboard stat slots can be customized under **Settings -> Display -> Home Stats**.

### 3.2 Reading Mode

See [Reading Mode](#4-reading-mode) below for more information.

### 3.3 Browse Files Screen

The Browse Files screen acts as a file and folder browser. Folder levels remain in the fast list layout. Book folders can use a one-line list, two-line list, 2x2, 3x3, or 4x4 cover grid, or the five-cover carousel. The full path to the current directory is shown at the top of the screen. File extensions are displayed alongside each filename, and directories are shown with brackets (for example, `[folder-name]`). Hidden directories can be shown from settings.

- **Navigate List:** Use **Left** (or **Volume Up**), or **Right** (or **Volume Down**) to move the selection cursor up and down through folders and books. You can also long-press these buttons to scroll a full page up or down.
- **Open Selection:** Press **Confirm** to open a folder or start reading a selected book. Selecting a `.bmp` file will open the image viewer.
- **Delete Files or Folders:** Hold and release **Confirm** to open the selected file or folder action menu, then choose **Delete**. You will be given an option to either confirm or cancel. Folder deletion is limited to empty folders.
- **Book Actions:** EPUB and XTC files can also show options such as **Delete Cache** or **Mark Finished** from the same action menu.
- **Sort and Filter:** Book views can sort by title or author and filter All, In Progress, Unread, or Finished.
- **More Info:** Open the book action menu to view catalog metadata and open the selected book from its information page.

### 3.4 Recent Books Screen

The Recent Books screen lists recently opened books with title and author. It can use a list or 3x3 cover grid. Removing an item from Recents does not delete the book or its reading statistics.

### 3.5 File Transfer Screen

The File Transfer screen allows you to upload and manage files on the device. When you enter the screen, choose **Join a Network**, **Calibre Wireless**, or **Create Hotspot**. The reader then starts the web server for the selected mode.

See the [web server docs](./docs/webserver.md) for more information on how to connect to the web server and upload files.

The web file manager can upload, download, rename, move, and delete files on the device.

The web interface also supports **WebDAV**, allowing you to mount the device as a network drive and manage files directly from your computer's file manager.

Download links for files already on the device are available in the web interface, so you can retrieve books or screenshots over Wi-Fi without connecting a cable.

A **Wi-Fi signal strength indicator** (dBm) is displayed on-screen during joined-network web server sessions.

> [!TIP] Advanced users can also manage files programmatically or via the command line using `curl`. See the [web server docs](./docs/webserver.md) for details.

### 3.5.1 Calibre Wireless Transfers

Duet supports sending books from Calibre using the CrossPoint Reader device plugin.

1. Install the plugin in Calibre:
   - Head to https://github.com/crosspoint-reader/calibre-plugins/releases to download the latest version of the crosspoint_reader plugin.

   - Download the zip file.

   - Open Calibre → Preferences → Plugins → Load plugin from file → Select the zip file.

2. On the device: File Transfer -> Calibre Wireless, then join a network.

3. Make sure your computer is on the same Wi-Fi network.

4. In Calibre, click "Send to device" to transfer books.

### 3.6 Settings

The Settings screen allows you to configure the device's behavior. There are a few settings you can adjust:

#### 3.6.1 Display

- **Sleep Screen**: Which sleep screen to display when the device sleeps:
  - "None" - A blank screen
  - "Dark" (default) - The default dark Duet logo sleep screen
  - "Light" - The same default sleep screen, on a white background
  - "Custom" - Custom images from the SD card; see [Sleep Screen](#37-sleep-screen) below for more information
  - "Cover" - The current book cover
  - "Cover + Custom" - The book cover image while actively reading, falls back to "Custom" behavior otherwise
  - "Page Overlay" - BMP or transparent PNG artwork over the stored reader page
  - "Reading Stats" - Recent reading stats on the sleep screen
  - "Minimal" - A minimal sleep screen
  - "Minimal Stats" - A compact stats sleep screen on X3; hidden on X4
  - "Dashboard" - The Dashboard current-book and statistics layout
  - "Quick Resume" - Keeps the current content visible while sleeping

- **Tap Power While Asleep to Cycle**: Choose Off or 1, 2, or 3 brief Power clicks to select a fresh Custom/Page Overlay image and return directly to sleep. CrumBLE's original version used one tap. Duet adds the multi-click choices and defaults to 3 because a single tap made accidental sleep-image changes too easy.

  The setting is labeled **3 clicks**, and the code intends to count the press that starts the sleeping device's wake path as click one. On the current physical builds, the reliable sequence can nevertheless feel like four physical presses: press once to start the wake-detection path, then make the three deliberate taps. This is a known alpha input quirk, not a hidden fourth setting. A deliberate hold wakes the device normally.

- **Sleep Screen Cover Mode**: How to display the book cover when "Cover" sleep screen is selected:
  - "Fit" (default) - Scale the image down to fit centered on the screen, padding with white borders as necessary
  - "Crop" - Scale the image down and crop as necessary to try to fill the screen (Note: this is experimental and may not work as expected)

- **Sleep Screen Cover Filter**: What filter will be applied to the book cover when "Cover" sleep screen is selected:
  - "None" (default) - The cover image will be converted to a grayscale image and displayed as it is
  - "Contrast" - The image will be displayed as a black & white image without grayscale conversion
  - "Inverted" - The image will be inverted as in white & black and will be displayed without grayscale conversion

- **Quick Resume on Timeout**: Whether to enable the "Quick Resume" sleep screen when the device goes to sleep due to inactivity (System > Time to Sleep). This is useful for quickly resuming reading without waiting for the device to fully wake up and load the book. This overwrites the Sleep Screen Cover Mode when enabled.

- **Hide Battery %**: Configure where to suppress the battery percentage display in the status bar; the battery icon will still be shown:
  - "Never" (default) - Always show battery percentage
  - "In Reader" - Show battery percentage everywhere except in reading mode
  - "Always" - Always hide battery percentage

- **Refresh Frequency**: Set how often the screen does a full refresh while reading to reduce ghosting; options are every 1, 5, 10, 15, or 30 pages.

- **UI Theme**: Set which UI theme to use:
  - "Classic" - The original Crosspoint theme
  - "Minimal" - A minimal theme with a large book cover
  - "Dashboard" - Current-book cover with dense configurable statistics
  - "Dashboard Extended" - Current-book card, chapter context, recent covers, statistics strip, and bottom navigation
  - "Lyra" - A theme with simple icons featuring your current book
  - "Lyra Extended" - Lyra, but displays 3 books instead of 1 on the **[Home Screen](#31-home-screen)**
  - "Lyra Carousel" - A carousel-based Lyra home layout
  - "RoundedRaff" - A rounded theme with additional visual styling

- **Home Stats**: Choose the contents of seven Dashboard rows, two footer slots, and four strip slots. Options include book time, time left, progress, daily average, pace, sessions, average session, days reading, estimated finish, streak, reader type, device/all-device totals, today's time, and total sessions.

- **Recent Books View**: Choose whether the Recent Books screen uses a list or grid layout.

- **Sunlight Fading Fix**: Configure whether to enable a software-fix for the issue where white X4 models may fade when used in direct sunlight:
  - "OFF" (default) - Disable the fix
  - "ON" - Enable the fix

> [!NOTE] A battery charging indicator is shown on the battery icon whenever the device is actively charging.

#### 3.6.2 Reader

- **Reader Font Family**: Choose the font used for reading:
  - "Lexend Deca" (default)
  - "Bitter"

- **Reader Font Size**: Adjust the text size for reading. A standard Duet SD-card family exposes 10, 12, 14, 16, 18, and 20 pt on both X3 and X4. Without an SD-card family, the firmware-only X3 fallback offers 10, 12, 14, and 16 pt, while the X4 fallback offers 16, 18, and 20 pt. Any manually installed family exposes the sizes actually present.

- **Reader Line Spacing**: Choose Tight, Normal, or Wide, then adjust the line height percentage more precisely if desired.

- **Reader Screen Margin**: Controls the screen margins in Reading Mode between 5 and 40 pixels in 5-pixel increments.

- **Reader Paragraph Alignment**: Set the alignment of paragraphs; options are "Justified" (default), "Left", "Center", "Right", or "Book's Style".

- **Embedded Style**: Whether to use the EPUB file's embedded HTML and CSS stylisation and formatting; options are "ON" or "OFF".

- **Hyphenation**: Whether to hyphenate text in Reading Mode; options are "ON" or "OFF".

- **Reading Orientation**: Set the screen orientation for reading EPUB files:
  - "Portrait" (default) - Standard portrait orientation
  - "Landscape CW" - Landscape, rotated clockwise
  - "Inverted" - Portrait, upside down
  - "Landscape CCW" - Landscape, rotated counter-clockwise

- **Extra Paragraph Spacing**: Set how to handle paragraph breaks:
  - "ON" - Vertical space will be added between paragraphs in Reading Mode
  - "OFF" - Paragraphs will not have vertical space added, but will have first-line indentation

- **Text Anti-Aliasing**: Whether to show smooth grey edges (anti-aliasing) on text in reading mode. Note this slows down page turns slightly.

- **Images**: Whether to display embedded images found in EPUB files; options are "Display" (default), "Placeholder", or "Suppress".

- **Bionic Reading**: Bolds the first part of each word to create visual fixation points; options are Off, Normal, and Subtle.

- **Render Profile**: CrossInk Default, Balanced, Light, and Safe Mode provide progressively more conservative fallbacks for difficult or memory-heavy books.

- **Text Darkness / Dark Reader Mode**: Select Normal, Dark, or Extra Dark text, and optionally use white-on-black reader presentation.

- **Guide Dots**: Adds guide dots between words; options are "ON" or "OFF" (default).

- **Customise Status Bar**: Configure the status bar displayed while reading:
  - Chapter Page Count - Show/Hide the current page in the chapter (ex: 5/25). Page count may change based on the font size and margins set.
  - Book Progress Percentage - Show/Hide the current percent progress in the book.
  - Progress Bar - Show/Hide a progress bar for either the book or chapter.
  - Progress Bar Thickness - Set the thickness of the progress bar
  - Title - Display the chapter or book title
  - Time Left - Display the estimated reading time left for the book or chapter
  - Battery - Show/Hide the battery indicator
  - XTC Status Bar - Show/Hide a status bar for XTC files

#### 3.6.3 Controls

- **Power Button**: Configure awake single-, double-, triple-, and long-press actions. Sleep-image cycling while already asleep is configured separately under Display.

- **Front Buttons**: Configure front-button remapping, orientation awareness, front-button long-press behavior, and the long-press menu action.

- **Side Buttons**: Configure side-button layout, orientation awareness, and side-button long-press behavior.

- **Side Button Layout (reader)**: Swap the order of the up and down volume buttons from "Prev/Next" (default) to "Next/Prev". You can also disable them entirely. This change is only in effect when reading.

- **Long-press Behavior**: Set whether long-pressing front page-turn buttons does nothing, skips to the next/previous chapter, or changes reader orientation.

- **Side Button Long-press Action**: Set whether long-pressing side buttons does nothing, skips chapters, changes font size, or changes orientation.

- **Single / Double / Triple / Long-press Action**: Map a Power gesture to a reader or system action:
  - "Sleep/Wake" - A long press puts the device into sleep mode
  - "Page Turn" - A short press in reading mode turns to the next page; a long press turns the device off
  - "Toggle Bookmark", "Reading Stats", "Mark Finished", "Refresh", "Change Font", "Guide Dots", "Bionic Reading", "Auto Page Turn", "Sync Progress", "File Transfer", "Calibre Wireless", "Join Network", "Hotspot", "Screenshot", "Dark Mode", "Browse Files", or "Save Clipping" - Run the matching action
  - "Footnotes" - A short press in reading mode opens the footnotes submenu; if only one footnote is present on the page, the referenced page is opened directly. The short press on the power button can be used to select the footnote in the submenu, and to go back to the original page after finish reading the footnote (like the back button).

- **Quick-return from footnotes**: Toggles on and off the quick return functionality from the footnotes. When the functionality it's active, a short press of the power button will act as the back button from the footnotes page.

#### 3.6.4 System

- **Time to Sleep**: Set the duration of inactivity before the device automatically goes to sleep. Values are in minutes, with a "Never" option at the end of the range.

- **Wi-Fi Networks**: Connect to Wi-Fi networks for file transfers and firmware updates.

- **KOReader Sync**: Options for setting up KOReader for syncing book progress.

- **OPDS Servers**: Manage one or more OPDS [(Open Publication Distribution System)](https://en.wikipedia.org/wiki/Open_Publication_Distribution_System) libraries for browsing and downloading books. See [OPDS Servers (Multiple Libraries)](#365-opds-servers-multiple-libraries) below.

- **Clear Reading Cache**: Clear the internal SD card cache.

- **Clean Library Cache**: Compare live books with cache directories and move confirmed orphans to recoverable `/.duet/books/.attic`. It refuses to clean after an incomplete or empty library scan.

- **Stats Export / Restore**: Create or restore a validated `.cstats` archive under `/.duet/backups/reading-stats`. The archive covers recognized global, daily, session, sync, library, per-book, and achievement state and maps compatible legacy records during migration. Importing an older archive that does not contain achievements preserves the current achievement ledger. Keep a normal SD-card backup as the primary recovery copy during the alpha.

- **Check for updates**: Check for Duet firmware updates over Wi-Fi. Firmware can also be updated without a USB connection by placing a `firmware.bin` file on the SD card.

- **Language**: Set the UI language. Duet currently ships 26 translation catalogs: English, Spanish, French, German, Czech, Slovak, Brazilian Portuguese, Russian, Swedish, Romanian, Catalan, Ukrainian, Belarusian, Italian, Polish, Finnish, Danish, Dutch, Turkish, Kazakh, Hungarian, Lithuanian, Slovenian, Valencian, Vietnamese, and Hebrew. Newer strings use a safe English fallback when a translation has not caught up.

#### 3.6.5 OPDS Servers (Multiple Libraries)

Duet supports saving multiple OPDS servers and switching between them when browsing catalogs.

1. Open **Settings -> System -> OPDS Servers**.

2. Select **Add Server** to create a new entry, or select an existing server to edit it.

3. Configure these fields:
   - **Server Name**: Optional display name (for example, "Home Calibre" or "Public Catalog").

   - **OPDS Server URL**: Full catalog root URL (for Calibre Content Server, usually ends with `/opds`).

   - **Username / Password**: Optional credentials for authenticated servers.

4. Use **Delete Server** inside a server entry to remove it.

Behavior notes:

- You can store up to 8 OPDS servers.
- OPDS authentication supports HTTP Basic auth. If you use Calibre Content Server with authentication enabled, set it to Basic (not Digest).

You can also manage OPDS servers from the web interface while in File Transfer mode:

1. Connect to the device web UI.
2. Open `http://<device-ip>/settings`.
3. Use the **OPDS Servers** card to add, edit, or delete entries.

For web-based Wi-Fi network management, see [Web Settings (Wi-Fi + OPDS)](#366-web-settings-wi-fi--opds).

#### 3.6.6 Web Settings (Wi-Fi + OPDS)

While in **File Transfer** mode, the web settings page includes management cards for both **Wi-Fi Networks** and **OPDS Servers**.

1. On device: open **File Transfer** and connect through **Join a Network** or **Create Hotspot**.
2. In a browser, open `http://<device-ip>/settings` or `http://crosspoint.local`.
3. In **Wi-Fi Networks**, add, edit, or delete saved network entries (SSID + optional password).
4. In **OPDS Servers**, add, edit, or delete OPDS catalogs.

Behavior notes:

- Passwords are never shown back in the web UI after saving.
- Leaving Password blank while editing keeps the existing saved password unchanged.
- The web UI can save hidden-network SSIDs, but connecting to hidden networks still depends on the device-side Wi-Fi connection flow.

#### 3.6.7 KOReader Sync Quick Setup

Duet can sync reading progress with KOReader-compatible sync servers. It also interoperates with KOReader apps/devices when they use the same server and credentials.

##### Option A: Free Public Server (`sync.koreader.rocks`)

1. Register a user once (only if needed):

```bash
USERNAME="user"
PASSWORD="pass"
PASSWORD_MD5="$(printf '%s' "$PASSWORD" | openssl md5 | awk '{print $2}')"

curl -i "https://sync.koreader.rocks/users/create" \
  -H "Accept: application/vnd.koreader.v1+json" \
  -H "Content-Type: application/json" \
  --data "{\"username\":\"$USERNAME\",\"password\":\"$PASSWORD_MD5\"}"
```

Already have KOReader Sync credentials? Skip registration; basic sync only requires using the same existing username/password on all devices.

When this returns `HTTP 402` with `{"code":2002,"message":"Username is already registered."}`, pick a different username or use that existing account.

2. On each device:
   - Go to **Settings -> System -> KOReader Sync**.

   - Set **Username** and **Password** (enter the plain password; Duet computes MD5 internally, and use the same values on all devices).

   - Set **Sync Server URL** to `https://sync.koreader.rocks`, or leave it empty (both use the same default KOReader sync server).

   - Run **Authenticate**.

3. While reading, press **Confirm** to open the reader menu, then select **Sync Progress**.
   - Choose **Apply Remote** to jump to remote progress.

   - Choose **Upload Local** to push current progress.

##### Option B: Self-Hosted Server (Docker Compose)

1. Start a sync server:

```bash
mkdir -p kosync-quickstart
cd kosync-quickstart

cat > compose.yaml <<'YAML'
services:
  kosync:
    image: koreader/kosync:latest
    ports:
      - "7200:7200"
      - "17200:17200"
    volumes:
      - ./data/redis:/var/lib/redis
    environment:
      - ENABLE_USER_REGISTRATION=true
    restart: unless-stopped
YAML

# Docker
docker compose up -d

# Podman (alternative)
podman compose up -d
```

> [!NOTE] `ENABLE_USER_REGISTRATION=true` is convenient for first setup. After creating your users, set it to `false` (or remove it) to avoid unexpected registrations.

2. Verify the server:

```bash
curl -H "Accept: application/vnd.koreader.v1+json" "http://<server-ip>:17200/healthcheck"
# Expected: {"state":"OK"}
```

3. Register a user once. Duet authenticates against KOReader Sync (`koreader/kosync`) using an MD5 key, so register using the MD5 of your password:

> [!WARNING] Sending a reusable MD5-derived password over plain HTTP is insecure. Create unique sync-only credentials and do not reuse main account passwords. Prefer `https://<server-ip>:7200` whenever traffic leaves a fully trusted LAN or when using untrusted networks. Use `curl -k` only for self-signed certificate testing.

```bash
USERNAME="user"
PASSWORD="pass"
PASSWORD_MD5="$(printf '%s' "$PASSWORD" | openssl md5 | awk '{print $2}')"

curl -i "http://<server-ip>:17200/users/create" \
  -H "Accept: application/vnd.koreader.v1+json" \
  -H "Content-Type: application/json" \
  --data "{\"username\":\"$USERNAME\",\"password\":\"$PASSWORD_MD5\"}"
```

If this returns `HTTP 402` with `{"code":2002,"message":"Username is already registered."}`, the account already exists.

4. On each device:
   - Go to **Settings -> System -> KOReader Sync**.

   - Set **Username** and **Password** (enter the plain password; Duet computes MD5 internally, and use the same values on all devices).

   - Set **Sync Server URL** to `http://<server-ip>:17200`.

   - Run **Authenticate**.

If you use the HTTPS listener, use `https://<server-ip>:7200` (`curl -k` only for self-signed certificate testing).

5. While reading, press **Confirm** to open the reader menu, then select **Sync Progress**.
   - Choose **Apply Remote** to jump to remote progress.

   - Choose **Upload Local** to push current progress.

### 3.7 Sleep Screen

The **Sleep Screen** setting controls what is displayed when the device goes to sleep:

| Mode | Behavior |
| --- | --- |
| **Dark** (default) | The Duet logo on a dark background. |
| **Light** | The Duet logo on a white background. |
| **Custom** | A custom image from the SD card (see below). Falls back to **Dark** if no custom image is found. |
| **Cover** | The cover of the currently open book. Falls back to **Dark** if no book is open. |
| **Cover + Custom** | The cover of the currently open book, shown only while actively reading. Falls back to **Custom** behavior when not reading. |
| **Page Overlay** | BMP or transparency-aware PNG artwork composited over the stored reader page. |
| **Reading Stats** | Recent reading statistics. |
| **Minimal** | A compact sleep screen based on the Minimal home layout. |
| **Minimal Stats** | A compact stats sleep screen on X3; hidden in the X4 settings UI. |
| **Dashboard** | The Dashboard current-book cover and configured statistics. |
| **Quick Resume** | Preserve the last visible content for a faster return. |
| **None** | A blank screen. |

#### Cover settings

When using **Cover** or **Cover + Custom**, two additional settings apply:

- **Sleep Screen Cover Mode**: **Fit** (scale to fit, white borders) or **Crop** (scale and crop to fill the screen).
- **Sleep Screen Cover Filter**: **None** (grayscale), **Contrast** (black & white), or **Inverted** (inverted black & white).

#### Custom images

To use custom sleep images, set the sleep screen mode to **Custom**, **Cover + Custom**, or **Page Overlay**, then place images on the SD card:

- **Multiple Images (recommended):** Create a `.sleep` directory in the root of the SD card and place any number of `.bmp` images inside. One will be randomly selected each time the device sleeps. (A directory named `sleep` is also accepted as a fallback.)
- **Single Image:** Place a file named `sleep.bmp` in the root directory. This is used as a fallback if no valid images are found in the `.sleep`/`sleep` directory.
- **Transparent Overlay:** In Page Overlay mode, `.png` files are also accepted. Pixels below the alpha threshold leave the stored reader page visible. PNG files are not used by ordinary Custom mode.

> [!TIP] For best results:
>
> - Use uncompressed BMP files with 24-bit color depth
> - X4: Use a resolution of 480x800 pixels to match the device's screen resolution.
> - X3: Use a resolution of 528x792 pixels to match the device's screen resolution.

> [!TIP] You can set an image as the sleep screen cover directly from the BMP image viewer in the **[Browse Files](#33-browse-files-screen)** screen.

---

### 3.8 Custom Fonts (SD Card)

Duet supports loading additional fonts from the SD card, extending beyond the two selectable built-ins, Lexend Deca and Bitter. ChareInk7 supplies selected fallback glyphs but is not a third selectable built-in. Custom fonts can include extended Unicode coverage, enabling CJK (Chinese, Japanese, Korean) and other scripts.

There are three ways to install fonts:

1. **Download from device (recommended):** Go to **Settings -> Reader -> Font Options -> Manage Fonts**, browse the available font families, and select one to download over Wi-Fi.
2. **Upload via web interface:** While in **File Transfer** mode, open the web UI in a browser and navigate to the **Fonts** tab to upload `.cpfont` files.
3. **Manual SD card copy:** Download the complete [123-family Duet Open Font Pack](https://github.com/lauren-alexandra/duet-xteink/releases/download/v0.1.0-alpha.8/Duet-Open-Font-Pack-v1.zip), choose compatible files from the credited [upstream CrossInk fonts repository](https://github.com/uxjulia/crossink-fonts/releases), or follow the upstream source recorded for each Duet family, then copy the selected `.cpfont` files to `/.fonts/` (preferred) or `/fonts/` on your SD card.

Once installed, custom fonts appear in **Settings -> Reader -> Font Options -> Font Family** alongside the built-in fonts.

Families are grouped as Serif, Sans Serif, Mono/Typewriter, Accessibility, Handwritten/Script, and Blackletter/Decorative. The picker shows compact Normal, Italic, and Bold specimens and synthesizes bold and/or italic when a family lacks that face. Use **Compare Fonts** for a matched-size A/B view.

See [docs/sd-card-fonts.md](./docs/sd-card-fonts.md) for full installation details and SD card folder structure.

---

### 3.9 Library Views, Search, and More Info

At a book-folder level, choose list, 2x2, 3x3, 4x4, or five-cover carousel presentation. Cover work is cached by exact layout size; the optional [desktop prefill](./docs/COVER_PREFILL.md) prepares all X3/X4 thumbnails on a computer for large libraries.

**Search Library** uses the optional `library_catalog.tsv` to rank matches across any title word, author, series, and catalog tags. Move through the suggestions/results with the navigation buttons. A short Confirm opens a result; a one-second Confirm hold opens **More Info**.

More Info can show cover, title, author, series/index, genre, an optional spice/heat tag, reading state, progress, and catalog description, with an **Open** action. Copying a book to the SD card does not generate a description: descriptions, categories, series data, and spice metadata come from the optional `/.duet/state/library_catalog.tsv`. Spice is a personal-library field rather than a Duet requirement. Leave `spice_level` absent or blank and Duet omits it from that book, collapses Reading Taste to genre and author, and leaves spice achievements inactive. A book remains readable without the catalog; only catalog-backed fields are missing.

### 3.10 Reading Statistics

Reading time accumulates while a supported book is actively open. A session is added to session count/history only after at least one page turn; time spent before that page turn is still retained. The active record is committed before deep sleep.

Duet has 33 top-level statistics pages covering current-book progress, calendar and heatmap history, sessions, pace, time of day, goals, streaks, Reading Dates, Reader DNA, Reading Signature, Wrapped, library/taste/series views, and separate This Device and All Devices totals. Device Split and All Devices appear after synced-device data exists; the guarded Book Dates editor opens from Reading Dates when a book can be edited. See the exact page-by-page list in [FEATURES.md](./FEATURES.md#the-33-top-level-pages).

True visible WPM and reference-page statistics require compatible `META-INF/x-locations.json` metadata inside an EPUB. Plain EPUBs still read normally. Run [EPUB WPM Preparation](./docs/EPUB_LOCATION_ENRICHMENT.md) once after adding new books; dry-run first, then use `--backup --backup-dir` so backups stay outside the device-ready shelf. Current-book WPM can feed Pace, DNA, and Signature pages through attributable ledger entries without reopening EPUBs during rendering. Duet shows a dash or excludes historical entries when it cannot calculate a defensible WPM and keeps screen-page pace internal for time-left and rhythm estimates.

**Reading Profile** is a raw seven-day, 12-metric grid. **Reader DNA** is a separate normalized 30-day six-axis view. Books under `/ignore_stats/` retain progress without contributing to aggregate statistics.

### 3.11 Nearby Sync

Nearby sync is direct between two Duet readers over ESP-NOW. It does not need a Wi-Fi network, cloud service, or account.

- **Nearby Position Sync** compares the current EPUB position and moves only after you choose Apply.
- **Nearby Stats Sync** protocol v6 exchanges global totals, per-book summaries/details, daily journal, attribution ledger, Stats Date, persistent achievement milestones, device name, and retained peer snapshots.

Both readers must run a protocol v6-capable Duet build; protocol v6 does not pair with the protocol v5 implementation in Alpha.7. Keep both readers on the sync screen until both report success. Repeated merge convergence remains an alpha acceptance target. KOReader Sync is separate and handles remote reading position, not Duet's statistics.

### 3.12 Apps and Utilities

The customizable Apps launcher includes Browse Files, Search Library, Recent Books, Reading Stats, Heatmap, Reading Profile, Saved Items, Favorites, Achievements, Dictionary, Tetris, If Found, Screen Clean, Nearby Stats Sync, File Transfer, OPDS, KOReader setup, Sleep, Read Me, Customize Home & Apps, and Settings.

**If Found** gives whoever finds a lost reader a simple way to see return instructions. Create a plain-text file named `/if_found.txt` at the SD-card root and put the contact or recovery message you are comfortable showing on the device inside it. Opening **If Found** from Home or Apps displays that message on a scrollable screen. If the file is absent or empty, Duet shows a setup reminder instead. The file stays on the SD card and is never part of Duet's public release package.

Duet includes 108 persistent achievements: 62 thresholds adapted from CPR-vCodex and 46 Duet milestones. Unlocks persist across restarts and can be adopted retroactively from reading history. Protocol v6 Nearby Stats Sync merges the highest milestone reached for each achievement metric, and `.cstats` includes both local and retained peer achievement ledgers.

---

## 4. Reading Mode

Once you have opened a book, the button layout changes to facilitate reading.

### Page Turning

| Action            | Buttons                              |
| ----------------- | ------------------------------------ |
| **Previous Page** | Press **Left** _or_ **Volume Up**    |
| **Next Page**     | Press **Right** _or_ **Volume Down** |

The role of the volume (side) buttons can be swapped in the **[Controls Settings](#363-controls)**.

If the **Short-press Action** setting is set to "Page Turn", you can also turn to the next page by briefly pressing the Power button.

### Chapter Navigation

- **Next Chapter:** Press and **hold** the **Right** (or **Volume Down**) button briefly, then release.
- **Previous Chapter:** Press and **hold** the **Left** (or **Volume Up**) button briefly, then release.

This feature can be disabled in the **[Controls Settings](#363-controls)** to help avoid changing chapters by mistake.

### Auto Page Turn

Auto Page Turn automatically advances pages at a set interval, useful for hands-free reading. This feature can be enabled and configured from the **[Reader Menu](#5-reader-menu)** while reading an EPUB.

### Tilt Page Turn (X3 only)

On the **Xteink X3**, the gyroscope can be used to turn pages by tilting the device. This feature is available in **Settings -> Controls**.

### Footnote Navigation

When reading an EPUB that contains footnotes, you can navigate to the footnote text by selecting the footnote reference in the book. From the footnote, you can return to your original reading position.

If the device goes to sleep or you close the book while viewing a footnote, the book reopens to your original reading position, not the footnote.

### System Navigation

- **Return to Home:** Press the **Back** button to close the book and return to the **[Home](#31-home-screen)** screen.
- **Return to Browse Files:** Press and hold the **Back** button to close the book and return to the **[Browse Files](#33-browse-files-screen)** screen.
- **Reader Menu:** Press **Confirm** to open the **[Reader Menu](#5-reader-menu)**, which includes chapter navigation, reading options, and more.

### Supported Languages

Duet renders text using the following Unicode character blocks, enabling support for a wide range of languages:

- **Latin Script (Basic, Supplement, Extended-A/B):** Covers English, German, French, Spanish, Portuguese, Italian, Dutch, Swedish, Norwegian, Danish, Finnish, Polish, Czech, Hungarian, Romanian, Slovak, Slovenian, Turkish, Catalan, and others.
- **Cyrillic Script (Standard and Extended):** Covers Russian, Ukrainian, Belarusian, Bulgarian, Serbian, Macedonian, Kazakh, Kyrgyz, Mongolian, and others.
- **Vietnamese:** Supported via extended Latin glyph coverage in the built-in reader fonts.

What is not supported with built-in reader fonts: Chinese, Japanese, Korean, Arabic, Greek, Hebrew, and Farsi. However, **CJK, Hebrew, Greek, and other extended scripts can be enabled by installing custom SD card fonts** — see [Custom Fonts (SD Card)](#38-custom-fonts-sd-card).

---

## 5. Reader Menu

Press **Confirm** while reading to open the compact left-side reader overlay. It provides Chapters, Dictionary, Go To, Sync, Stats, Tilt (where available), Auto Turn, Spacing, Reader Options, and More. **Sync** opens a KOReader/Nearby chooser; **More** opens the full Reader Menu.

Available options include:

- **Select Chapter** – Open the table of contents to jump to a specific chapter (see [Chapter Selection](#51-chapter-selection) below).
- **Footnotes** – Navigate to the footnotes for the current section _(only shown in books that contain footnotes)_.
- **Reader Options** – Open options for the current EPUB without leaving the book. Changes made here are saved as that book's override; use **Settings -> Reader** outside the book to change the global defaults instead.
- **Book Info** – Open the current book's More Info page.
- **Dictionary** – Select and look up a word without leaving the book.
- **Clippings** – Select/save text and browse saved excerpts.
- **Controls** – Open reader control options without leaving the book.
- **Reading Orientation** – Cycle through screen orientations without leaving the reader.
- **Auto Turn Interval** – Configure automatic page turns for hands-free reading.
- **Go to %** – Jump to a specific position in the book by percentage.
- **Add Bookmark / Remove Bookmark** – Toggle a bookmark on the current page.
- **View Bookmarks / Delete Bookmarks** – Manage existing bookmarks when the book has bookmarks.
- **Take screenshot** – Save a screenshot of the current page to the `screenshots/` folder.
- **Show page as QR** – Display a QR code encoding the current reading position.
- **Delete Book Cache** – Clear the cached layout data for the current book, forcing a re-index on next open.
- **Sync Progress** – Push or pull reading progress with a KOReader sync server (see [KOReader Sync Quick Setup](#367-koreader-sync-quick-setup)).
- **Reading Stats** – Open the current book's reading stats.
- **Mark Finished / Mark Unfinished** – Toggle whether the current book is marked as finished.

Press **Back** at any time to close the menu and return to your current page.

### 5.1 Chapter Selection

Accessible by selecting **Chapters** from the Reader Menu.

1. Use **Left** (or **Volume Up**), or **Right** (or **Volume Down**) to highlight the desired chapter.
2. Press **Confirm** to jump to that chapter.
3. _Alternatively, press **Back** to cancel and return to your current page._

---

### 5.2 Bookmarks

Bookmarks can be created to quickly save and restore your place in a book.

To create a bookmark, hold **Confirm** for 1 second while inside a book. A popup will appear letting you know a bookmark was created. The popup message will automatically disappear in a couple of seconds.

To open bookmarks, press **Confirm** while inside a book. Then navigate to the **Bookmarks** menu. Bookmarks can be opened by navigating to them and pressing **Confirm**, which will redirect you to that place in the book. You can delete bookmarks by holding **Confirm** for 1 second, and then pressing **Confirm** again to confirm deletion, or **Back** to cancel.

Bookmarks are stored as per-book `.bin` files in `/.duet/state/bookmarks`. Legacy bookmark files are migrated or merged when found.

### 5.3 Dictionary

Duet reads offline StarDict dictionaries from `/dictionaries/<Name>/`. The current release has a separate ready-to-copy WordNet 3.0 download. Follow [Dictionary Setup](./docs/DICTIONARY_SETUP.md) for the exact card structure, first index preparation, reader controls, `.dict.dz` extraction, and troubleshooting.

After the dictionary reports **Dictionary ready**, press **Confirm** inside a book and choose **Dictionary**. Use the page buttons to move between text rows, **Left/Right** to move between words, and **Confirm** to look up the selected word. The full reader menu's **Lookup history** returns to definitions previously opened in that book.

## 6. Current Limitations & Roadmap

Duet is in active alpha development. Current boundaries and test targets:

- **First-use Covers:** On-device generation can be slow for a large library. Use the [desktop cover prefill](./docs/COVER_PREFILL.md) after loading many books.
- **Unsupported Image Formats:** Most JPG and PNG images in EPUBs render correctly. GIFs and progressive JPEGs are not supported and will fall back to an `[Image]` placeholder.
- **Complex Chapters:** Memory-heavy chapters may fall back to a safer render profile, and guarded next-chapter pre-indexing can remain visible on difficult books.
- **Nearby Sync:** Repeated two-device convergence and physical `.cstats` restore remain explicit public-alpha acceptance targets.
- **Not Included:** PDF rendering, flashcards, a virtual pet, general web/RSS browsing, audio/audiobooks, and typed notes.

---

## 7. Troubleshooting Issues & Escaping Bootloop

If an issue or crash is encountered while using Duet, feel free to raise an issue ticket and attach the logs.

**Crash reports on SD card:** After a crash, Duet automatically saves a crash report to the SD card (no USB connection needed). Check the root of the SD card for a crash log file and include it with any bug report.

**Serial monitor logs:** For more detailed debugging, connect the device to a computer and run the custom debugging monitor script (requires Python 3 with `pyserial`, `colorama`, and `matplotlib`; install via `pip3 install pyserial colorama matplotlib`):

```
python3 scripts/debugging_monitor.py
```

The script auto-detects the serial port. You can also specify one explicitly:

```
python3 scripts/debugging_monitor.py /dev/ttyACM0        # Linux
python3 scripts/debugging_monitor.py /dev/tty.usbmodem1  # macOS
python3 scripts/debugging_monitor.py COM7                # Windows
```

**Features:**

- Color-coded log output by category (errors, memory, display, EPUB parsing, etc.)
- Live memory usage graph (free RAM, total RAM, max contiguous allocation) updated every second
- Interactive command prompt — type a command and press Enter to send it to the device
- Screenshot capture — saves the current display to `screenshot.bmp` when triggered by the device

**Options:**

| Option | Description |
| --- | --- |
| `--baud RATE` | Baud rate (default: 115200) |
| `--filter KEYWORD` | Show only lines containing the keyword (case-insensitive) |
| `--suppress KEYWORD` | Hide lines containing the keyword (case-insensitive) |

**Examples:**

```
# Show only memory-related log lines
python3 scripts/debugging_monitor.py --filter MEM

# Hide noisy SD card log lines
python3 scripts/debugging_monitor.py --suppress "[SD]"
```

Press **Ctrl-C** or close the graph window to exit.

If the device is stuck in a bootloop, press and release the Reset button. Then, press and hold on to the configured Back button and the Power Button to boot to the Home Screen.

Do not delete `/.duet`, `/.crossink`, or `/.crosspoint` as a generic troubleshooting step; together they contain current or recoverable settings, reading state, bookmarks, statistics, and caches. Back up the card first. Use the book-specific **Delete Cache**, **Clear Reading Cache**, or preservation-safe **Clean Library Cache** tools, and include the crash report in an issue. See [Troubleshooting](./docs/troubleshooting.md) for targeted recovery.
