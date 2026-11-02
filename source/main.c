/*
 * Aeroverse for Wii U (Aroma homebrew)
 * SDL2 + SDL2_ttf + libcurl. Talks to the normal Aeroverse JSON API.
 *
 * STAGE 1: read (Home feed, Communities, posts, replies, drawings)
 * STAGE 2: + sign in, write posts and replies, Yeah!
 * STAGE 3: + Super/Ultra Wiggle Paint gallery and drawing on the GamePad
 */
#ifndef STAGE
#define STAGE 3
#endif
#define APP_VERSION "1.0"
/* build_info.h is written by the GitHub workflow: APP_BUILD (run number) and UPDATE_REPO (owner/repo) */
#if defined(__has_include)
#if __has_include("build_info.h")
#include "build_info.h"
#endif
#endif
#ifndef APP_BUILD
#define APP_BUILD 0
#endif
#ifndef UPDATE_REPO
#define UPDATE_REPO ""
#endif
#define APP_FILE "fs:/vol/external01/wiiu/apps/aeroverse.wuhb"

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_ttf.h>
#include <curl/curl.h>
#include <coreinit/memory.h>
#include <vpad/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include "cJSON.h"
#include "cacert_pem.h"
#include "nunito_reg_ttf.h"
#include "nunito_bold_ttf.h"
#include "logo_img.h"
#include "icons_img.h"

#define SW 1280
#define SH 720
#ifndef API
#define API "https://aeroverse.lukwszystkorobicz.pl"
#endif
#define CX 140
#define CW 1000
#define TOPH 76
#define FOOTH 34
#define LOGIN_FILE "fs:/vol/external01/wiiu/apps/aeroverse_login.txt"

/* ------------------------------------------------------------------ colours (from app.css) */
static const uint32_t C_BG = 0xe8f3f8, C_DOT = 0xd3e3ea, C_LINE = 0xc9d6dd, C_INK = 0x323232, C_SOFT = 0x7a7a7a,
    C_GREEN = 0x5ac800, C_GREEN_D = 0x3f9a00, C_TOPLINE = 0x8fd4f2, C_SEC = 0x2d5d78, C_BLUE = 0x1b8fd3,
    C_BUB = 0xf5faf0, C_BUB_L = 0xd3e3c4, C_YEAH = 0xfff8e1, C_YEAH_T = 0x9a6a00, C_YEAH_L = 0xf0cf73,
    C_YEAH_ON = 0xffd54a, C_YEAH_ON_L = 0xd9a400, C_YEAH_ON_T = 0x4a3200, C_REPLY = 0xf6f6f6, C_REPLY_L = 0xe8e8e8,
    C_RED = 0xe5322d, C_WHITE = 0xffffff;

static const char *PALHEX[32] = { "ffffff","c2c3c7","83769c","5f574f","1d2b53","000000","7e2553","ab5236","ff004d","ff77a8","ffccaa","ffa300","ffec27","00e436","008751","29adff",
  "065ab5","4b2d7f","ff6e59","f0b27a","a8e6cf","2ab7ca","d291bc","8d5524","3f3f74","639bff","99e550","37946e","fbf236","d95763","663931","222034" };
static uint32_t PAL[32];
static const int AV_IDX[8] = { 8, 11, 13, 15, 17, 21, 22, 26 };

/* ------------------------------------------------------------------ basics */
static SDL_Renderer *R;
static SDL_Window *WIN;
static TTF_Font *F_SMALL, *F_BODY, *F_BOLD, *F_H, *F_LOGO, *F_BIG;
static uint32_t frameNo;
static int running = 1;

static char *xstrdup(const char *s) { if (!s) s = ""; size_t n = strlen(s); char *o = malloc(n + 1); memcpy(o, s, n + 1); return o; }
static void setc(uint32_t h) { SDL_SetRenderDrawColor(R, (h >> 16) & 255, (h >> 8) & 255, h & 255, 255); }
static void fillr(int x, int y, int w, int h, uint32_t c) { SDL_Rect r = { x, y, w, h }; setc(c); SDL_RenderFillRect(R, &r); }
static void boxr(int x, int y, int w, int h, uint32_t fill, uint32_t line, int bw) { fillr(x, y, w, h, line); fillr(x + bw, y + bw, w - 2 * bw, h - 2 * bw, fill); }
static int inr(int px, int py, int x, int y, int w, int h) { return px >= x && py >= y && px < x + w && py < y + h; }

typedef struct { SDL_Texture *t; int w, h; } Tex;
static void tex_free(Tex *x) { if (x->t) SDL_DestroyTexture(x->t); x->t = NULL; x->w = x->h = 0; }
static Tex mk_text(TTF_Font *f, const char *s, uint32_t col, int wrap) {
  Tex x = { 0 };
  if (!s || !*s) return x;
  SDL_Color c = { (col >> 16) & 255, (col >> 8) & 255, col & 255, 255 };
  SDL_Surface *sf = wrap > 0 ? TTF_RenderUTF8_Blended_Wrapped(f, s, c, wrap) : TTF_RenderUTF8_Blended(f, s, c);
  if (!sf) return x;
  x.t = SDL_CreateTextureFromSurface(R, sf); x.w = sf->w; x.h = sf->h;
  SDL_FreeSurface(sf);
  return x;
}
static void draw_tex(const Tex *x, int X, int Y) { if (!x->t) return; SDL_Rect d = { X, Y, x->w, x->h }; SDL_RenderCopy(R, x->t, NULL, &d); }
static void draw_tex_clip(const Tex *x, int X, int Y, int maxh) {
  if (!x->t) return;
  int h = x->h < maxh ? x->h : maxh;
  SDL_Rect s = { 0, 0, x->w, h }, d = { X, Y, x->w, h };
  SDL_RenderCopy(R, x->t, &s, &d);
}

/* small cached labels: draw_label(font, colour, text, x, y, align) align 0 left 1 centre 2 right */
typedef struct { TTF_Font *f; uint32_t col; char *s; Tex t; uint32_t used; } Lab;
#define NLAB 384
static Lab labCache[NLAB];
static Tex *label(TTF_Font *f, uint32_t col, const char *s) {
  static Tex empty;
  if (!s || !*s) return &empty;
  int freeI = -1, oldI = 0;
  for (int i = 0; i < NLAB; i++) {
    if (!labCache[i].s) { if (freeI < 0) freeI = i; continue; }
    if (labCache[i].f == f && labCache[i].col == col && !strcmp(labCache[i].s, s)) { labCache[i].used = frameNo; return &labCache[i].t; }
    if (labCache[i].used < labCache[oldI].used || !labCache[oldI].s) oldI = i;
  }
  int i = freeI >= 0 ? freeI : oldI;
  if (labCache[i].s) { free(labCache[i].s); tex_free(&labCache[i].t); }
  labCache[i].f = f; labCache[i].col = col; labCache[i].s = xstrdup(s); labCache[i].t = mk_text(f, s, col, 0); labCache[i].used = frameNo;
  return &labCache[i].t;
}
static int draw_label(TTF_Font *f, uint32_t col, const char *s, int x, int y, int align) {
  Tex *t = label(f, col, s);
  if (!t->t) return 0;
  int X = align == 1 ? x - t->w / 2 : align == 2 ? x - t->w : x;
  draw_tex(t, X, y);
  return t->w;
}
static int label_w(TTF_Font *f, const char *s) { int w = 0, h = 0; if (s && *s) TTF_SizeUTF8(f, s, &w, &h); return w; }

/* button like the site's .btn (green, darker bottom edge) */
static void draw_btn(int x, int y, int w, int h, const char *txt, int style, int focus) {
  /* style 0 green, 1 white alt, 2 yeah, 3 yeah on, 4 red */
  uint32_t bg = C_GREEN, ln = C_GREEN_D, tc = C_WHITE;
  if (style == 1) { bg = C_WHITE; ln = 0xb4c9d4; tc = C_SEC; }
  if (style == 2) { bg = C_YEAH; ln = C_YEAH_L; tc = C_YEAH_T; }
  if (style == 3) { bg = C_YEAH_ON; ln = C_YEAH_ON_L; tc = C_YEAH_ON_T; }
  if (style == 4) { bg = C_RED; ln = 0xa81f1b; tc = C_WHITE; }
  if (focus) fillr(x - 4, y - 4, w + 8, h + 8, C_BLUE);
  fillr(x, y, w, h, ln);
  fillr(x + 1, y + 1, w - 2, h - 4, bg);
  Tex *t = label(F_BOLD, tc, txt);
  if (t->t) draw_tex(t, x + (w - t->w) / 2, y + (h - 3 - t->h) / 2);
}

/* ------------------------------------------------------------------ text helpers */
static void fmt_ago(double ms, char *out, size_t n) {
  time_t now = time(NULL);
  double s = (double)now - ms / 1000.0;
  if ((double)now > 1.6e9 && s > -120) {
    if (s < 45) { snprintf(out, n, "just now"); return; }
    int m = (int)(s / 60 + 0.5); if (m < 60) { snprintf(out, n, "%d min ago", m); return; }
    int hr = (int)(m / 60.0 + 0.5); if (hr < 24) { snprintf(out, n, "%d hr ago", hr); return; }
    int d = (int)(hr / 24.0 + 0.5); if (d < 7) { snprintf(out, n, d == 1 ? "%d day ago" : "%d days ago", d); return; }
  }
  time_t t = (time_t)(ms / 1000.0);
  struct tm *g = gmtime(&t);
  static const char *mon[12] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
  if (g) snprintf(out, n, "%d %s %d", g->tm_mday, mon[g->tm_mon % 12], g->tm_year + 1900); else snprintf(out, n, "?");
}

/* same idea as stripMarkup() in the worker: drop [color=..] etc, ** __ ~~ */
static char *strip_markup(const char *s, size_t cap) {
  if (!s) s = "";
  size_t n = strlen(s);
  char *o = malloc(n + 16);
  size_t j = 0, i = 0;
  static const char *tags[] = { "color", "glow", "font", "size", "grad", "i" };
  while (i < n && j < cap) {
    if (s[i] == '[') {
      const char *e = strchr(s + i, ']');
      if (e) {
        size_t L = (size_t)(e - (s + i)) + 1;
        if (!strncmp(s + i, "[img=", 5)) { memcpy(o + j, "[image]", 7); j += 7; i += L; continue; }
        const char *p = s + i + 1; if (*p == '/') p++;
        int hit = 0;
        for (int k = 0; k < 6; k++) { size_t tl = strlen(tags[k]); if (!strncmp(p, tags[k], tl) && (p[tl] == '=' || p[tl] == ']')) { hit = 1; break; } }
        if (hit) { i += L; continue; }
      }
    }
    if ((s[i] == '*' && s[i + 1] == '*') || (s[i] == '_' && s[i + 1] == '_') || (s[i] == '~' && s[i + 1] == '~')) { i += 2; continue; }
    if (s[i] == '\r') { i++; continue; }
    o[j++] = s[i++];
  }
  if (i < n) { while (j > 0 && ((unsigned char)o[j - 1] & 0xC0) == 0x80) j--; if (j > 0 && ((unsigned char)o[j - 1] & 0xC0) == 0xC0) j--; memcpy(o + j, "...", 3); j += 3; }
  while (j > 0 && (o[j - 1] == '\n' || o[j - 1] == ' ')) j--;
  o[j] = 0;
  return o;
}

/* ------------------------------------------------------------------ drawings (320x180 RLE, same as the site) */
#define DW 320
#define DH 180
#define DPIX (DW * DH)
static int b32v(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'v') return c - 'a' + 10; return -1; }
static int hexv(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; }

/* decodes into index buffer (DPIX bytes) and palette; returns wiggle strength (0 = none), -1 on failure */
static int decode_drawing(const char *code, uint8_t *px, uint32_t *pal) {
  for (int i = 0; i < 32; i++) pal[i] = PAL[i];
  memset(px, 0, DPIX);
  if (!code || !*code) return -1;
  const char *p = code;
  int wig = 0;
  if (*p == '~') { p++; wig = 3; if (*p >= '1' && *p <= '9') { wig = *p - '0'; p++; } }
  if (*p == '{') {
    p++;
    int i = 0;
    while (*p && *p != '}') {
      uint32_t v = 0; int ok = 1;
      for (int k = 0; k < 6; k++) { int h = hexv(p[k]); if (h < 0) { ok = 0; break; } v = v * 16 + (uint32_t)h; }
      if (!ok) break;
      if (i < 32) pal[i] = v;
      i++; p += 6;
      if (*p == ',') p++; else break;
    }
    while (*p && *p != '}') p++;
    if (*p == '}') p++;
  }
  int o = 0;
  while (*p) {
    int c = b32v(*p);
    if (c < 0) break;
    p++;
    char *end;
    long n = strtol(p, &end, 36);
    if (end == p || n <= 0 || o + n > DPIX) break;
    memset(px + o, c, (size_t)n);
    o += (int)n;
    p = end;
    if (*p == ',') p++;
  }
  return wig;
}

/* builds an RGBA texture; frame>0 shifts pixels a little (the "wiggle" boil) */
static SDL_Texture *drawing_tex(const uint8_t *px, const uint32_t *pal, int wig, int frame) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, DW, DH, 32, SDL_PIXELFORMAT_RGBA32);
  if (!s) return NULL;
  uint8_t *d = s->pixels;
  int amp = wig > 0 ? (wig >= 6 ? 2 : 1) : 0;
  for (int y = 0; y < DH; y++) {
    uint8_t *row = d + y * s->pitch;
    for (int x = 0; x < DW; x++) {
      int sx = x, sy = y;
      if (amp && frame) {
        unsigned hsh = (unsigned)((x >> 2) * 73856093u) ^ (unsigned)((y >> 2) * 19349663u) ^ (unsigned)(frame * 83492791u);
        hsh ^= hsh >> 13; hsh *= 0x5bd1e995u; hsh ^= hsh >> 15;
        sx += (int)(hsh % (unsigned)(2 * amp + 1)) - amp;
        sy += (int)((hsh >> 8) % (unsigned)(2 * amp + 1)) - amp;
        if (sx < 0) sx = 0; if (sx >= DW) sx = DW - 1; if (sy < 0) sy = 0; if (sy >= DH) sy = DH - 1;
      }
      uint32_t c = pal[px[sy * DW + sx] & 31];
      row[x * 4] = (c >> 16) & 255; row[x * 4 + 1] = (c >> 8) & 255; row[x * 4 + 2] = c & 255; row[x * 4 + 3] = 255;
    }
  }
  SDL_Texture *t = SDL_CreateTextureFromSurface(R, s);
  SDL_FreeSurface(s);
  return t;
}

/* ------------------------------------------------------------------ network thread */
typedef void (*NetCb)(int serial, long code, const char *body, const char *err, int extra);
typedef struct { char method[8]; char url[400]; char *body; NetCb cb; int serial, extra; } NetReq;
typedef struct { NetCb cb; int serial, extra; long code; char *body; size_t len; char err[160]; } NetRes;
#define NQ 16
static NetReq reqQ[NQ]; static int reqHead, reqTail;
static NetRes resQ[NQ]; static int resHead, resTail;
static SDL_mutex *netMx; static SDL_cond *netCv;
static volatile int netQuit;
static char sessionTok[96];
static int netBusy;

typedef struct { char *p; size_t n; } Buf;
static size_t wr_cb(void *d, size_t sz, size_t nm, void *u) {
  Buf *b = u; size_t n = sz * nm;
  if (b->n + n > 48 * 1024 * 1024) return 0;
  char *np = realloc(b->p, b->n + n + 1); if (!np) return 0;
  b->p = np; memcpy(b->p + b->n, d, n); b->n += n; b->p[b->n] = 0;
  return n;
}
static size_t hdr_cb(char *d, size_t sz, size_t nm, void *u) {
  size_t n = sz * nm;
  (void)u;
  if (n > 16 && !strncasecmp(d, "set-cookie:", 11)) {
    const char *p = d + 11; while (*p == ' ') p++;
    if (!strncmp(p, "sid=", 4)) {
      p += 4;
      char tok[96]; int k = 0;
      while (k < 95 && p < d + n && *p != ';' && *p != '\r' && *p != '\n') tok[k++] = *p++;
      tok[k] = 0;
      SDL_LockMutex(netMx); snprintf(sessionTok, sizeof sessionTok, "%s", tok); SDL_UnlockMutex(netMx);
    }
  }
  return n;
}
static int prog_cb(void *u, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) { (void)u; (void)a; (void)b; (void)c; (void)d; return netQuit ? 1 : 0; }

static int net_thread(void *u) {
  (void)u;
  CURL *c = curl_easy_init();
  while (!netQuit) {
    SDL_LockMutex(netMx);
    while (reqHead == reqTail && !netQuit) SDL_CondWait(netCv, netMx);
    if (netQuit) { SDL_UnlockMutex(netMx); break; }
    NetReq rq = reqQ[reqTail]; reqTail = (reqTail + 1) % NQ;
    char tok[96]; snprintf(tok, sizeof tok, "%s", sessionTok);
    netBusy = 1;
    SDL_UnlockMutex(netMx);

    Buf b = { 0 };
    char err[CURL_ERROR_SIZE]; err[0] = 0;
    long code = 0;
    CURLcode rc = CURLE_FAILED_INIT;
    if (c) {
      curl_easy_reset(c);
      struct curl_blob blob; blob.data = (void *)cacert_pem; blob.len = cacert_pem_size; blob.flags = CURL_BLOB_COPY;
      curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &blob);
      curl_easy_setopt(c, CURLOPT_URL, rq.url);
      curl_easy_setopt(c, CURLOPT_USERAGENT, "AeroverseWiiU/" APP_VERSION);
      curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
      curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
      curl_easy_setopt(c, CURLOPT_TIMEOUT, strstr(rq.url, ".wuhb") ? 600L : 30L);
      curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
      curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wr_cb);
      curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
      curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, hdr_cb);
      curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
      curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, prog_cb);
      struct curl_slist *hl = NULL;
      char ck[128];
      if (tok[0]) { snprintf(ck, sizeof ck, "Cookie: sid=%s", tok); hl = curl_slist_append(hl, ck); }
      hl = curl_slist_append(hl, "X-Requested-With: aeroverse");
      hl = curl_slist_append(hl, "Accept: application/json");
      if (strcmp(rq.method, "GET")) {
        hl = curl_slist_append(hl, "Content-Type: application/json");
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, rq.method);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, rq.body ? rq.body : "");
      }
      curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
      rc = curl_easy_perform(c);
      curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
      curl_slist_free_all(hl);
    }
    free(rq.body);

    SDL_LockMutex(netMx);
    NetRes *r = &resQ[resHead];
    r->cb = rq.cb; r->serial = rq.serial; r->extra = rq.extra; r->code = code; r->body = b.p; r->len = b.n;
    snprintf(r->err, sizeof r->err, "%s", rc == CURLE_OK ? "" : (err[0] ? err : curl_easy_strerror(rc)));
    resHead = (resHead + 1) % NQ;
    netBusy = 0;
    SDL_UnlockMutex(netMx);
  }
  if (c) curl_easy_cleanup(c);
  return 0;
}
static void net_send(const char *method, const char *path, const char *body, NetCb cb, int serial, int extra) {
  SDL_LockMutex(netMx);
  if ((reqHead + 1) % NQ == reqTail) { SDL_UnlockMutex(netMx); return; }
  NetReq *q = &reqQ[reqHead];
  snprintf(q->method, sizeof q->method, "%s", method);
  if (!strncmp(path, "http", 4)) snprintf(q->url, sizeof q->url, "%s", path); else snprintf(q->url, sizeof q->url, "%s%s", API, path);
  q->body = body ? xstrdup(body) : NULL;
  q->cb = cb; q->serial = serial; q->extra = extra;
  reqHead = (reqHead + 1) % NQ;
  SDL_CondSignal(netCv);
  SDL_UnlockMutex(netMx);
}
static size_t lastLen;
static void net_poll(void) {
  for (;;) {
    SDL_LockMutex(netMx);
    if (resHead == resTail) { SDL_UnlockMutex(netMx); return; }
    NetRes r = resQ[resTail]; resTail = (resTail + 1) % NQ;
    SDL_UnlockMutex(netMx);
    lastLen = r.len;
    if (r.cb) r.cb(r.serial, r.code, r.body, r.err, r.extra);
    free(r.body);
  }
}
static int net_pending(void) { SDL_LockMutex(netMx); int p = netBusy || reqHead != reqTail; SDL_UnlockMutex(netMx); return p; }

/* error text from a JSON {"error": "..."} body */
static void api_error(long code, const char *body, const char *err, char *out, size_t n) {
  if (err && *err) { snprintf(out, n, "No connection: %s", err); return; }
  cJSON *j = body ? cJSON_Parse(body) : NULL;
  cJSON *e = j ? cJSON_GetObjectItem(j, "error") : NULL;
  if (cJSON_IsString(e)) snprintf(out, n, "%s", e->valuestring); else snprintf(out, n, "Server error (%ld)", code);
  cJSON_Delete(j);
}

/* ------------------------------------------------------------------ session / me */
typedef struct { int id; char username[24]; char display[48]; int admin; } Me;
static Me me;
static int meKnown;
static char toastMsg[200]; static uint32_t toastUntil;
static void toast(const char *m) { snprintf(toastMsg, sizeof toastMsg, "%s", m); toastUntil = SDL_GetTicks() + 2800; }

/* ---- self update from the GitHub releases of the repo that built this app */
static int updateBuild, updating;
static void update_check_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)serial; (void)err; (void)extra;
  if (code != 200 || !body) return;
  int b = atoi(body);
  if (b > APP_BUILD && APP_BUILD > 0) { updateBuild = b; toast("A new version of Aeroverse is available! See Home."); }
}
static void update_check(void) {
  if (!UPDATE_REPO[0]) return;
  net_send("GET", "https://github.com/" UPDATE_REPO "/releases/latest/download/version.txt", NULL, update_check_cb, 0, 0);
}
static void update_dl_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)serial; (void)extra;
  updating = 0;
  if (code != 200 || !body || lastLen < 100000 || memcmp(body, "WUHB", 4)) {
    char m[200]; snprintf(m, sizeof m, "Update failed: %s", err && *err ? err : "download error"); toast(m); return;
  }
  FILE *f = fopen(APP_FILE ".part", "wb");
  if (!f) { toast("Update failed: cannot write to the SD card."); return; }
  size_t w = fwrite(body, 1, lastLen, f);
  int ok = fclose(f) == 0 && w == lastLen;
  if (!ok) { remove(APP_FILE ".part"); toast("Update failed: SD card full?"); return; }
  remove(APP_FILE);
  if (rename(APP_FILE ".part", APP_FILE)) { toast("Update failed: cannot replace the app file."); return; }
  updateBuild = -1;
  toast("Updated! Press HOME, close Aeroverse and start it again.");
}
static void update_start(void) {
  if (updating || updateBuild <= 0) return;
  updating = 1;
  toast("Downloading the update... keep the app open.");
  net_send("GET", "https://github.com/" UPDATE_REPO "/releases/latest/download/aeroverse.wuhb", NULL, update_dl_cb, 0, 0);
}

static void save_token(void) {
  FILE *f = fopen(LOGIN_FILE, "w");
  if (!f) return;
  SDL_LockMutex(netMx); fprintf(f, "%s\n", sessionTok); SDL_UnlockMutex(netMx);
  fclose(f);
}
static void load_token(void) {
  FILE *f = fopen(LOGIN_FILE, "r");
  if (!f) return;
  char b[96] = { 0 };
  if (fgets(b, sizeof b, f)) {
    b[strcspn(b, "\r\n")] = 0;
    int ok = strlen(b) >= 16;
    for (char *p = b; *p; p++) if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) ok = 0;
    if (ok) snprintf(sessionTok, sizeof sessionTok, "%s", b);
  }
  fclose(f);
}
static void forget_token(void) { SDL_LockMutex(netMx); sessionTok[0] = 0; SDL_UnlockMutex(netMx); remove(LOGIN_FILE); }

static void me_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)serial; (void)extra; (void)err;
  if (code != 200 || !body) return;
  cJSON *j = cJSON_Parse(body);
  cJSON *u = j ? cJSON_GetObjectItem(j, "user") : NULL;
  memset(&me, 0, sizeof me);
  if (cJSON_IsObject(u)) {
    cJSON *v;
    if ((v = cJSON_GetObjectItem(u, "id")) && cJSON_IsNumber(v)) me.id = v->valueint;
    if ((v = cJSON_GetObjectItem(u, "username")) && cJSON_IsString(v)) snprintf(me.username, sizeof me.username, "%s", v->valuestring);
    if ((v = cJSON_GetObjectItem(u, "display")) && cJSON_IsString(v)) snprintf(me.display, sizeof me.display, "%s", v->valuestring);
    if ((v = cJSON_GetObjectItem(u, "is_admin"))) me.admin = cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valueint);
  } else if (sessionTok[0]) forget_token();
  meKnown = 1;
  cJSON_Delete(j);
}

/* ------------------------------------------------------------------ list items */
enum { K_POST, K_FULLPOST, K_REPLY, K_COMM, K_HEAD, K_MORE, K_ACTION, K_INFO, K_FIELD, K_DRAWF, K_WIG, K_WIGFULL, K_SEC, K_DCELL, K_PROFILE, K_PLAZA, K_GAME };
enum { A_NONE, A_WRITE_POST, A_WRITE_REPLY, A_SIGNIN, A_SIGNOUT, A_SEND, A_DRAW, A_MODE, A_WIG_DRAW, A_RETRY, A_DRAW_REPLY, A_PLAZA_FILTER, A_UPDATE };

typedef struct Item {
  int kind, act, id, yeahs, replies, mine, posts, sel_ok;
  double created;
  char *name, *user, *cname, *cid, *body, *drawing, *title;
  Tex tName, tBody, tTitle;
  SDL_Texture *dr[3]; int ndr, wig;
  int x, y, w, h, clip, dw, dh;
  SDL_Rect yeahR, btnR;
} Item;

static void item_free(Item *it) {
  free(it->name); free(it->user); free(it->cname); free(it->cid); free(it->body); free(it->drawing); free(it->title);
  tex_free(&it->tName); tex_free(&it->tBody); tex_free(&it->tTitle);
  for (int i = 0; i < 3; i++) if (it->dr[i]) SDL_DestroyTexture(it->dr[i]);
  memset(it, 0, sizeof *it);
}

static void item_drawing(Item *it, const char *code) {
  if (!code || !*code) return;
  uint8_t *px = malloc(DPIX); uint32_t pal[32];
  int wig = decode_drawing(code, px, pal);
  if (wig >= 0) {
    it->wig = wig;
    it->ndr = wig ? 3 : 1;
    for (int f = 0; f < it->ndr; f++) it->dr[f] = drawing_tex(px, pal, wig, f);
  }
  free(px);
}

/* ------------------------------------------------------------------ screens */
enum { S_FEED, S_COMMS, S_COMM, S_POST, S_WIGGLE, S_WENTRY, S_COMPOSE, S_LOGIN, S_PLAZA, S_GAMES, S_USER, S_STUB };
typedef struct { char *name, *user; int tags; float x, y, tx, ty, wait; } Person;
typedef struct Screen {
  int kind, serial;
  Item *it; int n, cap;
  float scroll, vel; int contentH;
  int sel;
  int loading; char err[200];
  int more, lastId, page;
  char cid[48], cname[96];
  int postId, mode, ckind;   /* ckind for compose: 0 post, 1 reply, 2 wiggle */
  char text[1001], text2[64], pass[104];
  uint8_t *draw; int drawWig;
  Item *parent;              /* copy of the post for S_POST */
  Person *pp; int np, filter, psel;
  int needPost;
} Screen;

#define MAXS 10
static Screen *stack[MAXS]; static int depth;
#define NTABS 5
static Screen *roots[NTABS];      /* home, communities, plaza, games, profile */
static int curTab;
static int serialCounter = 1;

static Screen *cur(void) { return depth ? stack[depth - 1] : NULL; }
static Screen *by_serial(int s) { for (int i = 0; i < depth; i++) if (stack[i]->serial == s) return stack[i]; for (int i = 0; i < NTABS; i++) if (roots[i] && roots[i]->serial == s) return roots[i]; return NULL; }
static Item *add_item(Screen *s, int kind) {
  if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 32; s->it = realloc(s->it, sizeof(Item) * (size_t)s->cap); }
  Item *it = &s->it[s->n++];
  memset(it, 0, sizeof *it);
  it->kind = kind;
  it->sel_ok = !(kind == K_HEAD || kind == K_INFO || kind == K_REPLY || kind == K_SEC || kind == K_PROFILE);
  return it;
}
static void clear_items(Screen *s, int from) { for (int i = from; i < s->n; i++) item_free(&s->it[i]); if (from < s->n) s->n = from; }
static Screen *new_screen(int kind) { Screen *s = calloc(1, sizeof(Screen)); s->kind = kind; s->serial = serialCounter++; return s; }
static void free_people(Screen *s) { for (int i = 0; i < s->np; i++) { free(s->pp[i].name); free(s->pp[i].user); } free(s->pp); s->pp = NULL; s->np = 0; }
static void free_screen(Screen *s) { if (!s) return; clear_items(s, 0); free(s->it); free(s->draw); free_people(s); if (s->parent) { item_free(s->parent); free(s->parent); } free(s); }
static void push(Screen *s) { if (depth < MAXS) stack[depth++] = s; else free_screen(s); }
static void pop(void) {
  if (depth <= 1) return;
  Screen *s = stack[--depth];
  int isRoot = 0; for (int i = 0; i < NTABS; i++) if (roots[i] == s) isRoot = 1;
  if (!isRoot) free_screen(s);
}

/* ------------------------------------------------------------------ building items from JSON */
static const char *jstr(cJSON *o, const char *k) { cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL; return cJSON_IsString(v) ? v->valuestring : NULL; }
static double jnum(cJSON *o, const char *k) { cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL; return cJSON_IsNumber(v) ? v->valuedouble : 0; }
static int jbool(cJSON *o, const char *k) { cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL; return cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valueint); }

static void fill_post(Item *it, cJSON *p) {
  cJSON *a = cJSON_GetObjectItem(p, "author"), *c = cJSON_GetObjectItem(p, "community");
  it->id = (int)jnum(p, "id"); it->yeahs = (int)jnum(p, "yeahs"); it->replies = (int)jnum(p, "replies"); it->mine = jbool(p, "mine");
  it->created = jnum(p, "created_at");
  const char *disp = jstr(a, "display"), *un = jstr(a, "username");
  it->name = xstrdup(disp && *disp ? disp : un);
  it->user = xstrdup(un);
  it->cname = xstrdup(cJSON_IsObject(c) ? jstr(c, "name") : NULL);
  it->cid = xstrdup(cJSON_IsObject(c) ? jstr(c, "id") : NULL);
  it->body = xstrdup(jstr(p, "body"));
  it->drawing = xstrdup(jstr(p, "drawing"));
}

/* creates textures + height. inner width depends on kind */
static void build_item(Item *it) {
  int full = it->kind == K_FULLPOST, rep = it->kind == K_REPLY;
  if (it->kind == K_POST || full || rep) {
    int av = rep ? 48 : 64;
    int bubX = 16 + av + 16, bubW = it->w - bubX - 16;
    it->tName = mk_text(F_BOLD, it->name, C_INK, 0);
    char *b = strip_markup(it->body, full ? 5000 : 700);
    it->tBody = mk_text(F_BODY, b, C_INK, bubW - 28);
    free(b);
    it->clip = full || rep ? 100000 : 6 * TTF_FontLineSkip(F_BODY);
    item_drawing(it, it->drawing);
    if (it->ndr) { it->dw = full ? 640 : 480; it->dh = full ? 360 : 270; if (it->dw > bubW - 28) { it->dw = 320; it->dh = 180; } }
    int bh = 14 + 32 + (it->tBody.t ? 6 + (it->tBody.h < it->clip ? it->tBody.h : it->clip) : 0) + (it->ndr ? 10 + it->dh : 0) + (rep ? 0 : 12 + 40) + 14;
    it->h = (bh + 24 > av + 24 ? bh + 24 : av + 24);
  } else if (it->kind == K_COMM) {
    it->tName = mk_text(F_H, it->name, C_SEC, it->w - 120);
    it->tBody = mk_text(F_BODY, it->body, C_SOFT, it->w - 120);
    it->h = 16 + it->tName.h + 4 + (it->tBody.t ? it->tBody.h + 4 : 0) + 30 + 16;
    if (it->h < 112) it->h = 112;
  } else if (it->kind == K_HEAD) {
    it->tName = mk_text(F_BIG, it->name, C_SEC, it->w - 40);
    char *b = strip_markup(it->body, 2000);
    it->tBody = mk_text(F_BODY, b, C_INK, it->w - 40);
    free(b);
    it->h = 20 + it->tName.h + (it->tBody.t ? 8 + it->tBody.h : 0) + 20;
  } else if (it->kind == K_INFO) {
    it->tBody = mk_text(F_BODY, it->body, C_SOFT, it->w - 40);
    it->h = (it->tBody.h ? it->tBody.h : 30) + 32;
  } else if (it->kind == K_MORE || it->kind == K_ACTION) {
    it->h = 76;
  } else if (it->kind == K_FIELD) {
    it->tBody = mk_text(F_BODY, it->body && *it->body ? it->body : "", C_INK, it->w - 56);
    int th = it->tBody.h > 34 ? it->tBody.h : 34;
    if (th > 300) th = 300;
    it->h = 16 + 30 + 8 + th + 24 + 16;
  } else if (it->kind == K_DRAWF) {
    item_drawing(it, it->drawing);
    it->dw = 480; it->dh = 270;
    it->h = 16 + 30 + 8 + it->dh + 16;
  } else if (it->kind == K_WIG || it->kind == K_WIGFULL) {
    int fullw = it->kind == K_WIGFULL;
    item_drawing(it, it->drawing);
    it->dw = fullw ? 640 : 320; it->dh = fullw ? 360 : 180;
    it->tTitle = mk_text(fullw ? F_BIG : F_BOLD, it->title, C_SEC, it->w - 24);
    if (!fullw && it->tTitle.w > it->w - 24) { tex_free(&it->tTitle); char t[64]; snprintf(t, sizeof t, "%.24s...", it->title ? it->title : ""); it->tTitle = mk_text(F_BOLD, t, C_SEC, 0); }
    it->h = 8 + it->dh + 10 + it->tTitle.h + 4 + 26 + 10 + 44 + 12;
  } else if (it->kind == K_SEC) {
    it->tName = mk_text(F_H, it->title, C_SEC, it->w);
    it->h = it->tName.h + 6;
  } else if (it->kind == K_DCELL) {
    item_drawing(it, it->drawing);
    it->dw = 320; it->dh = 180;
    it->h = 8 + 180 + 10 + 34 + 10;
  } else if (it->kind == K_PROFILE) {
    it->tName = mk_text(F_BIG, it->name, C_INK, it->w - 160);
    char *b = strip_markup(it->body, 1500);
    it->tBody = mk_text(F_BODY, b, C_INK, it->w - 160);
    free(b);
    int th = it->tName.h + 4 + 28 + (it->tBody.t ? 10 + it->tBody.h : 0) + 8 + 26;
    it->h = 20 + (th > 112 ? th : 112) + 20;
  } else if (it->kind == K_PLAZA) {
    it->h = 470;
  } else if (it->kind == K_GAME) {
    it->tName = mk_text(F_H, it->title, C_INK, it->w - 260);
    it->tBody = mk_text(F_BODY, it->body, C_SOFT, it->w - 260);
    int th = 18 + it->tName.h + 6 + it->tBody.h + 12 + 46 + 18;
    it->h = th > 196 ? th : 196;
  }
}

/* ------------------------------------------------------------------ loading */
static void load_screen(Screen *s, int append);

static void posts_cb(int serial, long code, const char *body, const char *err, int append) {
  Screen *s = by_serial(serial);
  if (!s) return;
  s->loading = 0;
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); return; }
  s->err[0] = 0;
  cJSON *j = cJSON_Parse(body);
  cJSON *arr = j ? cJSON_GetObjectItem(j, "posts") : NULL;
  if (s->n && s->it[s->n - 1].kind == K_MORE) clear_items(s, s->n - 1);
  int cnt = 0;
  cJSON *p;
  cJSON_ArrayForEach(p, arr) {
    Item *it = add_item(s, K_POST);
    it->w = CW;
    fill_post(it, p);
    build_item(it);
    s->lastId = it->id;
    cnt++;
  }
  if (!append && cnt == 0) { Item *it = add_item(s, K_INFO); it->w = CW; it->body = xstrdup("No posts here yet."); build_item(it); }
  if (cnt >= 15) { Item *it = add_item(s, K_MORE); it->w = CW; }
  cJSON_Delete(j);
}

static void comms_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)extra;
  Screen *s = by_serial(serial);
  if (!s) return;
  s->loading = 0;
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); return; }
  s->err[0] = 0;
  clear_items(s, 0);
  cJSON *j = cJSON_Parse(body);
  cJSON *arr = j ? cJSON_GetObjectItem(j, "communities") : NULL, *c;
  cJSON_ArrayForEach(c, arr) {
    const char *age = jstr(c, "age");
    if (age && strcmp(age, "all")) continue; /* same as the lite site: only all-ages communities */
    Item *it = add_item(s, K_COMM);
    it->w = CW;
    it->cid = xstrdup(jstr(c, "id"));
    it->name = xstrdup(jstr(c, "name"));
    it->body = xstrdup(jstr(c, "description"));
    it->posts = (int)jnum(c, "posts");
    build_item(it);
  }
  cJSON_Delete(j);
}

static void replies_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)extra;
  Screen *s = by_serial(serial);
  if (!s) return;
  s->loading = 0;
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); return; }
  s->err[0] = 0;
  /* keep the post (item 0); rebuild the rest */
  clear_items(s, 1);
  cJSON *j = cJSON_Parse(body);
  cJSON *arr = j ? cJSON_GetObjectItem(j, "replies") : NULL, *r;
  int n = cJSON_GetArraySize(arr);
  Item *h = add_item(s, K_INFO); h->w = CW;
  char t[64]; snprintf(t, sizeof t, n == 1 ? "1 reply" : "%d replies", n);
  h->body = xstrdup(n ? t : "No replies yet.");
  build_item(h);
  cJSON_ArrayForEach(r, arr) {
    Item *it = add_item(s, K_REPLY);
    it->w = CW - 40; it->x = 40;
    fill_post(it, r);
    build_item(it);
  }
#if STAGE >= 2
  Item *a = add_item(s, K_ACTION); a->w = CW; a->act = me.id ? A_WRITE_REPLY : A_SIGNIN;
  a->title = xstrdup(me.id ? "Write a reply" : "Sign in to reply");
  build_item(a);
#endif
  if (s->it[0].kind == K_FULLPOST) s->it[0].replies = n;
  cJSON_Delete(j);
}

#if STAGE >= 3
static void wiggle_cb(int serial, long code, const char *body, const char *err, int append) {
  Screen *s = by_serial(serial);
  if (!s) return;
  s->loading = 0;
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); return; }
  s->err[0] = 0;
  if (s->n && s->it[s->n - 1].kind == K_MORE) clear_items(s, s->n - 1);
  cJSON *j = cJSON_Parse(body);
  cJSON *arr = j ? cJSON_GetObjectItem(j, "entries") : NULL, *e;
  int cnt = 0;
  cJSON_ArrayForEach(e, arr) {
    Item *it = add_item(s, K_WIG);
    it->w = 336;
    cJSON *a = cJSON_GetObjectItem(e, "author");
    it->id = (int)jnum(e, "id"); it->yeahs = (int)jnum(e, "yeahs"); it->mine = jbool(e, "mine"); it->created = jnum(e, "created_at");
    it->title = xstrdup(jstr(e, "title")); it->drawing = xstrdup(jstr(e, "drawing"));
    const char *disp = jstr(a, "display"), *un = jstr(a, "username");
    it->name = xstrdup(disp && *disp ? disp : un); it->user = xstrdup(un);
    build_item(it);
    cnt++;
  }
  if (!append && !cnt) { Item *it = add_item(s, K_INFO); it->w = CW; it->body = xstrdup("No drawings yet. Be the first!"); build_item(it); }
  if (jbool(j, "more")) { Item *it = add_item(s, K_MORE); it->w = CW; }
  cJSON_Delete(j);
}
#endif

static void top_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)extra;
  Screen *s = by_serial(serial);
  if (!s) return;
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); return; }
  cJSON *j = cJSON_Parse(body);
  cJSON *arr = j ? cJSON_GetObjectItem(j, "posts") : NULL, *p;
  int n = 0;
  cJSON_ArrayForEach(p, arr) { Item *it = add_item(s, K_POST); it->w = CW; fill_post(it, p); build_item(it); n++; }
  if (!n) { Item *it = add_item(s, K_INFO); it->w = CW; it->body = xstrdup("No liked posts this week yet. Give a Yeah! to something you like."); build_item(it); }
  Item *h = add_item(s, K_SEC); h->w = CW; h->title = xstrdup("Drawings of the week"); build_item(h);
  arr = j ? cJSON_GetObjectItem(j, "drawings") : NULL; n = 0;
  cJSON_ArrayForEach(p, arr) { Item *it = add_item(s, K_DCELL); it->w = 336; fill_post(it, p); build_item(it); n++; }
  if (!n) { Item *it = add_item(s, K_INFO); it->w = CW; it->body = xstrdup("No liked drawings this week yet."); build_item(it); }
  Item *l = add_item(s, K_SEC); l->w = CW; l->title = xstrdup("Latest posts"); build_item(l);
  cJSON_Delete(j);
}

static void user_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)extra;
  Screen *s = by_serial(serial);
  if (!s) return;
  if (code != 200 || !body) { s->loading = 0; api_error(code, body, err, s->err, sizeof s->err); return; }
  cJSON *j = cJSON_Parse(body);
  cJSON *u = j ? cJSON_GetObjectItem(j, "user") : NULL;
  Item *it = add_item(s, K_PROFILE); it->w = CW;
  const char *disp = jstr(u, "display");
  it->name = xstrdup(disp && *disp ? disp : jstr(u, "username"));
  it->user = xstrdup(jstr(u, "username"));
  it->body = xstrdup(jstr(u, "bio"));
  it->posts = (int)jnum(u, "posts");
  it->created = jnum(u, "created_at");
  build_item(it);
#if STAGE >= 2
  if (me.id && it->user && !strcmp(it->user, me.username)) { Item *a = add_item(s, K_ACTION); a->w = CW; a->act = A_SIGNOUT; a->title = xstrdup("Sign out"); build_item(a); }
#endif
  Item *h = add_item(s, K_SEC); h->w = CW; h->title = xstrdup("Posts"); build_item(h);
  cJSON_Delete(j);
}

static void post_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)extra;
  Screen *s = by_serial(serial);
  if (!s) return;
  if (code != 200 || !body) { s->loading = 0; api_error(code, body, err, s->err, sizeof s->err); return; }
  cJSON *j = cJSON_Parse(body);
  cJSON *p = j ? cJSON_GetObjectItem(j, "post") : NULL;
  if (cJSON_IsObject(p)) {
    clear_items(s, 0);
    Item *f = add_item(s, K_FULLPOST); f->w = CW; fill_post(f, p); build_item(f);
    snprintf(s->cname, sizeof s->cname, "%s", f->name ? f->name : "");
    s->needPost = 0;
  }
  cJSON_Delete(j);
}

static void plaza_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)extra;
  Screen *s = by_serial(serial);
  if (!s) return;
  s->loading = 0;
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); return; }
  s->err[0] = 0;
  free_people(s);
  cJSON *j = cJSON_Parse(body);
  cJSON *arr = j ? cJSON_GetObjectItem(j, "people") : NULL, *p;
  int n = cJSON_GetArraySize(arr);
  s->pp = calloc((size_t)(n ? n : 1), sizeof(Person));
  cJSON_ArrayForEach(p, arr) {
    Person *q = &s->pp[s->np++];
    const char *disp = jstr(p, "display");
    q->name = xstrdup(disp && *disp ? disp : jstr(p, "username"));
    q->user = xstrdup(jstr(p, "username"));
    cJSON *t, *tags = cJSON_GetObjectItem(p, "tags");
    cJSON_ArrayForEach(t, tags) if (cJSON_IsString(t)) {
      if (!strcmp(t->valuestring, "popular")) q->tags |= 1;
      if (!strcmp(t->valuestring, "new")) q->tags |= 2;
      if (!strcmp(t->valuestring, "active")) q->tags |= 4;
    }
    q->x = q->tx = 60 + (float)(rand() % 880); q->y = q->ty = 200 + (float)(rand() % 220); q->wait = (float)(rand() % 120);
  }
  cJSON_Delete(j);
}

static void add_header_items(Screen *s) {
  if (s->kind == S_FEED) {
    if (updateBuild > 0) {
      Item *u = add_item(s, K_ACTION); u->w = CW; u->act = A_UPDATE;
      char t[96]; snprintf(t, sizeof t, "New version available (build %d) - tap to update", updateBuild);
      u->title = xstrdup(t); build_item(u);
    }
#if STAGE >= 2
    Item *a = add_item(s, K_ACTION); a->w = CW; a->act = me.id ? A_WRITE_POST : A_SIGNIN; a->title = xstrdup(me.id ? "Write a post" : "Sign in to post and draw."); build_item(a);
#endif
    Item *h = add_item(s, K_SEC); h->w = CW; h->title = xstrdup("Most liked this week"); build_item(h);
  }
  if (s->kind == S_PLAZA) {
    Item *h = add_item(s, K_HEAD); h->w = CW; h->name = xstrdup("Plaza");
    h->body = xstrdup("Popular, new and recently active people walk around here. Touch a character to visit their page."); build_item(h);
    static const char *FN[4] = { "Showing: Everyone", "Showing: Popular", "Showing: Newest", "Showing: Active now" };
    Item *f = add_item(s, K_ACTION); f->w = CW; f->act = A_PLAZA_FILTER; f->title = xstrdup(FN[s->filter & 3]); build_item(f);
    Item *pz = add_item(s, K_PLAZA); pz->w = CW; build_item(pz);
  }
  if (s->kind == S_GAMES) {
    Item *h = add_item(s, K_SEC); h->w = CW; h->title = xstrdup("Games & Challenges"); build_item(h);
#if STAGE >= 3
    Item *g1 = add_item(s, K_GAME); g1->w = CW; g1->id = 0; g1->title = xstrdup("Super Wiggle Paint");
    g1->body = xstrdup("Draw anything while the whole canvas shakes, flips upside down and changes colour. Say what it is and send it to the gallery.");
    build_item(g1);
    Item *g2 = add_item(s, K_GAME); g2->w = CW; g2->id = 1; g2->title = xstrdup("Ultra Wiggle Paint");
    g2->body = xstrdup("Everything is 3 times faster. The screen spins and changes colour too. Sometimes your last move disappears. Good luck!");
    build_item(g2);
    Item *i = add_item(s, K_INFO); i->w = CW; i->body = xstrdup("More challenges are on the way."); build_item(i);
#else
    Item *i = add_item(s, K_INFO); i->w = CW; i->body = xstrdup("Games come in the next update of the app."); build_item(i);
#endif
  }
  if (s->kind == S_STUB) {
    Item *i = add_item(s, K_INFO); i->w = CW; i->body = xstrdup("Signing in comes in the next update of the app."); build_item(i);
  }
  if (s->kind == S_COMM) {
    Item *h = add_item(s, K_HEAD); h->w = CW; h->name = xstrdup(s->cname); h->body = xstrdup(s->text); build_item(h);
#if STAGE >= 2
    Item *a = add_item(s, K_ACTION); a->w = CW; a->act = me.id ? A_WRITE_POST : A_SIGNIN; a->title = xstrdup(me.id ? "Write a post" : "Sign in to post"); build_item(a);
#endif
  }
#if STAGE >= 3
  if (s->kind == S_WIGGLE) {
    Item *h = add_item(s, K_HEAD); h->w = CW;
    h->name = xstrdup(s->mode ? "Ultra Wiggle Paint" : "Super Wiggle Paint");
    h->body = xstrdup(s->mode ? "Everything is 3x faster, the canvas flips and changes colour, and sometimes your last stroke disappears. Good luck!"
                              : "Draw anything while the canvas wiggles, then name what you drew. See what everyone else made below.");
    build_item(h);
    Item *a = add_item(s, K_ACTION); a->w = CW; a->act = me.id ? A_WIG_DRAW : A_SIGNIN; a->title = xstrdup(me.id ? "Start drawing" : "Sign in to play"); build_item(a);
  }
#endif
}

static void load_screen(Screen *s, int append) {
  char path[300];
  if (s->kind == S_COMPOSE || s->kind == S_LOGIN || s->kind == S_WENTRY) return;
  if (!append) {
    if (s->kind == S_POST && !s->needPost) clear_items(s, 1); else clear_items(s, 0);
    s->scroll = 0; s->vel = 0; s->sel = 0; s->lastId = 0; s->page = 0;
    add_header_items(s);
  }
  s->loading = 1; s->err[0] = 0;
  switch (s->kind) {
  case S_FEED:
    if (append) snprintf(path, sizeof path, "/api/posts?limit=15&before=%d", s->lastId);
    else { snprintf(path, sizeof path, "/api/posts?limit=15"); net_send("GET", "/api/top", NULL, top_cb, s->serial, 0); }
    net_send("GET", path, NULL, posts_cb, s->serial, 1);
    break;
  case S_USER:
    if (append) snprintf(path, sizeof path, "/api/posts?limit=15&user=%s&before=%d", s->cid, s->lastId);
    else {
      char up[80]; snprintf(up, sizeof up, "/api/users/%s", s->cid);
      net_send("GET", up, NULL, user_cb, s->serial, 0);
      snprintf(path, sizeof path, "/api/posts?limit=15&user=%s", s->cid);
    }
    net_send("GET", path, NULL, posts_cb, s->serial, 1);
    break;
  case S_PLAZA: net_send("GET", "/api/plaza", NULL, plaza_cb, s->serial, 0); break;
  case S_COMM:
    if (append) snprintf(path, sizeof path, "/api/posts?limit=15&community=%s&before=%d", s->cid, s->lastId); else snprintf(path, sizeof path, "/api/posts?limit=15&community=%s", s->cid);
    net_send("GET", path, NULL, posts_cb, s->serial, append);
    break;
  case S_COMMS: net_send("GET", "/api/communities", NULL, comms_cb, s->serial, 0); break;
  case S_POST:
    if (s->needPost) { snprintf(path, sizeof path, "/api/posts/%d", s->postId); net_send("GET", path, NULL, post_cb, s->serial, 0); }
    snprintf(path, sizeof path, "/api/posts/%d/replies", s->postId); net_send("GET", path, NULL, replies_cb, s->serial, 0);
    break;
#if STAGE >= 3
  case S_WIGGLE:
    if (append) s->page++;
    snprintf(path, sizeof path, "/api/wiggle?mode=%s&page=%d", s->mode ? "ultra" : "super", s->page);
    net_send("GET", path, NULL, wiggle_cb, s->serial, append);
    break;
#endif
  default: s->loading = 0; break;
  }
}

static Screen *make_profile_root(void);
static void go_tab(int t) {
  if (t < 0) t = 0;
  if (t >= NTABS) t = NTABS - 1;
  while (depth > 1) pop();
  depth = 0;
  curTab = t;
  if (t == 4) {
    /* profile tab: your page when signed in, the sign-in form otherwise (rebuilt every time) */
    if (roots[4]) free_screen(roots[4]);
    roots[4] = make_profile_root();
  }
  push(roots[t]);
  if (!roots[t]->n && !roots[t]->loading) load_screen(roots[t], 0);
}

/* ------------------------------------------------------------------ on-screen keyboard (stage 2+) */
#if STAGE >= 2
typedef struct { int open, mask, maxlen, shift, pl, kx, ky, multiline; char *target; size_t cap; char title[64]; void (*done)(void); } Kbd;
static Kbd kb;
static const char *KROWS[2][5][11] = {
  { { "1","2","3","4","5","6","7","8","9","0","-" }, { "q","w","e","r","t","y","u","i","o","p","?" },
    { "a","s","d","f","g","h","j","k","l","'","!" }, { "z","x","c","v","b","n","m",",",".","@","_" },
    { "ą","ć","ę","ł","ń","ó","ś","ź","ż",":",")" } },
  { { "!","\"","#","$","%","&","/","(",")","=","+" }, { "Q","W","E","R","T","Y","U","I","O","P","*" },
    { "A","S","D","F","G","H","J","K","L",";","~" }, { "Z","X","C","V","B","N","M","<",">","^","|" },
    { "Ą","Ć","Ę","Ł","Ń","Ó","Ś","Ź","Ż","[","]" } } };
#define KCOLS 11
#define KW 96
#define KH 70
#define KX0 ((SW - KCOLS * KW) / 2)
#define KY0 236
/* bottom row: Shift, Space, Backspace, Enter, Cancel, Done */
static const char *KBOT[6] = { "Shift", "Space", "Delete", "Enter", "Cancel", "Done" };
static const int KBOTW[6] = { 2, 4, 2, 1, 1, 1 };

static void kb_open(const char *title, char *target, size_t cap, int maxlen, int mask, int multiline, void (*done)(void)) {
  memset(&kb, 0, sizeof kb);
  kb.open = 1; kb.target = target; kb.cap = cap; kb.maxlen = maxlen; kb.mask = mask; kb.multiline = multiline; kb.done = done; kb.ky = 1;
  snprintf(kb.title, sizeof kb.title, "%s", title);
}
static int utf8_len(const char *s) { int n = 0; for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++; return n; }
static void kb_type(const char *t) {
  size_t L = strlen(kb.target), tl = strlen(t);
  if (L + tl + 1 > kb.cap || utf8_len(kb.target) >= kb.maxlen) return;
  memcpy(kb.target + L, t, tl + 1);
  if (kb.shift == 1) kb.shift = 0;
}
static void kb_back(void) {
  size_t L = strlen(kb.target);
  if (!L) return;
  L--; while (L > 0 && ((unsigned char)kb.target[L] & 0xC0) == 0x80) L--;
  kb.target[L] = 0;
}
static void kb_bottom(int i) {
  if (i == 0) kb.shift = kb.shift ? 0 : 1;
  else if (i == 1) kb_type(" ");
  else if (i == 2) kb_back();
  else if (i == 3) { if (kb.multiline) kb_type("\n"); else { kb.open = 0; if (kb.done) kb.done(); } }
  else if (i == 4) { kb.open = 0; }
  else if (i == 5) { kb.open = 0; if (kb.done) kb.done(); }
}
static void kb_press(int x, int y) {
  if (y < 5) kb_type(KROWS[kb.shift ? 1 : 0][y][x]);
  else {
    int acc = 0;
    for (int i = 0; i < 6; i++) { if (x < acc + KBOTW[i]) { kb_bottom(i); return; } acc += KBOTW[i]; }
  }
}
static void kb_draw(void) {
  SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(R, 20, 40, 55, 170); SDL_Rect all = { 0, 0, SW, SH }; SDL_RenderFillRect(R, &all);
  boxr(KX0 - 20, 30, KCOLS * KW + 40, SH - 50, 0xf3f8fb, C_TOPLINE, 3);
  draw_label(F_H, C_SEC, kb.title, KX0, 44, 0);
  /* text box */
  boxr(KX0, 88, KCOLS * KW, 132, C_WHITE, C_LINE, 2);
  char shown[1100];
  if (kb.mask) { int n = utf8_len(kb.target); if (n > 60) n = 60; memset(shown, '*', (size_t)n); shown[n] = 0; }
  else snprintf(shown, sizeof shown, "%s", kb.target);
  size_t sl = strlen(shown);
  if (sl < sizeof shown - 2 && (SDL_GetTicks() / 500) % 2) { shown[sl] = '|'; shown[sl + 1] = 0; }
  Tex t = mk_text(F_BODY, shown[0] ? shown : " ", C_INK, KCOLS * KW - 24);
  if (t.t) { int off = t.h > 116 ? t.h - 116 : 0; SDL_Rect s = { 0, off, t.w, t.h - off }, d = { KX0 + 12, 96, t.w, t.h - off }; SDL_RenderCopy(R, t.t, &s, &d); }
  tex_free(&t);
  char cnt[32]; snprintf(cnt, sizeof cnt, "%d / %d", utf8_len(kb.target), kb.maxlen);
  draw_label(F_SMALL, C_SOFT, cnt, KX0 + KCOLS * KW, 52, 2);
  for (int y = 0; y < 5; y++) for (int x = 0; x < KCOLS; x++) {
    int X = KX0 + x * KW, Y = KY0 + y * KH, foc = kb.kx == x && kb.ky == y;
    boxr(X + 3, Y + 3, KW - 6, KH - 6, foc ? 0xdff3c8 : C_WHITE, foc ? C_GREEN : C_LINE, foc ? 3 : 1);
    draw_label(F_H, C_INK, KROWS[kb.shift ? 1 : 0][y][x], X + KW / 2, Y + 16, 1);
  }
  int acc = 0;
  for (int i = 0; i < 6; i++) {
    int X = KX0 + acc * KW, Y = KY0 + 5 * KH, w = KBOTW[i] * KW;
    int foc = kb.ky == 5 && kb.kx >= acc && kb.kx < acc + KBOTW[i];
    const char *lab = KBOT[i];
    if (i == 0 && kb.shift) lab = "SHIFT";
    if (i == 3 && !kb.multiline) lab = "OK";
    draw_btn(X + 4, Y + 6, w - 8, KH - 12, lab, i == 5 ? 0 : i == 4 ? 4 : 1, foc);
    acc += KBOTW[i];
  }
  draw_label(F_SMALL, C_SOFT, "Touch the keys, or D-Pad + A.  B: delete   X: space   Y: shift   +: done", SW / 2, SH - 52, 1);
}
static void kb_input(uint32_t trig, int tapx, int tapy) {
  if (trig & VPAD_BUTTON_LEFT) { kb.kx--; }
  if (trig & VPAD_BUTTON_RIGHT) { kb.kx++; }
  if (trig & VPAD_BUTTON_UP) { kb.ky--; }
  if (trig & VPAD_BUTTON_DOWN) { kb.ky++; }
  if (kb.ky < 0) kb.ky = 5; if (kb.ky > 5) kb.ky = 0;
  if (kb.kx < 0) kb.kx = KCOLS - 1; if (kb.kx >= KCOLS) kb.kx = 0;
  if (trig & VPAD_BUTTON_A) kb_press(kb.kx, kb.ky);
  if (trig & VPAD_BUTTON_B) kb_back();
  if (trig & VPAD_BUTTON_X) kb_type(" ");
  if (trig & VPAD_BUTTON_Y) kb.shift = kb.shift ? 0 : 1;
  if (trig & VPAD_BUTTON_PLUS) { kb.open = 0; if (kb.done) kb.done(); }
  if (tapx >= 0) {
    int x = (tapx - KX0) / KW, y = (tapy - KY0) / KH;
    if (tapx >= KX0 && tapy >= KY0 && x < KCOLS && y <= 5) { kb.kx = x; kb.ky = y; kb_press(x, y); }
  }
}
#endif

/* ------------------------------------------------------------------ drawing editor (stage 2: reply/post drawings, stage 3: wiggle) */
#if STAGE >= 2
typedef struct {
  int open, ultra, wigForce, wig, color, size, eraser, strokes;
  uint8_t px[DPIX];
  uint8_t *undo[12]; int nundo;
  SDL_Texture *tex; int dirty;
  int drawing, lx, ly;
  uint32_t nextChaos; int flipX, flipY; uint32_t tint; int uiHue;
  uint32_t bPress;
  Screen *target;
} Editor;
static Editor ed;
#define EX 16
#define EY 16
#define ES 3
static uint8_t *ed_snapshot(void) { uint8_t *b = malloc(DPIX); memcpy(b, ed.px, DPIX); return b; }
static void ed_push_undo(void) {
  if (ed.nundo == 12) { free(ed.undo[0]); memmove(ed.undo, ed.undo + 1, sizeof(ed.undo[0]) * 11); ed.nundo--; }
  ed.undo[ed.nundo++] = ed_snapshot();
}
static void ed_undo(void) { if (!ed.nundo) return; ed.nundo--; memcpy(ed.px, ed.undo[ed.nundo], DPIX); free(ed.undo[ed.nundo]); ed.undo[ed.nundo] = NULL; ed.dirty = 1; }
static void ed_open(Screen *target, int wigForce, int ultra, const uint8_t *start) {
  memset(&ed, 0, sizeof ed);
  ed.open = 1; ed.target = target; ed.wigForce = wigForce; ed.wig = wigForce; ed.ultra = ultra; ed.color = 5; ed.size = 1; ed.dirty = 1;
  if (start) memcpy(ed.px, start, DPIX);
  ed.tex = SDL_CreateTexture(R, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, DW, DH);
  ed.nextChaos = SDL_GetTicks() + 2000;
}
static void ed_close(void) {
  for (int i = 0; i < ed.nundo; i++) free(ed.undo[i]);
  if (ed.tex) SDL_DestroyTexture(ed.tex);
  ed.tex = NULL; ed.open = 0; ed.nundo = 0;
}
static void ed_stamp(int cx, int cy) {
  int r = ed.size == 0 ? 0 : ed.size == 1 ? 1 : ed.size == 2 ? 3 : 6;
  uint8_t c = ed.eraser ? 0 : (uint8_t)ed.color;
  for (int y = cy - r; y <= cy + r; y++) for (int x = cx - r; x <= cx + r; x++) {
    if (x < 0 || y < 0 || x >= DW || y >= DH) continue;
    if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > r * r + r) continue;
    ed.px[y * DW + x] = c;
  }
}
static void ed_line(int x0, int y0, int x1, int y1) {
  int dx = abs(x1 - x0), dy = abs(y1 - y0), n = dx > dy ? dx : dy;
  if (!n) { ed_stamp(x0, y0); return; }
  for (int i = 0; i <= n; i++) ed_stamp(x0 + (x1 - x0) * i / n, y0 + (y1 - y0) * i / n);
  ed.dirty = 1;
}
static void ed_upload(int frame) {
  void *pix; int pitch;
  if (!ed.tex || SDL_LockTexture(ed.tex, NULL, &pix, &pitch)) return;
  int amp = ed.wig ? 1 : 0;
  for (int y = 0; y < DH; y++) {
    uint8_t *row = (uint8_t *)pix + y * pitch;
    for (int x = 0; x < DW; x++) {
      int sx = x, sy = y;
      if (amp && frame) {
        unsigned hsh = (unsigned)((x >> 2) * 73856093u) ^ (unsigned)((y >> 2) * 19349663u) ^ (unsigned)(frame * 83492791u);
        hsh ^= hsh >> 13; hsh *= 0x5bd1e995u; hsh ^= hsh >> 15;
        sx += (int)(hsh % 3u) - 1; sy += (int)((hsh >> 8) % 3u) - 1;
        if (sx < 0) sx = 0; if (sx >= DW) sx = DW - 1; if (sy < 0) sy = 0; if (sy >= DH) sy = DH - 1;
      }
      uint32_t c = PAL[ed.px[sy * DW + sx] & 31];
      row[x * 4] = (c >> 16) & 255; row[x * 4 + 1] = (c >> 8) & 255; row[x * 4 + 2] = c & 255; row[x * 4 + 3] = 255;
    }
  }
  SDL_UnlockTexture(ed.tex);
}
/* encode as the site's RLE: [~3]run,run,... */
static char *encode_drawing(const uint8_t *px, int wig) {
  static const char *B32 = "0123456789abcdefghijklmnopqrstuv";
  size_t cap = 64 * 1024, n = 0;
  char *o = malloc(cap);
  if (wig) { o[n++] = '~'; o[n++] = '3'; }
  int i = 0;
  while (i < DPIX) {
    int c = px[i] & 31, j = i;
    while (j < DPIX && (px[j] & 31) == c) j++;
    int len = j - i;
    char num[8]; int k = 0;
    do { num[k++] = "0123456789abcdefghijklmnopqrstuvwxyz"[len % 36]; len /= 36; } while (len && k < 7);
    if (n + 12 > cap) { cap *= 2; o = realloc(o, cap); }
    if (n && !(wig && n == 2)) o[n++] = ',';
    o[n++] = B32[c];
    while (k) o[n++] = num[--k];
    i = j;
  }
  o[n] = 0;
  return o;
}

/* editor layout */
#define EPX 996                   /* right panel */
static void ed_btn_rects(SDL_Rect *r) {
  /* 0 undo, 1 clear, 2 eraser, 3 wiggle, 4 cancel, 5 done, 6..9 sizes */
  int y = EY + DH * ES + 16, w = 150, h = 60, x = EX;
  for (int i = 0; i < 6; i++) { r[i] = (SDL_Rect){ x + i * (w + 12), y, w, h }; }
  for (int i = 0; i < 4; i++) r[6 + i] = (SDL_Rect){ EPX + i * 66, 16 + 8 * 56 + 16, 60, 56 };
}
static void ed_done(void);
static void ed_draw(void) {
  uint32_t now = SDL_GetTicks();
  int speed = ed.ultra ? 70 : 200;
  int frame = ed.wig ? 1 + (int)((now / (uint32_t)speed) % 3) : 0;
  static int lastFrame = -1;
  if (ed.dirty || frame != lastFrame) { ed_upload(frame); ed.dirty = 0; lastFrame = frame; }
  /* ultra chaos */
  if (ed.ultra && now > ed.nextChaos && ed.strokes > 0) {
    int r = rand() % 5;
    if (r == 0) ed.flipX = !ed.flipX;
    else if (r == 1) ed.flipY = !ed.flipY;
    else if (r == 2) ed.tint = 0x60ff60 | (uint32_t)(rand() % 256) << 16;
    else if (r == 3) ed.tint = 0x6060ff | (uint32_t)(rand() % 256) << 8;
    else { ed.tint = 0; ed.flipX = ed.flipY = 0; }
    ed.nextChaos = now + 800 + (uint32_t)(rand() % 1600);
  }
  if (ed.ultra) ed.uiHue = (int)(now / 12) % 360;
  /* background */
  uint32_t bg = 0xdfeff6;
  if (ed.ultra && ed.strokes) {
    float h = ed.uiHue / 60.0f; int i = (int)h; float f = h - i;
    int a = 200, b = (int)(200 + 55 * (1 - f)), c2 = (int)(200 + 55 * f), m = 255;
    int rr, gg, bb;
    switch (i % 6) { case 0: rr = m; gg = c2; bb = a; break; case 1: rr = b; gg = m; bb = a; break; case 2: rr = a; gg = m; bb = c2; break;
                     case 3: rr = a; gg = b; bb = m; break; case 4: rr = c2; gg = a; bb = m; break; default: rr = m; gg = a; bb = b; }
    bg = (uint32_t)(rr << 16 | gg << 8 | bb);
  }
  fillr(0, 0, SW, SH, bg);
  /* canvas */
  fillr(EX - 3, EY - 3, DW * ES + 6, DH * ES + 6, ed.wig ? C_GREEN : C_LINE);
  if (ed.tint) SDL_SetTextureColorMod(ed.tex, (ed.tint >> 16) & 255, (ed.tint >> 8) & 255, ed.tint & 255); else SDL_SetTextureColorMod(ed.tex, 255, 255, 255);
  SDL_Rect d = { EX, EY, DW * ES, DH * ES };
  int fl = (ed.flipX ? SDL_FLIP_HORIZONTAL : 0) | (ed.flipY ? SDL_FLIP_VERTICAL : 0);
  SDL_RenderCopyEx(R, ed.tex, NULL, &d, 0, NULL, (SDL_RendererFlip)fl);
  /* palette */
  for (int i = 0; i < 32; i++) {
    int X = EPX + (i % 4) * 66, Y = 16 + (i / 4) * 56;
    int sel = !ed.eraser && ed.color == i;
    fillr(X - (sel ? 4 : 1), Y - (sel ? 4 : 1), 60 + (sel ? 8 : 2), 50 + (sel ? 8 : 2), sel ? C_BLUE : C_LINE);
    fillr(X, Y, 60, 50, PAL[i]);
  }
  SDL_Rect r[10]; ed_btn_rects(r);
  static const char *SZ[4] = { "1", "2", "4", "8" };
  for (int i = 0; i < 4; i++) {
    draw_btn(r[6 + i].x, r[6 + i].y, r[6 + i].w, r[6 + i].h, SZ[i], ed.size == i ? 0 : 1, 0);
  }
  draw_label(F_SMALL, C_SEC, "Pen size", EPX, r[6].y + 62, 0);
  draw_btn(r[0].x, r[0].y, r[0].w, r[0].h, "Undo", 1, 0);
  draw_btn(r[1].x, r[1].y, r[1].w, r[1].h, "Clear", 1, 0);
  draw_btn(r[2].x, r[2].y, r[2].w, r[2].h, "Eraser", ed.eraser ? 0 : 1, 0);
  draw_btn(r[3].x, r[3].y, r[3].w, r[3].h, ed.wigForce ? (ed.ultra ? "ULTRA" : "Wiggle!") : ed.wig ? "Wiggle: on" : "Wiggle: off", ed.wig ? 0 : 1, 0);
  draw_btn(r[4].x, r[4].y, r[4].w, r[4].h, "Cancel", 4, 0);
  draw_btn(r[5].x, r[5].y, r[5].w, r[5].h, "Done", 0, 0);
  draw_label(F_SMALL, C_SEC, "Draw with the stylus.  Y: undo   L/R: pen size   +: done   B: cancel", EX, SH - 40, 0);
}
static void ed_touch(int tx, int ty, int down, int fresh) {
  SDL_Rect r[10]; ed_btn_rects(r);
  if (down) {
    if (tx >= EX && ty >= EY && tx < EX + DW * ES && ty < EY + DH * ES) {
      int cx = (tx - EX) / ES, cy = (ty - EY) / ES;
      if (ed.flipX) cx = DW - 1 - cx;
      if (ed.flipY) cy = DH - 1 - cy;
      if (!ed.drawing) { ed_push_undo(); ed.drawing = 1; ed.lx = cx; ed.ly = cy; }
      ed_line(ed.lx, ed.ly, cx, cy);
      ed.lx = cx; ed.ly = cy;
      return;
    }
  }
  if (!down && ed.drawing) {
    ed.drawing = 0; ed.strokes++;
    if (ed.ultra && ed.strokes > 2 && rand() % 22 == 0) { ed_undo(); toast("Ultra Wiggle ate your last stroke!"); }
  }
  if (!fresh) return;
  for (int i = 0; i < 32; i++) {
    int X = EPX + (i % 4) * 66, Y = 16 + (i / 4) * 56;
    if (inr(tx, ty, X, Y, 60, 50)) { ed.color = i; ed.eraser = 0; return; }
  }
  for (int i = 0; i < 4; i++) if (inr(tx, ty, r[6 + i].x, r[6 + i].y, r[6 + i].w, r[6 + i].h)) { ed.size = i; return; }
  if (inr(tx, ty, r[0].x, r[0].y, r[0].w, r[0].h)) ed_undo();
  else if (inr(tx, ty, r[1].x, r[1].y, r[1].w, r[1].h)) { ed_push_undo(); memset(ed.px, 0, DPIX); ed.dirty = 1; }
  else if (inr(tx, ty, r[2].x, r[2].y, r[2].w, r[2].h)) ed.eraser = !ed.eraser;
  else if (inr(tx, ty, r[3].x, r[3].y, r[3].w, r[3].h)) { if (!ed.wigForce) ed.wig = !ed.wig; }
  else if (inr(tx, ty, r[4].x, r[4].y, r[4].w, r[4].h)) ed_close();
  else if (inr(tx, ty, r[5].x, r[5].y, r[5].w, r[5].h)) ed_done();
}
static void rebuild_compose(Screen *s);
static void ed_done(void) {
  Screen *s = ed.target;
  int empty = 1;
  for (int i = 0; i < DPIX; i++) if (ed.px[i]) { empty = 0; break; }
  if (s && by_serial(s->serial)) {
    if (empty) { free(s->draw); s->draw = NULL; }
    else { if (!s->draw) s->draw = malloc(DPIX); memcpy(s->draw, ed.px, DPIX); s->drawWig = ed.wig; }
    rebuild_compose(s);
  }
  ed_close();
}
static void ed_buttons(uint32_t trig) {
  if (trig & VPAD_BUTTON_Y) ed_undo();
  if (trig & VPAD_BUTTON_L) { if (ed.size > 0) ed.size--; }
  if (trig & VPAD_BUTTON_R) { if (ed.size < 3) ed.size++; }
  if (trig & VPAD_BUTTON_PLUS) ed_done();
  if (trig & VPAD_BUTTON_B) {
    uint32_t now = SDL_GetTicks();
    if (ed.nundo == 0 || now - ed.bPress < 1500) ed_close(); else { ed.bPress = now; toast("Press B again to throw this drawing away"); }
  }
}
#endif

/* ------------------------------------------------------------------ compose / login screens (stage 2+) */
#if STAGE >= 2
static Screen *kbScreen;
static void compose_kb_done(void) { if (kbScreen && by_serial(kbScreen->serial)) rebuild_compose(kbScreen); }

static void rebuild_compose(Screen *s) {
  clear_items(s, 0);
  if (s->kind == S_LOGIN) {
    Item *h = add_item(s, K_HEAD); h->w = CW; h->name = xstrdup("Sign in"); h->body = xstrdup("Use your Aeroverse account. New here? Create an account on the website first, then sign in here."); build_item(h);
    Item *u = add_item(s, K_FIELD); u->w = CW; u->act = 1; u->title = xstrdup("Username"); u->body = xstrdup(s->text2); build_item(u);
    char m[110]; int n = utf8_len(s->pass); if (n > 100) n = 100; memset(m, '*', (size_t)n); m[n] = 0;
    Item *p = add_item(s, K_FIELD); p->w = CW; p->act = 2; p->title = xstrdup("Password"); p->body = xstrdup(m); build_item(p);
    Item *b = add_item(s, K_ACTION); b->w = CW; b->act = A_SEND; b->title = xstrdup(s->loading ? "Signing in..." : "Sign in"); build_item(b);
    return;
  }
  const char *head = s->ckind == 0 ? "New post" : s->ckind == 1 ? "Reply" : (s->mode ? "Ultra Wiggle Paint" : "Super Wiggle Paint");
  Item *h = add_item(s, K_HEAD); h->w = CW; h->name = xstrdup(head);
  char where[160];
  if (s->ckind == 0 && !s->cid[0]) snprintf(where, sizeof where, "Your post shows up on Home and on your profile.");
  else if (s->ckind == 0) snprintf(where, sizeof where, "Posting in %s", s->cname);
  else if (s->ckind == 1) snprintf(where, sizeof where, "Replying to %s", s->cname);
  else snprintf(where, sizeof where, "Draw something, then write what it is.");
  h->body = xstrdup(where); build_item(h);
  if (s->ckind == 2) {
    Item *d = add_item(s, K_DRAWF); d->w = CW; d->act = A_DRAW; d->title = xstrdup("Your drawing (tap to draw)");
    if (s->draw) d->drawing = encode_drawing(s->draw, 1);
    build_item(d);
    Item *t = add_item(s, K_FIELD); t->w = CW; t->act = 1; t->title = xstrdup("What did you draw?"); t->body = xstrdup(s->text2); build_item(t);
  } else {
    Item *t = add_item(s, K_FIELD); t->w = CW; t->act = 1; t->title = xstrdup(s->ckind == 0 ? "Your post" : "Your reply"); t->body = xstrdup(s->text); build_item(t);
    Item *d = add_item(s, K_DRAWF); d->w = CW; d->act = A_DRAW; d->title = xstrdup(s->draw ? "Drawing (tap to change)" : "Add a drawing (optional, tap to draw)");
    if (s->draw) d->drawing = encode_drawing(s->draw, s->drawWig);
    build_item(d);
  }
  Item *b = add_item(s, K_ACTION); b->w = CW; b->act = A_SEND; b->title = xstrdup(s->loading ? "Sending..." : "Send"); build_item(b);
}

static void sent_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)extra;
  Screen *s = by_serial(serial);
  if (!s) return;
  s->loading = 0;
  if (code != 200) { char e[200]; api_error(code, body, err, e, sizeof e); toast(e); rebuild_compose(s); return; }
  int ck = s->ckind, kind = s->kind;
  if (kind == S_LOGIN) {
    save_token();
    net_send("GET", "/api/me", NULL, me_cb, 0, 0);
    toast("Signed in!");
    pop();
    Screen *c = cur(); if (c) load_screen(c, 0);
    return;
  }
  toast(ck == 2 ? "Drawing sent!" : ck == 1 ? "Reply sent!" : "Posted!");
  pop();
  Screen *c = cur();
  if (c) load_screen(c, 0);
  if (ck == 0 && roots[0]) { clear_items(roots[0], 0); }
}

static void compose_send(Screen *s) {
  if (s->loading) return;
  cJSON *o = cJSON_CreateObject();
  if (s->kind == S_LOGIN) {
    if (!s->text2[0] || !s->pass[0]) { toast("Type your username and password."); cJSON_Delete(o); return; }
    cJSON_AddStringToObject(o, "username", s->text2);
    cJSON_AddStringToObject(o, "password", s->pass);
    char *b = cJSON_PrintUnformatted(o);
    net_send("POST", "/api/login", b, sent_cb, s->serial, 0);
    free(b);
  } else if (s->ckind == 2) {
    if (!s->draw) { toast("Draw something first!"); cJSON_Delete(o); return; }
    if (utf8_len(s->text2) < 2) { toast("Write what you drew (at least 2 letters)."); cJSON_Delete(o); return; }
    char *d = encode_drawing(s->draw, 1);
    cJSON_AddStringToObject(o, "title", s->text2);
    cJSON_AddStringToObject(o, "drawing", d);
    cJSON_AddStringToObject(o, "mode", s->mode ? "ultra" : "super");
    free(d);
    char *b = cJSON_PrintUnformatted(o);
    net_send("POST", "/api/wiggle", b, sent_cb, s->serial, 0);
    free(b);
  } else {
    if (!s->text[0] && !s->draw) { toast("Write something or draw something."); cJSON_Delete(o); return; }
    cJSON_AddStringToObject(o, "body", s->text);
    if (s->draw) { char *d = encode_drawing(s->draw, s->drawWig); cJSON_AddStringToObject(o, "drawing", d); free(d); }
    char path[64];
    if (s->ckind == 0) { if (s->cid[0]) cJSON_AddStringToObject(o, "community", s->cid); else cJSON_AddTrueToObject(o, "wall"); snprintf(path, sizeof path, "/api/posts"); }
    else snprintf(path, sizeof path, "/api/posts/%d/replies", s->postId);
    char *b = cJSON_PrintUnformatted(o);
    net_send("POST", path, b, sent_cb, s->serial, 0);
    free(b);
  }
  cJSON_Delete(o);
  s->loading = 1;
  rebuild_compose(s);
}

static void yeah_cb(int serial, long code, const char *body, const char *err, int id) {
  Screen *s = by_serial(serial);
  if (code != 200) { char e[200]; api_error(code, body, err, e, sizeof e); toast(e); return; }
  if (!s) return;
  cJSON *j = cJSON_Parse(body);
  for (int i = 0; i < s->n; i++) {
    Item *it = &s->it[i];
    if (it->id == id && (it->kind == K_POST || it->kind == K_FULLPOST || it->kind == K_WIG || it->kind == K_WIGFULL)) {
      it->yeahs = (int)jnum(j, "yeahs"); it->mine = jbool(j, "mine");
    }
  }
  cJSON_Delete(j);
}
static void do_yeah(Screen *s, Item *it) {
  if (!me.id) { toast("Sign in to give a Yeah!"); return; }
  char path[64];
  if (it->kind == K_WIG || it->kind == K_WIGFULL) snprintf(path, sizeof path, "/api/wiggle/%d/yeah", it->id);
  else snprintf(path, sizeof path, "/api/posts/%d/yeah", it->id);
  it->mine = !it->mine; it->yeahs += it->mine ? 1 : -1; /* instant feedback, server answer fixes it */
  net_send("POST", path, "{}", yeah_cb, s->serial, it->id);
}
static void open_login(void) {
  Screen *s = new_screen(S_LOGIN);
  push(s);
  rebuild_compose(s);
}
static void signout_cb(int serial, long code, const char *body, const char *err, int extra) { (void)serial; (void)code; (void)body; (void)err; (void)extra; }
#endif

static Screen *make_profile_root(void) {
#if STAGE >= 2
  if (me.id) { Screen *u = new_screen(S_USER); snprintf(u->cid, sizeof u->cid, "%s", me.username); return u; }
  Screen *l = new_screen(S_LOGIN); rebuild_compose(l); return l;
#else
  return new_screen(S_STUB);
#endif
}
static void open_user(const char *user) {
  if (!user || !*user) return;
  Screen *u = new_screen(S_USER);
  snprintf(u->cid, sizeof u->cid, "%s", user);
  push(u);
  load_screen(u, 0);
}
#if STAGE >= 2
static void do_signout(void) {
  net_send("POST", "/api/logout", "{}", signout_cb, 0, 0);
  forget_token(); memset(&me, 0, sizeof me);
  toast("Signed out.");
}
#endif

/* ------------------------------------------------------------------ layout + activation */
static void layout(Screen *s) {
  int y = TOPH + 18;
  {
    int col = 0, rowH = 0, gx = (SW - (3 * 336 + 2 * 12)) / 2;
    for (int i = 0; i < s->n; i++) {
      Item *it = &s->it[i];
      if (it->kind == K_WIG || it->kind == K_DCELL) {
        it->x = gx + col * (336 + 12); it->y = y; if (it->h > rowH) rowH = it->h;
        if (++col == 3) { col = 0; y += rowH + 12; rowH = 0; }
      } else {
        if (col) { col = 0; y += rowH + 12; rowH = 0; }
        it->x = CX + (it->kind == K_REPLY ? 40 : 0); it->y = y; y += it->h + 12;
      }
    }
    if (col) y += rowH + 12;
  }
  s->contentH = y + 70;
}

static void open_item(Screen *s, Item *it) {
  if (it->kind == K_PLAZA) {
    if (s->np && s->psel >= 0 && s->psel < s->np) open_user(s->pp[s->psel].user);
    return;
  }
  if (it->kind == K_ACTION && it->act == A_UPDATE) { update_start(); return; }
  if (it->kind == K_ACTION && it->act == A_PLAZA_FILTER) { s->filter = (s->filter + 1) % 4; static const char *FN[4] = { "Showing: Everyone", "Showing: Popular", "Showing: Newest", "Showing: Active now" }; free(it->title); it->title = xstrdup(FN[s->filter]); s->psel = 0; return; }
#if STAGE >= 2
  if (it->kind == K_ACTION && it->act == A_SIGNOUT) { do_signout(); go_tab(4); return; }
#endif
#if STAGE >= 3
  if (it->kind == K_GAME) { Screen *w = new_screen(S_WIGGLE); w->mode = it->id; push(w); load_screen(w, 0); return; }
#endif
  if (it->kind == K_POST || it->kind == K_DCELL) {
    Screen *p = new_screen(S_POST);
    p->postId = it->id;
    snprintf(p->cname, sizeof p->cname, "%s", it->name ? it->name : "");
    snprintf(p->cid, sizeof p->cid, "%s", it->cid ? it->cid : "");
    Item *f = add_item(p, K_FULLPOST);
    f->w = CW; f->id = it->id; f->yeahs = it->yeahs; f->replies = it->replies; f->mine = it->mine; f->created = it->created;
    f->name = xstrdup(it->name); f->user = xstrdup(it->user); f->cname = xstrdup(it->cname); f->cid = xstrdup(it->cid);
    f->body = xstrdup(it->body); f->drawing = xstrdup(it->drawing);
    build_item(f);
    push(p);
    load_screen(p, 0);
  } else if (it->kind == K_COMM) {
    Screen *c = new_screen(S_COMM);
    snprintf(c->cid, sizeof c->cid, "%s", it->cid);
    snprintf(c->cname, sizeof c->cname, "%s", it->name);
    snprintf(c->text, sizeof c->text, "%s", it->body ? it->body : "");
    push(c);
    load_screen(c, 0);
  } else if (it->kind == K_MORE) {
    if (!s->loading) load_screen(s, 1);
  }
#if STAGE >= 2
  else if (it->kind == K_FULLPOST) do_yeah(s, it);
  else if (it->kind == K_ACTION) {
    if (it->act == A_SIGNIN) open_login();
    else if (it->act == A_WRITE_POST || it->act == A_WRITE_REPLY) {
      Screen *c = new_screen(S_COMPOSE);
      c->ckind = it->act == A_WRITE_POST ? 0 : 1;
      snprintf(c->cid, sizeof c->cid, "%s", s->cid);
      snprintf(c->cname, sizeof c->cname, "%s", s->cname);
      c->postId = s->postId;
      push(c);
      rebuild_compose(c);
    }
#if STAGE >= 3
    else if (it->act == A_WIG_DRAW) {
      Screen *c = new_screen(S_COMPOSE);
      c->ckind = 2; c->mode = s->mode;
      push(c);
      rebuild_compose(c);
      ed_open(c, 1, c->mode, NULL);
    } else if (it->act == A_MODE) {
      s->mode = !s->mode;
      load_screen(s, 0);
    }
#endif
    else if (it->act == A_SEND) compose_send(s);
  } else if (it->kind == K_FIELD) {
    kbScreen = s;
    if (s->kind == S_LOGIN) {
      if (it->act == 1) kb_open("Username", s->text2, sizeof s->text2, 20, 0, 0, compose_kb_done);
      else kb_open("Password", s->pass, sizeof s->pass, 100, 1, 0, compose_kb_done);
    } else if (s->ckind == 2) kb_open("What did you draw?", s->text2, sizeof s->text2, 40, 0, 0, compose_kb_done);
    else kb_open(s->ckind == 0 ? "Write a post" : "Write a reply", s->text, sizeof s->text, s->ckind == 0 ? 1000 : 400, 0, 1, compose_kb_done);
  } else if (it->kind == K_DRAWF) {
    ed_open(s, s->ckind == 2, s->mode && s->ckind == 2, s->draw);
    if (s->ckind != 2 && s->draw) ed.wig = s->drawWig;
  }
#endif
#if STAGE >= 3
  else if (it->kind == K_WIG) {
    Screen *w = new_screen(S_WENTRY);
    Item *f = add_item(w, K_WIGFULL);
    f->w = CW; f->id = it->id; f->yeahs = it->yeahs; f->mine = it->mine; f->created = it->created;
    f->title = xstrdup(it->title); f->name = xstrdup(it->name); f->user = xstrdup(it->user); f->drawing = xstrdup(it->drawing);
    build_item(f);
    push(w);
  } else if (it->kind == K_WIGFULL) do_yeah(s, it);
#endif
  else if (it->kind == K_INFO && s->err[0]) load_screen(s, 0);
}

/* ------------------------------------------------------------------ drawing items */
static uint32_t av_color(const char *u) {
  uint32_t n = 0;
  for (const unsigned char *p = (const unsigned char *)(u ? u : "?"); *p; p++) n = n * 31 + *p;
  return PAL[AV_IDX[n % 8]];
}
static void draw_avatar(const char *name, const char *user, int x, int y, int s) {
  fillr(x, y, s, s, C_LINE);
  fillr(x + 2, y + 2, s - 4, s - 4, av_color(user));
  char ini[8] = "?";
  if (name && *name) {
    int L = 1; unsigned char c = (unsigned char)name[0];
    if (c >= 0xF0) L = 4; else if (c >= 0xE0) L = 3; else if (c >= 0xC0) L = 2;
    memcpy(ini, name, (size_t)L); ini[L] = 0;
    if (L == 1 && ini[0] >= 'a' && ini[0] <= 'z') ini[0] -= 32;
  }
  Tex *t = label(s >= 60 ? F_H : F_BOLD, C_WHITE, ini);
  if (t->t) draw_tex(t, x + (s - t->w) / 2, y + (s - t->h) / 2);
}
static void draw_drawing(Item *it, int x, int y) {
  if (!it->ndr) return;
  int f = 0;
  if (it->ndr > 1) f = (int)((SDL_GetTicks() / 180) % 3);
  SDL_Rect d = { x, y, it->dw, it->dh };
  fillr(x - 1, y - 1, it->dw + 2, it->dh + 2, 0x889999);
  SDL_RenderCopy(R, it->dr[f], NULL, &d);
}

static void draw_item(Screen *s, Item *it, int X, int Y, int focus) {
  (void)s;
  char buf[96];
  if (focus) fillr(X - 4, Y - 4, it->w + 8, it->h + 8, C_BLUE);
  switch (it->kind) {
  case K_POST: case K_FULLPOST: case K_REPLY: {
    int rep = it->kind == K_REPLY, av = rep ? 48 : 64;
    if (rep) boxr(X, Y, it->w, it->h, C_REPLY, C_REPLY_L, 1); else boxr(X, Y, it->w, it->h, C_WHITE, C_LINE, 1);
    draw_avatar(it->name, it->user, X + 16, Y + 12, av);
    int bx = X + 16 + av + 16, bw = it->w - (16 + av + 16) - 16, by = Y + 12, bh = it->h - 24;
    if (!rep) {
      boxr(bx, by, bw, bh, C_BUB, C_BUB_L, 1);
      /* the little speech-bubble arrow */
      for (int i = 0; i < 9; i++) { setc(C_BUB_L); SDL_RenderDrawLine(R, bx - 9 + i, by + 22 - i, bx - 9 + i, by + 22 + i); }
      for (int i = 1; i < 9; i++) { setc(C_BUB); SDL_RenderDrawLine(R, bx - 8 + i, by + 23 - i, bx - 8 + i, by + 21 + i); }
    }
    int tx = bx + 14, ty = by + 12;
    draw_tex(&it->tName, tx, ty);
    int ux = tx + it->tName.w + 10;
    snprintf(buf, sizeof buf, "@%s", it->user ? it->user : "");
    ux += draw_label(F_SMALL, C_SOFT, buf, ux, ty + 4, 0) + 10;
    if (it->cname && *it->cname && !rep) draw_label(F_SMALL, C_BLUE, it->cname, ux, ty + 4, 0);
    fmt_ago(it->created, buf, sizeof buf);
    draw_label(F_SMALL, C_SOFT, buf, bx + bw - 14, ty + 4, 2);
    ty += 32;
    if (it->tBody.t) { ty += 6; draw_tex_clip(&it->tBody, tx, ty, it->clip); ty += it->tBody.h < it->clip ? it->tBody.h : it->clip; }
    if (it->ndr) { ty += 10; draw_drawing(it, tx, ty); ty += it->dh; }
    if (!rep) {
      ty += 12;
      snprintf(buf, sizeof buf, "Yeah! %d", it->yeahs);
      int yw = label_w(F_BOLD, buf) + 36;
      draw_btn(tx, ty, yw, 40, buf, it->mine ? 3 : 2, 0);
      it->yeahR = (SDL_Rect){ tx - X, ty - Y, yw, 40 };
      snprintf(buf, sizeof buf, it->replies == 1 ? "1 reply" : "%d replies", it->replies);
      draw_label(F_SMALL, C_SOFT, buf, tx + yw + 14, ty + 9, 0);
    }
    break; }
  case K_COMM:
    boxr(X, Y, it->w, it->h, C_WHITE, C_LINE, 1);
    draw_avatar(it->name, it->cid, X + 16, Y + 16, 80);
    draw_tex(&it->tName, X + 112, Y + 16);
    draw_tex(&it->tBody, X + 112, Y + 16 + it->tName.h + 4);
    snprintf(buf, sizeof buf, it->posts == 1 ? "1 post" : "%d posts", it->posts);
    draw_label(F_SMALL, C_SOFT, buf, X + 112, Y + it->h - 16 - 26, 0);
    break;
  case K_HEAD:
    fillr(X, Y, it->w, it->h, 0x9fcfe6);
    /* hero gradient like .page.wide .hero */
    for (int i = 1; i < it->h - 1; i++) {
      float t = (float)i / it->h;
      int r = (int)(0xbf + (0xe6 - 0xbf) * t), g = (int)(0xe7 + (0xf6 - 0xe7) * t), b = (int)(0xfa + (0xfd - 0xfa) * t);
      SDL_SetRenderDrawColor(R, (Uint8)r, (Uint8)g, (Uint8)b, 255);
      SDL_RenderDrawLine(R, X + 1, Y + i, X + it->w - 2, Y + i);
    }
    draw_tex(&it->tName, X + 20, Y + 20);
    draw_tex(&it->tBody, X + 20, Y + 20 + it->tName.h + 8);
    break;
  case K_INFO:
    if (it->tBody.t) draw_tex(&it->tBody, X + 4, Y + 16);
    break;
  case K_MORE:
    draw_btn(X, Y + 8, it->w, 60, s->loading ? "Loading..." : "Load more", 1, 0);
    break;
  case K_ACTION:
    draw_btn(X, Y + 8, it->w, 60, it->title, it->act == A_MODE ? 1 : 0, 0);
    break;
  case K_FIELD:
    boxr(X, Y, it->w, it->h, C_WHITE, C_LINE, 1);
    draw_label(F_BOLD, C_SEC, it->title, X + 20, Y + 16, 0);
    boxr(X + 20, Y + 54, it->w - 40, it->h - 54 - 16, 0xfbfdfe, 0xb4c9d4, 2);
    if (it->tBody.t) draw_tex_clip(&it->tBody, X + 32, Y + 64, it->h - 54 - 36);
    else draw_label(F_BODY, 0xa0a8ad, "Tap here to type", X + 32, Y + 64, 0);
    break;
  case K_DRAWF:
    boxr(X, Y, it->w, it->h, C_WHITE, C_LINE, 1);
    draw_label(F_BOLD, C_SEC, it->title, X + 20, Y + 16, 0);
    if (it->ndr) draw_drawing(it, X + 20, Y + 54);
    else { boxr(X + 20, Y + 54, it->dw, it->dh, 0xfbfdfe, 0xb4c9d4, 2); draw_label(F_BOLD, 0xa0a8ad, "Tap to draw", X + 20 + it->dw / 2, Y + 54 + it->dh / 2 - 14, 1); }
    break;
  case K_SEC:
    draw_tex(&it->tName, X + 2, Y);
    break;
  case K_DCELL: {
    boxr(X, Y, it->w, it->h, C_WHITE, C_LINE, 1);
    draw_drawing(it, X + 8, Y + 8);
    int fy = Y + 8 + 180 + 10;
    draw_avatar(it->name, it->user, X + 10, fy, 30);
    char nm[48]; snprintf(nm, sizeof nm, "%.18s", it->name ? it->name : "");
    draw_label(F_SMALL, C_INK, nm, X + 48, fy + 3, 0);
    snprintf(buf, sizeof buf, "Yeah! %d", it->yeahs);
    draw_label(F_SMALL, C_YEAH_T, buf, X + it->w - 12, fy + 3, 2);
    break; }
  case K_PROFILE: {
    boxr(X, Y, it->w, it->h, C_WHITE, C_LINE, 1);
    draw_avatar(it->name, it->user, X + 20, Y + 20, 112);
    int ty = Y + 20;
    draw_tex(&it->tName, X + 152, ty); ty += it->tName.h + 4;
    snprintf(buf, sizeof buf, "@%s", it->user ? it->user : "");
    draw_label(F_BODY, C_SOFT, buf, X + 152, ty, 0); ty += 28;
    if (it->tBody.t) { ty += 10; draw_tex(&it->tBody, X + 152, ty); ty += it->tBody.h; }
    ty += 8;
    char since[48]; fmt_ago(it->created, since, sizeof since);
    if (it->posts == 1) snprintf(buf, sizeof buf, "1 post   ·   joined %s", since);
    else snprintf(buf, sizeof buf, "%d posts   ·   joined %s", it->posts, since);
    draw_label(F_SMALL, C_SOFT, buf, X + 152, ty, 0);
    break; }
  case K_GAME: {
    boxr(X, Y, it->w, it->h, C_WHITE, C_LINE, 1);
    int ultra = it->id == 1;
    /* game art like .gc-art: bright gradient box with big words */
    for (int i = 0; i < it->h - 32; i++) {
      float t = (float)i / (it->h - 32);
      int r = ultra ? (int)(255 - 60 * t) : (int)(90 - 40 * t), g = ultra ? (int)(60 + 40 * t) : (int)(200 - 30 * t), b = ultra ? (int)(170 + 60 * t) : (int)(40 + 180 * t);
      SDL_SetRenderDrawColor(R, (Uint8)r, (Uint8)g, (Uint8)b, 255);
      SDL_RenderDrawLine(R, X + 16, Y + 16 + i, X + 16 + 200, Y + 16 + i);
    }
    int wob = (int)(3 * sinf(SDL_GetTicks() / (ultra ? 60.0f : 180.0f)));
    draw_label(F_BIG, C_WHITE, ultra ? "ULTRA" : "Super", X + 116 + wob, Y + it->h / 2 - 44, 1);
    draw_label(F_BIG, C_WHITE, "Wiggle", X + 116 - wob, Y + it->h / 2, 1);
    int ty = Y + 18;
    draw_tex(&it->tName, X + 240, ty); ty += it->tName.h + 6;
    draw_tex(&it->tBody, X + 240, ty); ty += it->tBody.h + 12;
    draw_btn(X + 240, ty, ultra ? 240 : 120, 46, ultra ? "Play if you dare" : "Play", 0, 0);
    break; }
  case K_PLAZA: {
    /* sky + grass + tree, people walk around (2D version of the 3D plaza) */
    for (int i = 0; i < it->h; i++) {
      float t = (float)i / it->h;
      int r, g, b;
      if (t < 0.38f) { float k = t / 0.38f; r = (int)(0xa9 + (0xd8 - 0xa9) * k); g = (int)(0xdc + (0xf0 - 0xdc) * k); b = (int)(0xf6 + (0xfb - 0xf6) * k); }
      else { float k = (t - 0.38f) / 0.62f; r = (int)(0x8c - 0x30 * k); g = (int)(0xd8 - 0x40 * k); b = (int)(0x4a - 0x10 * k); }
      SDL_SetRenderDrawColor(R, (Uint8)r, (Uint8)g, (Uint8)b, 255);
      SDL_RenderDrawLine(R, X, Y + i, X + it->w - 1, Y + i);
    }
    fillr(X + it->w / 2 - 14, Y + 120, 28, 90, 0x8d5524);
    for (int k = 0; k < 3; k++) { int cx = X + it->w / 2 + (k - 1) * 44, cy = Y + 110 - (k == 1) * 30; for (int dy = -48; dy <= 48; dy++) { int hw = (int)sqrtf((float)(48 * 48 - dy * dy)); setc(k == 1 ? 0x37946e : 0x3f9a50); SDL_RenderDrawLine(R, cx - hw, cy + dy, cx + hw, cy + dy); } }
    if (!s->np) { draw_label(F_BOLD, C_SEC, s->loading ? "Loading the plaza..." : "Nobody is here yet.", X + it->w / 2, Y + it->h - 60, 1); break; }
    int fmask = s->filter == 1 ? 1 : s->filter == 2 ? 2 : s->filter == 3 ? 4 : 7;
    if (s->psel >= s->np) s->psel = 0;
    for (int i = 0; i < s->np; i++) {
      Person *q = &s->pp[i];
      if (!(q->tags & fmask) && fmask != 7) continue;
      /* walking */
      if (q->wait > 0) q->wait -= 1;
      else {
        float dx = q->tx - q->x, dy = q->ty - q->y, d = sqrtf(dx * dx + dy * dy);
        if (d < 2) { q->tx = 40 + (float)(rand() % (it->w - 80)); q->ty = 230 + (float)(rand() % (it->h - 290)); q->wait = (float)(60 + rand() % 180); }
        else { q->x += dx / d * 1.2f; q->y += dy / d * 1.2f; }
      }
      int px = X + (int)q->x, py = Y + (int)q->y;
      int bob = q->wait > 0 ? 0 : (int)(2 * sinf(SDL_GetTicks() / 90.0f + i));
      fillr(px - 13, py - 26 + bob, 26, 28, av_color(q->user));
      fillr(px - 11, py + 2, 8, 12, 0x3f3f74); fillr(px + 3, py + 2, 8, 12, 0x3f3f74);
      draw_avatar(q->name, q->user, px - 19, py - 66 + bob, 38);
      int sel = focus && i == s->psel;
      int tw = label_w(F_SMALL, q->name) + 14;
      fillr(px - tw / 2, py - 96 + bob, tw, 26, sel ? C_GREEN : C_WHITE);
      draw_label(F_SMALL, sel ? C_WHITE : 0x222222, q->name, px, py - 94 + bob, 1);
    }
    break; }
  case K_WIG: case K_WIGFULL: {
    int fullw = it->kind == K_WIGFULL;
    boxr(X, Y, it->w, it->h, C_WHITE, C_LINE, 1);
    int dx = X + (it->w - it->dw) / 2;
    draw_drawing(it, dx, Y + 8);
    int ty = Y + 8 + it->dh + 10;
    draw_tex(&it->tTitle, fullw ? dx : X + 12, ty);
    ty += it->tTitle.h + 4;
    snprintf(buf, sizeof buf, "by %s", it->name ? it->name : "?");
    draw_label(F_SMALL, C_SOFT, buf, fullw ? dx : X + 12, ty, 0);
    ty += 36;
    snprintf(buf, sizeof buf, "Yeah! %d", it->yeahs);
    int yw = label_w(F_BOLD, buf) + 36;
    draw_btn(fullw ? dx : X + 12, ty, yw, 40, buf, it->mine ? 3 : 2, 0);
    it->yeahR = (SDL_Rect){ (fullw ? dx : X + 12) - X, ty - Y, yw, 40 };
    break; }
  }
}

/* ------------------------------------------------------------------ chrome: header, footer */
static SDL_Texture *bgTex, *logoTex;
static void make_logo(void) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom((void *)LOGO_RGBA, LOGO_W, LOGO_H, 32, LOGO_W * 4, SDL_PIXELFORMAT_RGBA32);
  if (!s) return;
  logoTex = SDL_CreateTextureFromSurface(R, s);
  if (logoTex) { SDL_SetTextureBlendMode(logoTex, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(logoTex, SDL_ScaleModeLinear); }
  SDL_FreeSurface(s);
}
static void make_bg(void) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, SW, SH, 32, SDL_PIXELFORMAT_RGBA32);
  if (!s) return;
  for (int y = 0; y < SH; y++) {
    uint8_t *row = (uint8_t *)s->pixels + y * s->pitch;
    for (int x = 0; x < SW; x++) {
      float dx = (float)(x % 14) - 7.0f, dy = (float)(y % 14) - 7.0f;
      float d = sqrtf(dx * dx + dy * dy);
      float a = d < 1.2f ? 1.0f : d < 1.9f ? (1.9f - d) / 0.7f : 0.0f;
      int r = (int)(0xe8 + (0xd3 - 0xe8) * a), g = (int)(0xf3 + (0xe3 - 0xf3) * a), b = (int)(0xf8 + (0xea - 0xf8) * a);
      row[x * 4] = (uint8_t)r; row[x * 4 + 1] = (uint8_t)g; row[x * 4 + 2] = (uint8_t)b; row[x * 4 + 3] = 255;
    }
  }
  bgTex = SDL_CreateTextureFromSurface(R, s);
  SDL_FreeSurface(s);
}

typedef struct { int x, w; } TabR;
static TabR tabR[NTABS];
static SDL_Texture *iconTex[5];
static void make_icons(void) {
  for (int i = 0; i < 5; i++) {
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormatFrom((void *)ICONS_RGBA[i], ICON_SZ, ICON_SZ, 32, ICON_SZ * 4, SDL_PIXELFORMAT_RGBA32);
    if (!sf) continue;
    iconTex[i] = SDL_CreateTextureFromSurface(R, sf);
    if (iconTex[i]) SDL_SetTextureBlendMode(iconTex[i], SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(sf);
  }
}
/* same tabs as the website: Home, Communities, Plaza, Games, Profile / Sign in */
static void draw_header(void) {
  fillr(0, 0, SW, TOPH, C_WHITE);
  fillr(0, TOPH, SW, 3, C_TOPLINE);
  if (logoTex) { SDL_Rect d = { 40, (TOPH - LOGO_H) / 2, LOGO_W, LOGO_H }; SDL_RenderCopy(R, logoTex, NULL, &d); }
  else draw_label(F_LOGO, C_GREEN, "Aeroverse", 40, 12, 0);
  const char *names[NTABS] = { "Home", "Communities", "Plaza", "Games", "Sign in" };
#if STAGE >= 2
  if (me.id) names[4] = "Profile";
#endif
  int w = 132, x = SW - 30 - NTABS * w;
  for (int i = 0; i < NTABS; i++) {
    tabR[i] = (TabR){ x, w };
    int on = curTab == i;
    uint32_t c = on ? C_GREEN : 0x6b6b6b;
    if (iconTex[i]) { SDL_SetTextureColorMod(iconTex[i], (c >> 16) & 255, (c >> 8) & 255, c & 255); SDL_Rect d = { x + w / 2 - 16, 8, 32, 32 }; SDL_RenderCopy(R, iconTex[i], NULL, &d); }
    draw_label(F_SMALL, c, names[i], x + w / 2, 42, 1);
    if (on) fillr(x + 8, TOPH - 4, w - 16, 4, C_GREEN);
    x += w;
  }
}
static void draw_footer(const char *hint) {
  SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(R, 255, 255, 255, 225);
  SDL_Rect r = { 0, SH - FOOTH, SW, FOOTH };
  SDL_RenderFillRect(R, &r);
  fillr(0, SH - FOOTH, SW, 1, C_LINE);
  draw_label(F_SMALL, C_SOFT, hint, 24, SH - FOOTH + 5, 0);
  char ver[64]; snprintf(ver, sizeof ver, "Aeroverse for Wii U %s (build %d)", APP_VERSION, APP_BUILD);
  draw_label(F_SMALL, 0xa0a8ad, ver, SW - 24, SH - FOOTH + 5, 2);
}

/* ------------------------------------------------------------------ main */
static int touchDown, touchStartX, touchStartY, touchLastY, touchMoved, touchTapX = -1, touchTapY = -1, touchX, touchY;
static int touchFresh, swallowTouch;

static void ensure_visible(Screen *s) {
  if (s->sel < 0 || s->sel >= s->n) return;
  Item *it = &s->it[s->sel];
  int top = it->y - (int)s->scroll, bot = top + it->h;
  if (top < TOPH + 10) s->scroll -= (TOPH + 10 - top);
  else if (bot > SH - FOOTH - 10) { int need = bot - (SH - FOOTH - 10); int room = top - (TOPH + 10); s->scroll += need < room ? need : room; }
}
static void move_sel(Screen *s, int dir) {
  if (!s->n) return;
  int i = s->sel;
  for (int k = 0; k < s->n; k++) {
    i += dir;
    if (i < 0 || i >= s->n) return;
    if (s->it[i].sel_ok) { s->sel = i; ensure_visible(s); return; }
  }
}
static void move_sel_grid(Screen *s, int dir) {
  /* left/right in the wiggle grid */
  int i = s->sel + dir;
  if (i >= 0 && i < s->n && s->sel >= 0 && (s->it[i].kind == K_WIG || s->it[i].kind == K_DCELL) && s->it[i].kind == s->it[s->sel].kind) { s->sel = i; ensure_visible(s); }
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  SDL_SetMainReady();
  for (int i = 0; i < 32; i++) PAL[i] = (uint32_t)strtoul(PALHEX[i], NULL, 16);
  srand((unsigned)time(NULL));

  if (SDL_Init(SDL_INIT_VIDEO) < 0) return 1;
  WIN = SDL_CreateWindow("Aeroverse", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SW, SH, 0);
  R = SDL_CreateRenderer(WIN, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!R) R = SDL_CreateRenderer(WIN, -1, 0);
  SDL_RenderSetLogicalSize(R, SW, SH);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  TTF_Init();
  F_SMALL = TTF_OpenFontRW(SDL_RWFromConstMem(nunito_reg_ttf, (int)nunito_reg_ttf_size), 1, 19);
  F_BODY = TTF_OpenFontRW(SDL_RWFromConstMem(nunito_reg_ttf, (int)nunito_reg_ttf_size), 1, 23);
  F_BOLD = TTF_OpenFontRW(SDL_RWFromConstMem(nunito_bold_ttf, (int)nunito_bold_ttf_size), 1, 23);
  F_H = TTF_OpenFontRW(SDL_RWFromConstMem(nunito_bold_ttf, (int)nunito_bold_ttf_size), 1, 28);
  F_BIG = TTF_OpenFontRW(SDL_RWFromConstMem(nunito_bold_ttf, (int)nunito_bold_ttf_size), 1, 34);
  F_LOGO = TTF_OpenFontRW(SDL_RWFromConstMem(nunito_bold_ttf, (int)nunito_bold_ttf_size), 1, 40);
  if (!F_SMALL || !F_BODY || !F_BOLD || !F_H || !F_BIG || !F_LOGO) {
    /* fall back to the console's own font */
    void *fp = NULL; uint32_t fs = 0;
    if (OSGetSharedData(OS_SHAREDDATATYPE_FONT_STANDARD, 0, &fp, &fs) && fp) {
      if (!F_SMALL) F_SMALL = TTF_OpenFontRW(SDL_RWFromConstMem(fp, (int)fs), 1, 19);
      if (!F_BODY) F_BODY = TTF_OpenFontRW(SDL_RWFromConstMem(fp, (int)fs), 1, 23);
      if (!F_BOLD) F_BOLD = TTF_OpenFontRW(SDL_RWFromConstMem(fp, (int)fs), 1, 23);
      if (!F_H) F_H = TTF_OpenFontRW(SDL_RWFromConstMem(fp, (int)fs), 1, 28);
      if (!F_BIG) F_BIG = TTF_OpenFontRW(SDL_RWFromConstMem(fp, (int)fs), 1, 34);
      if (!F_LOGO) F_LOGO = TTF_OpenFontRW(SDL_RWFromConstMem(fp, (int)fs), 1, 40);
    }
  }
  make_bg();
  make_logo();
  make_icons();

  curl_global_init(CURL_GLOBAL_DEFAULT);
  netMx = SDL_CreateMutex(); netCv = SDL_CreateCond();
  SDL_Thread *nt = SDL_CreateThreadWithStackSize(net_thread, "net", 512 * 1024, NULL);

  load_token();
  if (sessionTok[0]) net_send("GET", "/api/me", NULL, me_cb, 0, 0); else meKnown = 1;

  roots[0] = new_screen(S_FEED);
  roots[1] = new_screen(S_COMMS);
  roots[2] = new_screen(S_PLAZA);
  roots[3] = new_screen(S_GAMES);
  go_tab(0);
  update_check();

  uint32_t holdT = 0; int lastMeId = -1;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) running = 0;
    if (!running) break;
    frameNo++;
    net_poll();

    { static int shownUpd; if (updateBuild > 0 && !shownUpd) { shownUpd = 1; if (roots[0]) load_screen(roots[0], 0); } }
    /* refresh pages when sign-in state changes (buttons depend on it) */
    if (meKnown && lastMeId != me.id) {
      if (lastMeId != -1) {
        if (curTab == 4) go_tab(4);
        else for (int i = 0; i < depth; i++) if (stack[i]->kind == S_COMM || stack[i]->kind == S_WIGGLE || stack[i]->kind == S_POST || stack[i]->kind == S_FEED || stack[i]->kind == S_USER) load_screen(stack[i], 0);
        if (roots[0] && roots[0] != cur()) clear_items(roots[0], 0);
      }
      lastMeId = me.id;
    }

    /* ---- input */
    VPADStatus st; VPADReadError verr = VPAD_READ_SUCCESS;
    memset(&st, 0, sizeof st);
    int got = VPADRead(VPAD_CHAN_0, &st, 1, &verr) > 0 && verr == VPAD_READ_SUCCESS;
    uint32_t trig = got ? st.trigger : 0, hold = got ? st.hold : 0;
    touchTapX = -1; touchFresh = 0;
    int tDown = 0;
    if (got) {
      VPADTouchData tp;
      VPADGetTPCalibratedPoint(VPAD_CHAN_0, &tp, &st.tpNormal);
      if (st.tpNormal.touched && tp.validity == VPAD_VALID) {
        tDown = 1;
        touchX = tp.x; touchY = tp.y;
        if (touchX < 0) touchX = 0; if (touchX >= SW) touchX = SW - 1; if (touchY < 0) touchY = 0; if (touchY >= SH) touchY = SH - 1;
      } else if (st.tpNormal.touched && touchDown) tDown = 1; /* keep last point on invalid samples */
    }
    if (tDown && !touchDown) { touchStartX = touchX; touchStartY = touchY; touchLastY = touchY; touchMoved = 0; touchFresh = 1; }
    if (!tDown && touchDown && touchMoved < 18) { touchTapX = touchStartX; touchTapY = touchStartY; }
#if STAGE >= 2
    { static int wasModal; int modal = ed.open || kb.open; if (wasModal && !modal) swallowTouch = 1; wasModal = modal; }
#endif
    if (swallowTouch) { touchTapX = -1; touchFresh = 0; if (!tDown) swallowTouch = 0; }
    /* key repeat for the D-Pad */
    uint32_t dirs = hold & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT);
    if (trig & dirs) holdT = SDL_GetTicks() + 380;
    else if (dirs && SDL_GetTicks() > holdT) { trig |= dirs; holdT = SDL_GetTicks() + 90; }

    Screen *s = cur();
    const char *hint = "Touch or D-Pad to choose   A: open   B: back   L/R: tabs   Y: refresh   HOME: exit";
#if STAGE >= 2
    if (ed.open) {
      ed_touch(touchX, touchY, tDown, touchFresh);
      ed_buttons(trig);
    } else if (kb.open) {
      kb_input(trig, touchTapX, touchTapY);
    } else
#endif
    if (s) {
      /* touch scrolling with momentum */
      if (tDown && touchDown) {
        int dy = touchY - touchLastY;
        if (abs(touchY - touchStartY) > 8 || abs(touchX - touchStartX) > 8) touchMoved += abs(dy) + 1;
        if (touchMoved >= 18 && touchStartY > TOPH) { s->scroll -= dy; s->vel = (float)-dy; }
        touchLastY = touchY;
      } else if (!tDown) { s->scroll += s->vel; s->vel *= 0.92f; if (fabsf(s->vel) < 0.3f) s->vel = 0; }
      float sy = got ? st.leftStick.y + st.rightStick.y : 0;
      if (fabsf(sy) > 0.15f) s->scroll -= sy * 22.0f;

      if (touchTapX >= 0) {
        if (touchTapY < TOPH) {
          for (int i = 0; i < NTABS; i++) if (touchTapX >= tabR[i].x && touchTapX < tabR[i].x + tabR[i].w) go_tab(i);
        } else {
          int cy = touchTapY + (int)s->scroll;
          for (int i = 0; i < s->n; i++) {
            Item *it = &s->it[i];
            if (inr(touchTapX, cy, it->x, it->y, it->w, it->h)) {
              if (it->sel_ok) s->sel = i;
              if (it->kind == K_POST || it->kind == K_FULLPOST || it->kind == K_REPLY) {
                int av = it->kind == K_REPLY ? 48 : 64;
                if (inr(touchTapX, cy, it->x + 16, it->y + 12, av, av)) { open_user(it->user); break; }
              }
              if (it->kind == K_PROFILE) break;
              if (it->kind == K_PLAZA) {
                int best = -1; float bd = 70 * 70;
                int fmask = s->filter == 1 ? 1 : s->filter == 2 ? 2 : s->filter == 3 ? 4 : 7;
                for (int k = 0; k < s->np; k++) {
                  if (fmask != 7 && !(s->pp[k].tags & fmask)) continue;
                  float dx = (float)(touchTapX - it->x) - s->pp[k].x, dy = (float)(cy - it->y) - (s->pp[k].y - 40);
                  if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = k; }
                }
                if (best >= 0) { s->psel = best; open_user(s->pp[best].user); }
                break;
              }
#if STAGE >= 2
              int onYeah = it->yeahR.w && inr(touchTapX - it->x, cy - it->y, it->yeahR.x, it->yeahR.y, it->yeahR.w, it->yeahR.h);
              if ((it->kind == K_POST || it->kind == K_WIG) && onYeah) { do_yeah(s, it); break; }
              if (it->kind == K_FULLPOST || it->kind == K_WIGFULL) { if (onYeah) do_yeah(s, it); break; }
#endif
              if (it->sel_ok || it->kind == K_INFO) open_item(s, it);
              break;
            }
          }
        }
      }
      s = cur(); /* may have changed */
      if (s) {
        if (trig & VPAD_BUTTON_UP) move_sel(s, -1);
        if (trig & VPAD_BUTTON_DOWN) move_sel(s, 1);
        if (trig & VPAD_BUTTON_LEFT) move_sel_grid(s, -1);
        if (trig & VPAD_BUTTON_RIGHT) move_sel_grid(s, 1);
        if (s->sel >= 0 && s->sel < s->n && s->it[s->sel].kind == K_PLAZA && s->np) {
          if (trig & VPAD_BUTTON_LEFT) s->psel = (s->psel + s->np - 1) % s->np;
          if (trig & VPAD_BUTTON_RIGHT) s->psel = (s->psel + 1) % s->np;
        }
        if ((trig & VPAD_BUTTON_A) && s->sel >= 0 && s->sel < s->n && s->it[s->sel].sel_ok) open_item(s, &s->it[s->sel]);
#if STAGE >= 2
        if ((trig & VPAD_BUTTON_X) && s->sel >= 0 && s->sel < s->n && (s->it[s->sel].kind == K_POST || s->it[s->sel].kind == K_WIG)) do_yeah(s, &s->it[s->sel]);
        if ((trig & VPAD_BUTTON_PLUS) && (s->kind == S_COMPOSE || s->kind == S_LOGIN)) compose_send(s);
#endif
        if (trig & VPAD_BUTTON_B) { if (depth > 1) pop(); }
        if (trig & (VPAD_BUTTON_R | VPAD_BUTTON_ZR)) { if (curTab < NTABS - 1) go_tab(curTab + 1); }
        if (trig & (VPAD_BUTTON_L | VPAD_BUTTON_ZL)) { if (curTab > 0) go_tab(curTab - 1); }
        s = cur();
        if ((trig & VPAD_BUTTON_Y) && s && s->kind != S_COMPOSE && s->kind != S_LOGIN && s->kind != S_WENTRY) load_screen(s, 0);
        if (s && (s->kind == S_COMPOSE || s->kind == S_LOGIN)) hint = "Tap a box to type or draw   +: send   B: back";
        else if (s && (s->kind == S_POST || s->kind == S_WENTRY)) hint = "A on the post: Yeah!   B: back   Y: refresh   L/R: tabs";
      }
    }
    touchDown = tDown;

    /* ---- draw */
    s = cur();
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_NONE);
    if (bgTex) SDL_RenderCopy(R, bgTex, NULL, NULL); else fillr(0, 0, SW, SH, C_BG);
#if STAGE >= 2
    if (ed.open) { ed_draw(); goto overlays; }
#endif
    if (s) {
      layout(s);
      float maxS = (float)(s->contentH - SH);
      if (maxS < 0) maxS = 0;
      if (s->scroll > maxS) { s->scroll = maxS; s->vel = 0; }
      if (s->scroll < 0) { s->scroll = 0; s->vel = 0; }
      if (s->sel >= s->n) s->sel = s->n - 1;
      if (s->sel >= 0 && s->sel < s->n && !s->it[s->sel].sel_ok) { int k = s->sel; while (k < s->n && !s->it[k].sel_ok) k++; s->sel = k < s->n ? k : s->sel; }
      int off = (int)s->scroll;
      SDL_Rect clip = { 0, TOPH + 3, SW, SH - TOPH - 3 };
      SDL_RenderSetClipRect(R, &clip);
      for (int i = 0; i < s->n; i++) {
        Item *it = &s->it[i];
        int Y = it->y - off;
        if (Y + it->h < TOPH || Y > SH) continue;
        draw_item(s, it, it->x, Y, i == s->sel && it->sel_ok);
      }
      int endY = s->contentH - 70 - off;
      if (s->loading) draw_label(F_BOLD, C_SEC, "Loading...", SW / 2, endY + 6, 1);
      if (s->err[0]) {
        boxr(CX, endY, CW, 64, 0xffe8e8, 0xd99, 1);
        char eb[260]; snprintf(eb, sizeof eb, "%s   (press Y to try again)", s->err);
        draw_label(F_SMALL, C_RED, eb, CX + 16, endY + 20, 0);
      }
      SDL_RenderSetClipRect(R, NULL);
    }
    draw_header();
    draw_footer(hint);
#if STAGE >= 2
    if (kb.open) kb_draw();
  overlays:
#endif
    if (toastMsg[0] && SDL_GetTicks() < toastUntil) {
      int w = label_w(F_BOLD, toastMsg) + 48;
      fillr(SW / 2 - w / 2, SH - 120, w, 56, C_SEC);
      draw_label(F_BOLD, C_WHITE, toastMsg, SW / 2, SH - 120 + 14, 1);
    }
    if (net_pending()) { int k = (int)(SDL_GetTicks() / 120) % 8; for (int i = 0; i < 8; i++) fillr(SW - 30 - (7 - i) * 10, TOPH + 10, 6, 6, i == k ? C_GREEN : 0xc8dfe8); }
#ifdef HOST_TEST
    { extern SDL_Renderer *hs_R; extern void hs_flush(void); hs_R = R; hs_flush(); }
#endif
    SDL_RenderPresent(R);
  }

  /* ---- shutdown */
  SDL_LockMutex(netMx); netQuit = 1; SDL_CondSignal(netCv); SDL_UnlockMutex(netMx);
  if (nt) SDL_WaitThread(nt, NULL);
  curl_global_cleanup();
  TTF_Quit();
  SDL_DestroyRenderer(R);
  SDL_DestroyWindow(WIN);
  SDL_Quit();
  return 0;
}
