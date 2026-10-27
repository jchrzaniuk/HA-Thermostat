#!/usr/bin/env python3
"""Generate LVGL 8 UI assets: fonts with Polish letters, large digits and menu icons.

Uses the same Montserrat-Medium.ttf as LVGL's built-in fonts (from the managed
component), so glyphs match. Output: main/ui_assets.c. Requires Pillow.
"""
import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
TTF = ROOT / 'managed_components/lvgl__lvgl/scripts/built_in_font/Montserrat-Medium.ttf'
OUT = ROOT / 'main/ui_assets.c'
POLISH = 'ĄĆĘŁŃÓŚŹŻąćęłńóśźż'
# name, size, characters, line_height, base_line, fallback. Metrics follow
# lv_font_montserrat_<size>.c (48 px scaled for 80 px), so mixed text aligns.
FONTS = [
    ('lv_font_pl_14', 14, POLISH, 16, 3, '&lv_font_montserrat_14'),
    ('lv_font_pl_20', 20, POLISH, 22, 4, '&lv_font_montserrat_20'),
    ('lv_font_temp_80', 80, '-0123456789', 87, 15, 'NULL'),
]


def glyph(font, ch):
    mask, (ox, oy) = font.getmask2(ch, mode='L')
    w, h = mask.size
    pixels = [mask.getpixel((x, y)) for y in range(h) for x in range(w)]
    rows = [pixels[y * w:(y + 1) * w] for y in range(h)]
    cols = [[r[x] for r in rows] for x in range(w)]
    top = next(i for i, r in enumerate(rows) if any(r))
    bottom = max(i for i, r in enumerate(rows) if any(r))
    left = next(i for i, c in enumerate(cols) if any(c))
    right = max(i for i, c in enumerate(cols) if any(c))
    box = [rows[y][left:right + 1] for y in range(top, bottom + 1)]
    ascent, _ = font.getmetrics()
    nibbles = [round(v * 15 / 255) for row in box for v in row]
    if len(nibbles) % 2: nibbles.append(0)
    data = [(nibbles[i] << 4) | nibbles[i + 1] for i in range(0, len(nibbles), 2)]
    return {
        'adv': round(font.getlength(ch) * 16), 'w': right - left + 1, 'h': bottom - top + 1,
        'x': ox + left, 'y': ascent - (oy + bottom + 1), 'data': data,
    }


def font_source(name, size, characters, line_height, base_line, fallback):
    font = ImageFont.truetype(str(TTF), size)
    chars = sorted(characters, key=ord)
    bitmap, dscs, index = [], [], 0
    for ch in chars:
        g = glyph(font, ch)
        bitmap.append(f'    /* U+{ord(ch):04X} "{ch}" */\n    ' +
                      ', '.join(f'0x{b:x}' for b in g['data']) + ',')
        dscs.append(f'    {{.bitmap_index = {index}, .adv_w = {g["adv"]}, .box_w = {g["w"]}, '
                    f'.box_h = {g["h"]}, .ofs_x = {g["x"]}, .ofs_y = {g["y"]}}},')
        index += len(g['data'])
    start = ord(chars[0])
    offsets = ', '.join(f'0x{ord(c) - start:x}' for c in chars)
    return f'''
/* {name}: Montserrat Medium {size} px, 4 bpp: {''.join(chars)} */
static LV_ATTRIBUTE_LARGE_CONST const uint8_t {name}_bitmap[] = {{
{chr(10).join(bitmap)}
}};
static const lv_font_fmt_txt_glyph_dsc_t {name}_glyphs[] = {{
    {{.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}},
{chr(10).join(dscs)}
}};
static const uint16_t {name}_unicode[] = {{{offsets}}};
static const lv_font_fmt_txt_cmap_t {name}_cmaps[] = {{{{
    .range_start = {start}, .range_length = {ord(chars[-1]) - start + 1}, .glyph_id_start = 1,
    .unicode_list = {name}_unicode, .glyph_id_ofs_list = NULL, .list_length = {len(chars)},
    .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY,
}}}};
static lv_font_fmt_txt_glyph_cache_t {name}_cache;
static const lv_font_fmt_txt_dsc_t {name}_dsc = {{
    .glyph_bitmap = {name}_bitmap, .glyph_dsc = {name}_glyphs, .cmaps = {name}_cmaps,
    .kern_dsc = NULL, .kern_scale = 0, .cmap_num = 1, .bpp = 4, .kern_classes = 0,
    .bitmap_format = 0, .cache = &{name}_cache,
}};
const lv_font_t {name} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt, .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {line_height}, .base_line = {base_line}, .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -1, .underline_thickness = 1,
    .dsc = &{name}_dsc, .fallback = {fallback},
}};
'''


def catmull(points, steps=12):
    """Smooth closed curve through the given points."""
    out = []
    n = len(points)
    for i in range(n):
        p0, p1, p2, p3 = (points[(i + k) % n] for k in (-1, 0, 1, 2))
        for s in range(steps):
            t = s / steps
            out.append(tuple(0.5 * (2 * p1[c] + (-p0[c] + p2[c]) * t +
                                    (2 * p0[c] - 5 * p1[c] + 4 * p2[c] - p3[c]) * t * t +
                                    (-p0[c] + 3 * p1[c] - 3 * p2[c] + p3[c]) * t ** 3) for c in (0, 1)))
    return out


def leaf(draw, b):
    base, tip = (0.16 * b, 0.84 * b), (0.88 * b, 0.12 * b)
    mid = ((base[0] + tip[0]) / 2, (base[1] + tip[1]) / 2)
    chord = math.dist(base, tip)
    normal = ((tip[1] - base[1]) / chord, (base[0] - tip[0]) / chord)
    points = []
    for side in (1, -1):
        bulge = 0.30 * chord * side
        edge = []
        for i in range(41):
            t = i / 40
            offset = bulge * 4 * t * (1 - t)
            edge.append((base[0] + (tip[0] - base[0]) * t + normal[0] * offset,
                         base[1] + (tip[1] - base[1]) * t + normal[1] * offset))
        points += reversed(edge) if side == 1 else edge
    draw.polygon(points, fill=255)
    w = round(0.055 * b)
    draw.line([(0.06 * b, 0.94 * b), mid, (tip[0] - 0.16 * b, tip[1] + 0.16 * b)], fill=0, width=w)
    draw.line([(0.04 * b, 0.96 * b), (base[0] + 0.06 * b, base[1] - 0.06 * b)], fill=255, width=w)


def flame(draw, b):
    outer = [(0.50, 0.95), (0.24, 0.76), (0.26, 0.48), (0.42, 0.28), (0.52, 0.05),
             (0.66, 0.30), (0.78, 0.50), (0.77, 0.77)]
    draw.polygon([(x * b, y * b) for x, y in catmull(outer)], fill=255)
    inner = [(0.50, 0.90), (0.38, 0.80), (0.40, 0.64), (0.50, 0.48), (0.60, 0.64), (0.62, 0.80)]
    draw.polygon([(x * b, y * b) for x, y in catmull(inner)], fill=0)


def stroke(draw, b, points, width):
    w = width * b
    draw.line([(x * b, y * b) for x, y in points], fill=255, width=round(w), joint='curve')
    for x, y in (points[0], points[-1]):
        draw.ellipse((x * b - w / 2, y * b - w / 2, x * b + w / 2, y * b + w / 2), fill=255)


def snow(draw, b):
    for k in range(3):
        a = math.radians(90 + 60 * k)
        dx, dy = 0.42 * math.cos(a), 0.42 * math.sin(a)
        stroke(draw, b, [(0.5 - dx, 0.5 - dy), (0.5 + dx, 0.5 + dy)], 0.075)
        for sign in (1, -1):
            sx, sy = 0.5 + sign * dx * 0.62, 0.5 + sign * dy * 0.62
            for turn in (-0.8, 0.8):
                ba = a + (0 if sign > 0 else math.pi) + turn
                stroke(draw, b, [(sx, sy), (sx + 0.15 * math.cos(ba), sy + 0.15 * math.sin(ba))], 0.07)


def power(draw, b):
    w = round(0.09 * b)
    draw.arc((0.16 * b, 0.18 * b, 0.84 * b, 0.86 * b), 300, 240, fill=255, width=w)
    stroke(draw, b, [(0.5, 0.08), (0.5, 0.46)], 0.09)


def automatic(draw, b):
    font = ImageFont.truetype(str(TTF), round(0.86 * b))
    draw.text((0.5 * b, 0.53 * b), 'A', font=font, fill=255, anchor='mm')


def star(draw, b):
    points = []
    for k in range(10):
        a = math.radians(-90 + 36 * k)
        r = 0.46 if k % 2 == 0 else 0.19
        points.append((0.5 * b + r * b * math.cos(a), 0.54 * b + r * b * math.sin(a)))
    draw.polygon(points, fill=255)


def back(draw, b):
    stroke(draw, b, [(0.80, 0.5), (0.24, 0.5)], 0.09)
    stroke(draw, b, [(0.46, 0.27), (0.22, 0.5), (0.46, 0.73)], 0.09)


def image_source(name, shape, size, scale=8):
    """Alpha-only image; LVGL tints it with the img_recolor style."""
    big = size * scale
    image = Image.new('L', (big, big), 0)
    shape(ImageDraw.Draw(image), big)
    small = image.resize((size, size), Image.LANCZOS)
    data = ', '.join(f'0x{v:02x}' for v in small.tobytes())
    return f'''
static const uint8_t {name}_map[] = {{ {data} }};
const lv_img_dsc_t {name} = {{
    .header = {{.cf = LV_IMG_CF_ALPHA_8BIT, .always_zero = 0, .reserved = 0, .w = {size}, .h = {size}}},
    .data_size = {size * size}, .data = {name}_map,
}};
'''


ICONS = {'back': back, 'heat': flame, 'cool': snow, 'off': power, 'auto': automatic,
         'preset': star, 'eco': leaf}
IMAGES = [('ui_leaf', leaf, 24)] + [(f'ui_icon_{n}_{s}', f, s) for n, f in ICONS.items() for s in (28, 48)]


source = ('/* Generated by tools/assets/gen_ui_assets.py — do not edit. */\n'
          '#include "lvgl.h"\n' + ''.join(font_source(*f) for f in FONTS) +
          ''.join(image_source(*i) for i in IMAGES))
OUT.write_text(source)
print(OUT)
