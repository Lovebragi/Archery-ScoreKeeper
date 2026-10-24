#!/usr/bin/env python3
"""Helpers shared by the LVGL text-font generator and its host-side test.

No third-party dependency: this module only reads C sources, the character
inventory, and the generated LVGL font C file. The rasterizer itself
(tools/gen_lvgl_text_font.py) needs Pillow.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ASCII_FIRST = 0x20
ASCII_LAST = 0x7E

INVENTORY_LINE_RE = re.compile(r"^\s*U\+([0-9A-Fa-f]{4,6})\b")
C_STRING_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
C_COMMENT_RE = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
C_CHAR_LITERAL_RE = re.compile(r"'(?:[^'\\\n]|\\.)+'")

_ESCAPES = {
    "n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'",
    "0": "\0", "a": "\a", "b": "\b", "f": "\f", "v": "\v", "?": "?",
}


def is_printable(codepoint: int) -> bool:
    """Control characters never participate in glyph coverage."""
    return codepoint >= 0x20 and codepoint not in (0x7F,)


def iter_c_string_literals(text: str):
    """Yield decoded contents of ordinary C string literals, skipping comments."""
    cleaned = C_COMMENT_RE.sub(lambda m: " " * len(m.group(0)), text)
    cleaned = C_CHAR_LITERAL_RE.sub("''", cleaned)
    for match in C_STRING_RE.finditer(cleaned):
        body = match.group(1)
        out = []
        i = 0
        while i < len(body):
            ch = body[i]
            if ch == "\\" and i + 1 < len(body):
                nxt = body[i + 1]
                if nxt == "x":
                    j = i + 2
                    while j < len(body) and body[j] in "0123456789abcdefABCDEF":
                        j += 1
                    try:
                        out.append(chr(int(body[i + 2:j], 16)))
                    except ValueError:
                        pass
                    i = j
                    continue
                if nxt == "u" and i + 5 < len(body):
                    try:
                        out.append(chr(int(body[i + 2:i + 6], 16)))
                    except ValueError:
                        pass
                    i += 6
                    continue
                out.append(_ESCAPES.get(nxt, nxt))
                i += 2
                continue
            out.append(ch)
            i += 1
        yield "".join(out)


def scan_source_codepoints(paths) -> set[int]:
    """Collect printable code points used inside C string literals."""
    found: set[int] = set()
    for path in paths:
        text = Path(path).read_text(encoding="utf-8")
        for literal in iter_c_string_literals(text):
            for ch in literal:
                cp = ord(ch)
                if is_printable(cp):
                    found.add(cp)
    return found


def load_inventory(path) -> set[int]:
    """Read a `U+XXXX` inventory file; blank lines and # comments are ignored."""
    inventory: set[int] = set()
    file = Path(path)
    if not file.is_file():
        return inventory
    for line in file.read_text(encoding="utf-8").splitlines():
        match = INVENTORY_LINE_RE.match(line)
        if match:
            inventory.add(int(match.group(1), 16))
    return inventory


def save_inventory(path, codepoints, comment: str | None = None) -> None:
    """Write a sorted, human-reviewable `U+XXXX` inventory."""
    lines = []
    if comment:
        lines.extend(f"# {line}" for line in comment.splitlines())
    for cp in sorted(codepoints):
        try:
            glyph = chr(cp)
        except ValueError:
            glyph = ""
        note = f"  # {glyph}" if glyph.strip() else ""
        lines.append(f"U+{cp:04X}{note}")
    Path(path).write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_glyph_set(inventory, scanned) -> list[int]:
    """ASCII printable is always included; the rest is inventory ∪ scanned."""
    codepoints = {cp for cp in range(ASCII_FIRST, ASCII_LAST + 1)}
    codepoints.update(inventory)
    codepoints.update(scanned)
    return sorted(cp for cp in codepoints if is_printable(cp))


# ---------------------------------------------------------------------------
# Generated LVGL font reader
# ---------------------------------------------------------------------------

_GLYPH_DSC_RE = re.compile(
    r"\{\s*\.bitmap_index\s*=\s*(\d+)\s*,\s*\.adv_w\s*=\s*(\d+)\s*,\s*"
    r"\.box_w\s*=\s*(\d+)\s*,\s*\.box_h\s*=\s*(\d+)\s*,\s*"
    r"\.ofs_x\s*=\s*(-?\d+)\s*,\s*\.ofs_y\s*=\s*(-?\d+)\s*\}"
)
_CMAP_RE = re.compile(
    r"\{\s*\.range_start\s*=\s*(\d+)\s*,\s*\.range_length\s*=\s*(\d+)\s*,\s*"
    r"\.glyph_id_start\s*=\s*(\d+)\s*,\s*\.unicode_list\s*=\s*(\w+)\s*,\s*"
    r"\.glyph_id_ofs_list\s*=\s*(\w+)\s*,\s*\.list_length\s*=\s*(\d+)\s*,\s*"
    r"\.type\s*=\s*(\w+)\s*\}"
)
_FONT_RE = re.compile(
    r"const lv_font_t (\w+) = \{.*?\.line_height\s*=\s*(\d+)\s*,\s*"
    r"\.base_line\s*=\s*(\d+)\s*,",
    re.S,
)
_CMAP_TYPES = {
    "LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL": 0,
    "LV_FONT_FMT_TXT_CMAP_SPARSE_FULL": 1,
    "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY": 2,
    "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY": 3,
}


def _braced_block(text: str, start: int) -> str:
    """Return the contents of the brace block that starts at or after `start`."""
    open_index = text.index("{", start)
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_index + 1:index]
    raise ValueError("unbalanced braces in generated font")


def _numbers(text: str) -> list[int]:
    return [int(token, 0) for token in re.findall(r"0x[0-9A-Fa-f]+|\d+", text)]


def parse_font_c(path) -> dict:
    """Parse the generator's LVGL font output into plain Python data."""
    text = Path(path).read_text(encoding="utf-8")
    bitmap_match = re.search(r"static const uint8_t glyph_bitmap\[\] = \{", text)
    if not bitmap_match:
        raise ValueError(f"{path}: glyph_bitmap[] not found")
    bitmap = bytes(_numbers(_braced_block(text, bitmap_match.start())))

    dsc_match = re.search(r"static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc\[\] = \{", text)
    if not dsc_match:
        raise ValueError(f"{path}: glyph_dsc[] not found")
    glyphs = []
    for match in _GLYPH_DSC_RE.finditer(_braced_block(text, dsc_match.start())):
        glyphs.append({
            "bitmap_index": int(match.group(1)),
            "adv_w": int(match.group(2)),
            "box_w": int(match.group(3)),
            "box_h": int(match.group(4)),
            "ofs_x": int(match.group(5)),
            "ofs_y": int(match.group(6)),
        })

    cmaps = []
    cmap_match = re.search(r"static const lv_font_fmt_txt_cmap_t cmaps\[\] = \{", text)
    if not cmap_match:
        raise ValueError(f"{path}: cmaps[] not found")
    for match in _CMAP_RE.finditer(_braced_block(text, cmap_match.start())):
        cmap_type = _CMAP_TYPES.get(match.group(7))
        if cmap_type is None:
            raise ValueError(f"{path}: unknown cmap type {match.group(7)}")
        unicode_list = None
        if match.group(4) != "NULL":
            list_match = re.search(
                r"static const uint16_t " + match.group(4) + r"\[\] = \{", text)
            if not list_match:
                raise ValueError(f"{path}: {match.group(4)} not found")
            unicode_list = _numbers(_braced_block(text, list_match.start()))
        cmaps.append({
            "range_start": int(match.group(1)),
            "range_length": int(match.group(2)),
            "glyph_id_start": int(match.group(3)),
            "unicode_list": unicode_list,
            "list_length": int(match.group(6)),
            "type": cmap_type,
        })

    font_match = _FONT_RE.search(text)
    if not font_match:
        raise ValueError(f"{path}: public lv_font_t not found")

    return {
        "path": str(path),
        "name": font_match.group(1),
        "line_height": int(font_match.group(2)),
        "base_line": int(font_match.group(3)),
        "bitmap": bitmap,
        "glyphs": glyphs,
        "cmaps": cmaps,
    }


def cmap_codepoints(font: dict) -> set[int]:
    """Every code point the generated cmap tables can resolve."""
    covered: set[int] = set()
    for cmap in font["cmaps"]:
        if cmap["type"] == 2:  # FORMAT0_TINY
            for offset in range(cmap["range_length"]):
                covered.add(cmap["range_start"] + offset)
        elif cmap["type"] == 3:  # SPARSE_TINY
            for offset in (cmap["unicode_list"] or []):
                covered.add(cmap["range_start"] + offset)
        else:
            raise ValueError(f"unsupported cmap type {cmap['type']}")
    return covered


def glyph_bitmap_bytes(font: dict, glyph_index: int) -> bytes:
    """Return the packed 4 bpp bytes of one glyph, checking the byte stride."""
    glyph = font["glyphs"][glyph_index]
    bits = glyph["box_w"] * glyph["box_h"] * 4
    size = (bits + 7) // 8
    start = glyph["bitmap_index"]
    end = start + size
    if end > len(font["bitmap"]):
        raise ValueError("glyph bitmap out of range")
    return font["bitmap"][start:end]


def glyph_pixels(font: dict, glyph_index: int) -> list[int]:
    """Decode one glyph exactly as LVGL's PLAIN 4 bpp reader does."""
    glyph = font["glyphs"][glyph_index]
    data = glyph_bitmap_bytes(font, glyph_index)
    pixels = []
    for byte in data:
        pixels.append(byte >> 4)
        pixels.append(byte & 0x0F)
    return pixels[: glyph["box_w"] * glyph["box_h"]]


def glyph_index_for(font: dict, codepoint: int) -> int | None:
    """Resolve a code point the way LVGL does, returning the glyph_dsc index."""
    for cmap in font["cmaps"]:
        start = cmap["range_start"]
        if codepoint < start or codepoint >= start + cmap["range_length"]:
            continue
        if cmap["type"] == 2:
            return cmap["glyph_id_start"] + (codepoint - start)
        if cmap["type"] == 3:
            offset = codepoint - start
            for index, value in enumerate(cmap["unicode_list"] or []):
                if value == offset:
                    return cmap["glyph_id_start"] + index
            return None
    return None


def main(argv: list[str]) -> int:
    """Small CLI so the inventory can be inspected without Pillow."""
    if len(argv) < 3:
        print("usage: lvgl_font_tools.py <font.c> <android|inventory>", file=sys.stderr)
        return 2
    font = parse_font_c(argv[1])
    covered = cmap_codepoints(font)
    print(f"{font['name']}: {len(font['glyphs'])} glyphs, {len(covered)} code points, "
          f"line_height={font['line_height']} base_line={font['base_line']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
