# CPR-vCodex User Guide

Welcome to the **CPR-vCodex** firmware. This guide outlines the hardware controls, navigation, and reading features of the device.

- [CPR-vCodex User Guide](#cpr-vcodex-user-guide)
  - [1. Hardware Overview](#1-hardware-overview)
    - [Button Layout](#button-layout)
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
    - [3.6 Settings](#36-settings)
      - [3.6.1 Display](#361-display)
      - [3.6.2 Reader](#362-reader)
      - [3.6.3 Controls](#363-controls)
      - [3.6.4 System](#364-system)
      - [3.6.5 OPDS Servers (Multiple Libraries)](#365-opds-servers-multiple-libraries)
      - [3.6.6 KOReader Sync Quick Setup](#366-koreader-sync-quick-setup)
    - [3.7 Sleep Screen](#37-sleep-screen)
  - [4. Reading Mode](#4-reading-mode)
    - [Page Turning](#page-turning)
    - [Chapter Navigation](#chapter-navigation)
    - [System Navigation](#system-navigation)
    - [Supported Languages](#supported-languages)
  - [5. Chapter Selection Screen](#5-chapter-selection-screen)
  - [6. Current Limitations \& Roadmap](#6-current-limitations--roadmap)
  - [7. Troubleshooting Issues \& Escaping Bootloop](#7-troubleshooting-issues--escaping-bootloop)


## 1. Hardware Overview

The device utilises the standard buttons on the Xteink X4 (in the same layout as the manufacturer firmware, by default):

### Button Layout
| Location        | Buttons                                              |
| --------------- | ---------------------------------------------------- |
| **Bottom Edge** | **Back**, **Confirm**, **Left**, **Right**           |
| **Right Side**  | **Power**, **Volume Up**, **Volume Down**, **Reset** |

Button layout can be customized in the **[Controls Settings](#363-controls)**.

### Taking a Screenshot
When the Power Button and Volume Down button are pressed at the same time, it will take a screenshot and save it in the folder `screenshots/`.

Alternatively, while reading a book, press the **Confirm** button to open the reader menu and select **Take screenshot**.

---

## 2. Power & Startup

### Power On / Off

To turn the device on or off, **press and hold the Power button for approximately half a second**.
In the **[Controls Settings](#363-controls)** you can configure the power button to turn the device off with a short press instead of a long one.

To reboot the device (for example after a firmware update or if it's frozen), press and release the Reset button, and then quickly press and hold the Power button for a few seconds.

### First Launch

Upon turning the device on for the first time, you will be placed on the **[Home](#31-home-screen)** screen.

> [!NOTE]
> On subsequent restarts, the firmware will automatically reopen the last book you were reading.

---

## 3. Screens

### 3.1 Home Screen

The Home screen is the main entry point to the firmware. From here you can navigate to **[Reading Mode](#4-reading-mode)** with the most recently read book, the **[Browse Files](#33-browse-files-screen)** screen, the **[Recent Books](#34-recent-books-screen)** screen, the **[File Transfer](#35-file-transfer-screen)** screen, or **[Settings](#36-settings)**.

### 3.2 Reading Mode

See [Reading Mode](#4-reading-mode) below for more information.

### 3.3 Browse Files Screen

The Browse Files screen acts as a file and folder browser.

* **Navigate List:** Use **Left** (or **Volume Up**), or **Right** (or **Volume Down**) to move the selection cursor up and down through folders and books. You can also long-press these buttons to scroll a full page up or down.
* **Open Selection:** Press **Confirm** to open a folder or read a selected book. 
* **Delete Files:** Hold and release **Confirm** to delete the selected file. You will be given an option to either confirm or cancel deletion. Folder deletion is not supported.

### 3.4 Recent Books Screen

The Recent Books screen lists the most recently opened books in a chronological view, displaying title and author.

### 3.5 File Transfer Screen

The File Transfer screen allows you to upload new e-books to the device. When you enter the screen, you'll be prompted with a WiFi selection dialog and then your X4 will start hosting a web server.

See the [webserver docs](./docs/webserver.md) for more information on how to connect to the web server and upload files.

> [!TIP]
> Advanced users can also manage files programmatically or via the command line using `curl`. See the [webserver docs](./docs/webserver.md) for details.

### 3.5.1 Calibre Wireless Transfers

CPR-vCodex supports sending books from Calibre using the CrossPoint Reader device plugin.

1. Install the plugin in Calibre:
   - Head to https://github.com/crosspoint-reader/calibre-plugins/releases to download the latest version of the crosspoint_reader plugin.
   - Download the zip file.
   - Open Calibre → Preferences → Plugins → Load plugin from file → Select the zip file.
2. On the device: File Transfer → Connect to Calibre → Join a network.
3. Make sure your computer is on the same WiFi network.
4. In Calibre, click "Send to device" to transfer books.

### 3.6 Settings

The Settings screen allows you to configure the device's behavior. There are a few settings you can adjust:

#### 3.6.1 Display

- **Sleep Screen**: Which sleep screen to display when the device sleeps:
  - "Dark" (default) - The default dark Crosspoint logo sleep screen
  - "Light" - The same default sleep screen, on a white background
  - "Custom" - Custom images from the SD card; see [Sleep Screen](#37-sleep-screen) below for more information
  - "Cover" - The book cover image (Note: this is experimental and may not work as expected)
  - "None" - A blank screen
  - "Cover + Custom" - The book cover image, falls back to "Custom" behavior
  - "Reading Dashboard" - Reading goal, streak, totals, achievement progress, and recent-book context
  - "Cover + Stats v1" - The book cover plus compact book/global reading stats
  - "Cover + Stats v2" - The book cover plus a compact global stats footer
  - "Custom + Stats v1" - Custom sleep image plus the same compact book/global reading stats
  - "Custom + Stats v2" - Custom sleep image plus the compact global stats footer
- **Sleep Screen Cover Mode**: How to display the book cover when "Cover" sleep screen is selected:
  - "Fit" (default) - Scale the image down to fit centered on the screen, padding with white borders as necessary
  - "Crop" - Scale the image down and crop as necessary to try to fill the screen (Note: this is experimental and may not work as expected)
- **Sleep Screen Cover Filter**: What filter will be applied to the book cover when "Cover" sleep screen is selected:
  - "None" (default) - The cover image will be converted to a grayscale image and displayed as it is
  - "Contrast" - The image will be displayed as a black & white image without grayscale conversion
  - "Inverted" - The image will be inverted as in white & black and will be displayed without grayscale conversion
- **Status Bar**: Configure the status bar displayed while reading:
  - "None" - No status bar
  - "No Progress" - Show status bar without reading progress
  - "Full w/ Percentage" - Show status bar with book progress (as percentage)
  - "Full w/ Book Bar" - Show status bar with book progress (as bar)
  - "Book Bar Only" - Show book progress (as bar)
  - "Full w/ Chapter Bar" - Show status bar with chapter progress (as bar)
- **Hide Battery %**: Configure where to suppress the battery percentage display in the status bar; the battery icon will still be shown:
  - "Never" (default) - Always show battery percentage
  - "In Reader" - Show battery percentage everywhere except in reading mode
  - "Always" - Always hide battery percentage
- **Refresh Frequency**: Set how often the screen does a full refresh while reading to reduce ghosting; options are every 1, 5, 10, 15, or 30 pages.

- **UI Theme**: Set which UI theme to use:
  - "Lyra" - The new theme for Crosspoint featuring rounded elements and menu icons
  - "Lyra vCodex" - CPR-vCodex's 3-book Home carousel with reading progress
  - "Lyra Carousel" - cover-focused 3-book Home carousel with recent/favorite book source
- **Sunlight Fading Fix**: Configure whether to enable a software-fix for the issue where white X4 models may fade when used in direct sunlight:
  - "OFF" (default) - Disable the fix
  - "ON" - Enable the fix

#### 3.6.2 Reader
- **Reader Font Family**: Choose the font used for reading:
  - "Bookerly" (default) - Amazon's reading font
  - "Noto Sans" - Google's sans-serif font
  - "Open Dyslexic" - Font designed for readers with dyslexia
- **Reader Font Size**: Adjust the text size for reading; options are "Small", "Medium" (default), "Large", or "X Large".

- **Reader Line Spacing**: Adjust the spacing between lines; options are "Tight", "Normal" (default), or "Wide".
- **Reader Screen Margin**: Controls the screen margins in Reading Mode between 5 and 40 pixels in 5-pixel increments.
- **Reader Paragraph Alignment**: Set the alignment of paragraphs; options are "Justified" (default), "Left", "Center", or "Right".
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
- **Bionic Reading**: Emphasizes the first portion of EPUB words to guide fixation points. `Normal` uses a stronger bold prefix, while `Subtle` uses a lighter emphasis.

#### 3.6.3 Controls

- **Remap Front Buttons**: A menu for customising the function of each bottom edge button.
- **Side Button Layout (reader)**: Swap the order of the up and down volume buttons from "Prev/Next" (default) to "Next/Prev". This change is only in effect when reading.

- **Long-press Chapter Skip**: Set whether long-pressing page turn buttons skips to the next/previous chapter:
  - "Chapter Skip" (default) - Long-pressing skips to next/previous chapter
  - "Page Scroll" - Long-pressing scrolls a page up/down
- **Short Power Button Click**: Controls the effect of a short click of the power button:
  - "Ignore" (default) - Require a long press to turn off the device
  - "Sleep" - A short press puts the device into sleep mode
  - "Page Turn" - A short press in reading mode turns to the next page; a long press turns the device off

#### 3.6.4 System

- **Time to Sleep**: Set the duration of inactivity before the device automatically goes to sleep; options are 1, 5, 10 (default), 15 or 30 minutes.

- **WiFi Networks**: Connect to WiFi networks for file transfers and firmware updates.
- **KOReader Sync**: Options for setting up KOReader for syncing book progress.
- **OPDS Servers**: Manage one or more OPDS [(Open Publication Distribution System)](https://en.wikipedia.org/wiki/Open_Publication_Distribution_System) libraries for browsing and downloading books. See [OPDS Servers (Multiple Libraries)](#365-opds-servers-multiple-libraries) below.
- **Clear Reading Cache**: Clear the internal SD card cache.
- **Check for updates**: Check for Crosspoint firmware updates over WiFi.
- **Language**: Set the system language (see **[Supported Languages](#supported-languages)** for more information).

#### 3.6.5 OPDS Servers (Multiple Libraries)

CrossPoint supports saving multiple OPDS servers and switching between them when browsing catalogs.

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

#### 3.6.6 KOReader Sync Quick Setup

CPR-vCodex can sync reading progress with KOReader-compatible sync servers.
It also interoperates with KOReader apps/devices when they use the same server and credentials.

Each account is stored as a named profile. A profile contains its own username, password, server, document matching method, metadata preference, and sync behavior. Up to eight profiles can be saved, and changing or registering one profile does not silently replace the active profile.

Profile options:

* **Send Metadata:** Include the EPUB filename, title, and authors in progress uploads. This is off by default.
* **Sync Behavior — Ask Every Time:** Show the remote/local choice during a manual sync.
* **Sync Behavior — Smart Sync:** Check both supported document hashes, upload when local progress is farther ahead, apply remote progress when it is farther ahead, and do nothing when both sides match.
* **Sign Up:** Create the account using this profile's username, password, and server. It neither activates the profile nor changes an already configured server.

##### Option A: Free Public Server (`sync.koreader.rocks`)

1. Add a profile under **Settings -> System -> KOReader Sync**, enter a username and password, leave the server empty (or use `https://sync.koreader.rocks`), save it, and choose **Sign Up** if the account does not exist yet.

The equivalent command-line registration is:

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

2. On each CPR-vCodex device:
   - Go to **Settings -> System -> KOReader Sync**.
   - Add or edit a named profile with the same **Username** and **Password** (enter the plain password; CPR-vCodex computes MD5 internally).
   - Set **Sync Server URL** to `https://sync.koreader.rocks`, or leave it empty (both use the default KOReader sync server).
   - Choose the metadata and sync behavior preferences, then save, **Set as Active**, and run **Authenticate**.

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

> [!NOTE]
> `ENABLE_USER_REGISTRATION=true` is convenient for first setup. After creating your users, set it to `false` (or remove it) to avoid unexpected registrations.

2. Verify the server:

```bash
curl -H "Accept: application/vnd.koreader.v1+json" "http://<server-ip>:17200/healthcheck"
# Expected: {"state":"OK"}
```

3. Register a user once with the profile's **Sign Up** action. The equivalent command-line method is shown below.
CPR-vCodex authenticates against KOReader Sync (`koreader/kosync`) using an MD5 key, so register using the MD5 of your password:

> [!WARNING]
> Sending a reusable MD5-derived password over plain HTTP is insecure.
> Create unique sync-only credentials and do not reuse main account passwords.
> Prefer `https://<server-ip>:7200` whenever traffic leaves a fully trusted LAN or when using untrusted networks.
> Use `curl -k` only for self-signed certificate testing.

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

4. On each CPR-vCodex device:
   - Go to **Settings -> System -> KOReader Sync**.
   - Add or edit a named profile with the same **Username** and **Password** (enter the plain password; CPR-vCodex computes MD5 internally).
   - Set that profile's **Sync Server URL** to `http://<server-ip>:17200`.
   - Choose the metadata and sync behavior preferences, then save, **Set as Active**, and run **Authenticate**.

If you use the HTTPS listener, use `https://<server-ip>:7200` (`curl -k` only for self-signed certificate testing).

5. While reading, press **Confirm** to open the reader menu, then select **Sync Progress**.
   - Choose **Apply Remote** to jump to remote progress.
   - Choose **Upload Local** to push current progress.

### 3.7 Sleep Screen

The **Sleep Screen** setting controls what is displayed when the device goes to sleep:

| Mode | Behavior |
|------|----------|
| **Dark** (default) | The CPR-vCodex logo on a dark background. |
| **Light** | The CPR-vCodex logo on a white background. |
| **Custom** | A custom image from the SD card (see below). Falls back to **Dark** if no custom image is found. |
| **Cover** | The cover of the currently open book. Falls back to **Dark** if no book is open. |
| **Cover + Custom** | The cover of the currently open book. Falls back to **Custom** behavior if no book is open. |
| **Reading Dashboard** | A reading summary with daily goal, streak, totals, achievements, and recent-book progress. |
| **Cover + Stats v1** | The current book cover with compact book/global reading stats. Falls back to **Reading Dashboard** if no cover can be rendered. |
| **Cover + Stats v2** | The current book cover with a compact global stats footer. Falls back to **Reading Dashboard** if no cover can be rendered. |
| **Custom + Stats v1** | A custom sleep image with the same compact book/global stats overlay as **Cover + Stats v1**. Falls back to **Reading Dashboard** if no custom image is found. |
| **Custom + Stats v2** | A custom sleep image with the same compact stats footer as **Cover + Stats v2**. Falls back to **Reading Dashboard** if no custom image is found. |
| **None** | A blank screen. |

#### Cover settings

When using **Cover** or **Cover + Custom**, two additional settings apply:

- **Sleep Screen Cover Mode**: **Fit** (scale to fit, white borders) or **Crop** (scale and crop to fill the screen).
- **Sleep Screen Cover Filter**: **None** (grayscale), **Contrast** (black & white), or **Inverted** (inverted black & white).

#### Custom images

To use custom sleep images, set the sleep screen mode to **Custom**, **Cover + Custom**, **Custom + Stats v1**, or **Custom + Stats v2**, then place images on the SD card:

- **Multiple Images (recommended):** Create a `.sleep` directory in the root of the SD card and place any number of `.bmp` or `.png` images inside. One will be randomly selected each time the device sleeps. (A directory named `sleep` is also accepted as a fallback, and another folder can be selected from Browse Files.)
- **Single Image:** Place a file named `sleep.bmp` or `sleep.png` in the root directory. This is used as a fallback if no valid images are found in the configured sleep-image directory.

> [!TIP]
> For best results:
> - Use uncompressed BMP files with 24-bit color depth
> - X4: Use a resolution of 480x800 pixels to match the device's screen resolution.
> - X3: Use a resolution of 528x792 pixels to match the device's screen resolution.

---

## 4. Reading Mode

Once you have opened a book, the button layout changes to facilitate reading.

### Page Turning
| Action            | Buttons                              |
| ----------------- | ------------------------------------ |
| **Previous Page** | Press **Left** _or_ **Volume Up**    |
| **Next Page**     | Press **Right** _or_ **Volume Down** |

The role of the volume (side) buttons can be swapped in the **[Controls Settings](#363-controls)**.

If the **Short Power Button Click** setting is set to "Page Turn", you can also turn to the next page by briefly pressing the Power button.

### Chapter Navigation
* **Next Chapter:** Press and **hold** the **Right** (or **Volume Down**) button briefly, then release.
* **Previous Chapter:** Press and **hold** the **Left** (or **Volume Up**) button briefly, then release.

This feature can be disabled in the **[Controls Settings](#363-controls)** to help avoid changing chapters by mistake.


### System Navigation
* **Return to Home:** Press the **Back** button to close the book and return to the **[Home](#31-home-screen)** screen.
* **Return to Browse Files:** Press and hold the **Back** button to close the book and return to the **[Browse Files](#33-browse-files-screen)** screen.
* **Chapter Menu:** Press **Confirm** to open the **[Table of Contents/Chapter Selection](#5-chapter-selection-screen)** screen.

### Supported Languages

CPR-vCodex renders text using the following Unicode character blocks, enabling support for a wide range of languages:

*   **Latin Script (Basic, Supplement, Extended-A):** Covers English, German, French, Spanish, Portuguese, Italian, Dutch, Swedish, Norwegian, Danish, Finnish, Polish, Czech, Hungarian, Romanian, Slovak, Slovenian, Turkish, and others.
*   **Cyrillic Script (Standard and Extended):** Covers Russian, Ukrainian, Belarusian, Bulgarian, Serbian, Macedonian, Kazakh, Kyrgyz, Mongolian, and others.

The built-in fonts do not provide complete glyph coverage for Chinese, Japanese, Korean, Vietnamese, Hebrew, Arabic, Greek, or Farsi. EPUB `<ruby>` annotations are supported, including native layout for Chinese/Japanese reading aids, when the selected SD-card font contains the required glyphs. Complex right-to-left shaping is not supported.

---

## 5. Chapter Selection Screen

Accessible by pressing **Confirm** while inside a book.

1.  Use **Left** (or **Volume Up**), or **Right** (or **Volume Down**) to highlight the desired chapter.
2.  Press **Confirm** to jump to that chapter.
3.  *Alternatively, press **Back** to cancel and return to your current page.*

---

## 6. Current Limitations & Roadmap

Please note that this firmware is currently in active development. The following features are **not yet supported** but are planned for future updates:

* **Images:** EPUB images render, including common raster images, packed low-depth PNGs, scaling, and local image references inside SVG. Very large or unusual images may fall back to a placeholder if they cannot be decoded within the available memory.
* **Cover Images:** Large cover images embedded into EPUB require several seconds (~10s for ~2000 pixel tall image) to convert for sleep screen and home screen thumbnail. Consider optimizing the EPUB with e.g. https://github.com/bigbag/epub-to-xtc-converter to speed this up.

---

## 7. Troubleshooting Issues & Escaping Bootloop

If an issue or crash is encountered while using Crosspoint, feel free to raise an issue ticket and attach the serial monitor logs. The logs can be obtained by connecting the device to a computer and starting a serial monitor. Either [Serial Monitor](https://www.serialmonitor.org/) or the following command can be used:

```
pio device monitor
```

If the device is stuck in a bootloop, press and release the Reset button. Then, press and hold on to the configured Back button and the Power Button to boot to the Home Screen.

### SD firmware recovery (including USB-locked X3)

The e-ink screen keeps its last image without power, so an unchanged screen does not necessarily mean the device is bricked. To enter the firmware picker without navigating the normal UI:

1. Put one known-good `.bin` firmware file in the SD card root. For deterministic blind recovery, use a clean FAT32/exFAT card with no directories or other BIN files.
2. Start with the device fully asleep. Hold the upper side/page button (`UP`), then press and hold Power as well.
3. Hold both buttons for 5–7 seconds. Release `UP` first, keep holding Power for about one more second, then release it.
4. Wait at least 30 seconds for the SD card and recovery picker to initialize.
5. Press the logical `Confirm` button once to select the BIN. With the default front-button mapping, this is the second bottom-edge button.
6. Wait up to two minutes while the firmware is validated.
7. Press the logical `Right` button once to accept the final confirmation. With the default mapping, this is the fourth bottom-edge button. The final dialog deliberately uses `Left` for cancel and `Right` for confirm; pressing `Confirm` again does not start flashing.
8. Do not press buttons, remove the SD card, or interrupt power for at least three minutes. The device restarts automatically after a successful flash.

When the display is not working, connect a serial monitor if possible and look for `Recovery firmware mode`, `Selected`, `image validation`, and `SD firmware update complete` messages before repeating any step.

There can be issues with broken cache or config. In this case, delete the `.crosspoint` directory on your SD card (or consider deleting only `settings.bin`, `state.bin`, or `epub_*` cache directories in the `.crosspoint/` folder).
