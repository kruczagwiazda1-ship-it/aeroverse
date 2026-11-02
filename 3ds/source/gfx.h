/*
 * gfx.h - tiny software renderer for the two 3DS screens.
 * Everything is drawn on the CPU into 32-bit canvases (0xAARRGGBB), the platform layer copies them to the screens.
 * Text uses stb_truetype with the website's Nunito font.
 */
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

typedef struct { uint32_t *px; int w, h; int cx0, cy0, cx1, cy1; } Cv;
typedef struct { uint32_t *px; int w, h; } Img;

static inline uint32_t blend(uint32_t d, uint32_t s, int a) {   /* a 0..255 */
  if (a >= 255) return s | 0xff000000u;
  if (a <= 0) return d;
  uint32_t rb = (((s & 0xff00ff) * a) + ((d & 0xff00ff) * (255 - a))) >> 8;
  uint32_t g = (((s & 0x00ff00) * a) + ((d & 0x00ff00) * (255 - a))) >> 8;
  return 0xff000000u | (rb & 0xff00ff) | (g & 0x00ff00);
}
static void cv_init(Cv *c, int w, int h) { c->w = w; c->h = h; c->px = malloc((size_t)w * h * 4); c->cx0 = 0; c->cy0 = 0; c->cx1 = w; c->cy1 = h; }
static void cv_clip(Cv *c, int x, int y, int w, int h) {
  c->cx0 = x < 0 ? 0 : x; c->cy0 = y < 0 ? 0 : y;
  c->cx1 = x + w > c->w ? c->w : x + w; c->cy1 = y + h > c->h ? c->h : y + h;
}
static void cv_noclip(Cv *c) { c->cx0 = 0; c->cy0 = 0; c->cx1 = c->w; c->cy1 = c->h; }
static inline void pset(Cv *c, int x, int y, uint32_t col, int a) {
  if (x < c->cx0 || y < c->cy0 || x >= c->cx1 || y >= c->cy1) return;
  uint32_t *p = &c->px[y * c->w + x];
  *p = blend(*p, col, a);
}
/* colours are 0xAARRGGBB; alpha 0 means opaque (so plain 0xRRGGBB works) */
static inline int alpha_of(uint32_t col) { int a = (int)(col >> 24); return a ? a : 255; }

static void fill(Cv *c, int x, int y, int w, int h, uint32_t col) {
  int a = alpha_of(col);
  int x0 = x < c->cx0 ? c->cx0 : x, y0 = y < c->cy0 ? c->cy0 : y;
  int x1 = x + w > c->cx1 ? c->cx1 : x + w, y1 = y + h > c->cy1 ? c->cy1 : y + h;
  if (x0 >= x1 || y0 >= y1) return;
  for (int yy = y0; yy < y1; yy++) {
    uint32_t *p = &c->px[yy * c->w + x0];
    if (a >= 255) { uint32_t v = col | 0xff000000u; for (int xx = x0; xx < x1; xx++) *p++ = v; }
    else for (int xx = x0; xx < x1; xx++, p++) *p = blend(*p, col, a);
  }
}
static uint32_t lerpc(uint32_t a, uint32_t b, float t) {
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  int r = (int)(((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t));
  int g = (int)(((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t));
  int bl = (int)((a & 255) + (((int)(b & 255) - (int)(a & 255)) * t));
  return (uint32_t)((r << 16) | (g << 8) | bl);
}
static void vgrad(Cv *c, int x, int y, int w, int h, uint32_t top, uint32_t bot) {
  for (int i = 0; i < h; i++) fill(c, x, y + i, w, 1, lerpc(top, bot, h > 1 ? (float)i / (h - 1) : 0));
}
/* rounded rectangle with anti-aliased corners; optional vertical gradient (bot != 0) */
static void rrect_g(Cv *c, int x, int y, int w, int h, int r, uint32_t top, uint32_t bot) {
  if (r * 2 > h) r = h / 2;
  if (r * 2 > w) r = w / 2;
  int a0 = alpha_of(top);
  for (int i = 0; i < h; i++) {
    uint32_t col = bot ? (lerpc(top, bot, h > 1 ? (float)i / (h - 1) : 0)) : top;
    int yy = y + i;
    if (yy < c->cy0 || yy >= c->cy1) continue;
    float dy = 0;
    if (i < r) dy = r - i - 0.5f; else if (i >= h - r) dy = i - (h - r) + 0.5f;
    if (dy <= 0) { fill(c, x, yy, w, 1, (col & 0xffffff) | ((uint32_t)a0 << 24)); continue; }
    float half = sqrtf((float)r * r - dy * dy);   /* horizontal extent of the corner at this row */
    float inset = r - half;
    int full0 = (int)ceilf(inset), full1 = w - full0;
    if (full1 > full0) fill(c, x + full0, yy, full1 - full0, 1, (col & 0xffffff) | ((uint32_t)a0 << 24));
    /* fractional edge pixels */
    float frac = full0 - inset;
    if (full0 > 0) {
      int a = (int)(a0 * frac);
      pset(c, x + full0 - 1, yy, col, a); pset(c, x + w - full0, yy, col, a);
    }
  }
}
static void rrect(Cv *c, int x, int y, int w, int h, int r, uint32_t col) { rrect_g(c, x, y, w, h, r, col, 0); }
static void rrect_border(Cv *c, int x, int y, int w, int h, int r, uint32_t border, uint32_t fillc) {
  rrect(c, x, y, w, h, r, border);
  rrect(c, x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, fillc);
}
/* soft drop shadow under cards */
static void shadow(Cv *c, int x, int y, int w, int h, int r) {
  rrect(c, x, y + 2, w, h, r, 0x18000000);
  rrect(c, x - 1, y + 1, w + 2, h + 2, r + 1, 0x0c000000);
}
static void circle(Cv *c, float cx, float cy, float rad, uint32_t col) {
  int a0 = alpha_of(col);
  int x0 = (int)floorf(cx - rad - 1), x1 = (int)ceilf(cx + rad + 1), y0 = (int)floorf(cy - rad - 1), y1 = (int)ceilf(cy + rad + 1);
  for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
    float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
    float cov = rad + 0.5f - d;
    if (cov <= 0) continue;
    if (cov > 1) cov = 1;
    pset(c, x, y, col, (int)(a0 * cov));
  }
}
static void line(Cv *c, float x0, float y0, float x1, float y1, float th, uint32_t col) {
  float dx = x1 - x0, dy = y1 - y0, L = sqrtf(dx * dx + dy * dy);
  int n = (int)(L / (th > 2 ? th * 0.4f : 0.7f)) + 1;
  for (int i = 0; i <= n; i++) { float t = (float)i / n; circle(c, x0 + dx * t, y0 + dy * t, th / 2, col); }
}

/* ---- images */
static Img img_from_rgba(const uint8_t *rgba, int w, int h) {
  Img m = { malloc((size_t)w * h * 4), w, h };
  for (int i = 0; i < w * h; i++) m.px[i] = ((uint32_t)rgba[i * 4 + 3] << 24) | ((uint32_t)rgba[i * 4] << 16) | ((uint32_t)rgba[i * 4 + 1] << 8) | rgba[i * 4 + 2];
  return m;
}
static void img_free(Img *m) { free(m->px); m->px = NULL; }
/* area-average downscale (or nearest upscale) into a new image */
static Img img_scale(const Img *s, int w, int h) {
  Img d = { malloc((size_t)w * h * 4), w, h };
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    int sx0 = x * s->w / w, sx1 = (x + 1) * s->w / w, sy0 = y * s->h / h, sy1 = (y + 1) * s->h / h;
    if (sx1 <= sx0) sx1 = sx0 + 1;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    uint32_t A = 0, Rr = 0, G = 0, B = 0, n = 0;
    for (int yy = sy0; yy < sy1 && yy < s->h; yy++) for (int xx = sx0; xx < sx1 && xx < s->w; xx++) {
      uint32_t p = s->px[yy * s->w + xx], a = p >> 24;
      A += a; Rr += ((p >> 16) & 255) * a; G += ((p >> 8) & 255) * a; B += (p & 255) * a; n++;
    }
    d.px[y * w + x] = A ? (((A / n) << 24) | ((Rr / A) << 16) | ((G / A) << 8) | (B / A)) : 0;
  }
  return d;
}
/* draw an image (nearest neighbour) with alpha; tint multiplies the colour (0 = none), alpha 0..255 */
static void img_draw(Cv *c, const Img *m, int dx, int dy, int dw, int dh, uint32_t tint, int alpha) {
  if (!m || !m->px || dw <= 0 || dh <= 0) return;
  int x0 = dx < c->cx0 ? c->cx0 : dx, y0 = dy < c->cy0 ? c->cy0 : dy;
  int x1 = dx + dw > c->cx1 ? c->cx1 : dx + dw, y1 = dy + dh > c->cy1 ? c->cy1 : dy + dh;
  for (int y = y0; y < y1; y++) {
    int sy = (y - dy) * m->h / dh;
    const uint32_t *row = m->px + sy * m->w;
    uint32_t *p = &c->px[y * c->w + x0];
    for (int x = x0; x < x1; x++, p++) {
      uint32_t s = row[(x - dx) * m->w / dw];
      int a = (int)(s >> 24) * alpha / 255;
      if (!a) continue;
      if (tint) {
        uint32_t r = ((s >> 16) & 255) * ((tint >> 16) & 255) / 255, g = ((s >> 8) & 255) * ((tint >> 8) & 255) / 255, b = (s & 255) * (tint & 255) / 255;
        s = (r << 16) | (g << 8) | b;
      }
      *p = blend(*p, s, a);
    }
  }
}
/* object-fit: cover into a rect */
static void img_cover(Cv *c, const Img *m, int x, int y, int w, int h) {
  if (!m || !m->px) return;
  float sa = (float)m->w / m->h, da = (float)w / h;
  Img sub; int sx, sy, sw, sh;
  if (sa > da) { sh = m->h; sw = (int)(m->h * da); sx = (m->w - sw) / 2; sy = 0; }
  else { sw = m->w; sh = (int)(m->w / da); sx = 0; sy = (m->h - sh) / 2; }
  /* draw the cropped part */
  for (int yy = 0; yy < h; yy++) {
    int py = y + yy; if (py < c->cy0 || py >= c->cy1) continue;
    int syy = sy + yy * sh / h;
    for (int xx = 0; xx < w; xx++) {
      int px = x + xx; if (px < c->cx0 || px >= c->cx1) continue;
      uint32_t s = m->px[syy * m->w + sx + xx * sw / w];
      int a = (int)(s >> 24); if (a) c->px[py * c->w + px] = blend(c->px[py * c->w + px], s, a);
    }
  }
  (void)sub;
}

/* ---- text */
typedef struct { int font, size; uint32_t cp; uint8_t *bm; int w, h, xo, yo, adv; } Glyph;
#define NGLYPH 1024
static Glyph glyphs[NGLYPH];
static stbtt_fontinfo fonts[3];
static int fontAsc[3];   /* ascent in font units */
enum { FR = 0, FB = 1, FM = 2 };   /* regular, bold, medium (regular made thicker: easier to read on the 3DS screens) */
static void text_init(const unsigned char *reg, const unsigned char *bold) {
  stbtt_InitFont(&fonts[0], reg, stbtt_GetFontOffsetForIndex(reg, 0));
  stbtt_InitFont(&fonts[1], bold, stbtt_GetFontOffsetForIndex(bold, 0));
  fonts[2] = fonts[0];
  for (int i = 0; i < 3; i++) { int a, d, g; stbtt_GetFontVMetrics(&fonts[i], &a, &d, &g); fontAsc[i] = a; }
}
static Glyph *glyph(int font, int size, uint32_t cp) {
  uint32_t hsh = (cp * 2654435761u ^ (uint32_t)(size * 131 + font * 7)) % NGLYPH;
  for (int k = 0; k < 8; k++) {
    Glyph *g = &glyphs[(hsh + k) % NGLYPH];
    if (g->size == size && g->font == font && g->cp == cp && (g->bm || g->adv)) return g;
    if (!g->size) {
      float sc = stbtt_ScaleForPixelHeight(&fonts[font], (float)size);
      int gi = stbtt_FindGlyphIndex(&fonts[font], (int)cp);
      if (!gi && cp != ' ') gi = stbtt_FindGlyphIndex(&fonts[font], '?');
      int adv, lsb; stbtt_GetGlyphHMetrics(&fonts[font], gi, &adv, &lsb);
      g->font = font; g->size = size; g->cp = cp; g->adv = (int)lroundf(adv * sc);
      if (g->adv <= 0) g->adv = 1;
      g->bm = stbtt_GetGlyphBitmap(&fonts[font], sc, sc, gi, &g->w, &g->h, &g->xo, &g->yo);
      if (font == FM && g->bm && g->w > 0) {   /* thicken: max of the glyph and the glyph moved 1 px right */
        int nw = g->w + 1;
        uint8_t *nb = malloc((size_t)nw * g->h);
        for (int y = 0; y < g->h; y++) for (int x = 0; x < nw; x++) {
          int a = x < g->w ? g->bm[y * g->w + x] : 0, b = x > 0 ? g->bm[y * g->w + x - 1] : 0;
          nb[y * nw + x] = (uint8_t)(a > b ? a : b);
        }
        stbtt_FreeBitmap(g->bm, NULL); g->bm = nb; g->w = nw;
      }
      return g;
    }
  }
  /* table crowded: drop one slot */
  Glyph *g = &glyphs[hsh];
  if (g->bm) free(g->bm);
  memset(g, 0, sizeof *g);
  return glyph(font, size, cp);
}
static uint32_t utf8_next(const char **s) {
  const unsigned char *p = (const unsigned char *)*s;
  uint32_t c = *p++;
  if (c >= 0xF0 && (p[0] & 0xC0) == 0x80) { c = ((c & 7) << 18) | ((p[0] & 63) << 12) | ((p[1] & 63) << 6) | (p[2] & 63); p += 3; }
  else if (c >= 0xE0 && (p[0] & 0xC0) == 0x80) { c = ((c & 15) << 12) | ((p[0] & 63) << 6) | (p[1] & 63); p += 2; }
  else if (c >= 0xC0 && (p[0] & 0xC0) == 0x80) { c = ((c & 31) << 6) | (p[0] & 63); p += 1; }
  *s = (const char *)p;
  return c;
}
static int skip_cp(uint32_t c) { return (c >= 0x1F000 && c < 0x20000) || (c >= 0x2600 && c < 0x2800) || c == 0xFE0F || c == 0x200D; }   /* emoji: not in the font */
static int text_w(int font, int size, const char *s) {
  int w = 0;
  while (s && *s) { uint32_t c = utf8_next(&s); if (c == '\n') c = ' '; if (skip_cp(c)) continue; w += glyph(font, size, c)->adv; }
  return w;
}
/* y is the top of the line; returns the width */
static int text(Cv *c, int font, int size, int x, int y, const char *s, uint32_t col) {
  int base = y + (int)lroundf(fontAsc[font] * stbtt_ScaleForPixelHeight(&fonts[font], (float)size));
  int x0 = x, a0 = alpha_of(col);
  while (s && *s) {
    uint32_t cp = utf8_next(&s);
    if (cp == '\n') cp = ' ';
    if (skip_cp(cp)) continue;
    Glyph *g = glyph(font, size, cp);
    if (g->bm) for (int yy = 0; yy < g->h; yy++) for (int xx = 0; xx < g->w; xx++) {
      int a = g->bm[yy * g->w + xx];
      if (a) pset(c, x + g->xo + xx, base + g->yo + yy, col, a * a0 / 255);
    }
    x += g->adv;
  }
  return x - x0;
}
static int text_c(Cv *c, int font, int size, int cx, int y, const char *s, uint32_t col) { int w = text_w(font, size, s); return text(c, font, size, cx - w / 2, y, s, col); }
/* single line, cut with "..." to fit maxw */
static void text_fit(Cv *c, int font, int size, int x, int y, const char *s, uint32_t col, int maxw) {
  if (!s) return;
  if (text_w(font, size, s) <= maxw) { text(c, font, size, x, y, s, col); return; }
  char buf[256]; int n = 0; const char *p = s;
  int dots = text_w(font, size, "...");
  int w = 0;
  while (*p && n < 240) {
    const char *q = p; uint32_t cp = utf8_next(&q);
    int aw = (cp == '\n' || skip_cp(cp)) ? 0 : glyph(font, size, cp)->adv;
    if (w + aw + dots > maxw) break;
    if (cp == '\n') { buf[n++] = ' '; p = q; w += glyph(font, size, ' ')->adv; continue; }
    memcpy(buf + n, p, (size_t)(q - p)); n += (int)(q - p); w += aw; p = q;
  }
  memcpy(buf + n, "...", 4);
  text(c, font, size, x, y, buf, col);
}
/* word-wrapped text; draws (if c) and returns the height; maxLines 0 = no limit */
static int text_wrap(Cv *c, int font, int size, int x, int y, int maxw, const char *s, uint32_t col, int lineH, int maxLines) {
  if (!s) return 0;
  int lines = 0;
  const char *p = s;
  char buf[512];
  while (*p) {
    /* take as many words as fit */
    int n = 0, w = 0, lastSpaceN = -1; const char *lastSpaceP = NULL;
    const char *q = p;
    int brk = 0;
    while (*q) {
      const char *r = q; uint32_t cp = utf8_next(&r);
      if (cp == '\n') { q = r; brk = 1; break; }
      int aw = skip_cp(cp) ? 0 : glyph(font, size, cp)->adv;
      if (w + aw > maxw && n > 0) break;
      if (cp == ' ') { lastSpaceN = n; lastSpaceP = r; }
      if (n + (r - q) >= 500) break;
      memcpy(buf + n, q, (size_t)(r - q)); n += (int)(r - q); w += aw; q = r;
    }
    if (*q && !brk && lastSpaceN > 0) { n = lastSpaceN; q = lastSpaceP; }
    buf[n] = 0;
    lines++;
    int last = maxLines && lines == maxLines && *q;
    if (c) {
      if (last) text_fit(c, font, size, x, y, p, col, maxw);
      else text(c, font, size, x, y, buf, col);
    }
    y += lineH;
    p = q;
    if (last) break;
  }
  return lines * lineH;
}
