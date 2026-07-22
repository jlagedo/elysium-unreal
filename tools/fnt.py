"""Decode a VtMB `.fnt` bitmap-font (the engine's pre-rasterized VGUI font cache).

Layout (reverse-engineered, little-endian):
  Header: u32[9]. u32[0] = page count, u32[8] = glyph-table offset (= 292).
  @36:    256-byte char->glyph index table (one u8 per ANSI codepoint; codepoints
          with no glyph point at a fallback slot).
  @292:   `glyphCount` entries of 44 bytes each (glyphCount = max index + 1):
            @0  advance      u8   pen movement after the glyph
            @18 leftBearing  s8   x offset of the bitmap from the pen
            @20 -height      s16  glyph bitmap height, stored negated
            @24 page         u8   which -pageN atlas holds the glyph
            @26 width        s16  glyph bitmap width
            @28 u0,v0,u1,v1  4x f32  UV rect in the page (0..1)
  Trailing 96 bytes after the glyph table are unused here.

The glyph pixels live in the ALPHA channel of each `-pageN.tth/.ttz` atlas
(white RGB + alpha mask); slice by the UV rect and use alpha as coverage.
"""
import struct
from dataclasses import dataclass

CHARMAP_OFF = 36
CHARMAP_LEN = 256
ENTRY_SIZE = 44


@dataclass
class Glyph:
    page: int
    x: int
    y: int
    w: int
    h: int
    advance: int
    left: int      # left bearing


@dataclass
class Font:
    pages: int
    line_height: int          # from header metrics (u32[4])
    charmap: bytes            # 256 bytes: codepoint -> glyph index
    glyphs: list              # list[Glyph] indexed by glyph index

    def glyph_for(self, ch: str) -> Glyph:
        return self.glyphs[self.charmap[ord(ch) & 0xFF]]


def parse(data: bytes) -> Font:
    hdr = struct.unpack_from("<9I", data, 0)
    pages = hdr[0]
    line_height = hdr[4]
    glyph_off = hdr[8]
    charmap = data[CHARMAP_OFF:CHARMAP_OFF + CHARMAP_LEN]
    count = max(charmap) + 1

    glyphs = []
    for i in range(count):
        e = data[glyph_off + i * ENTRY_SIZE: glyph_off + (i + 1) * ENTRY_SIZE]
        u0, v0, u1, v1 = struct.unpack_from("<4f", e, 28)
        page = e[24]
        advance = e[0]
        left = struct.unpack_from("<b", e, 18)[0]
        w = struct.unpack_from("<h", e, 26)[0]
        h = -struct.unpack_from("<h", e, 20)[0]
        glyphs.append(Glyph(page, round(u0 * 256), round(v0 * 256), w, h, advance, left))
    return Font(pages, line_height, charmap, glyphs)


def render(font: Font, page_alphas, text: str, tracking: int = 1):
    """Render `text` to an (RGBA) PIL image: white glyphs in the alpha channel.

    `page_alphas[i]` is the alpha channel (PIL 'L' image) of page i. Returns the
    tightly-cropped image plus the baseline y so callers can stack lines.
    """
    from PIL import Image

    # baseline = tallest glyph in the run; ascenders sit above it.
    asc = max((font.glyph_for(c).h for c in text if c != " "), default=font.line_height)
    W = 4096
    canvas = Image.new("RGBA", (W, asc + 8), (0, 0, 0, 0))
    pen = 0
    for ch in text:
        if ch == " ":
            pen += font.line_height // 3
            continue
        g = font.glyph_for(ch)
        src = page_alphas[g.page].crop((g.x, g.y, g.x + g.w, g.y + g.h))
        glyph_img = Image.new("RGBA", (g.w, g.h), (255, 255, 255, 0))
        glyph_img.putalpha(src)
        canvas.alpha_composite(glyph_img, (pen + g.left, asc - g.h))
        pen += g.advance + tracking
    return canvas.crop((0, 0, min(pen + 8, W), asc + 8))
