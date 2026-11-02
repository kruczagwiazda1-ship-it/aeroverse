/*
 * aero.h - software drawing helpers for the Frutiger Aero look of the website.
 * Everything is drawn into RGBA "canvases" on the CPU and turned into SDL textures,
 * so it works with any SDL renderer (rounded corners, gradients, shadows, globe, 3D plaza).
 * Included once from main.c.
 */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define S14(v) ((int)((v) * 1.4f + 0.5f))   /* website px -> TV px */

typedef struct { int w, h; uint8_t *p; } Cv;   /* straight RGBA */

static Cv cv_new(int w, int h) { Cv c; c.w = w; c.h = h; c.p = calloc((size_t)w * h, 4); return c; }
static void cv_free(Cv *c) { free(c->p); c->p = NULL; }

/* blend colour (0xAARRGGBB) with coverage a (0..1) onto pixel */
static inline void cv_blend(Cv *c, int x, int y, uint32_t col, float a) {
  if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
  float sa = ((col >> 24) & 255) / 255.0f * a;
  if (sa <= 0.001f) return;
  uint8_t *d = c->p + ((size_t)y * c->w + x) * 4;
  float da = d[3] / 255.0f, oa = sa + da * (1 - sa);
  if (oa <= 0) return;
  float sr = (col >> 16) & 255, sg = (col >> 8) & 255, sb = col & 255;
  d[0] = (uint8_t)((sr * sa + d[0] * da * (1 - sa)) / oa + 0.5f);
  d[1] = (uint8_t)((sg * sa + d[1] * da * (1 - sa)) / oa + 0.5f);
  d[2] = (uint8_t)((sb * sa + d[2] * da * (1 - sa)) / oa + 0.5f);
  d[3] = (uint8_t)(oa * 255 + 0.5f);
}

typedef struct { float p; uint32_t c; } Stop;
static uint32_t lerp_col(uint32_t a, uint32_t b, float t) {
  if (t < 0) t = 0; if (t > 1) t = 1;
  uint32_t o = 0;
  for (int s = 0; s < 32; s += 8) { int x = (a >> s) & 255, y = (b >> s) & 255; o |= (uint32_t)(x + (y - x) * t + 0.5f) << s; }
  return o;
}
static uint32_t grad_at(const Stop *st, int n, float t) {
  if (n == 1 || t <= st[0].p) return st[0].c;
  for (int i = 1; i < n; i++) if (t <= st[i].p) return lerp_col(st[i - 1].c, st[i].c, (t - st[i - 1].p) / (st[i].p - st[i - 1].p + 1e-6f));
  return st[n - 1].c;
}

/* signed distance from point to a rounded rectangle */
static inline float sd_rrect(float px, float py, float x, float y, float w, float h, float r) {
  float cx = x + w / 2, cy = y + h / 2, qx = fabsf(px - cx) - (w / 2 - r), qy = fabsf(py - cy) - (h / 2 - r);
  float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
  float inside = qx > qy ? qx : qy; if (inside > 0) inside = 0;
  return sqrtf(ox * ox + oy * oy) + inside - r;
}

/* filled rounded rect, vertical gradient (angle 180deg like CSS). deg != 180 -> diagonal gradient */
static void cv_rrect(Cv *c, float x, float y, float w, float h, float r, const Stop *st, int n, float deg) {
  int x0 = (int)floorf(x) - 1, y0 = (int)floorf(y) - 1, x1 = (int)ceilf(x + w) + 1, y1 = (int)ceilf(y + h) + 1;
  float ang = (deg - 90) * 3.14159265f / 180.0f, gx = cosf(ang), gy = sinf(ang);
  float glen = fabsf(w * gx) + fabsf(h * gy);
  for (int py = y0; py < y1; py++) for (int px = x0; px < x1; px++) {
    float d = sd_rrect(px + 0.5f, py + 0.5f, x, y, w, h, r);
    float a = 0.5f - d; if (a <= 0) continue; if (a > 1) a = 1;
    float t;
    if (deg == 180) t = (py + 0.5f - y) / h;
    else t = (((px + 0.5f - x - w / 2) * gx + (py + 0.5f - y - h / 2) * gy) / glen) + 0.5f;
    cv_blend(c, px, py, grad_at(st, n, t), a);
  }
}
static void cv_rrect1(Cv *c, float x, float y, float w, float h, float r, uint32_t col) { Stop s = { 0, col }; cv_rrect(c, x, y, w, h, r, &s, 1, 180); }

/* soft shadow under a rounded rect */
static void cv_shadow(Cv *c, float x, float y, float w, float h, float r, float blur, uint32_t col) {
  int x0 = (int)(x - blur) - 1, y0 = (int)(y - blur) - 1, x1 = (int)(x + w + blur) + 1, y1 = (int)(y + h + blur) + 1;
  for (int py = y0; py < y1; py++) for (int px = x0; px < x1; px++) {
    float d = sd_rrect(px + 0.5f, py + 0.5f, x, y, w, h, r);
    float t = 0.5f - d / blur; if (t <= 0) continue; if (t > 1) t = 1;
    cv_blend(c, px, py, col, t * t * (3 - 2 * t));
  }
}
static void cv_circle(Cv *c, float cx, float cy, float rad, uint32_t col) { cv_rrect1(c, cx - rad, cy - rad, rad * 2, rad * 2, rad, col); }

static SDL_Texture *cv_tex(Cv *c) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(c->p, c->w, c->h, 32, c->w * 4, SDL_PIXELFORMAT_RGBA32);
  if (!s) return NULL;
  SDL_Texture *t = SDL_CreateTextureFromSurface(R, s);
  SDL_FreeSurface(s);
  if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  return t;
}

/* ---------- nine-patch: a box style rendered once, stretched to any size ---------- */
typedef struct { SDL_Texture *t; int tw, th, m, ins; } Nine;   /* m = slice size, ins = how far the image reaches outside the box (shadow) */
static void nine_draw(const Nine *n, int x, int y, int w, int h) {
  if (!n->t) return;
  int m = n->m, X = x - n->ins, Y = y - n->ins, W = w + 2 * n->ins, H = h + 2 * n->ins;
  if (W < 2 * m) W = 2 * m;
  if (H < 2 * m) H = 2 * m;
  int sx[4] = { 0, m, n->tw - m, n->tw }, sy[4] = { 0, m, n->th - m, n->th };
  int dx[4] = { X, X + m, X + W - m, X + W }, dy[4] = { Y, Y + m, Y + H - m, Y + H };
  for (int j = 0; j < 3; j++) for (int i = 0; i < 3; i++) {
    SDL_Rect s = { sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j] }, d = { dx[i], dy[j], dx[i + 1] - dx[i], dy[j + 1] - dy[j] };
    if (s.w > 0 && s.h > 0 && d.w > 0 && d.h > 0) SDL_RenderCopy(R, n->t, &s, &d);
  }
}
/* box with radius r, border bw/bcol, fill stops, shadow (offy, blur, colour) */
static Nine nine_make(float r, float bw, uint32_t bcol, const Stop *fill, int nf, float shOff, float shBlur, uint32_t shCol) {
  Nine n; memset(&n, 0, sizeof n);
  int ins = (int)ceilf(shBlur + fabsf(shOff));
  int box = (int)ceilf(r * 2 + 8);
  n.tw = n.th = box + 2 * ins; n.m = (int)ceilf(r + ins + 2); n.ins = ins;
  if (n.m * 2 > n.tw) n.m = n.tw / 2;
  Cv c = cv_new(n.tw, n.th);
  if (shBlur > 0) cv_shadow(&c, ins, ins + shOff, box, box, r, shBlur, shCol);
  if (bw > 0) cv_rrect1(&c, ins, ins, box, box, r, bcol);
  /* fill: vertical stops apply over the small box only; good enough for subtle card gradients */
  if (((fill[0].c >> 24) & 255) == 0 && nf == 1) {
    /* transparent fill = ring only: cut the inside out */
    float ir = r - bw > 0 ? r - bw : 0;
    for (int py = 0; py < c.h; py++) for (int px = 0; px < c.w; px++) {
      float d = sd_rrect(px + 0.5f, py + 0.5f, ins + bw, ins + bw, box - 2 * bw, box - 2 * bw, ir);
      float cov = 0.5f - d; if (cov <= 0) continue; if (cov > 1) cov = 1;
      uint8_t *pp = c.p + ((size_t)py * c.w + px) * 4; pp[3] = (uint8_t)(pp[3] * (1 - cov));
    }
  } else cv_rrect(&c, ins + bw, ins + bw, box - 2 * bw, box - 2 * bw, r - bw > 0 ? r - bw : 0, fill, nf, 180);
  n.t = cv_tex(&c);
  cv_free(&c);
  return n;
}

/* ---------- pill buttons: fixed height, exact gloss gradient, stretched only in the middle ---------- */
typedef struct { SDL_Texture *t; int tw, th, cap, ins; } Pill;
static Pill pill_make(int h, float r, uint32_t bcol, int bottom, const Stop *fill, int nf, uint32_t hilite, float shBlur, uint32_t shCol) {
  Pill p; memset(&p, 0, sizeof p);
  int ins = (int)ceilf(shBlur) + 1;
  int w = (int)(r * 2 + 8);
  p.tw = w + 2 * ins; p.th = h + 2 * ins + 2; p.cap = (int)ceilf(r + ins + 1); p.ins = ins;
  Cv c = cv_new(p.tw, p.th);
  if (shBlur > 0) cv_shadow(&c, ins, ins + 2, w, h, r, shBlur, shCol);
  cv_rrect1(&c, ins, ins, w, h, r, bcol);
  float ir = r - 1 > 0 ? r - 1 : 0;
  cv_rrect(&c, ins + 1, ins + 1, w - 2, h - 1 - bottom, ir, fill, nf, 180);
  if (hilite) { /* inset 0 1px 0 highlight */
    for (int x = ins + 2; x < ins + w - 2; x++) { float d = sd_rrect(x + 0.5f, ins + 1.5f, ins + 1, ins + 1, w - 2, h - 1 - bottom, ir); if (d < -0.5f) cv_blend(&c, x, ins + 1, hilite, 1); }
  }
  p.t = cv_tex(&c);
  cv_free(&c);
  return p;
}
static void pill_draw(const Pill *p, int x, int y, int w) {
  if (!p->t) return;
  int X = x - p->ins, W = w + 2 * p->ins, Y = y - p->ins, H = p->th;
  if (W < 2 * p->cap) W = 2 * p->cap;
  SDL_Rect s1 = { 0, 0, p->cap, p->th }, d1 = { X, Y, p->cap, H };
  SDL_Rect s2 = { p->cap, 0, p->tw - 2 * p->cap, p->th }, d2 = { X + p->cap, Y, W - 2 * p->cap, H };
  SDL_Rect s3 = { p->tw - p->cap, 0, p->cap, p->th }, d3 = { X + W - p->cap, Y, p->cap, H };
  SDL_RenderCopy(R, p->t, &s1, &d1);
  if (d2.w > 0) SDL_RenderCopy(R, p->t, &s2, &d2);
  SDL_RenderCopy(R, p->t, &s3, &d3);
}

/* ---------- polygon fill (even-odd scanline) for the globe ---------- */
static void cv_poly(Cv *c, const float *xy, int n, uint32_t col) {
  float miny = 1e9f, maxy = -1e9f;
  for (int i = 0; i < n; i++) { if (xy[i * 2 + 1] < miny) miny = xy[i * 2 + 1]; if (xy[i * 2 + 1] > maxy) maxy = xy[i * 2 + 1]; }
  int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
  if (y0 < 0) y0 = 0; if (y1 > c->h) y1 = c->h;
  float xs[256];
  for (int y = y0; y < y1; y++) {
    /* 2 sub-scanlines for smoother edges */
    for (int sub = 0; sub < 2; sub++) {
      float sy = y + 0.25f + sub * 0.5f;
      int k = 0;
      for (int i = 0, j = n - 1; i < n && k < 255; j = i++) {
        float ay = xy[i * 2 + 1], by = xy[j * 2 + 1];
        if ((ay > sy) != (by > sy)) xs[k++] = xy[i * 2] + (sy - ay) * (xy[j * 2] - xy[i * 2]) / (by - ay);
      }
      for (int a = 1; a < k; a++) { float v = xs[a]; int b = a - 1; while (b >= 0 && xs[b] > v) { xs[b + 1] = xs[b]; b--; } xs[b + 1] = v; }
      for (int a = 0; a + 1 < k; a += 2) {
        int xa = (int)floorf(xs[a]), xb = (int)ceilf(xs[a + 1]);
        for (int x = xa; x < xb; x++) {
          float cov = 1;
          if (x < xs[a]) cov -= xs[a] - x;
          if (x + 1 > xs[a + 1]) cov -= x + 1 - xs[a + 1];
          if (cov > 0) cv_blend(c, x, y, col, cov * 0.5f);
        }
      }
    }
  }
}
static void cv_line(Cv *c, float x0, float y0, float x1, float y1, float wdt, uint32_t col) {
  float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  int steps = (int)(len * 2) + 1;
  for (int i = 0; i <= steps; i++) {
    float t = (float)i / steps, cx = x0 + dx * t, cy = y0 + dy * t, r = wdt / 2;
    for (int py = (int)(cy - r - 1); py <= (int)(cy + r + 1); py++) for (int px = (int)(cx - r - 1); px <= (int)(cx + r + 1); px++) {
      float d = sqrtf((px + 0.5f - cx) * (px + 0.5f - cx) + (py + 0.5f - cy) * (py + 0.5f - cy)) - r;
      float a = 0.5f - d; if (a <= 0) continue; if (a > 1) a = 1;
      uint8_t *pp = c->p + ((size_t)py * c->w + px) * 4;
      if (px < 0 || py < 0 || px >= c->w || py >= c->h) continue;
      if (pp[3] < 255 || a > 0.3f) cv_blend(c, px, py, col, a * 0.35f);
    }
  }
}
