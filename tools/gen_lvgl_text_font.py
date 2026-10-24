#!/usr/bin/env python3
"""Generate an LVGL 9 `lv_font_t` C source from a TrueType/OpenType font.

Why this exists: `lv_font_conv` is a Node tool and this repository ships no
Node toolchain; more importantly its 4 bpp PLAIN output was observed to be
corrupt on current Node versions. Rasterizing with Pillow/FreeType and emitting
the LVGL format directly keeps the pipeline reproducible and lets the emitted C
file be parsed back and verified byte for byte (see tests/test_archery_fonts.py).

Output format: one dense FORMAT0_TINY cmap for printable ASCII plus one
SPARSE_TINY cmap for every other code point, 4 bpp PLAIN bitmaps packed
continuously (high nibble first), no kerning.

Example:
    python tools/gen_lvgl_text_font.py \
        --font /path/NotoSansSC-VF.ttf --weight 400 --size 16 \
        --name archery_font_16 \
        --inventory assets/fonts/archery_chars.txt \
        --scan main/archery_ui.c main/archery_app.c main/archery_fonts.c \
        --update-inventory \
        --output assets/fonts/archery_font_16.c \
        --manifest assets/fonts/archery_font_16.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import lvgl_font_tools as tools  # noqa: E402  (local import after sys.path)

CMAP_FORMAT0_TINY = "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY"
CMAP_SPARSE_TINY = "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY"
GLYPH_ID_ASCII_START = 1


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--font", required=True, help="TTF/OTF source font")
    parser.add_argument("--weight", type=int, default=None,
                        help="variable-font weight axis value (e.g. 400)")
    parser.add_argument("--size", type=int, required=True, help="pixel size")
    parser.add_argument("--name", required=True, help="C symbol / file base name")
    parser.add_argument("--inventory", help="U+XXXX inventory file to read and update")
    parser.add_argument("--scan", nargs="*", default=[],
                        help="C sources whose string literals are added to the set")
    parser.add_argument("--update-inventory", action="store_true",
                        help="write the union back to --inventory")
    parser.add_argument("--output", required=True, help="generated .c file")
    parser.add_argument("--manifest", help="optional .json manifest with glyph hashes")
    parser.add_argument("--bpp", type=int, default=4, choices=(4,))
    return parser.parse_args(argv)


def load_font(path: str, size: int, weight: int | None):
    from PIL import ImageFont  # imported lazily so the test needs no Pillow

    font = ImageFont.truetype(path, size)
    if weight is not None:
        axes = font.get_variation_axes()
        if axes:
            names = [axis.get("name") for axis in axes]
            if names and names[0] in (b"Weight", "Weight"):
                font.set_variation_by_axes([weight])
            else:
                font.set_variation_by_axes([weight])
    return font


def render_glyph(font, codepoint: int) -> dict:
    from PIL import Image, ImageDraw

    char = chr(codepoint)
    x0, y0, x1, y1 = font.getbbox(char, anchor="ls")
    width, height = x1 - x0, y1 - y0
    advance = int(round(font.getlength(char) * 16))
    if advance < 0:
        advance = 0
    if width <= 0 or height <= 0:
        return {
            "box_w": 0, "box_h": 0, "ofs_x": 0, "ofs_y": 0,
            "adv_w": advance, "data": b"",
        }
    if width > 255 or height > 255:
        raise SystemExit(f"glyph U+{codepoint:04X} is {width}x{height}, too large for LVGL 8-bit box")

    image = Image.new("L", (width, height), 0)
    ImageDraw.Draw(image).text((-x0, -y0), char, font=font, fill=255, anchor="ls")

    nibbles = []
    for y in range(height):
        for x in range(width):
            value = image.getpixel((x, y))
            nibbles.append((value * 15 + 127) // 255)

    data = bytearray()
    for index in range(0, len(nibbles), 2):
        high = nibbles[index]
        low = nibbles[index + 1] if index + 1 < len(nibbles) else 0
        data.append((high << 4) | low)

    return {
        "box_w": width, "box_h": height,
        "ofs_x": x0, "ofs_y": -y1,
        "adv_w": advance, "data": bytes(data),
    }


def emit_c(name: str, font_label: str, size: int, line_height: int, base_line: int,
           codepoints: list[int], rendered: list[dict]) -> str:
    # ASCII block first (glyph id 1..95), then the sparse block in code-point order.
    ascii_count = tools.ASCII_LAST - tools.ASCII_FIRST + 1
    sparse = [(cp, glyph) for cp, glyph in zip(codepoints, rendered)
              if cp > tools.ASCII_LAST]
    if len(sparse) > 0xFFFF:
        raise SystemExit("too many non-ASCII glyphs for uint16 unicode_list")

    bitmap = bytearray()
    dsc_lines = ['    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0,'
                 ' .ofs_x = 0, .ofs_y = 0},  /* id 0 reserved */']
    glyph_hashes = []
    for glyph in rendered:
        index = len(bitmap)
        bitmap.extend(glyph["data"])
        dsc_lines.append(
            "    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d,"
            " .ofs_x = %d, .ofs_y = %d},"
            % (index, glyph["adv_w"], glyph["box_w"], glyph["box_h"],
               glyph["ofs_x"], glyph["ofs_y"]))
        glyph_hashes.append(hashlib.sha256(glyph["data"]).hexdigest())

    if sparse:
        range_start = sparse[0][0]
        range_end = sparse[-1][0]
        range_length = range_end - range_start + 1
        if range_length > 0xFFFF:
            raise SystemExit("non-ASCII span too wide for one LVGL cmap range")
        unicode_list = [cp - range_start for cp, _ in sparse]
        unicode_list_lines = []
        for i in range(0, len(unicode_list), 12):
            unicode_list_lines.append(
                "    " + ", ".join("0x%04X" % value
                                   for value in unicode_list[i:i + 12]) + ",")
        unicode_text = (
            "static const uint16_t unicode_list_1[] = {\n"
            + "\n".join(unicode_list_lines) + "\n};\n")
        sparse_cmap = (
            "    {.range_start = %d, .range_length = %d, .glyph_id_start = %d,\n"
            "     .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL,\n"
            "     .list_length = %d, .type = %s},"
            % (range_start, range_length, GLYPH_ID_ASCII_START + ascii_count,
               len(unicode_list), CMAP_SPARSE_TINY))
        cmap_num = 2
    else:
        unicode_text = ""
        sparse_cmap = ""
        cmap_num = 1

    bitmap_lines = []
    for i in range(0, len(bitmap), 12):
        bitmap_lines.append("    " + ", ".join("0x%02X" % value
                                               for value in bitmap[i:i + 12]) + ",")

    guard = name.upper()
    return f"""/* Generated by tools/gen_lvgl_text_font.py -- do not edit by hand.
 * font: {font_label} (see assets/README.md for source, license, and command)
 * size: {size}px, bpp: 4, bitmap_format: PLAIN, no kerning
 * glyphs: {len(rendered)} (ASCII {tools.ASCII_FIRST:#04x}..{tools.ASCII_LAST:#04x} + {len(sparse)} extra)
 * See assets/README.md for the source font, license, and regeneration command.
 */
#include "lvgl.h"

#ifndef {guard}
#define {guard} 1
#endif

#if {guard}

static const uint8_t glyph_bitmap[] = {{
{chr(10).join(bitmap_lines)}
}};

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {{
{chr(10).join(dsc_lines)}
}};

{unicode_text}static const lv_font_fmt_txt_cmap_t cmaps[] = {{
    {{.range_start = {tools.ASCII_FIRST}, .range_length = {ascii_count},
     .glyph_id_start = {GLYPH_ID_ASCII_START}, .unicode_list = NULL,
     .glyph_id_ofs_list = NULL, .list_length = 0, .type = {CMAP_FORMAT0_TINY}}},
{sparse_cmap}
}};

static lv_font_fmt_txt_dsc_t font_dsc = {{
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = {cmap_num},
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
    .stride = 0,
}};

const lv_font_t {name} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {line_height},
    .base_line = {base_line},
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -2,
    .underline_thickness = 1,
    .dsc = &font_dsc,
}};

#endif /* {guard} */
"""


def main(argv=None) -> int:
    args = parse_args(argv)
    inventory = tools.load_inventory(args.inventory) if args.inventory else set()
    scanned = tools.scan_source_codepoints(args.scan)
    codepoints = tools.build_glyph_set(inventory, scanned)
    if not codepoints:
        raise SystemExit("empty glyph set")
    if len(codepoints) > 3000:
        raise SystemExit("glyph set is suspiciously large; keep the subset to UI strings")

    font = load_font(args.font, args.size, args.weight)
    ascent, descent = font.getmetrics()
    line_height = ascent + descent
    base_line = descent
    if line_height > 255 or base_line > 255:
        raise SystemExit("line height does not fit LVGL's uint8 fields")

    rendered = []
    for codepoint in codepoints:
        glyph = render_glyph(font, codepoint)
        if codepoint != 0x20 and glyph["box_w"] == 0:
            raise SystemExit(
                f"U+{codepoint:04X} renders empty; the source font may not contain it")
        rendered.append(glyph)

    code = emit_c(args.name, Path(args.font).name, args.size, line_height, base_line,
                  codepoints, rendered)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(code, encoding="utf-8")

    # Parse the file we just wrote and compare it with the in-memory data.
    emitted = tools.parse_font_c(output)
    if emitted["name"] != args.name:
        raise SystemExit("emitted font symbol mismatch")
    if emitted["line_height"] != line_height or emitted["base_line"] != base_line:
        raise SystemExit("emitted line metrics mismatch")
    if len(emitted["glyphs"]) != len(rendered) + 1:
        raise SystemExit("emitted glyph count mismatch")
    for index, glyph in enumerate(rendered):
        # glyph_dsc[0] is the reserved "not found" slot.
        if tools.glyph_bitmap_bytes(emitted, index + 1) != glyph["data"]:
            raise SystemExit(f"emitted glyph {index} differs from the rasterized bitmap")
    if tools.cmap_codepoints(emitted) != set(codepoints):
        raise SystemExit("emitted cmap does not match the requested code points")

    if args.manifest:
        manifest = {
            "name": args.name,
            "size": args.size,
            "bpp": args.bpp,
            "line_height": line_height,
            "base_line": base_line,
            "ascii_first": tools.ASCII_FIRST,
            "ascii_last": tools.ASCII_LAST,
            "codepoints": codepoints,
            "glyphs": [
                {
                    "codepoint": codepoint,
                    "glyph": index + 1,
                    "sha256": hashlib.sha256(glyph["data"]).hexdigest(),
                    "box_w": glyph["box_w"],
                    "box_h": glyph["box_h"],
                    "ofs_x": glyph["ofs_x"],
                    "ofs_y": glyph["ofs_y"],
                    "adv_w": glyph["adv_w"],
                }
                for index, (codepoint, glyph) in enumerate(zip(codepoints, rendered))
            ],
        }
        Path(args.manifest).write_text(
            json.dumps(manifest, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")

    if args.inventory and args.update_inventory:
        tools.save_inventory(
            args.inventory,
            [cp for cp in codepoints if cp > tools.ASCII_LAST],
            comment=("Archery scorekeeper UI glyph inventory (non-ASCII).\n"
                     "Printable ASCII U+0020..U+007E is always included implicitly.\n"
                     "Regenerate with tools/gen_lvgl_text_font.py --scan main/archery_*.c;"
                     " tests/test_archery_fonts.py fails when UI text needs a missing glyph."))

    print(f"{args.name}: {len(codepoints)} code points, {len(emitted['bitmap'])} bitmap bytes, "
          f"line_height={line_height}, base_line={base_line}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
