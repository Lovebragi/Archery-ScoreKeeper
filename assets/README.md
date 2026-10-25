<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

### Archery scorekeeper Chinese subset

The archery scorekeeper renders Chinese with a purpose-built subset instead of a
full CJK face (the display has 8 MB Flash, no PSRAM, and the application knows
every string it can print).

| File | Content and use |
| --- | --- |
| [`fonts/archery_chars.txt`](fonts/archery_chars.txt) | Non-ASCII code point inventory. Printable ASCII `U+0020`–`U+007E` is always included implicitly. |
| [`fonts/archery_font_16.c`](fonts/archery_font_16.c) | Body text, 16 px, 4 bpp PLAIN, 24 px line height, about 19 KB of bitmap data. |
| [`fonts/archery_font_20.c`](fonts/archery_font_20.c) | Titles and emphasis, 20 px, 4 bpp PLAIN, 30 px line height, about 29 KB of bitmap data. |
| [`fonts/archery_font_16.json`](fonts/archery_font_16.json), [`fonts/archery_font_20.json`](fonts/archery_font_20.json) | Per-glyph metrics and SHA-256 of the packed bitmap, checked by `tests/test_archery_fonts.py`. |
| [`fonts/OFL.txt`](fonts/OFL.txt) | SIL Open Font License 1.1 of the source family. |

- Source family: Noto Sans SC, weight 400 (variable font default instance is
  weight 100, so the generator sets the axis explicitly).
- Source: <https://github.com/google/fonts/tree/main/ofl/notosanssc>
  (`NotoSansSC[wght].ttf`, OFL-1.1). The source font is not committed because
  the generated subset is sufficient and keeps the checkout small.
- Conversion: `tools/gen_lvgl_text_font.py` rasterizes with Pillow/FreeType and
  emits the LVGL 9 `fmt_txt` format directly. `lv_font_conv` is intentionally not
  used; see the command in
  [`docs/applications/archery-scorekeeper.md`](../docs/applications/archery-scorekeeper.md)
  and the inventory header.
- Destination: linked into the `main` component by `main/CMakeLists.txt`; the UI
  selects `archery_font_16` / `archery_font_20` through `main/archery_fonts.h`.
- Flash impact: about 79 KB of glyph bitmaps plus descriptors, resident in
  read-only Flash (no static RAM).
- Coverage contract: the static test fails when UI text needs a code point that
  is missing from either font or from the inventory. Regenerate both sizes after
  changing UI text.


## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.
