# SD Card Fonts

CPR-vCodex supports loading additional fonts from the SD card. Common
downloadable families are provided by CrossPoint, while the CPR-vCodex source
provides vCodex-specific additions such as ChareInk and Lexend.

## Installing Fonts

There are three ways to install fonts.

### Option 1: Download from device

1. Connect your CPR-vCodex reader to WiFi
2. Go to **Settings > Reader > Manage Fonts**
3. Browse available font families and tap to download
4. Downloaded fonts appear immediately in **Settings > Reader > Font Family**

### Option 2: Upload via web browser

1. Connect your CPR-vCodex reader to WiFi
2. Open the web interface in your browser (shown on the WiFi screen)
3. Navigate to the **Fonts** tab
4. Upload `.cpfont` files using the upload form

### Option 3: Manual SD card copy

For the fastest full CPR-vCodex font install, download
[`all-fonts.zip`](https://github.com/franssjz/cpr-vcodex/releases/download/sd-fonts-m1-b4/all-fonts.zip)
and extract it into the root of the microSD card. It creates the ready-to-use
`/fonts/<family>/*.cpfont` tree.

For a single family, download the `.cpfont` files for the family you want from
the [CPR-vCodex SD font release](https://github.com/franssjz/cpr-vcodex/releases/tag/sd-fonts-m1-b4),
then create a folder with the family name and copy its `.cpfont` files to one
of two locations on your SD card:

- `/.fonts/` - hidden directory (preferred; keeps the SD root tidy when mounted on a desktop)
- `/fonts/` - visible directory (use this if your OS hides dot-files and you'd rather see the folder in your file manager)

Both roots are always scanned at boot and the results are merged: a family
installed in `/fonts/` shows up even when `/.fonts/` also exists, and vice
versa. The two roots only collide if the same family name appears in both. In
that case the copy in `/.fonts/` wins and the duplicate in `/fonts/` is ignored.

```text
SD Card Root/
|-- .fonts/                     Hidden root (preferred)
|   `-- ChareInk/
|       |-- ChareInk_12.cpfont
|       |-- ChareInk_14.cpfont
|       |-- ChareInk_16.cpfont
|       `-- ChareInk_18.cpfont
`-- fonts/                      Visible root (equally valid)
    |-- Lexend/
    |   |-- Lexend_10.cpfont
    |   |-- Lexend_12.cpfont
    |   |-- Lexend_14.cpfont
    |   |-- Lexend_16.cpfont
    |   `-- Lexend_18.cpfont
    `-- MyFont/
        |-- MyFont_12.cpfont
        `-- ...
```

Insert the SD card and power on your CPR-vCodex reader. The installed families
will appear under **Settings > Reader > Font Family**.

## Available Pre-Built Fonts

The current list of CPR-vCodex-only pre-built fonts is maintained in
`lib/EpdFont/scripts/sd-fonts.yaml` and published as CPR-vCodex release assets:

- Stable device manifest: https://github.com/franssjz/cpr-vcodex/releases/tag/sd-fonts-m1-b4
- Manual CPR-vCodex font package: https://github.com/franssjz/cpr-vcodex/releases/download/sd-fonts-m1-b4/all-fonts.zip
- Device manifest: https://github.com/franssjz/cpr-vcodex/releases/download/sd-fonts-m1-b4/fonts.json

The `sd-fonts-m<META>-b<BIN>` tag is tied to the manifest schema and `.cpfont`
binary format supported by the firmware. When either format changes, update the
version constants and publish a new SD font release.

## Converting Custom Fonts

To convert your own TrueType/OpenType fonts:

### Prerequisites

    pip install freetype-py fonttools

### Single font (one style)

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      MyFont-Regular.ttf \
      --intervals latin-ext \
      --sizes 12,14,16,18 \
      --style regular \
      --name MyFont \
      --output-dir ./MyFont/

### Multi-style font

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py \
      --regular MyFont-Regular.ttf \
      --bold MyFont-Bold.ttf \
      --italic MyFont-Italic.ttf \
      --bolditalic MyFont-BoldItalic.ttf \
      --intervals latin-ext \
      --sizes 12,14,16,18 \
      --name MyFont \
      --output-dir ./MyFont/

### Available Unicode interval presets

| Preset | Coverage |
|--------|----------|
| `ascii` | U+0020-U+007E (Basic Latin) |
| `latin1` | U+0080-U+00FF (Latin-1 Supplement) |
| `latin-ext` | European languages (Latin + Extended-A/B + punctuation + ligatures) |
| `greek` | Greek + Extended Greek |
| `cyrillic` | Cyrillic + Supplement |
| `georgian` | Georgian + Georgian Supplement |
| `armenian` | Armenian |
| `ethiopic` | Ethiopic + Extended |
| `vietnamese` | Vietnamese subset (o/u horn and combining marks) |
| `ipa-chars` | IPA Extensions + Spacing Modifier Letters (phonetic transcription) |
| `punctuation` | General punctuation (U+2000-U+206F) |
| `cjk` | CJK Unified Ideographs + Hiragana + Katakana + Fullwidth |
| `hangul` | Korean Hangul syllables + Jamo + Compatibility Jamo |
| `cherokee` | Cherokee historic + supplement block |
| `tifinagh` | Tifinagh |
| `symbols` | Math, currency, arrows, box-drawing, misc symbols, dingbats |
| `reading` | Literary fiction coverage: Latin, Greek, Cyrillic, math/symbol blocks, supplemental punctuation, and CJK quote marks |
| `builtin` | Matches built-in Bookerly coverage exactly |

Combine presets with commas: `--intervals latin-ext,greek,cyrillic`

You can also specify arbitrary Unicode ranges directly:
`--intervals latin-ext,(0x2100-0x214F)`

To list all presets with codepoint counts:

    python3 lib/EpdFont/scripts/fontconvert_sdcard.py --list-presets

### Additional options

`--force-autohint` - force FreeType's auto-hinter instead of the font's native hinting.

Install custom fonts via WiFi upload or manual SD card copy.
