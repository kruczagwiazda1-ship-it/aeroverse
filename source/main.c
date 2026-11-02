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
#define APP_VERSION "2.1"
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
#define APP_FILE2 "fs:/vol/external01/wiiu/apps/aeroverse_update.wuhb"
#include <sys/stat.h>
#include <unistd.h>

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
#include "postfonts_bin.h"
#include "logo_img.h"
#include "icons_img.h"
#include "body_img.h"

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
    C_RED = 0xe5322d, C_WHITE = 0xffffff, C_SEC2 = 0x145d86, C_ORANGE = 0xf09a1a;

static const char *PALHEX[32] = { "ffffff","c2c3c7","83769c","5f574f","1d2b53","000000","7e2553","ab5236","ff004d","ff77a8","ffccaa","ffa300","ffec27","00e436","008751","29adff",
  "065ab5","4b2d7f","ff6e59","f0b27a","a8e6cf","2ab7ca","d291bc","8d5524","3f3f74","639bff","99e550","37946e","fbf236","d95763","663931","222034" };
static uint32_t PAL[32];
static const int AV_IDX[8] = { 8, 11, 13, 15, 17, 21, 22, 26 };

/* ------------------------------------------------------------------ basics */
static SDL_Renderer *R;
static SDL_Window *WIN;
static TTF_Font *F_SMALL, *F_BODY, *F_BOLD, *F_H, *F_LOGO, *F_BIG, *F_NAME, *F_TITLE, *F_HUGE, *F_BODYS, *F_BADGE, *F_TINY;
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

static void draw_btn(int x, int y, int w, int h, const char *txt, int style, int focus);
static void card(int x, int y, int w, int h);

/* ------------------------------------------------------------------ text helpers */
static long utc_now(void);
static int img_host_ok(const char *u);
static void parse_date_header(const char *h);
static void fmt_ago(double ms, char *out, size_t n) {
  long now = utc_now();
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
  if (*p == '~') { p++; wig = 2; if (*p >= '1' && *p <= '9') { wig = *p - '0'; p++; } }   /* same as the website: '~' alone = 2 */
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

/* website wigFrames(): 4 frames of wavy displacement, amp = strength * 1.1; frame < 0 = still picture */
static void wig_pixels(const uint8_t *px, const uint32_t *pal, float amp, float phX, float phY, uint8_t *dst, int pitch) {
  int ox[DH], oy[DW];
  for (int y = 0; y < DH; y++) ox[y] = (int)lroundf(amp * sinf(y / 6.0f + phX));
  for (int x = 0; x < DW; x++) oy[x] = (int)lroundf(amp * sinf(x / 7.0f + phY));
  for (int y = 0; y < DH; y++) {
    uint8_t *row = dst + y * pitch;
    for (int x = 0; x < DW; x++) {
      int sx = x + ox[y], sy = y + oy[x];
      if (sx < 0) sx = 0; if (sx >= DW) sx = DW - 1; if (sy < 0) sy = 0; if (sy >= DH) sy = DH - 1;
      uint32_t c = pal[px[sy * DW + sx] & 31];
      row[x * 4] = (c >> 16) & 255; row[x * 4 + 1] = (c >> 8) & 255; row[x * 4 + 2] = c & 255; row[x * 4 + 3] = 255;
    }
  }
}
static SDL_Texture *drawing_tex(const uint8_t *px, const uint32_t *pal, int wig, int frame) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, DW, DH, 32, SDL_PIXELFORMAT_RGBA32);
  if (!s) return NULL;
  if (frame >= 0) wig_pixels(px, pal, (wig ? wig : 2) * 1.1f, frame * 1.7f, frame * 2.2f, s->pixels, s->pitch);
  else wig_pixels(px, pal, 0, 0, 0, s->pixels, s->pitch);
  SDL_Texture *t = SDL_CreateTextureFromSurface(R, s);
  SDL_FreeSurface(s);
  return t;
}

#include "aero.h"
#include "plaza3d.h"
#include "rich.h"

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
  if (n > 10 && !strncasecmp(d, "date:", 5)) { char tmp[80]; size_t l = n < 79 ? n : 79; memcpy(tmp, d, l); tmp[l] = 0; parse_date_header(tmp); }
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

#include "images.h"
#include "audio.h"

/* error text from a JSON {"error": "..."} body */
static void api_error(long code, const char *body, const char *err, char *out, size_t n) {
  if (err && *err) { snprintf(out, n, "No connection: %s", err); return; }
  cJSON *j = body ? cJSON_Parse(body) : NULL;
  cJSON *e = j ? cJSON_GetObjectItem(j, "error") : NULL;
  if (cJSON_IsString(e)) snprintf(out, n, "%s", e->valuestring); else snprintf(out, n, "Server error (%ld)", code);
  cJSON_Delete(j);
}

/* ------------------------------------------------------------------ session / me */
typedef struct { int id; char username[24]; char display[48]; int admin; char avatar[64]; } Me;
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
/* after an update there can be two copies (aeroverse.wuhb and aeroverse_update.wuhb): delete the older one.
   The copy that is running is locked, so it can never be deleted by mistake. */
static void update_cleanup(void) {
  remove(APP_FILE ".part");
  struct stat a, b;
  if (stat(APP_FILE, &a) == 0 && stat(APP_FILE2, &b) == 0) {
    if (a.st_mtime <= b.st_mtime) remove(APP_FILE); else remove(APP_FILE2);
  }
}
static void update_check(void) {
  update_cleanup();
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
  /* the running app file is locked by the system, so the update goes into the other of two file names;
     on the next start the older copy is removed (see update_cleanup) */
  remove(APP_FILE2 ".tmp");
  if (remove(APP_FILE) == 0 && rename(APP_FILE ".part", APP_FILE) == 0) { updateBuild = -1; toast("Updated! Press HOME, close Aeroverse and start it again."); return; }
  if (remove(APP_FILE2) == 0 || access(APP_FILE2, F_OK) != 0) {
    if (rename(APP_FILE ".part", APP_FILE2) == 0) { updateBuild = -1; toast("Updated! Close Aeroverse (HOME), restart the console and open the new Aeroverse icon."); return; }
  }
  /* we are running from the second name: put the update back into the first one */
  remove(APP_FILE2 ".part");
  toast("Update failed: cannot replace the app file. Copy it from GitHub by hand this time.");
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
    if ((v = cJSON_GetObjectItem(u, "avatar")) && cJSON_IsString(v)) snprintf(me.avatar, sizeof me.avatar, "%s", v->valuestring);
  } else if (sessionTok[0]) forget_token();
  meKnown = 1;
  cJSON_Delete(j);
}

/* ------------------------------------------------------------------ list items */
enum { K_POST, K_FULLPOST, K_REPLY, K_COMM, K_HEAD, K_MORE, K_ACTION, K_INFO, K_FIELD, K_DRAWF, K_WIG, K_WIGFULL, K_SEC, K_DCELL, K_PROFILE, K_PLAZA, K_GAME,
       K_HERO, K_PROMPT, K_BANNER, K_CHEAD, K_CNEW, K_FEAT, K_EMPTY, K_SEP, K_CHIPS, K_LTABS };
enum { A_NONE, A_WRITE_POST, A_WRITE_REPLY, A_SIGNIN, A_SIGNOUT, A_SEND, A_DRAW, A_MODE, A_WIG_DRAW, A_RETRY, A_DRAW_REPLY, A_PLAZA_FILTER, A_UPDATE };

typedef struct Item {
  int kind, act, id, yeahs, replies, mine, posts, sel_ok, icon;
  double created, lastPost;
  char *name, *user, *cname, *cid, *body, *drawing, *title, *style, *msg, *age, *avatar;
  char *pics[4]; int npics, picH[4];
  Tex tName, tBody, tTitle;
  SDL_Texture *rich; int rw, rh, richH, richClip;   /* rendered post body (4px margin around) */
  SDL_Texture *dr[4]; int ndr, wig;
  SDL_Texture *bgTex;                                /* one-off background (hero, banner) */
  int x, y, w, h, clip, dw, dh;
  SDL_Rect yeahR, btnR, avR;
} Item;

static void item_free(Item *it) {
  free(it->name); free(it->user); free(it->cname); free(it->cid); free(it->body); free(it->drawing); free(it->title); free(it->style); free(it->msg); free(it->age); free(it->avatar);
  for (int i = 0; i < it->npics; i++) free(it->pics[i]);
  tex_free(&it->tName); tex_free(&it->tBody); tex_free(&it->tTitle);
  if (it->rich) SDL_DestroyTexture(it->rich);
  if (it->bgTex) SDL_DestroyTexture(it->bgTex);
  for (int i = 0; i < 4; i++) if (it->dr[i]) SDL_DestroyTexture(it->dr[i]);
  memset(it, 0, sizeof *it);
}

static void item_drawing(Item *it, const char *code) {
  if (!code || !*code) return;
  uint8_t *px = malloc(DPIX); uint32_t pal[32];
  int wig = decode_drawing(code, px, pal);
  if (wig >= 0) {
    it->wig = wig;
    it->ndr = wig ? 4 : 1;
    for (int f = 0; f < it->ndr; f++) it->dr[f] = drawing_tex(px, pal, wig, wig ? f : -1);
  }
  free(px);
}

/* ------------------------------------------------------------------ screens */
enum { S_FEED, S_COMMS, S_COMM, S_POST, S_WIGGLE, S_WENTRY, S_COMPOSE, S_LOGIN, S_PLAZA, S_GAMES, S_USER, S_STUB };
typedef struct { char *name, *user, *avatar; int tags, shirt, tex, faceDone; Walker w; } Person;
typedef struct Screen {
  int kind, serial;
  Item *it; int n, cap;
  Item *side; int nside, capside;       /* right column (like the website's sidebar) */
  float scroll, vel; int contentH;
  int sel;
  int loading; char err[200];
  int more, lastId, page;
  char cid[48], cname[96];
  int postId, mode, ckind;   /* ckind for compose: 0 post, 1 reply, 2 wiggle */
  char text[1001], text2[64], pass[104];
  uint8_t *draw; int drawWig;
  Item *parent;
  Person *pp; int np, filter, psel;
  int needPost;
  /* 3D plaza view */
  float az, polar, dist; int autoRot;
} Screen;

#define MAXS 10
static Screen *stack[MAXS]; static int depth;
#define NTABS 5
static Screen *roots[NTABS];      /* home, communities, plaza, games, profile */
static int curTab;
static int serialCounter = 1;

/* page geometry (website layout scaled for the TV): main column + sidebar, or one centred column */
#define MAINX 36
#define MAINW 836
#define SIDEX 892
#define SIDEW 352
#define FULLX 36
#define FULLW 1208
static int has_side(const Screen *s) { return s && (s->kind == S_FEED || s->kind == S_COMMS || s->kind == S_COMM); }
static int colx(const Screen *s) { return s && s->kind == S_LOGIN ? (SW - 620) / 2 : has_side(s) ? MAINX : CX; }
static int colw(const Screen *s) { return s && s->kind == S_LOGIN ? 620 : has_side(s) ? MAINW : CW; }

static Screen *cur(void) { return depth ? stack[depth - 1] : NULL; }
static Screen *by_serial(int s) { for (int i = 0; i < depth; i++) if (stack[i]->serial == s) return stack[i]; for (int i = 0; i < NTABS; i++) if (roots[i] && roots[i]->serial == s) return roots[i]; return NULL; }
static Item *new_item(Item **arr, int *n, int *cap, int kind) {
  if (*n == *cap) { *cap = *cap ? *cap * 2 : 32; *arr = realloc(*arr, sizeof(Item) * (size_t)*cap); }
  Item *it = &(*arr)[(*n)++];
  memset(it, 0, sizeof *it);
  it->kind = kind;
  it->sel_ok = !(kind == K_HEAD || kind == K_INFO || kind == K_REPLY || kind == K_SEC || kind == K_PROFILE || kind == K_EMPTY || kind == K_SEP || kind == K_CHEAD);
  return it;
}
static Item *add_item(Screen *s, int kind) { Item *it = new_item(&s->it, &s->n, &s->cap, kind); it->w = kind == K_HERO ? FULLW : colw(s); return it; }
static Item *add_side(Screen *s, int kind) { Item *it = new_item(&s->side, &s->nside, &s->capside, kind); it->w = SIDEW; it->sel_ok = 0; return it; }
static void clear_items(Screen *s, int from) { for (int i = from; i < s->n; i++) item_free(&s->it[i]); if (from < s->n) s->n = from; }
static void clear_side(Screen *s) { for (int i = 0; i < s->nside; i++) item_free(&s->side[i]); s->nside = 0; }
static Screen *new_screen(int kind) { Screen *s = calloc(1, sizeof(Screen)); s->kind = kind; s->serial = serialCounter++; s->az = 0.6f; s->polar = 1.0f; s->dist = 64; s->autoRot = 1; return s; }
static void free_people(Screen *s) { for (int i = 0; i < s->np; i++) { free(s->pp[i].name); free(s->pp[i].user); free(s->pp[i].avatar); } free(s->pp); s->pp = NULL; s->np = 0; }
static void free_screen(Screen *s) { if (!s) return; clear_items(s, 0); clear_side(s); free(s->it); free(s->side); free(s->draw); free_people(s); if (s->parent) { item_free(s->parent); free(s->parent); } free(s); }
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
  it->lastPost = jnum(a, "last_post_at");
  const char *disp = jstr(a, "display"), *un = jstr(a, "username");
  it->name = xstrdup(disp && *disp ? disp : un);
  it->user = xstrdup(un);
  it->cname = xstrdup(cJSON_IsObject(c) ? jstr(c, "name") : NULL);
  it->cid = xstrdup(cJSON_IsObject(c) ? jstr(c, "id") : NULL);
  it->body = xstrdup(jstr(p, "body"));
  it->drawing = xstrdup(jstr(p, "drawing"));
  it->style = xstrdup(jstr(p, "style"));
  it->avatar = xstrdup(jstr(a, "avatar"));
  /* [img=https://...] pictures from allowed hosts are shown under the text */
  const char *b = it->body;
  while (b && (b = strstr(b, "[img=")) && it->npics < 4) {
    const char *e = strchr(b, ']'); if (!e) break;
    size_t L = (size_t)(e - b - 5);
    if (L < 190) { char u[200]; memcpy(u, b + 5, L); u[L] = 0; if (img_host_ok(u)) { it->picH[it->npics] = 240; it->pics[it->npics++] = xstrdup(u); } }
    b = e + 1;
  }
}

/* post card geometry (website .post scaled x1.4) */
#define PC_PAD 17
#define PC_AV 66
#define PC_BUBX (PC_PAD + 73 + 17)
#define BUB_PX 17
#define BUB_PY 14

static void build_rich(Item *it, int width, int px, int maxH) {
  if (it->rich) { SDL_DestroyTexture(it->rich); it->rich = NULL; }
  it->richH = 0;
  if (!it->body || !*it->body) return;
  int clipped = 0;
  it->rich = rich_render(it->body, it->style, width, px, C_INK, maxH, &it->rw, &it->rh, &clipped);
  it->richH = it->rh - 8; it->richClip = clipped;
}

/* creates textures + height */
static void build_item(Item *it) {
  int full = it->kind == K_FULLPOST, rep = it->kind == K_REPLY;
  if (it->kind == K_POST || full) {
    int bubW = it->w - PC_BUBX - PC_PAD, inner = bubW - 2 * BUB_PX;
    it->tName = mk_text(F_NAME, it->name, C_INK, 0);
    build_rich(it, inner, S14(16), full ? 0 : S14(16) * 1.45f * 9);
    item_drawing(it, it->drawing);
    if (it->ndr) { it->dw = full ? 640 : 480; it->dh = full ? 360 : 270; if (it->dw > inner) { it->dw = 320; it->dh = 180; } }
    int ph = 0; for (int k = 0; k < it->npics; k++) ph += 10 + it->picH[k];
    int bh = BUB_PY + 30 + 4 + 25 + (it->rich ? 8 + it->richH : 0) + ph + (it->ndr ? 10 + it->dh : 0) + 14 + 44 + BUB_PY;
    it->h = PC_PAD + (bh > PC_AV + 4 ? bh : PC_AV + 4) + PC_PAD;
  } else if (rep) {
    int inner = it->w - 14 - 53 - 14 - 14;
    it->tName = mk_text(F_NAME, it->name, C_INK, 0);
    build_rich(it, inner, S14(15), 0);
    item_drawing(it, it->drawing);
    if (it->ndr) { it->dw = 448; it->dh = 252; if (it->dw > inner) { it->dw = 320; it->dh = 180; } }
    int bh = 11 + 30 + (it->rich ? 4 + it->richH : 0) + (it->ndr ? 8 + it->dh : 0) + 11;
    it->h = bh > 53 + 22 ? bh : 53 + 22;
  } else if (it->kind == K_COMM) {
    it->tName = mk_text(F_NAME, it->name, C_INK, it->w - 200);
    it->h = 84;
  } else if (it->kind == K_BANNER) {
    it->tName = mk_text(F_HUGE, it->name, C_WHITE, it->w - 40);
    it->h = S14(190) + 84;
  } else if (it->kind == K_CHEAD) {
    it->tName = mk_text(F_TITLE, it->name, C_INK, it->w - 160);
    it->tBody = mk_text(F_SMALL, it->body, C_SOFT, it->w - 160);
    int top = 20 + it->tName.h + 6 + 25 + 6 + (it->tBody.t ? it->tBody.h : 0) + 20;
    if (top < 20 + 100 + 20) top = 140;
    it->tTitle = mk_text(F_BODYS, it->msg, C_INK, it->w - 44);
    it->h = top + (it->msg && *it->msg ? 1 + 16 + 22 + 6 + it->tTitle.h + 18 : 0);
  } else if (it->kind == K_HEAD) {
    it->tName = mk_text(F_BIG, it->name, C_SEC2, it->w - 44);
    char *b = strip_markup(it->body, 2000);
    it->tBody = mk_text(F_BODYS, b, C_INK, it->w - 44);
    free(b);
    it->h = 22 + it->tName.h + (it->tBody.t ? 8 + it->tBody.h : 0) + 22;
  } else if (it->kind == K_INFO) {
    it->tBody = mk_text(F_BODYS, it->body, C_SOFT, it->w - 40);
    it->h = (it->tBody.h ? it->tBody.h : 30) + 24;
  } else if (it->kind == K_EMPTY) {
    it->tBody = mk_text(F_SMALL, it->body, C_SOFT, it->w - 44);
    it->h = S14(36) * 2 + it->tBody.h;
  } else if (it->kind == K_SEP) {
    it->h = 18;
  } else if (it->kind == K_MORE || it->kind == K_ACTION) {
    it->h = 64;
  } else if (it->kind == K_PROMPT) {
    it->tBody = mk_text(F_BODYS, it->body, C_INK, it->w - 240);
    it->h = it->act == 1 ? 156 : 82;
  } else if (it->kind == K_CNEW) {
    it->h = 84;
  } else if (it->kind == K_FEAT) {
    item_drawing(it, it->drawing);
    it->dw = it->w - 28; it->dh = it->dw * 180 / 320;
    it->h = 14 + it->dh + 10 + 24 + 12;
  } else if (it->kind == K_FIELD) {
    it->tBody = mk_text(F_BODYS, it->body && *it->body ? it->body : "", C_INK, it->w - 64);
    int th = it->tBody.h > 34 ? it->tBody.h : 34;
    if (th > 300) th = 300;
    it->h = 18 + 30 + 8 + th + 28 + 18;
  } else if (it->kind == K_DRAWF) {
    item_drawing(it, it->drawing);
    it->dw = 480; it->dh = 270;
    it->h = 18 + 30 + 8 + it->dh + 18;
  } else if (it->kind == K_WIG || it->kind == K_WIGFULL) {
    int fullw = it->kind == K_WIGFULL;
    item_drawing(it, it->drawing);
    it->dw = fullw ? 640 : 320; it->dh = fullw ? 360 : 180;
    it->tTitle = mk_text(fullw ? F_BIG : F_NAME, it->title, C_INK, it->w - 28);
    if (!fullw && it->tTitle.w > it->w - 28) { tex_free(&it->tTitle); char t[64]; snprintf(t, sizeof t, "%.22s...", it->title ? it->title : ""); it->tTitle = mk_text(F_NAME, t, C_INK, 0); }
    it->h = 14 + it->dh + 10 + it->tTitle.h + 4 + 26 + 10 + 44 + 14;
  } else if (it->kind == K_SEC) {
    it->tName = mk_text(F_H, it->title, C_SEC2, it->w - 40);
    it->h = it->tName.h + 6;
  } else if (it->kind == K_DCELL) {
    item_drawing(it, it->drawing);
    it->dw = (it->w - 24); it->dh = it->dw * 180 / 320;
    it->h = 12 + it->dh + 10 + 30 + 10;
  } else if (it->kind == K_PROFILE) {
    it->tName = mk_text(F_TITLE, it->name, C_INK, it->w - 300);
    char *b = strip_markup(it->body, 1500);
    it->tBody = mk_text(F_BODYS, b, C_INK, it->w - 300);
    free(b);
    int th = it->tName.h + 4 + 28 + (it->tBody.t ? 10 + it->tBody.h : 0) + 8 + 26;
    it->h = 24 + (th > 230 ? th : 230) + 24;
  } else if (it->kind == K_CHIPS) {
    it->h = 48;
  } else if (it->kind == K_LTABS) {
    it->h = 62;
  } else if (it->kind == K_PLAZA) {
    it->h = 560;
  } else if (it->kind == K_HERO) {
    it->h = 540;
  } else if (it->kind == K_GAME) {
    it->tName = mk_text(F_NAME, it->title, C_INK, it->w - 280);
    it->tBody = mk_text(F_SMALL, it->body, C_SOFT, it->w - 280);
    int th = 18 + it->tName.h + 8 + it->tBody.h + 10 + 40 + 18;
    it->h = th > 176 ? th : 176;
  }
}

/* ------------------------------------------------------------------ loading */
static void load_screen(Screen *s, int append);
static void people_cb(int serial, long code, const char *body, const char *err, int extra);
static int heroLoaded;
static void plaza_setup_people(Screen *s);

static void add_empty(Screen *s, const char *txt) { Item *it = add_item(s, K_EMPTY); it->body = xstrdup(txt); build_item(it); }
static void add_sec(Screen *s, const char *title, int icon) { Item *h = add_item(s, K_SEC); h->title = xstrdup(title); h->icon = icon; build_item(h); }
static void add_cnew(Item *it) { it->title = xstrdup("Start a community"); it->body = xstrdup("Send in your idea for approval"); build_item(it); }

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
    fill_post(it, p);
    build_item(it);
    s->lastId = it->id;
    cnt++;
  }
  if (append != 2 && !s->lastId && cnt == 0) add_empty(s, s->kind == S_FEED ? "Nobody has posted yet." : "No posts here yet. Be the first!");
  if (cnt >= 15) add_item(s, K_MORE);
  for (int i = 0; i < s->n; i++) if (s->it[i].kind == K_MORE) build_item(&s->it[i]);
  cJSON_Delete(j);
}

static void drawings_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)err; (void)extra;
  Screen *s = by_serial(serial);
  if (!s || code != 200 || !body) return;
  cJSON *j = cJSON_Parse(body), *d, *arr = j ? cJSON_GetObjectItem(j, "drawings") : NULL;
  /* sidebar: "Featured Drawings" then the start-a-community box, like the website */
  clear_side(s);
  Item *h = add_side(s, K_SEC); h->title = xstrdup("Featured Drawings"); h->icon = 2; build_item(h);
  int n = 0;
  cJSON_ArrayForEach(d, arr) {
    if (n >= 3) break;
    Item *it = add_side(s, K_FEAT);
    cJSON *a = cJSON_GetObjectItem(d, "author"), *c = cJSON_GetObjectItem(d, "community");
    it->id = (int)jnum(d, "id"); it->drawing = xstrdup(jstr(d, "drawing"));
    const char *disp = jstr(a, "display"), *un = jstr(a, "username");
    it->name = xstrdup(disp && *disp ? disp : un); it->user = xstrdup(un);
    it->cname = xstrdup(cJSON_IsObject(c) ? jstr(c, "name") : NULL);
    it->avatar = xstrdup(jstr(a, "avatar"));
    build_item(it);
    it->sel_ok = 0;
    n++;
  }
  add_cnew(add_side(s, K_CNEW));
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
  int n = 0;
  cJSON_ArrayForEach(c, arr) {
    const char *age = jstr(c, "age");
    if (age && strcmp(age, "all")) continue; /* only all-ages communities, like the lite site */
    if (n == 1) add_sec(s, "Communities", 0);
    Item *it = add_item(s, n == 0 ? K_BANNER : K_COMM);
    it->cid = xstrdup(jstr(c, "id"));
    it->name = xstrdup(jstr(c, "name"));
    it->body = xstrdup(jstr(c, "description"));
    it->msg = xstrdup(jstr(c, "message"));
    it->avatar = xstrdup(jstr(c, "thumb"));
    it->age = xstrdup(age ? age : "all");
    it->posts = (int)jnum(c, "posts");
    build_item(it);
    n++;
  }
  if (n == 1) add_sec(s, "Communities", 0);
  Item *cn = add_item(s, K_CNEW); cn->w = 480; cn->title = xstrdup("Create a community"); cn->body = xstrdup("Needs approval from the admin"); build_item(cn);
  cJSON_Delete(j);
}

static void replies_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)extra;
  Screen *s = by_serial(serial);
  if (!s) return;
  s->loading = 0;
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); return; }
  s->err[0] = 0;
  clear_items(s, 1);   /* keep the post (item 0) */
  cJSON *j = cJSON_Parse(body);
  cJSON *arr = j ? cJSON_GetObjectItem(j, "replies") : NULL, *r;
  int n = cJSON_GetArraySize(arr);
  int iw = CW - PC_BUBX - PC_PAD;
  if (n) { Item *sp = add_item(s, K_SEP); sp->w = iw; build_item(sp); }
  cJSON_ArrayForEach(r, arr) {
    Item *it = add_item(s, K_REPLY);
    it->w = iw;
    fill_post(it, r);
    build_item(it);
  }
#if STAGE >= 2
  Item *a = add_item(s, K_ACTION); a->w = iw; a->act = me.id ? A_WRITE_REPLY : A_SIGNIN;
  a->title = xstrdup(me.id ? "Write a reply" : "Sign in to reply");
  build_item(a);
#endif
  if (s->n && s->it[0].kind == K_FULLPOST) s->it[0].replies = n;
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
    it->w = (CW - 24) / 3;
    cJSON *a = cJSON_GetObjectItem(e, "author");
    it->id = (int)jnum(e, "id"); it->yeahs = (int)jnum(e, "yeahs"); it->mine = jbool(e, "mine"); it->created = jnum(e, "created_at");
    it->title = xstrdup(jstr(e, "title")); it->drawing = xstrdup(jstr(e, "drawing"));
    const char *disp = jstr(a, "display"), *un = jstr(a, "username");
    it->name = xstrdup(disp && *disp ? disp : un); it->user = xstrdup(un);
    build_item(it);
    cnt++;
  }
  if (!append && !cnt) add_empty(s, "No drawings yet. Be the first!");
  if (jbool(j, "more")) { Item *m = add_item(s, K_MORE); build_item(m); }
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
  cJSON_ArrayForEach(p, arr) { Item *it = add_item(s, K_POST); fill_post(it, p); build_item(it); n++; }
  if (!n) add_empty(s, "No liked posts this week yet. Give a Yeah! to something you like.");
  add_sec(s, "Drawings of the week", 2);
  arr = j ? cJSON_GetObjectItem(j, "drawings") : NULL; n = 0;
  cJSON_ArrayForEach(p, arr) { Item *it = add_item(s, K_DCELL); it->w = (MAINW - 24) / 3; fill_post(it, p); build_item(it); n++; }
  if (!n) add_empty(s, "No liked drawings this week yet.");
  add_sec(s, "Latest posts", 0);
  cJSON_Delete(j);
}

static void user_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)extra;
  Screen *s = by_serial(serial);
  if (!s) return;
  if (code != 200 || !body) { s->loading = 0; api_error(code, body, err, s->err, sizeof s->err); return; }
  cJSON *j = cJSON_Parse(body);
  cJSON *u = j ? cJSON_GetObjectItem(j, "user") : NULL;
  Item *it = add_item(s, K_PROFILE);
  const char *disp = jstr(u, "display");
  it->name = xstrdup(disp && *disp ? disp : jstr(u, "username"));
  it->user = xstrdup(jstr(u, "username"));
  it->body = xstrdup(jstr(u, "bio"));
  it->posts = (int)jnum(u, "posts");
  it->created = jnum(u, "created_at");
  it->avatar = xstrdup(jstr(u, "avatar"));
  it->icon = (int)jnum(u, "shirt");
  build_item(it);
#if STAGE >= 2
  if (me.id && it->user && !strcmp(it->user, me.username)) {
    Item *w = add_item(s, K_PROMPT); w->act = 1; w->body = xstrdup("Write something on your page..."); w->title = xstrdup("Write"); build_item(w);
    Item *a = add_item(s, K_ACTION); a->act = A_SIGNOUT; a->title = xstrdup("Sign out"); build_item(a);
  }
#endif
  add_sec(s, "Posts", 0);
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
    snprintf(s->cid, sizeof s->cid, "%s", f->cid ? f->cid : "");
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
    if (s->np >= 60) break;
    Person *q = &s->pp[s->np++];
    const char *disp = jstr(p, "display");
    q->name = xstrdup(disp && *disp ? disp : jstr(p, "username"));
    q->user = xstrdup(jstr(p, "username"));
    q->shirt = (int)jnum(p, "shirt");
    q->avatar = xstrdup(jstr(p, "avatar"));
    cJSON *t, *tags = cJSON_GetObjectItem(p, "tags");
    cJSON_ArrayForEach(t, tags) if (cJSON_IsString(t)) {
      if (!strcmp(t->valuestring, "popular")) q->tags |= 1;
      if (!strcmp(t->valuestring, "new")) q->tags |= 2;
      if (!strcmp(t->valuestring, "active")) q->tags |= 4;
    }
  }
  plaza_setup_people(s);
  cJSON_Delete(j);
}

static void add_header_items(Screen *s) {
  if (s->kind == S_FEED) {
    Item *hero = add_item(s, K_HERO); build_item(hero); hero->sel_ok = 0;
    if (!heroLoaded) { heroLoaded = 1; net_send("GET", "/api/people", NULL, people_cb, 0, 0); }
    if (updateBuild > 0) {
      Item *u = add_item(s, K_ACTION); u->act = A_UPDATE;
      char t[96]; snprintf(t, sizeof t, "New version available (build %d) - tap to update", updateBuild);
      u->title = xstrdup(t); build_item(u);
    }
    Item *pr = add_item(s, K_PROMPT);
#if STAGE >= 2
    pr->act = me.id ? 1 : 0;
    pr->body = xstrdup(me.id ? "Write something on your page..." : "Sign in to post and draw."); pr->title = xstrdup(me.id ? "Write" : "Sign in");
#else
    pr->body = xstrdup("Sign in to post and draw."); pr->title = xstrdup("Sign in");
#endif
    build_item(pr);
    add_sec(s, "Most liked this week", 1);
    clear_side(s);
    add_cnew(add_side(s, K_CNEW));
  }
  if (s->kind == S_COMMS) { clear_side(s); net_send("GET", "/api/drawings", NULL, drawings_cb, s->serial, 0); }
  if (s->kind == S_PLAZA) {
    add_sec(s, "Plaza", 3);
    Item *i = add_item(s, K_INFO); i->body = xstrdup("Popular, new and recently active people walk around here. Drag to look around, touch a character to visit their page."); build_item(i);
    Item *f = add_item(s, K_CHIPS); build_item(f);
    Item *pz = add_item(s, K_PLAZA); build_item(pz);
  }
  if (s->kind == S_GAMES) {
    add_sec(s, "Games & Challenges", 4);
#if STAGE >= 3
    Item *g1 = add_item(s, K_GAME); g1->id = 0; g1->title = xstrdup("Super Wiggle Paint");
    g1->body = xstrdup("Draw anything while the whole canvas shakes, flips upside down and changes colour. Say what it is and send it to the gallery.");
    build_item(g1);
    Item *g2 = add_item(s, K_GAME); g2->id = 1; g2->title = xstrdup("Ultra Wiggle Paint");
    g2->body = xstrdup("Everything is 3 times faster. The buttons spin and change colour too. Sometimes your last move disappears, and sometimes a page pops up to bug you. Good luck!");
    build_item(g2);
    add_empty(s, "More challenges are on the way.");
#else
    add_empty(s, "Games come in the next update of the app.");
#endif
  }
  if (s->kind == S_STUB) add_empty(s, "Signing in comes in the next update of the app.");
  if (s->kind == S_COMM) {
    Item *h = add_item(s, K_CHEAD); h->name = xstrdup(s->cname); h->body = xstrdup(s->text); h->age = xstrdup(s->text2[0] ? s->text2 : "all");
    h->msg = xstrdup(s->parent ? s->parent->msg : NULL); h->avatar = xstrdup(s->parent ? s->parent->avatar : NULL); build_item(h);
    Item *pr = add_item(s, K_PROMPT);
    char t[160];
#if STAGE >= 2
    if (me.id) { snprintf(t, sizeof t, "Share your thoughts in this community..."); pr->act = 1; } else
#endif
    snprintf(t, sizeof t, "Sign in to post in %s.", s->cname);
    pr->body = xstrdup(t);
#if STAGE >= 2
    pr->title = xstrdup(me.id ? "Write" : "Sign in");
#else
    pr->title = xstrdup("Sign in");
#endif
    build_item(pr);
    clear_side(s);
    net_send("GET", "/api/drawings", NULL, drawings_cb, s->serial, 0);
  }
#if STAGE >= 3
  if (s->kind == S_WIGGLE) {
    Item *h = add_item(s, K_HEAD);
    h->name = xstrdup(s->mode ? "Ultra Wiggle Paint" : "Super Wiggle Paint");
    h->body = xstrdup(s->mode ? "Everything is 3 times faster. The buttons spin and change colour too. Sometimes your last move disappears. Good luck!"
                              : "Draw anything while the whole canvas shakes, flips upside down and changes colour. Say what it is and send it to the gallery.");
    build_item(h);
    Item *a = add_item(s, K_ACTION); a->act = me.id ? A_WIG_DRAW : A_SIGNIN; a->title = xstrdup(me.id ? "Start drawing" : "Sign in to play"); build_item(a);
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
  sfx(3);
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

/* ------------------------------------------------------------------ drawing editor: the website's openEditor() */
#if STAGE >= 2
typedef struct { char open[24], close[12]; int kind; } PTag;   /* kind: 1 b, 2 i, 3 u, 4 s, 5 color, 6 size, 7 font, 8 grad, 9 glow */
typedef struct { char text[1200]; PTag st[12]; int nst; } PSnap;
typedef struct {
  int open, bg, font, size, align, deco, col1, col2, tfont, tsize;
  char text[1200]; PTag st[12]; int nst;
  PSnap hist[24]; int nh, hpos;
  uint8_t *draw; int drawWig;
  SDL_Texture *prev; int prevW, prevH, prevDirty; SDL_Texture *drawTex;
  char cid[48], cname[96]; int wall, busy;
  int autoDraw;
} PostEd;
static PostEd pe;
#endif
#if STAGE >= 2
enum { T_PEN, T_ERASER, T_FILL, T_PICK, T_LINE, T_RECT, T_FRECT, T_ELLIPSE };
typedef struct {
  int open, challenge, ultra, K, wig, strength, color, size, tool, grid, zoomLvl;
  uint8_t px[DPIX], base[DPIX];
  uint8_t *undo[40]; int nundo; uint8_t *redo[40]; int nredo;
  SDL_Texture *tex, *gridTex; float gridZ;
  int drawing, x0, y0, lx, ly, curX, curY, started;
  /* super / ultra wiggle */
  float supPh; uint32_t chaosAt, lastTick, wf;
  int fx, fy, inv, sep; float hue, sat, sc; int tint;
  float uiRot[5]; float uiHue;
  int annoy, tabsOpened, cancelArmed; uint32_t cancelT;
  char title[48];
  Screen *target;
} Editor;
static Editor ed;
static SDL_Texture *edIcons[8];
static void ed_make_icons(void) {
  if (edIcons[0]) return;
  for (int k = 0; k < 8; k++) {
    Cv c = cv_new(32, 32); uint32_t w = 0xffffffff;
    switch (k) {
    case T_PEN: cv_line(&c, 6, 26, 6, 26, 3, w); for (int r = 0; r < 3; r++) cv_line(&c, 7, 25, 24, 8, 3, w); cv_line(&c, 6, 26, 9, 21, 2.5f, w); break;
    case T_ERASER: { float p[] = { 6, 20, 16, 9, 26, 19, 19, 26, 11, 26 }; for (int r = 0; r < 3; r++) cv_line(&c, p[0], p[1], p[2], p[3], 2.5f, w), cv_line(&c, p[2], p[3], p[4], p[5], 2.5f, w), cv_line(&c, p[4], p[5], p[6], p[7], 2.5f, w), cv_line(&c, p[6], p[7], p[8], p[9], 2.5f, w), cv_line(&c, p[8], p[9], p[0], p[1], 2.5f, w), cv_line(&c, 15, 26, 28, 26, 2.5f, w); } break;
    case T_FILL: for (int r = 0; r < 3; r++) { cv_line(&c, 8, 16, 16, 8, 2.5f, w); cv_line(&c, 16, 8, 25, 16, 2.5f, w); cv_line(&c, 25, 16, 17, 24, 2.5f, w); cv_line(&c, 17, 24, 8, 16, 2.5f, w); } cv_circle(&c, 26, 24, 2.6f, w); break;
    case T_PICK: for (int r = 0; r < 3; r++) { cv_line(&c, 18, 6, 26, 14, 2.5f, w); cv_line(&c, 22, 10, 9, 23, 2.5f, w); cv_line(&c, 9, 23, 6, 26, 2.5f, w); } break;
    case T_LINE: for (int r = 0; r < 3; r++) cv_line(&c, 6, 26, 26, 6, 2.5f, w); break;
    case T_RECT: for (int r = 0; r < 3; r++) { cv_line(&c, 5, 8, 27, 8, 2.5f, w); cv_line(&c, 27, 8, 27, 24, 2.5f, w); cv_line(&c, 27, 24, 5, 24, 2.5f, w); cv_line(&c, 5, 24, 5, 8, 2.5f, w); } break;
    case T_FRECT: cv_rrect1(&c, 5, 8, 22, 16, 0, w); break;
    default: for (int a = 0; a < 64; a++) { float t0 = a * 6.2831853f / 64, t1 = (a + 1) * 6.2831853f / 64; for (int r = 0; r < 2; r++) cv_line(&c, 16 + 11 * cosf(t0), 16 + 7.5f * sinf(t0), 16 + 11 * cosf(t1), 16 + 7.5f * sinf(t1), 2.3f, w); } break;
    }
    edIcons[k] = cv_tex(&c); cv_free(&c);
  }
}
static void ed_snap(void) {
  if (ed.nundo == 40) { free(ed.undo[0]); memmove(ed.undo, ed.undo + 1, sizeof(ed.undo[0]) * 39); ed.nundo--; }
  ed.undo[ed.nundo] = malloc(DPIX); memcpy(ed.undo[ed.nundo++], ed.px, DPIX);
  for (int i = 0; i < ed.nredo; i++) free(ed.redo[i]);
  ed.nredo = 0;
}
static void ed_undo(void) { if (!ed.nundo) return; if (ed.nredo < 40) { ed.redo[ed.nredo] = malloc(DPIX); memcpy(ed.redo[ed.nredo++], ed.px, DPIX); } ed.nundo--; memcpy(ed.px, ed.undo[ed.nundo], DPIX); free(ed.undo[ed.nundo]); }
static void ed_redo(void) { if (!ed.nredo) return; if (ed.nundo < 40) { ed.undo[ed.nundo] = malloc(DPIX); memcpy(ed.undo[ed.nundo++], ed.px, DPIX); } ed.nredo--; memcpy(ed.px, ed.redo[ed.nredo], DPIX); free(ed.redo[ed.nredo]); }
static void ed_chaos_reset(void) { ed.fx = ed.fy = ed.inv = ed.sep = 0; ed.hue = 0; ed.sat = 1; ed.sc = 1; ed.tint = -1; for (int i = 0; i < 5; i++) ed.uiRot[i] = 0; ed.uiHue = 0; }
static void ed_open(Screen *target, int challenge, int ultra, const uint8_t *start) {
  for (int i = 0; i < ed.nundo; i++) free(ed.undo[i]);
  for (int i = 0; i < ed.nredo; i++) free(ed.redo[i]);
  SDL_Texture *keepTex = ed.tex, *keepGrid = ed.gridTex; float keepZ = ed.gridZ;
  memset(&ed, 0, sizeof ed);
  ed.tex = keepTex; ed.gridTex = keepGrid; ed.gridZ = keepZ;
  ed.open = 1; ed.target = target; ed.challenge = challenge; ed.ultra = ultra; ed.K = ultra ? 3 : 1;
  ed.wig = challenge; ed.strength = ultra ? 5 : challenge ? 3 : 2;
  ed.color = 5; ed.size = 1; ed.tool = T_PEN; ed.grid = 1; ed.zoomLvl = 2;
  if (start) memcpy(ed.px, start, DPIX);
  if (!ed.tex) ed.tex = SDL_CreateTexture(R, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, DW, DH);
  ed_chaos_reset();
  ed_make_icons();
}
static void ed_close(void) {
  for (int i = 0; i < ed.nundo; i++) free(ed.undo[i]);
  for (int i = 0; i < ed.nredo; i++) free(ed.redo[i]);
  ed.nundo = ed.nredo = 0; ed.open = 0;
}
static void ed_stamp(int x, int y, int c) {
  int o = (ed.size - 1) / 2;
  for (int yy = y - o; yy < y - o + ed.size; yy++) for (int xx = x - o; xx < x - o + ed.size; xx++) if (xx >= 0 && yy >= 0 && xx < DW && yy < DH) ed.px[yy * DW + xx] = (uint8_t)c;
}
static void ed_line(int x0, int y0, int x1, int y1, int c, int stamp) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    if (stamp) ed_stamp(x0, y0, c); else if (x0 >= 0 && y0 >= 0 && x0 < DW && y0 < DH) ed.px[y0 * DW + x0] = (uint8_t)c;
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err; if (e2 >= dy) { err += dy; x0 += sx; } if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
static void ed_rect(int x0, int y0, int x1, int y1, int c, int filled) {
  int a = x0 < x1 ? x0 : x1, b = x0 < x1 ? x1 : x0, t = y0 < y1 ? y0 : y1, d = y0 < y1 ? y1 : y0;
  if (filled) { for (int y = t; y <= d; y++) for (int x = a; x <= b; x++) if (x >= 0 && y >= 0 && x < DW && y < DH) ed.px[y * DW + x] = (uint8_t)c; return; }
  ed_line(a, t, b, t, c, 1); ed_line(a, d, b, d, c, 1); ed_line(a, t, a, d, c, 1); ed_line(b, t, b, d, c, 1);
}
static void ed_ellipse(int x0, int y0, int x1, int y1, int c) {
  float cx = (x0 + x1) / 2.0f, cy = (y0 + y1) / 2.0f, rx = abs(x1 - x0) / 2.0f, ry = abs(y1 - y0) / 2.0f;
  if (rx < 1 || ry < 1) { ed_line(x0, y0, x1, y1, c, 1); return; }
  int steps = (int)ceilf(3.14159265f * (rx + ry) * 2), px0 = 0, py0 = 0, first = 1;
  for (int i = 0; i <= steps; i++) {
    float a = (float)i / steps * 6.2831853f; int x = (int)lroundf(cx + cosf(a) * rx), y = (int)lroundf(cy + sinf(a) * ry);
    if (!first) ed_line(px0, py0, x, y, c, 1); else ed_stamp(x, y, c);
    px0 = x; py0 = y; first = 0;
  }
}
static void ed_flood(int x, int y, int c) {
  int target = ed.px[y * DW + x]; if (target == c) return;
  int *stack = malloc(sizeof(int) * 2 * DPIX); int n = 0;
  stack[n++] = x; stack[n++] = y;
  while (n) {
    int sy = stack[--n], sx = stack[--n], l = sx;
    while (l >= 0 && ed.px[sy * DW + l] == target) l--;
    l++; int up = 0, dn = 0;
    for (int xx = l; xx < DW && ed.px[sy * DW + xx] == target; xx++) {
      ed.px[sy * DW + xx] = (uint8_t)c;
      if (sy > 0) { int t = ed.px[(sy - 1) * DW + xx] == target; if (t && !up && n < 2 * DPIX - 2) { stack[n++] = xx; stack[n++] = sy - 1; } up = t; }
      if (sy < DH - 1) { int t = ed.px[(sy + 1) * DW + xx] == target; if (t && !dn && n < 2 * DPIX - 2) { stack[n++] = xx; stack[n++] = sy + 1; } dn = t; }
    }
  }
  free(stack);
}

/* CSS filter chain on one colour: hue-rotate -> invert -> saturate -> sepia, then the multiply tint */
static uint32_t css_filter(uint32_t c, float hue, int inv, float sat, int sep, int tint) {
  float r = ((c >> 16) & 255) / 255.0f, g = ((c >> 8) & 255) / 255.0f, b = (c & 255) / 255.0f;
  if (hue != 0) {
    float a = hue * 3.14159265f / 180, co = cosf(a), si = sinf(a);
    float nr = r * (0.213f + co * 0.787f - si * 0.213f) + g * (0.715f - co * 0.715f - si * 0.715f) + b * (0.072f - co * 0.072f + si * 0.928f);
    float ng = r * (0.213f - co * 0.213f + si * 0.143f) + g * (0.715f + co * 0.285f + si * 0.140f) + b * (0.072f - co * 0.072f - si * 0.283f);
    float nb = r * (0.213f - co * 0.213f - si * 0.787f) + g * (0.715f - co * 0.715f + si * 0.715f) + b * (0.072f + co * 0.928f + si * 0.072f);
    r = nr; g = ng; b = nb;
  }
  if (inv) { r = 1 - r; g = 1 - g; b = 1 - b; }
  if (sat != 1) {
    float nr = r * (0.213f + 0.787f * sat) + g * (0.715f - 0.715f * sat) + b * (0.072f - 0.072f * sat);
    float ng = r * (0.213f - 0.213f * sat) + g * (0.715f + 0.285f * sat) + b * (0.072f - 0.072f * sat);
    float nb = r * (0.213f - 0.213f * sat) + g * (0.715f - 0.715f * sat) + b * (0.072f + 0.928f * sat);
    r = nr; g = ng; b = nb;
  }
  if (sep) { float nr = r * .393f + g * .769f + b * .189f, ng = r * .349f + g * .686f + b * .168f, nb = r * .272f + g * .534f + b * .131f; r = nr; g = ng; b = nb; }
  if (tint >= 0) { /* hsl(tint, 95%, 58%) multiply */
    float h = tint / 60.0f, C = (1 - fabsf(2 * 0.58f - 1)) * 0.95f, X = C * (1 - fabsf(fmodf(h, 2) - 1)), m = 0.58f - C / 2, tr, tg, tb;
    int i = (int)h % 6;
    if (i == 0) { tr = C; tg = X; tb = 0; } else if (i == 1) { tr = X; tg = C; tb = 0; } else if (i == 2) { tr = 0; tg = C; tb = X; } else if (i == 3) { tr = 0; tg = X; tb = C; } else if (i == 4) { tr = X; tg = 0; tb = C; } else { tr = C; tg = 0; tb = X; }
    r *= tr + m; g *= tg + m; b *= tb + m;
  }
  if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0; if (r > 1) r = 1; if (g > 1) g = 1; if (b > 1) b = 1;
  return ((uint32_t)(r * 255) << 16) | ((uint32_t)(g * 255) << 8) | (uint32_t)(b * 255);
}
static void ed_upload(void) {
  void *pix; int pitch;
  if (!ed.tex || SDL_LockTexture(ed.tex, NULL, &pix, &pitch)) return;
  uint32_t pal[32];
  int chaos = ed.challenge && ed.started;
  for (int i = 0; i < 32; i++) pal[i] = chaos ? css_filter(PAL[i], ed.hue, ed.inv, ed.sat, ed.sep, ed.tint) : PAL[i];
  float amp = 0, phX = 0, phY = 0;
  if (chaos) { amp = (1.2f + ed.strength * 0.7f) * (ed.ultra ? 1.4f : 1); phX = ed.supPh; phY = ed.supPh * 1.3f; }   /* wigLive() */
  else if (ed.wig && !ed.drawing) { amp = ed.strength * 1.1f; phX = (ed.wf % 4) * 1.7f; phY = (ed.wf % 4) * 2.2f; }   /* wigFrames() preview */
  wig_pixels(ed.px, pal, amp, phX, phY, pix, pitch);
  SDL_UnlockTexture(ed.tex);
}
static void ed_chaos_pick(void) {
  int n = 1 + ((rand() % 100) < 45 ? 1 : 0);
  static const int tints[6] = { 120, 210, 300, 55, 0, 180 };
  for (int i = 0; i < n; i++) {
    int k = rand() % 9;
    if (k == 0) { ed.fx = 1; ed.fy = 1; }
    else if (k == 1) ed.fx = !ed.fx;
    else if (k == 2) ed.fy = !ed.fy;
    else if (k == 3 || k == 4) { ed.tint = tints[rand() % 6]; ed.hue = (float)(rand() % 360); ed.sat = 1.5f + (rand() % 200) / 100.0f; }
    else if (k == 5) ed.inv = !ed.inv;
    else if (k == 6) ed.sc = 0.7f + (rand() % 50) / 100.0f;
    else if (k == 7) { ed.sep = !ed.sep; ed.hue = (float)(rand() % 360); }
    else { ed.fx = ed.fy = ed.inv = ed.sep = 0; ed.sat = 1; ed.sc = 1; ed.hue = 0; ed.tint = -1; }
  }
  if (ed.ultra) {
    for (int i = 0; i < 5; i++) ed.uiRot[i] = (rand() % 10 == 0) ? 180 : (float)((rand() % 160) - 80) / 10.0f;
    ed.uiHue = (float)(rand() % 360);
    if (!ed.drawing && ed.nundo && (rand() % 1000) < 25) { ed_undo(); toast("Oops! Your last move vanished."); }
  }
}

/* layout (website .ed grid scaled for the TV) */
#define EMX 20
#define EMY 8
#define EMW 1240
#define EMH 704
#define EBODY_Y (EMY + 48)
#define EFOOT_H 66
#define ETOOLS_X (EMX + 12)
#define ESIDE_W 220
#define ESIDE_X (EMX + EMW - 12 - ESIDE_W)
#define ESTAGE_X (ETOOLS_X + 108 + 12)
#define ESTAGE_W (ESIDE_X - 12 - ESTAGE_X)
static float ed_zoom(void) {
  float fit = fminf((ESTAGE_W - 16) / 320.0f, (EMH - 48 - EFOOT_H - 24 - 48 - 16) / 180.0f);
  return ed.zoomLvl == 0 ? 1.0f : ed.zoomLvl == 1 ? fminf(2.0f, fit) : fit;
}
static void ed_canvas_rect(SDL_Rect *r) {
  float z = ed_zoom(); int w = (int)(320 * z), h = (int)(180 * z);
  int availH = EMH - 48 - EFOOT_H - 24 - 48;
  r->x = ESTAGE_X + (ESTAGE_W - w) / 2; r->y = EBODY_Y + 12 + 8 + (availH - 16 - h) / 2; r->w = w; r->h = h;
}
typedef struct { int x, y, w, h; } Box;
static Box ed_tool_box(int k) { Box b = { ETOOLS_X + (k % 2) * 54, EBODY_Y + 12 + (k / 2) * 52, 50, 48 }; return b; }
static Box ed_bar_box(int i) {   /* 0 -, 1 zoom label, 2 +, 3 Grid, 4 Undo, 5 Redo, 6 Wiggle, 7..11 strength, 12 Clear */
  static const int W[13] = { 40, 46, 40, 76, 76, 76, 96, 28, 28, 28, 28, 28, 80 };
  int x = ESTAGE_X, y = EMY + EMH - EFOOT_H - 12 - 40;
  for (int k = 0; k < i; k++) { if ((k >= 6 && k <= 11) && !(ed.wig && !ed.challenge) && !(k == 6 && !ed.challenge)) continue; if (ed.challenge && k >= 6 && k <= 11) continue; x += W[k] + (k == 1 ? 4 : 8); }
  Box b = { x, y, W[i], 38 };
  return b;
}
static int ed_bar_visible(int i) { if (i >= 6 && i <= 11) { if (ed.challenge) return 0; if (i > 6 && !ed.wig) return 0; } return 1; }
static Box ed_pal_box(int i) { int cw = (ESIDE_W - 4) / 8; Box b = { ESIDE_X + 2 + (i % 8) * cw, EBODY_Y + 12 + 58 + 2 + (i / 8) * cw, cw, cw }; return b; }
static Box ed_size_box(int i) { int bw = (ESIDE_W - 12) / 4; int cw = (ESIDE_W - 4) / 8; Box b = { ESIDE_X + i * (bw + 4), EBODY_Y + 12 + 58 + 4 + cw * 4 + 12, bw, 46 }; return b; }
static Box ed_title_box(void) { Box b = { EMX + 18, EMY + EMH - EFOOT_H + 11, 440, 44 }; return b; }
static Box ed_foot_btn(int i) { int w = i == 0 ? 130 : 120; int x = i == 0 ? EMX + EMW - 18 - 120 - 14 - 130 : EMX + EMW - 18 - 120; Box b = { x, EMY + EMH - EFOOT_H + 9, w, 48 }; return b; }
static Box ed_annoy_btn(void) { Box b = { SW / 2 - 140, SH / 2 + 110, 280, 56 }; return b; }

/* old-school .ed-btn: beige, 2px dark border, light/dark bevel; pressed = green */
static void ed_btn(Box b, const char *txt, SDL_Texture *icon, int pressed, int disabled) {
  uint32_t bg = pressed ? C_GREEN : 0xf0ece0;
  if (ed.ultra && ed.started && !pressed) bg = css_filter(bg, ed.uiHue, 0, 1.8f, 0, -1);
  fillr(b.x, b.y, b.w, b.h, 0x222222);
  fillr(b.x + 2, b.y + 2, b.w - 4, b.h - 4, bg);
  if (!pressed) { fillr(b.x + 2, b.y + 2, b.w - 4, 2, 0xffffff); fillr(b.x + 2, b.y + 2, 2, b.h - 4, 0xffffff); fillr(b.x + 2, b.y + b.h - 4, b.w - 4, 2, 0xb4ae9c); fillr(b.x + b.w - 4, b.y + 2, 2, b.h - 4, 0xb4ae9c); }
  else { fillr(b.x + 2, b.y + 2, b.w - 4, 2, 0x3a8a00); fillr(b.x + 2, b.y + 2, 2, b.h - 4, 0x3a8a00); }
  uint32_t fg = pressed ? 0xffffff : disabled ? 0xa09a8a : 0x222222;
  if (icon) { SDL_SetTextureColorMod(icon, (fg >> 16) & 255, (fg >> 8) & 255, fg & 255); SDL_Rect d = { b.x + (b.w - 30) / 2, b.y + (b.h - 30) / 2, 30, 30 }; SDL_RenderCopy(R, icon, NULL, &d); }
  if (txt) { TTF_Font *pf = font_get(7, 13, 0); Tex t = mk_text(pf ? pf : F_TINY, txt, fg, 0); draw_tex(&t, b.x + (b.w - t.w) / 2, b.y + (b.h - t.h) / 2 + 1); tex_free(&t); }
}

static void ed_send_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)serial; (void)extra;
  if (code != 200) { char e[200]; api_error(code, body, err, e, sizeof e); toast(e); return; }
  toast("Sent! Your drawing is in the gallery.");
  ed_close();
  Screen *c = cur(); if (c && c->kind == S_WIGGLE) load_screen(c, 0);
}
static void rebuild_compose(Screen *s);
static char *encode_drawing(const uint8_t *px, int wig);
static void ed_title_done(void) { }
static void ed_done(void) {
  int empty = 1;
  for (int i = 0; i < DPIX; i++) if (ed.px[i]) { empty = 0; break; }
  if (empty) { toast("The canvas is empty. Draw something first."); return; }
  if (ed.challenge) {
    char t[48]; snprintf(t, sizeof t, "%s", ed.title);
    char *p = t; while (*p == ' ') p++;
    if (utf8_len(p) < 2) { toast("Write what you drew first."); return; }
    char *code = encode_drawing(ed.px, ed.strength);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "title", p); cJSON_AddStringToObject(o, "drawing", code); cJSON_AddStringToObject(o, "mode", ed.ultra ? "ultra" : "super");
    char *b = cJSON_PrintUnformatted(o);
    net_send("POST", "/api/wiggle", b, ed_send_cb, 0, 0);
    free(b); free(code); cJSON_Delete(o);
    toast("Sending...");
    return;
  }
  Screen *s = ed.target;
  if (!s) {   /* drawing for the NEW POST editor */
    if (!pe.draw) pe.draw = malloc(DPIX);
    memcpy(pe.draw, ed.px, DPIX); pe.drawWig = ed.wig ? ed.strength : 0;
    if (pe.drawTex) { SDL_DestroyTexture(pe.drawTex); pe.drawTex = NULL; }
  } else if (by_serial(s->serial)) {
    if (!s->draw) s->draw = malloc(DPIX);
    memcpy(s->draw, ed.px, DPIX); s->drawWig = ed.wig ? ed.strength : 0;
    rebuild_compose(s);
  }
  ed_close();
}
static void ed_cancel(void) {
  uint32_t now = SDL_GetTicks();
  if (ed.nundo && !(ed.cancelArmed && now - ed.cancelT < 4000)) { ed.cancelArmed = 1; ed.cancelT = now; toast("Press Cancel again to throw this drawing away."); return; }
  ed_close();
}

/* Ultra Wiggle uiChaos(): panels are drawn into a texture and then shown rotated and colour shifted */
static SDL_Texture *panelTex; static int panelOn;
static void panel_begin(void) {
  if (!(ed.ultra && ed.started)) { panelOn = 0; return; }
  if (!panelTex) { panelTex = SDL_CreateTexture(R, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, SW, SH); if (panelTex) SDL_SetTextureBlendMode(panelTex, SDL_BLENDMODE_BLEND); }
  if (!panelTex || SDL_SetRenderTarget(R, panelTex) != 0) { panelOn = 0; return; }
  panelOn = 1;
  SDL_SetRenderDrawColor(R, 0, 0, 0, 0); SDL_RenderClear(R);
  SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
}
static void panel_end(int x, int y, int w, int h, int idx) {
  if (!panelOn) return;
  SDL_SetRenderTarget(R, NULL);
  float hh = fmodf(ed.uiHue + idx * 70, 360) / 60.0f; int i = (int)hh; float f = hh - i;
  float rgb[6][3] = { { 1, f, 0 }, { 1 - f, 1, 0 }, { 0, 1, f }, { 0, 1 - f, 1 }, { f, 0, 1 }, { 1, 0, 1 - f } };
  float *c = rgb[i % 6];
  SDL_SetTextureColorMod(panelTex, (Uint8)(150 + 105 * c[0]), (Uint8)(150 + 105 * c[1]), (Uint8)(150 + 105 * c[2]));
  SDL_Rect r = { x, y, w, h };
  SDL_RenderCopyEx(R, panelTex, &r, &r, ed.uiRot[idx], NULL, SDL_FLIP_NONE);
  SDL_SetTextureColorMod(panelTex, 255, 255, 255);
  panelOn = 0;
}
static void ed_draw(void) {
  uint32_t now = SDL_GetTicks();
  /* timers: live wiggle every 50 ms, preview frames every 125 ms, chaos */
  if (ed.challenge && ed.started) {
    if (now - ed.lastTick >= 50) { ed.supPh += 0.55f * ed.K; ed.lastTick = now; }
    if (now > ed.chaosAt) { ed_chaos_pick(); ed.chaosAt = now + (uint32_t)((450 + rand() % 600) / ed.K); }
  } else if (now - ed.lastTick >= 125) { ed.wf++; ed.lastTick = now; }
  ed_upload();
  /* dim page + modal */
  SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(R, 0, 0, 0, 140); SDL_Rect all = { 0, 0, SW, SH }; SDL_RenderFillRect(R, &all);
  fillr(EMX - 1, EMY - 1, EMW + 2, EMH + 2, 0x888888);
  fillr(EMX, EMY, EMW, EMH, 0xffffff);
  /* head */
  panel_begin();
  { TTF_Font *pf = font_get(7, 17, 0); Tex t = mk_text(pf ? pf : F_NAME, ed.ultra ? "ULTRA WIGGLE" : ed.challenge ? "SUPER WIGGLE" : "DRAW", 0x323232, 0);
    draw_tex(&t, EMX + 20, EMY + (48 - t.h) / 2); tex_free(&t); }
  draw_label(F_SMALL, C_SOFT, "320 \xC3\x97 180 pixels \xC2\xB7 32 colours", EMX + EMW - 20, EMY + 13, 2);
  fillr(EMX, EMY + 47, EMW, 1, C_LINE);
  panel_end(EMX, EMY, EMW, 48, 0);
  /* body */
  uint32_t bodyCol = 0xd9d4c7;
  fillr(EMX, EBODY_Y, EMW, EMH - 48 - EFOOT_H, bodyCol);
  /* tools */
  panel_begin();
  for (int k = 0; k < 8; k++) ed_btn(ed_tool_box(k), NULL, edIcons[k], ed.tool == k, 0);
  panel_end(ETOOLS_X - 4, EBODY_Y + 8, 116, 4 * 52 + 8, 1);
  /* stage: .ed-wrap */
  SDL_Rect cr; ed_canvas_rect(&cr);
  int wrapX = ESTAGE_X, wrapY = EBODY_Y + 12, wrapW = ESTAGE_W, wrapH = EMH - 48 - EFOOT_H - 24 - 48;
  fillr(wrapX, wrapY, wrapW, wrapH, 0x222222);
  fillr(wrapX + 2, wrapY + 2, wrapW - 4, wrapH - 4, 0x8c8878);
  int live = ed.challenge && ed.started;
  float sc = live ? ed.sc : 1;
  double jellyA = 0; float jsx = 1, jsy = 1;
  if (live) { /* @keyframes superjelly */
    float per = ed.ultra ? 110 : 320, t = fmodf((float)now, per) / per * 4; int seg = (int)t; float f = t - seg;
    static const float KA[5] = { -.6f, .5f, -.4f, .6f, -.6f }, KX[5] = { 1.005f, .995f, 1.006f, .996f, 1.005f }, KY[5] = { .995f, 1.006f, .995f, 1.005f, .995f };
    jellyA = KA[seg] + (KA[seg + 1] - KA[seg]) * f; jsx = KX[seg] + (KX[seg + 1] - KX[seg]) * f; jsy = KY[seg] + (KY[seg + 1] - KY[seg]) * f;
  }
  SDL_Rect dr = { cr.x + (int)(cr.w * (1 - sc * jsx) / 2), cr.y + (int)(cr.h * (1 - sc * jsy) / 2), (int)(cr.w * sc * jsx), (int)(cr.h * sc * jsy) };
  SDL_RenderSetClipRect(R, &(SDL_Rect){ wrapX + 2, wrapY + 2, wrapW - 4, wrapH - 4 });
  int fl = (ed.fx ? SDL_FLIP_HORIZONTAL : 0) | (ed.fy ? SDL_FLIP_VERTICAL : 0);
  SDL_RenderCopyEx(R, ed.tex, NULL, &dr, jellyA, NULL, (SDL_RendererFlip)fl);
  /* pixel grid (zoom >= 2) */
  float z = ed_zoom();
  if (ed.grid && z >= 2 && !live) {
    if (!ed.gridTex || ed.gridZ != z) {
      if (ed.gridTex) SDL_DestroyTexture(ed.gridTex);
      Cv g = cv_new(cr.w, cr.h);
      for (int x = 0; x <= 320; x++) { int px = (int)(x * z); if (px >= cr.w) px = cr.w - 1; for (int y = 0; y < cr.h; y++) cv_blend(&g, px, y, 0x24000000, 1); }
      for (int y = 0; y <= 180; y++) { int py = (int)(y * z); if (py >= cr.h) py = cr.h - 1; for (int x = 0; x < cr.w; x++) cv_blend(&g, x, py, 0x24000000, 1); }
      ed.gridTex = cv_tex(&g); cv_free(&g); ed.gridZ = z;
    }
    SDL_RenderCopy(R, ed.gridTex, NULL, &cr);
  }
  SDL_RenderSetClipRect(R, NULL);
  /* bar */
  panel_begin();
  for (int i = 0; i < 13; i++) {
    if (!ed_bar_visible(i)) continue;
    Box b = ed_bar_box(i);
    if (i == 1) { char zl[16]; snprintf(zl, sizeof zl, "%dx", (int)(z + 0.01f)); TTF_Font *pf = font_get(7, 13, 0); Tex t = mk_text(pf ? pf : F_TINY, zl, 0x222222, 0); draw_tex(&t, b.x + (b.w - t.w) / 2, b.y + (b.h - t.h) / 2); tex_free(&t); continue; }
    static const char *L[13] = { "-", "", "+", "Grid", "Undo", "Redo", "Wiggle", "1", "2", "3", "4", "5", "Clear" };
    int pressed = (i == 3 && ed.grid) || (i == 6 && ed.wig) || (i >= 7 && i <= 11 && ed.strength == i - 6);
    int dis = (i == 4 && !ed.nundo) || (i == 5 && !ed.nredo);
    ed_btn(b, L[i], NULL, pressed, dis);
  }
  if (ed.curX >= 0) { char pos[24]; snprintf(pos, sizeof pos, "%d,%d", ed.curX, ed.curY); TTF_Font *pf = font_get(7, 12, 0); Tex t = mk_text(pf ? pf : F_TINY, pos, 0x222222, 0); draw_tex(&t, ESTAGE_X + ESTAGE_W - t.w - 4, EMY + EMH - EFOOT_H - 12 - 40 + 12); tex_free(&t); }
  panel_end(ESTAGE_X - 4, EMY + EMH - EFOOT_H - 12 - 44, ESTAGE_W + 8, 48, 2);
  /* side: current swatch, palette, sizes */
  panel_begin();
  { int sx = ESIDE_X, sy = EBODY_Y + 12;
    fillr(sx, sy, 80, 46, 0x222222); fillr(sx + 2, sy + 2, 76, 42, 0xffffff); fillr(sx + 4, sy + 4, 72, 38, PAL[ed.color]); }
  { int cw = (ESIDE_W - 4) / 8; fillr(ESIDE_X, EBODY_Y + 12 + 58, cw * 8 + 4, cw * 4 + 4, 0x222222); }
  for (int i = 0; i < 32; i++) {
    Box b = ed_pal_box(i);
    fillr(b.x, b.y, b.w, b.h, PAL[i]);
    if (i == ed.color) { fillr(b.x - 2, b.y - 2, b.w + 4, b.h + 4, 0x000000); fillr(b.x, b.y, b.w, b.h, 0xffffff); fillr(b.x + 3, b.y + 3, b.w - 6, b.h - 6, PAL[i]); }
  }
  static const int SZ[4] = { 1, 2, 4, 8 };
  for (int i = 0; i < 4; i++) {
    Box b = ed_size_box(i); ed_btn(b, NULL, NULL, ed.size == SZ[i], 0);
    int q = (int)((SZ[i] * 2 + 2 < 18 ? SZ[i] * 2 + 2 : 18) * 1.4f);
    fillr(b.x + (b.w - q) / 2, b.y + (b.h - q) / 2, q, q, ed.size == SZ[i] ? 0xffffff : 0x222222);
  }
  if (ed.ultra && ed.started) draw_label(F_TINY, 0x7a3ce0, "ULTRA!", ESIDE_X + ESIDE_W / 2, ed_size_box(0).y + 70, 1);
  panel_end(ESIDE_X - 6, EBODY_Y + 6, ESIDE_W + 12, ed_size_box(0).y + 100 - EBODY_Y, 3);
  /* foot */
  panel_begin();
  fillr(EMX, EMY + EMH - EFOOT_H, EMW, 1, C_LINE);
  if (ed.challenge) {
    Box t = ed_title_box();
    boxr(t.x, t.y, t.w, t.h, 0xffffff, 0xbdbdbd, 1);
    if (ed.title[0]) draw_label(F_BODYS, C_INK, ed.title, t.x + 12, t.y + 10, 0); else draw_label(F_BODYS, 0x9aa5ab, "What is it? (for example: a cat)", t.x + 12, t.y + 10, 0);
  }
  Box cb = ed_foot_btn(0), db = ed_foot_btn(1);
  draw_btn(cb.x, cb.y, cb.w, cb.h, "Cancel", 1, 0);
  draw_btn(db.x, db.y, db.w, db.h, ed.challenge ? "Send" : "Done", 0, 0);
  panel_end(EMX, EMY + EMH - EFOOT_H, EMW, EFOOT_H, 4);
  /* the Ultra Wiggle Paint annoyance page */
  if (ed.annoy) {
    SDL_SetRenderDrawColor(R, 0, 0, 0, 120); SDL_RenderFillRect(R, &all);
    int w = 640, h = 380, x = (SW - w) / 2, y = (SH - h) / 2;
    card(x, y, w, h);
    float a = 6 * sinf(now / 80.0f);
    static SDL_Texture *face;
    if (!face) { Cv c = cv_new(84, 84); Stop st[2] = { { 0, 0xff7de04a }, { 1, 0xff3fb21c } }; cv_rrect(&c, 0, 0, 84, 84, 20, st, 2, 180); cv_circle(&c, 28, 34, 7, 0xffffffff); cv_circle(&c, 56, 34, 7, 0xffffffff); for (int r = 0; r < 3; r++) for (int k = 0; k < 20; k++) { float t0 = 3.14159265f * (0.15f + 0.7f * k / 20), t1 = 3.14159265f * (0.15f + 0.7f * (k + 1) / 20); cv_line(&c, 42 + 20 * cosf(t0), 46 + 14 * sinf(t0), 42 + 20 * cosf(t1), 46 + 14 * sinf(t1), 6, 0xffffffff); } face = cv_tex(&c); cv_free(&c); }
    SDL_Rect fd = { SW / 2 - 42, y + 24, 84, 84 }; SDL_RenderCopyEx(R, face, NULL, &fd, a, NULL, SDL_FLIP_NONE);
    draw_label(F_TITLE, 0x1a6e1a, "Hi there!", SW / 2, y + 120, 1);
    Tex t = mk_text(F_BODY, "This is the Ultra Wiggle Paint annoyance.\nPlease close this page and go back to the previous one. Your drawing is waiting for you there.", C_INK, w - 80);
    draw_tex(&t, SW / 2 - t.w / 2, y + 170); tex_free(&t);
    Box b = ed_annoy_btn(); draw_btn(b.x, b.y, b.w, b.h, "Close this page", 0, 0);
  }
}

static void ed_touch(int tx, int ty, int down, int fresh) {
  if (ed.annoy) { if (fresh) { Box b = ed_annoy_btn(); if (inr(tx, ty, b.x, b.y, b.w, b.h)) ed.annoy = 0; } return; }
  SDL_Rect cr; ed_canvas_rect(&cr);
  float fxr = (tx - cr.x) / (float)cr.w, fyr = (ty - cr.y) / (float)cr.h;
  if (ed.fx) fxr = 1 - fxr; if (ed.fy) fyr = 1 - fyr;
  int x = (int)(fxr * DW), y = (int)(fyr * DH);
  if (x < 0) x = 0; if (y < 0) y = 0; if (x >= DW) x = DW - 1; if (y >= DH) y = DH - 1;
  int onCanvas = tx >= cr.x && ty >= cr.y && tx < cr.x + cr.w && ty < cr.y + cr.h;
  if (onCanvas || ed.drawing) { ed.curX = x; ed.curY = y; }
  if (down && fresh && onCanvas) {
    if (ed.tool == T_PICK) { ed.color = ed.px[y * DW + x]; ed.tool = T_PEN; return; }
    ed_snap();
    if (ed.tool == T_FILL) { ed_flood(x, y, ed.color); return; }
    ed.drawing = 1; ed.x0 = ed.lx = x; ed.y0 = ed.ly = y; memcpy(ed.base, ed.px, DPIX);
    if (ed.challenge && !ed.started) { ed.started = 1; ed.chaosAt = SDL_GetTicks() + 250; }
    if (ed.ultra && ed.challenge && ed.tabsOpened < 2 && (rand() % 100) < 4) { ed.tabsOpened++; ed.annoy = 1; }
    if (ed.tool == T_PEN || ed.tool == T_ERASER) ed_stamp(x, y, ed.tool == T_ERASER ? 0 : ed.color);
    return;
  }
  if (down && ed.drawing) {
    if (ed.tool == T_PEN || ed.tool == T_ERASER) ed_line(ed.lx, ed.ly, x, y, ed.tool == T_ERASER ? 0 : ed.color, 1);
    else {
      memcpy(ed.px, ed.base, DPIX);
      if (ed.tool == T_LINE) ed_line(ed.x0, ed.y0, x, y, ed.color, 1);
      else if (ed.tool == T_RECT) ed_rect(ed.x0, ed.y0, x, y, ed.color, 0);
      else if (ed.tool == T_FRECT) ed_rect(ed.x0, ed.y0, x, y, ed.color, 1);
      else if (ed.tool == T_ELLIPSE) ed_ellipse(ed.x0, ed.y0, x, y, ed.color);
    }
    ed.lx = x; ed.ly = y;
    return;
  }
  if (!down && ed.drawing) { ed.drawing = 0; return; }
  if (!fresh) return;
  for (int k = 0; k < 8; k++) { Box b = ed_tool_box(k); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { ed.tool = k; return; } }
  for (int i = 0; i < 32; i++) { Box b = ed_pal_box(i); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { ed.color = i; if (ed.tool == T_ERASER || ed.tool == T_PICK) ed.tool = T_PEN; return; } }
  static const int SZ[4] = { 1, 2, 4, 8 };
  for (int i = 0; i < 4; i++) { Box b = ed_size_box(i); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { ed.size = SZ[i]; return; } }
  for (int i = 0; i < 13; i++) {
    if (!ed_bar_visible(i) || i == 1) continue;
    Box b = ed_bar_box(i);
    if (!inr(tx, ty, b.x, b.y, b.w, b.h)) continue;
    if (i == 0 && ed.zoomLvl > 0) ed.zoomLvl--;
    else if (i == 2 && ed.zoomLvl < 2) ed.zoomLvl++;
    else if (i == 3) ed.grid = !ed.grid;
    else if (i == 4) ed_undo();
    else if (i == 5) ed_redo();
    else if (i == 6) { ed.wig = !ed.wig; toast(ed.wig ? "Wiggle on: the drawing will wobble." : "Wiggle off."); }
    else if (i >= 7 && i <= 11) ed.strength = i - 6;
    else if (i == 12) { ed_snap(); memset(ed.px, 0, DPIX); }
    return;
  }
  if (ed.challenge) { Box t = ed_title_box(); if (inr(tx, ty, t.x, t.y, t.w, t.h)) { kb_open("What is it?", ed.title, sizeof ed.title, 40, 0, 0, ed_title_done); return; } }
  { Box b = ed_foot_btn(0); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { ed_cancel(); return; } }
  { Box b = ed_foot_btn(1); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { ed_done(); return; } }
}
static void ed_buttons(uint32_t trig) {
  if (ed.annoy) { if (trig & (VPAD_BUTTON_A | VPAD_BUTTON_B)) ed.annoy = 0; return; }
  if (trig & VPAD_BUTTON_Y) ed_undo();
  if (trig & VPAD_BUTTON_X) ed_redo();
  static const int SZ[4] = { 1, 2, 4, 8 };
  int si = ed.size == 1 ? 0 : ed.size == 2 ? 1 : ed.size == 4 ? 2 : 3;
  if (trig & VPAD_BUTTON_L) { if (si > 0) ed.size = SZ[si - 1]; }
  if (trig & VPAD_BUTTON_R) { if (si < 3) ed.size = SZ[si + 1]; }
  if (trig & VPAD_BUTTON_ZL) ed.tool = (ed.tool + 7) % 8;
  if (trig & VPAD_BUTTON_ZR) ed.tool = (ed.tool + 1) % 8;
  if (trig & VPAD_BUTTON_LEFT) ed.color = (ed.color + 31) % 32;
  if (trig & VPAD_BUTTON_RIGHT) ed.color = (ed.color + 1) % 32;
  if (trig & VPAD_BUTTON_UP) ed.color = (ed.color + 24) % 32;
  if (trig & VPAD_BUTTON_DOWN) ed.color = (ed.color + 8) % 32;
  if ((trig & VPAD_BUTTON_MINUS) && ed.challenge) kb_open("What is it?", ed.title, sizeof ed.title, 40, 0, 0, ed_title_done);
  if (trig & VPAD_BUTTON_PLUS) ed_done();
  if (trig & VPAD_BUTTON_B) ed_cancel();
}
/* encode as the website: [~N]run,run,... with '~' alone meaning strength 2 */
static char *encode_drawing(const uint8_t *px, int wig) {
  static const char *B32 = "0123456789abcdefghijklmnopqrstuv";
  size_t cap = 64 * 1024, n = 0;
  char *o = malloc(cap);
  if (wig) { o[n++] = '~'; if (wig != 2) o[n++] = (char)('0' + wig); }
  size_t head = n;
  int i = 0;
  while (i < DPIX) {
    int c = px[i] & 31, j = i;
    while (j < DPIX && (px[j] & 31) == c) j++;
    int len = j - i;
    char num[8]; int k = 0;
    do { num[k++] = "0123456789abcdefghijklmnopqrstuvwxyz"[len % 36]; len /= 36; } while (len && k < 7);
    if (n + 12 > cap) { cap *= 2; o = realloc(o, cap); }
    if (n > head) o[n++] = ',';
    o[n++] = B32[c];
    while (k) o[n++] = num[--k];
    i = j;
  }
  o[n] = 0;
  return o;
}
#endif

/* ------------------------------------------------------------------ NEW POST editor: the website's openPostEditor() */
#if STAGE >= 2
static const uint32_t TXT_COLORS[12] = { 0x000000, 0xe5322d, 0xff8a00, 0xf2c200, 0x5ac800, 0x0aa86b, 0x1b8fd3, 0x0b4fa8, 0x7a3ce0, 0xe0409a, 0x8a8a8a, 0xffffff };
static const char *PFONT_N[NPOSTFONTS] = { "Normal", "Serif", "Script", "Condensed", "Typewriter", "Handwriting", "Code", "Pixel", "Fancy", "Poster", "Playfair", "Dancing",
  "Marker", "Righteous", "Orbitron", "Bangers", "Fredoka", "Abril", "Shadows", "Creepster", "Retro VT", "Satisfy", "Comfortaa", "Rock Salt", "Chewy", "Cinzel", "Indie", "Monoton", "GameCube" };
static const char *PDECO_N[4] = { "No frame", "White frame", "Glow", "Square" };
static const char *PSIZE_N[5] = { "Small", "Normal", "Big", "Bigger", "Huge" };
static const uint32_t PSW_THEME[8][2] = { { 0x0b74c9, 0x86d6ff }, { 0xffffff, 0xdef0fc }, { 0x3fae4a, 0xd9f7a8 }, { 0x3b9be0, 0x7cc4f5 }, { 0xff9a3c, 0xa04bd6 }, { 0x0a1f4d, 0x1b4a8a }, { 0xbfeaff, 0x6cc3f2 }, { 0xfff7c2, 0xffd24d } };

static void pe_snap(void) {
  if (pe.hpos < pe.nh - 1) pe.nh = pe.hpos + 1;
  if (pe.nh == 24) { memmove(pe.hist, pe.hist + 1, sizeof(PSnap) * 23); pe.nh--; }
  memcpy(pe.hist[pe.nh].text, pe.text, sizeof pe.text); memcpy(pe.hist[pe.nh].st, pe.st, sizeof pe.st); pe.hist[pe.nh].nst = pe.nst;
  pe.hpos = pe.nh++;
}
static void pe_hist(int d) {
  int np = pe.hpos + d; if (np < 0 || np >= pe.nh) return;
  pe.hpos = np; memcpy(pe.text, pe.hist[np].text, sizeof pe.text); memcpy(pe.st, pe.hist[np].st, sizeof pe.st); pe.nst = pe.hist[np].nst; pe.prevDirty = 1;
}
static void pe_append(const char *s) { size_t L = strlen(pe.text), n = strlen(s); if (L + n + 1 < sizeof pe.text) memcpy(pe.text + L, s, n + 1); pe.prevDirty = 1; }
static void pe_close_to(int idx) { while (pe.nst > idx) { pe.nst--; pe_append(pe.st[pe.nst].close); } }
static int pe_find(int kind) { for (int i = pe.nst - 1; i >= 0; i--) if (pe.st[i].kind == kind) return i; return -1; }
static void pe_toggle(int kind, const char *open, const char *close) {
  int f = pe_find(kind);
  if (f >= 0) {
    /* close everything above it, close it, reopen the ones above */
    PTag keep[12]; int nk = 0;
    for (int i = f + 1; i < pe.nst; i++) keep[nk++] = pe.st[i];
    pe_close_to(f);
    for (int i = 0; i < nk; i++) { pe_append(keep[i].open); pe.st[pe.nst++] = keep[i]; }
    if (open) { /* switch value (new colour / size / font) */
      pe_append(open); snprintf(pe.st[pe.nst].open, sizeof pe.st[0].open, "%s", open); snprintf(pe.st[pe.nst].close, sizeof pe.st[0].close, "%s", close); pe.st[pe.nst].kind = kind; pe.nst++;
    }
  } else if (open && pe.nst < 12) {
    pe_append(open); snprintf(pe.st[pe.nst].open, sizeof pe.st[0].open, "%s", open); snprintf(pe.st[pe.nst].close, sizeof pe.st[0].close, "%s", close); pe.st[pe.nst].kind = kind; pe.nst++;
  }
  pe_snap();
}
static void pe_line(const char *prefix) {
  size_t L = strlen(pe.text);
  if (L && pe.text[L - 1] != '\n') pe_append("\n");
  pe_append(prefix);
  pe_snap();
}
static char *pe_markup(void) {   /* text with all open tags closed */
  char *o = malloc(sizeof pe.text + 200); strcpy(o, pe.text);
  for (int i = pe.nst - 1; i >= 0; i--) strcat(o, pe.st[i].close);
  return o;
}
static void pe_kb_done(void) { pe.prevDirty = 1; pe_snap(); }
static void pe_open(Screen *ctx, int autoDraw) {
  memset(&pe, 0, sizeof pe);
  pe.open = 1; pe.size = 1; pe.col1 = 1; pe.col2 = 6; pe.tsize = 1;
  snprintf(pe.cid, sizeof pe.cid, "%s", ctx && ctx->kind == S_COMM ? ctx->cid : "");
  snprintf(pe.cname, sizeof pe.cname, "%s", ctx && ctx->kind == S_COMM ? ctx->cname : "");
  pe.wall = !pe.cid[0];
  pe.prevDirty = 1;
  pe_snap();
  if (autoDraw) ed_open(NULL, 0, 0, NULL);
}
static void pe_free_tex(void) { if (pe.prev) SDL_DestroyTexture(pe.prev); if (pe.drawTex) SDL_DestroyTexture(pe.drawTex); pe.prev = pe.drawTex = NULL; }
static void pe_close(void) { pe_free_tex(); free(pe.draw); pe.draw = NULL; pe.open = 0; }

/* layout */
#define PMX 20
#define PMY 8
#define PMW 1240
#define PMH 704
#define PROW_Y(i) (PMY + 60 + (i) * 54)
#define PLAB_W 104
typedef struct { int x, y, w, h; } PBox;
static PBox pe_btn(int row, int idx) {   /* generic toolbar buttons laid out left to right */
  static int widths[5][24];
  static int init;
  if (!init) {
    init = 1;
    for (int i = 0; i < 12; i++) widths[0][i] = 46;                       /* Format: undo redo B I U S H quote list rule clear */
    widths[1][0] = 230; widths[1][1] = 170; for (int i = 0; i < 12; i++) widths[1][2 + i] = 36;   /* Text: font, size, 12 colours */
    widths[2][0] = 40; widths[2][1] = 40; widths[2][2] = 46; widths[2][3] = 46; widths[2][4] = 46; /* Effects */
    for (int i = 0; i < 20; i++) widths[3][i] = 36;                       /* Post backgrounds */
    widths[4][0] = 230; widths[4][1] = 50; widths[4][2] = 50; widths[4][3] = 50; widths[4][4] = 46; widths[4][5] = 46; widths[4][6] = 46; widths[4][7] = 230;   /* Layout */
  }
  int x = PMX + 24 + PLAB_W;
  for (int i = 0; i < idx; i++) x += widths[row][i] + 8;
  PBox b = { x, PROW_Y(row) + (widths[row][idx] == 36 ? 6 : 2), widths[row][idx], widths[row][idx] == 36 ? 36 : 44 };
  return b;
}
static PBox pe_area(void) { PBox b = { PMX + 24, PROW_Y(5) + 4, PMW - 48 - (pe.draw ? 300 : 0), PMY + PMH - 72 - (PROW_Y(5) + 4) - 12 }; return b; }
static PBox pe_foot(int i) { PBox b; b.y = PMY + PMH - 62; b.h = 48; if (i == 0) { b.x = PMX + 18; b.w = 120; } else if (i == 1) { b.w = 160; b.x = PMX + PMW / 2 - 80; } else { b.w = 120; b.x = PMX + PMW - 18 - 120; } return b; }

static void pe_tb(PBox b, const char *txt, TTF_Font *f, uint32_t fg, int pressed) {
  /* .fmt toolbar button: white, light border, bottom edge */
  fillr(b.x, b.y, b.w, b.h, pressed ? 0x5ac800 : 0xb4c9d4);
  fillr(b.x + 1, b.y + 1, b.w - 2, b.h - 4, pressed ? 0xe8f9d6 : 0xffffff);
  if (txt) { Tex *t = label(f ? f : F_NAME, fg, txt); draw_tex(t, b.x + (b.w - t->w) / 2, b.y + (b.h - 3 - t->h) / 2); }
}
static void pe_swatch(PBox b, uint32_t col, int round, int sel, uint32_t col2) {
  if (sel) fillr(b.x - 3, b.y - 3, b.w + 6, b.h + 6, C_GREEN);
  if (round) {
    static SDL_Texture *disc; if (!disc) { Cv c = cv_new(36, 36); cv_circle(&c, 18, 18, 17, 0xffffffff); disc = cv_tex(&c); cv_free(&c); }
    SDL_Rect d = { b.x, b.y, b.w, b.h };
    SDL_SetTextureColorMod(disc, 0x1a, 0x6f, 0xb0); SDL_RenderCopy(R, disc, NULL, &d);
    SDL_Rect d2 = { b.x + 1, b.y + 1, b.w - 2, b.h / 2 - 1 };
    SDL_Rect s2 = { 1, 1, 34, 17 };
    SDL_SetTextureColorMod(disc, (col >> 16) & 255, (col >> 8) & 255, col & 255); SDL_RenderCopy(R, disc, &s2, &d2);
    SDL_Rect d3 = { b.x + 1, b.y + b.h / 2, b.w - 2, b.h / 2 - 1 }, s3 = { 1, 18, 34, 17 };
    SDL_SetTextureColorMod(disc, (col2 >> 16) & 255, (col2 >> 8) & 255, col2 & 255); SDL_RenderCopy(R, disc, &s3, &d3);
    SDL_SetTextureColorMod(disc, 255, 255, 255);
  } else { fillr(b.x, b.y, b.w, b.h, 0xb4c9d4); fillr(b.x + 1, b.y + 1, b.w - 2, b.h - 2, col); }
}
static char *pe_style_code(char *o, size_t n) {
  if (pe.align || pe.deco) snprintf(o, n, "%d.%d.%d.%d%d", pe.bg, pe.font, pe.size, pe.align, pe.deco); else snprintf(o, n, "%d.%d.%d", pe.bg, pe.font, pe.size);
  return o;
}
static void pe_draw(void) {
  SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(R, 0, 0, 0, 140); SDL_Rect all = { 0, 0, SW, SH }; SDL_RenderFillRect(R, &all);
  fillr(PMX - 1, PMY - 1, PMW + 2, PMH + 2, 0x888888);
  fillr(PMX, PMY, PMW, PMH, 0xffffff);
  { TTF_Font *pf = font_get(7, 17, 0); Tex t = mk_text(pf ? pf : F_NAME, "NEW POST", 0x323232, 0); draw_tex(&t, PMX + 20, PMY + 16); tex_free(&t); }
  char cnt[32]; snprintf(cnt, sizeof cnt, "%d / 1000", utf8_len(pe.text)); draw_label(F_SMALL, C_SOFT, cnt, PMX + PMW - 20, PMY + 16, 2);
  fillr(PMX, PMY + 50, PMW, 1, C_LINE);
  static const char *ROWS[5] = { "Format", "Text", "Effects", "Post", "Layout" };
  for (int r = 0; r < 5; r++) draw_label(F_NAME, 0x555555, ROWS[r], PMX + 24, PROW_Y(r) + 12, 0);
  /* Format */
  static const char *FT[11] = { "<", ">", "B", "I", "U", "S", "H", "\"", "\xE2\x80\xA2-", "\xE2\x80\x94", "Tx" };
  for (int i = 0; i < 11; i++) {
    int pressed = (i == 2 && pe_find(1) >= 0) || (i == 3 && pe_find(2) >= 0) || (i == 4 && pe_find(3) >= 0) || (i == 5 && pe_find(4) >= 0);
    TTF_Font *f = F_NAME; const char *t = FT[i];
    if (i <= 1 || i >= 7) { f = font_get(0, 22, 1); }
    PBox b = pe_btn(0, i);
    pe_tb(b, (i == 3) ? NULL : t, f, 0x222222, pressed);
    if (i == 3) { TTF_Font *it = font_get(0, 22, 1); TTF_SetFontStyle(it, TTF_STYLE_ITALIC); Tex x = mk_text(it, "I", 0x222222, 0); TTF_SetFontStyle(it, TTF_STYLE_NORMAL); draw_tex(&x, b.x + (b.w - x.w) / 2, b.y + 8); tex_free(&x); }
    if (i == 4) fillr(b.x + 16, b.y + 31, 14, 2, 0x222222);
    if (i == 5) fillr(b.x + 14, b.y + 20, 18, 2, 0x222222);
  }
  /* Text: inline font + size + colours */
  { char t[64]; snprintf(t, sizeof t, "%s", pe.tfont ? PFONT_N[pe.tfont] : "Font"); pe_tb(pe_btn(1, 0), t, F_BODYS, 0x222222, pe_find(7) >= 0); }
  { char t[64]; snprintf(t, sizeof t, "%s", pe.tsize == 1 ? "Size" : PSIZE_N[pe.tsize]); pe_tb(pe_btn(1, 1), t, F_BODYS, 0x222222, pe_find(6) >= 0); }
  int curCol = -1; { int f = pe_find(5); if (f >= 0) { for (int k = 0; k < 12; k++) { char h2[24]; snprintf(h2, sizeof h2, "[color=#%06x]", TXT_COLORS[k]); if (!strcmp(pe.st[f].open, h2)) curCol = k; } } }
  for (int i = 0; i < 12; i++) pe_swatch(pe_btn(1, 2 + i), TXT_COLORS[i], 0, curCol == i, 0);
  /* Effects: two pickers + colour / gradient / glow */
  pe_swatch(pe_btn(2, 0), TXT_COLORS[pe.col1], 0, 0, 0);
  pe_swatch(pe_btn(2, 1), TXT_COLORS[pe.col2], 0, 0, 0);
  { PBox b = pe_btn(2, 2); pe_tb(b, NULL, NULL, 0, pe_find(5) >= 0 && curCol < 0); draw_label(F_NAME, TXT_COLORS[pe.col1], "A", b.x + b.w / 2, b.y + 9, 1); }
  { PBox b = pe_btn(2, 3); pe_tb(b, NULL, NULL, 0, pe_find(8) >= 0); Tex *a = label(F_NAME, 0xffffff, "A"); SDL_SetTextureColorMod(a->t, 0x00, 0xa2, 0xff); draw_tex(a, b.x + b.w / 2 - a->w / 2 - 1, b.y + 9); SDL_SetTextureColorMod(a->t, 255, 255, 255); }
  { PBox b = pe_btn(2, 4); pe_tb(b, NULL, NULL, 0, pe_find(9) >= 0); fillr(b.x + 12, b.y + 8, b.w - 24, 26, 0x0b74c9); draw_label(F_NAME, 0xffffff, "A", b.x + b.w / 2, b.y + 9, 1); }
  draw_label(F_SMALL, C_SOFT, "Tap a picker to change it. Effects apply to what you type next.", pe_btn(2, 4).x + 66, PROW_Y(2) + 14, 0);
  /* Post backgrounds */
  for (int i = 0; i < 20; i++) {
    PBox b = pe_btn(3, i);
    if (i == 0) { pe_swatch(b, 0xffffff, 0, pe.bg == 0, 0); draw_label(F_NAME, 0x222222, "\xC3\x97", b.x + b.w / 2, b.y + 5, 1); }
    else if (i < 12) pe_swatch(b, PC_BG[i], 0, pe.bg == i, 0);
    else pe_swatch(b, PSW_THEME[i - 12][0], 1, pe.bg == i, PSW_THEME[i - 12][1]);
  }
  /* Layout */
  { char t[64]; snprintf(t, sizeof t, "%s", PFONT_N[pe.font]); PBox b = pe_btn(4, 0); pe_tb(b, NULL, NULL, 0, 0); TTF_Font *f = font_get(pe.font, 20, 0); Tex x = mk_text(f ? f : F_BODYS, t, 0x222222, 0); draw_tex_clip(&x, b.x + 12, b.y + (b.h - 3 - x.h) / 2, b.h); tex_free(&x); }
  pe_tb(pe_btn(4, 1), "A-", F_NAME, 0x222222, pe.size == 0);
  pe_tb(pe_btn(4, 2), "A", F_NAME, 0x222222, pe.size == 1);
  pe_tb(pe_btn(4, 3), "A+", F_NAME, 0x222222, pe.size == 2);
  for (int a = 0; a < 3; a++) {
    PBox b = pe_btn(4, 4 + a); pe_tb(b, NULL, NULL, 0, pe.align == a);
    for (int l = 0; l < 3; l++) { int w = l == 1 ? 14 : 22; int x = a == 0 ? b.x + 12 : a == 1 ? b.x + (b.w - w) / 2 : b.x + b.w - 12 - w; fillr(x, b.y + 11 + l * 7, w, 3, 0x222222); }
  }
  pe_tb(pe_btn(4, 7), PDECO_N[pe.deco], F_BODYS, 0x222222, 0);
  /* text area with live preview */
  PBox a = pe_area();
  fillr(a.x, a.y, a.w, a.h, C_GREEN); fillr(a.x + 2, a.y + 2, a.w - 4, a.h - 4, 0xffffff);
  if (pe.prevDirty) {
    if (pe.prev) SDL_DestroyTexture(pe.prev); pe.prev = NULL;
    char *m = pe_markup(); char sc[24]; int clipped = 0;
    if (m[0]) pe.prev = rich_render(m, pe_style_code(sc, sizeof sc), a.w - 40, S14(16), C_INK, a.h - 24, &pe.prevW, &pe.prevH, &clipped);
    free(m); pe.prevDirty = 0;
  }
  if (pe.prev) { SDL_Rect d = { a.x + 20 - 4, a.y + 12 - 4, pe.prevW, pe.prevH }; SDL_RenderSetClipRect(R, &(SDL_Rect){ a.x + 2, a.y + 2, a.w - 4, a.h - 4 }); SDL_RenderCopy(R, pe.prev, NULL, &d); SDL_RenderSetClipRect(R, NULL); }
  else draw_label(F_BODY, 0x9aa5ab, "Tap here to write...", a.x + 20, a.y + 14, 0);
  if (pe.draw) {
    if (!pe.drawTex) { uint32_t pal[32]; for (int i = 0; i < 32; i++) pal[i] = PAL[i]; pe.drawTex = drawing_tex(pe.draw, pal, pe.drawWig, -1); }
    SDL_Rect d = { a.x + a.w + 16, a.y, 284, 160 }; fillr(d.x - 1, d.y - 1, d.w + 2, d.h + 2, 0xcfe6f1); SDL_RenderCopy(R, pe.drawTex, NULL, &d);
    draw_btn(a.x + a.w + 16, a.y + 172, 140, 38, "Remove", 1, 0);
  }
  /* foot */
  fillr(PMX, PMY + PMH - 72, PMW, 1, C_LINE);
  PBox f0 = pe_foot(0), f1 = pe_foot(1), f2 = pe_foot(2);
  draw_btn(f0.x, f0.y, f0.w, f0.h, "Close", 1, 0);
  draw_btn(f1.x, f1.y, f1.w, f1.h, pe.draw ? "Edit drawing" : "Draw", 1, 0);
  draw_btn(f2.x, f2.y, f2.w, f2.h, pe.busy ? "..." : "Post", 0, 0);
}
static void pe_post_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)serial; (void)extra;
  pe.busy = 0;
  if (code != 200) { char e[200]; api_error(code, body, err, e, sizeof e); toast(e); return; }
  toast("Posted!");
  pe_close();
  Screen *c = cur(); if (c) load_screen(c, 0);
  if (roots[0] && roots[0] != c) clear_items(roots[0], 0);
}
static void pe_post(void) {
  if (pe.busy) return;
  char *m = pe_markup();
  char *t = m; while (*t == ' ' || *t == '\n') t++;
  if (!*t && !pe.draw) { toast("Write something or draw something first."); free(m); return; }
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "body", t);
  char sc[24]; cJSON_AddStringToObject(o, "style", pe_style_code(sc, sizeof sc));
  if (pe.draw) { char *d = encode_drawing(pe.draw, pe.drawWig); cJSON_AddStringToObject(o, "drawing", d); free(d); }
  if (pe.cid[0]) cJSON_AddStringToObject(o, "community", pe.cid); else cJSON_AddTrueToObject(o, "wall");
  char *b = cJSON_PrintUnformatted(o);
  net_send("POST", "/api/posts", b, pe_post_cb, 0, 0);
  free(b); cJSON_Delete(o); free(m);
  pe.busy = 1;
}
static void pe_touch(int tx, int ty) {
  for (int i = 0; i < 11; i++) {
    PBox b = pe_btn(0, i); if (!inr(tx, ty, b.x, b.y, b.w, b.h)) continue;
    switch (i) {
    case 0: pe_hist(-1); break; case 1: pe_hist(1); break;
    case 2: pe_toggle(1, pe_find(1) >= 0 ? NULL : "**", "**"); break;
    case 3: pe_toggle(2, pe_find(2) >= 0 ? NULL : "[i]", "[/i]"); break;
    case 4: pe_toggle(3, pe_find(3) >= 0 ? NULL : "__", "__"); break;
    case 5: pe_toggle(4, pe_find(4) >= 0 ? NULL : "~~", "~~"); break;
    case 6: pe_line("# "); break; case 7: pe_line("> "); break; case 8: pe_line("- "); break;
    case 9: pe_line("---\n"); break;
    case 10: pe_close_to(0); pe.tfont = 0; pe.tsize = 1; pe_snap(); break;
    }
    pe.prevDirty = 1; return;
  }
  { PBox b = pe_btn(1, 0); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { pe.tfont = (pe.tfont + 1) % NPOSTFONTS; char o[24]; snprintf(o, sizeof o, "[font=%d]", pe.tfont); pe_toggle(7, pe.tfont ? o : NULL, "[/font]"); return; } }
  { PBox b = pe_btn(1, 1); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { pe.tsize = (pe.tsize + 1) % 5; char o[24]; snprintf(o, sizeof o, "[size=%d]", pe.tsize); pe_toggle(6, pe.tsize != 1 ? o : NULL, "[/size]"); return; } }
  for (int i = 0; i < 12; i++) { PBox b = pe_btn(1, 2 + i); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { char o[24]; snprintf(o, sizeof o, "[color=#%06x]", TXT_COLORS[i]); int f = pe_find(5); pe_toggle(5, (f >= 0 && !strcmp(pe.st[f].open, o)) ? NULL : o, "[/color]"); return; } }
  { PBox b = pe_btn(2, 0); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { pe.col1 = (pe.col1 + 1) % 12; return; } }
  { PBox b = pe_btn(2, 1); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { pe.col2 = (pe.col2 + 1) % 12; return; } }
  { PBox b = pe_btn(2, 2); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { char o[24]; snprintf(o, sizeof o, "[color=#%06x]", TXT_COLORS[pe.col1]); pe_toggle(5, pe_find(5) >= 0 ? NULL : o, "[/color]"); return; } }
  { PBox b = pe_btn(2, 3); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { char o[32]; snprintf(o, sizeof o, "[grad=#%06x,#%06x]", TXT_COLORS[pe.col1], TXT_COLORS[pe.col2]); pe_toggle(8, pe_find(8) >= 0 ? NULL : o, "[/grad]"); return; } }
  { PBox b = pe_btn(2, 4); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { char o[24]; snprintf(o, sizeof o, "[glow=#%06x]", TXT_COLORS[pe.col2]); pe_toggle(9, pe_find(9) >= 0 ? NULL : o, "[/glow]"); return; } }
  for (int i = 0; i < 20; i++) { PBox b = pe_btn(3, i); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { pe.bg = i; pe.prevDirty = 1; return; } }
  { PBox b = pe_btn(4, 0); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { pe.font = (pe.font + 1) % NPOSTFONTS; pe.prevDirty = 1; return; } }
  for (int i = 0; i < 3; i++) { PBox b = pe_btn(4, 1 + i); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { pe.size = i; pe.prevDirty = 1; return; } }
  for (int i = 0; i < 3; i++) { PBox b = pe_btn(4, 4 + i); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { pe.align = i; pe.prevDirty = 1; return; } }
  { PBox b = pe_btn(4, 7); if (inr(tx, ty, b.x, b.y, b.w, b.h)) { pe.deco = (pe.deco + 1) % 4; pe.prevDirty = 1; return; } }
  PBox a = pe_area();
  if (inr(tx, ty, a.x, a.y, a.w, a.h)) { kb_open("Write a post", pe.text, 1000, 1000, 0, 1, pe_kb_done); return; }
  if (pe.draw && inr(tx, ty, a.x + a.w + 16, a.y + 172, 140, 38)) { free(pe.draw); pe.draw = NULL; if (pe.drawTex) SDL_DestroyTexture(pe.drawTex); pe.drawTex = NULL; return; }
  PBox f0 = pe_foot(0), f1 = pe_foot(1), f2 = pe_foot(2);
  if (inr(tx, ty, f0.x, f0.y, f0.w, f0.h)) { pe_close(); return; }
  if (inr(tx, ty, f1.x, f1.y, f1.w, f1.h)) { ed_open(NULL, 0, 0, pe.draw); if (pe.draw && pe.drawWig) { ed.wig = 1; ed.strength = pe.drawWig; } return; }
  if (inr(tx, ty, f2.x, f2.y, f2.w, f2.h)) { pe_post(); return; }
}
static void pe_buttons(uint32_t trig) {
  if (trig & VPAD_BUTTON_B) pe_close();
  if (trig & VPAD_BUTTON_PLUS) pe_post();
  if (trig & VPAD_BUTTON_A) kb_open("Write a post", pe.text, 1000, 1000, 0, 1, pe_kb_done);
  if (trig & VPAD_BUTTON_Y) pe_hist(-1);
}
#endif

/* ------------------------------------------------------------------ compose / login screens (stage 2+) */
#if STAGE >= 2
static Screen *kbScreen;
static void compose_kb_done(void) { if (kbScreen && by_serial(kbScreen->serial)) rebuild_compose(kbScreen); }

static void rebuild_compose(Screen *s) {
  clear_items(s, 0);
  if (s->kind == S_LOGIN) {
    /* website login card: tabs Sign in / Create account, fields, big green button */
    Item *t = add_item(s, K_LTABS); build_item(t); t->sel_ok = 0;
    Item *u = add_item(s, K_FIELD); u->act = 1; u->title = xstrdup("Username"); u->body = xstrdup(s->text2); build_item(u);
    if (s->mode) { Item *d = add_item(s, K_FIELD); d->act = 3; d->title = xstrdup("Display name (optional)"); d->body = xstrdup(s->cname); build_item(d); }
    char m[110]; int n = utf8_len(s->pass); if (n > 100) n = 100; memset(m, '*', (size_t)n); m[n] = 0;
    Item *p = add_item(s, K_FIELD); p->act = 2; p->title = xstrdup(s->mode ? "Password (8 or more characters)" : "Password"); p->body = xstrdup(m); build_item(p);
    if (s->mode) { Item *i = add_item(s, K_INFO); i->body = xstrdup("By creating an account you accept the Aeroverse rules: no piracy, no console modding help, no swearing in All ages, no NSFW or gore, be kind."); build_item(i); }
    Item *b = add_item(s, K_ACTION); b->act = A_SEND; b->title = xstrdup(s->loading ? "..." : s->mode ? "Create account" : "Sign in"); build_item(b);
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
    if (s->draw) d->drawing = encode_drawing(s->draw, 3);
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
    if (s->mode) { cJSON_AddStringToObject(o, "display", s->cname[0] ? s->cname : s->text2); cJSON_AddTrueToObject(o, "rules"); }
    char *b = cJSON_PrintUnformatted(o);
    net_send("POST", s->mode ? "/api/register" : "/api/login", b, sent_cb, s->serial, 0);
    free(b);
  } else if (s->ckind == 2) {
    if (!s->draw) { toast("Draw something first!"); cJSON_Delete(o); return; }
    if (utf8_len(s->text2) < 2) { toast("Write what you drew (at least 2 letters)."); cJSON_Delete(o); return; }
    char *d = encode_drawing(s->draw, 3);
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
  if (it->mine) sfx(2);
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
static int post_wrap_top, post_wrap_bot;   /* S_POST: one card around the post and its replies, like the website */
static void layout_list(Item *it, int n, int x0, int y, int *outY) {
  int col = 0, rowH = 0, gx = x0;
  for (int i = 0; i < n; i++) {
    Item *t = &it[i];
    if (t->kind == K_WIG || t->kind == K_DCELL) {
      int gap = 12;
      t->x = gx; t->y = y; if (t->h > rowH) rowH = t->h;
      gx += t->w + gap;
      if (++col == 3) { col = 0; y += rowH + 12; rowH = 0; gx = x0; }
    } else {
      if (col) { col = 0; y += rowH + 12; rowH = 0; gx = x0; }
      t->x = x0 + (t->kind == K_HERO ? FULLX - x0 : 0);
      t->y = y; y += t->h + (t->kind == K_SEC ? 8 : 14);
    }
  }
  if (col) y += rowH + 12;
  *outY = y;
}
static void layout(Screen *s) {
  int y = TOPH + 20, y2;
  int x0 = colx(s);
  /* the hero spans both columns; the sidebar starts below it */
  int start = 0;
  if (s->n && s->it[0].kind == K_HERO) { s->it[0].x = FULLX; s->it[0].y = y; y += s->it[0].h + 18; start = 1; }
  int sideY = y;
  if (s->kind == S_POST) {
    y += 0;
    int top = y;
    for (int i = 0; i < s->n; i++) {
      Item *t = &s->it[i];
      t->y = y + (i == 0 ? 0 : 0);
      if (t->kind == K_FULLPOST) { t->x = CX; y += t->h - PC_PAD; }
      else { t->x = CX + PC_BUBX; y += t->h + 10; }
    }
    post_wrap_top = top; post_wrap_bot = y + PC_PAD;
    y = post_wrap_bot + 20;
  } else layout_list(s->it + start, s->n - start, x0, y, &y);
  layout_list(s->side, s->nside, SIDEX, sideY, &y2);
  if (y2 > y) y = y2;
  s->contentH = y + 50;
}

static void open_post_id(int id) {
  Screen *p = new_screen(S_POST);
  p->postId = id; p->needPost = 1;
  push(p);
  load_screen(p, 0);
}
static void open_item(Screen *s, Item *it) {
  if (it->kind == K_FEAT) { open_post_id(it->id); return; }
  if (it->kind == K_CNEW) { toast("New communities are sent in on the website."); return; }
  if (it->kind == K_PROMPT) {
#if STAGE >= 2
    if (!me.id) { open_login(); return; }
    pe_open(s, 0);
#else
    toast("Signing in comes in the next update.");
#endif
    return;
  }
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
    f->w = CW; f->id = it->id; f->yeahs = it->yeahs; f->replies = it->replies; f->mine = it->mine; f->created = it->created; f->lastPost = it->lastPost;
    f->name = xstrdup(it->name); f->user = xstrdup(it->user); f->cname = xstrdup(it->cname); f->cid = xstrdup(it->cid);
    f->body = xstrdup(it->body); f->drawing = xstrdup(it->drawing); f->style = xstrdup(it->style);
    build_item(f);
    push(p);
    load_screen(p, 0);
  } else if (it->kind == K_COMM || it->kind == K_BANNER) {
    Screen *c = new_screen(S_COMM);
    snprintf(c->cid, sizeof c->cid, "%s", it->cid);
    snprintf(c->cname, sizeof c->cname, "%s", it->name);
    snprintf(c->text, sizeof c->text, "%s", it->body ? it->body : "");
    snprintf(c->text2, sizeof c->text2, "%s", it->age ? it->age : "all");
    c->parent = calloc(1, sizeof(Item)); c->parent->msg = xstrdup(it->msg); c->parent->posts = it->posts; c->parent->avatar = xstrdup(it->avatar);
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
    else if (it->act == A_WIG_DRAW) ed_open(s, 1, s->mode, NULL); else if (it->act == A_MODE) {
      s->mode = !s->mode;
      load_screen(s, 0);
    }
#endif
    else if (it->act == A_SEND) compose_send(s);
  } else if (it->kind == K_FIELD) {
    kbScreen = s;
    if (s->kind == S_LOGIN) {
      if (it->act == 1) kb_open("Username", s->text2, sizeof s->text2, 20, 0, 0, compose_kb_done);
      else if (it->act == 3) kb_open("Display name", s->cname, 25, 24, 0, 0, compose_kb_done);
      else kb_open("Password", s->pass, sizeof s->pass, 100, 1, 0, compose_kb_done);
    } else if (s->ckind == 2) kb_open("What did you draw?", s->text2, sizeof s->text2, 40, 0, 0, compose_kb_done);
    else kb_open(s->ckind == 0 ? "Write a post" : "Write a reply", s->text, sizeof s->text, s->ckind == 0 ? 1000 : 400, 0, 1, compose_kb_done);
  } else if (it->kind == K_DRAWF) {
    ed_open(s, 0, 0, s->draw);
    if (s->draw && s->drawWig) { ed.wig = 1; ed.strength = s->drawWig; }
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

/* ------------------------------------------------------------------ styles (Frutiger Aero, from app.css) */
static Nine NCARD, NBUB, NFOCUS, NFOCUS_S, NHEAD;
static Pill PGREEN, PGREEN_S, PALT, PALT_S, PRED, PYEAH, PYEAHON, PCHIP, PCHIP_ON;
static SDL_Texture *bgTex, *logoTex, *headTex, *icoSmile, *icoPencil, *icoTree, *icoGames, *icoDoc, *icoPlus, *icoNote;
static void make_styles(void) {
  Stop card[2] = { { 0, 0xeeffffff }, { 1, 0xe6f4fcff } };
  NCARD = nine_make(S14(14), 1, 0xffffffff, card, 2, 6, 20, 0x33146ea0);
  Stop bub[1] = { { 0, 0xfff3fbff } };
  NBUB = nine_make(S14(10), 1, 0xffcfe6f1, bub, 1, 0, 0, 0);
  Stop none[1] = { { 0, 0x00ffffff } };
  NFOCUS = nine_make(S14(14) + 5, 4, 0xff1b8fd3, none, 1, 0, 0, 0);
  NFOCUS_S = nine_make(10, 3, 0xff1b8fd3, none, 1, 0, 0, 0);
  Stop gl[4] = { { 0, 0xffa6ee5c }, { .5f, 0xff5ac800 }, { .51f, 0xff46b000 }, { 1, 0xff7ad82c } };
  PGREEN = pill_make(48, 24, 0xff3f9a00, 0, gl, 4, 0xccffffff, 3, 0x4d005000);
  PGREEN_S = pill_make(38, 19, 0xff3f9a00, 0, gl, 4, 0xccffffff, 3, 0x4d005000);
  Stop al[2] = { { 0, 0xffffffff }, { 1, 0xffdff2fb } };
  PALT = pill_make(48, 24, 0xff9ccbe3, 0, al, 2, 0xffffffff, 0, 0);
  PALT_S = pill_make(38, 19, 0xff9ccbe3, 0, al, 2, 0xffffffff, 0, 0);
  Stop rd[4] = { { 0, 0xffff8c87 }, { .5f, 0xffe5322d }, { .51f, 0xffcf2420 }, { 1, 0xffef5a55 } };
  PRED = pill_make(48, 24, 0xffa81f1b, 0, rd, 4, 0x99ffffff, 3, 0x4d500000);
  Stop ye[2] = { { 0, 0xfffffbe8 }, { 1, 0xffffeaa0 } };
  PYEAH = pill_make(44, 20, 0xfff0cf73, 3, ye, 2, 0, 0, 0);
  Stop yo[1] = { { 0, 0xffffd54a } };
  PYEAHON = pill_make(44, 20, 0xffd9a400, 3, yo, 1, 0, 0, 0);
  Stop ch[2] = { { 0, 0xffffffff }, { 1, 0xffe8f6fd } };
  PCHIP = pill_make(42, 21, 0xffc9d6dd, 0, ch, 2, 0, 0, 0);
  PCHIP_ON = pill_make(42, 21, 0xff3f9a00, 0, gl, 4, 0, 0, 0);
  /* small icons drawn like the website's svg icons */
  Cv c = cv_new(30, 30);
  cv_circle(&c, 15, 15, 12, 0xff9a6a00); cv_circle(&c, 15, 15, 9.6f, 0x00000000); 
  memset(c.p, 0, 30 * 30 * 4);
  for (int y = 0; y < 30; y++) for (int x = 0; x < 30; x++) { float d = sqrtf((x + .5f - 15) * (x + .5f - 15) + (y + .5f - 15) * (y + .5f - 15)); float a = 1.2f - fabsf(d - 11.5f); if (a > 0) cv_blend(&c, x, y, 0xff9a6a00, a > 1 ? 1 : a); }
  cv_circle(&c, 11, 12, 1.8f, 0xff9a6a00); cv_circle(&c, 19, 12, 1.8f, 0xff9a6a00);
  for (int x = 9; x <= 21; x++) { float t = (x - 15) / 6.0f; float yy = 18 + 2.6f * (1 - t * t); cv_blend(&c, x, (int)yy, 0xff9a6a00, 1); cv_blend(&c, x, (int)yy + 1, 0xff9a6a00, .6f); }
  icoSmile = cv_tex(&c); cv_free(&c);
  c = cv_new(30, 30); cv_line(&c, 6, 24, 22, 8, 5, 0xfff09a1a); cv_line(&c, 6, 24, 22, 8, 5, 0xfff09a1a); cv_line(&c, 6, 24, 22, 8, 5, 0xfff09a1a); icoPencil = cv_tex(&c); cv_free(&c);
  c = cv_new(30, 30);
  { float tr[] = { 15, 3, 6, 16, 10, 16, 6, 24, 24, 24, 20, 16, 24, 16 }; cv_poly(&c, tr, 7, 0xff145d86); cv_poly(&c, tr, 7, 0xff145d86); }
  for (int y = 24; y < 29; y++) for (int x = 14; x < 17; x++) cv_blend(&c, x, y, 0xff145d86, 1);
  icoTree = cv_tex(&c); cv_free(&c);
  c = cv_new(30, 30); cv_rrect1(&c, 3, 8, 24, 14, 6, 0xff145d86); cv_rrect1(&c, 8, 12, 2, 6, 0, 0xffffffff); cv_rrect1(&c, 6, 14, 6, 2, 0, 0xffffffff); cv_circle(&c, 20, 13, 1.5f, 0xffffffff); cv_circle(&c, 23, 16, 1.5f, 0xffffffff); icoGames = cv_tex(&c); cv_free(&c);
  c = cv_new(26, 30); { float d[] = { 3, 2, 16, 2, 23, 9, 23, 28, 3, 28 }; cv_poly(&c, d, 5, 0xff6b6b6b); cv_poly(&c, d, 5, 0xff6b6b6b); } icoDoc = cv_tex(&c); cv_free(&c);
  c = cv_new(30, 30); cv_circle(&c, 9, 23, 5, 0xff6b6b6b); cv_circle(&c, 22, 20, 5, 0xff6b6b6b);
  cv_rrect1(&c, 12, 5, 2.5f, 18, 0, 0xff6b6b6b); cv_rrect1(&c, 25, 3, 2.5f, 17, 0, 0xff6b6b6b);
  { float bm[] = { 12, 5, 27.5f, 2, 27.5f, 7, 12, 10 }; cv_poly(&c, bm, 4, 0xff6b6b6b); cv_poly(&c, bm, 4, 0xff6b6b6b); }
  icoNote = cv_tex(&c); cv_free(&c);
  c = cv_new(40, 40); cv_rrect1(&c, 2, 2, 36, 36, 4, 0xff9a9a9a); cv_rrect1(&c, 3, 3, 34, 34, 3, 0xffffffff); cv_rrect1(&c, 19, 10, 2.5f, 20, 0, 0xff555555); cv_rrect1(&c, 10, 19, 20, 2.5f, 0, 0xff555555); icoPlus = cv_tex(&c); cv_free(&c);
  /* header bar: white gloss + shadow */
  c = cv_new(4, TOPH + 14);
  for (int y = 0; y < TOPH; y++) { uint32_t col = lerp_col(0xeeffffff, 0xddeaf9ff, (float)y / TOPH); for (int x = 0; x < 4; x++) cv_blend(&c, x, y, col | 0xff000000u, 1); }
  for (int x = 0; x < 4; x++) cv_blend(&c, x, TOPH - 1, 0xaaffffff, 1);
  for (int y = 0; y < 14; y++) { float a = (1 - y / 14.0f); for (int x = 0; x < 4; x++) cv_blend(&c, x, TOPH + y, 0x40147aaa, a * a); }
  headTex = cv_tex(&c); cv_free(&c);
}

/* sky background of body.aero: linear-gradient(#5cc4f2 0, #aee8fb 380px, #e4f8ff 760px, #f3fff0 100%) */
static void make_bg(void) {
  Cv c = cv_new(4, SH);
  Stop st[3] = { { 0, 0xff5cc4f2 }, { 380.0f / SH, 0xffaee8fb }, { 760.0f / SH, 0xffe4f8ff } };
  for (int y = 0; y < SH; y++) { uint32_t col = grad_at(st, 3, (float)y / SH); for (int x = 0; x < 4; x++) cv_blend(&c, x, y, col, 1); }
  bgTex = cv_tex(&c); cv_free(&c);
}
static void make_logo(void) {
  SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom((void *)LOGO_RGBA, LOGO_W, LOGO_H, 32, LOGO_W * 4, SDL_PIXELFORMAT_RGBA32);
  if (!s) return;
  logoTex = SDL_CreateTextureFromSurface(R, s);
  if (logoTex) { SDL_SetTextureBlendMode(logoTex, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(logoTex, SDL_ScaleModeLinear); }
  SDL_FreeSurface(s);
}

static void card(int x, int y, int w, int h) { nine_draw(&NCARD, x, y, w, h); }
static void focus_ring(int x, int y, int w, int h) { nine_draw(&NFOCUS, x - 5, y - 5, w + 10, h + 10); }

/* buttons: style 0 green gloss, 1 white alt, 2 yeah, 3 yeah on, 4 red */
static void draw_btn(int x, int y, int w, int h, const char *txt, int style, int focus) {
  int small = h <= 40;
  const Pill *p = style == 1 ? (small ? &PALT_S : &PALT) : style == 2 ? &PYEAH : style == 3 ? &PYEAHON : style == 4 ? &PRED : (small ? &PGREEN_S : &PGREEN);
  int ph = style == 2 || style == 3 ? 44 : small ? 38 : 48;
  int yy = y + (h - ph) / 2;
  if (focus) nine_draw(&NFOCUS_S, x - 6, yy - 6, w + 12, ph + 12);
  pill_draw(p, x, yy, w);
  uint32_t tc = style == 1 ? C_SEC2 : style == 2 ? C_YEAH_T : style == 3 ? C_YEAH_ON_T : C_WHITE;
  TTF_Font *f = small ? F_NAME : F_BOLD;
  int icon = (style == 2 || style == 3) ? 30 : 0;
  Tex *t = label(f, tc, txt);
  if (!t->t) return;
  int tx = x + (w - t->w - (icon ? icon + 6 : 0)) / 2, ty = yy + (ph - (style >= 2 && style <= 3 ? 3 : 0) - t->h) / 2;
  if (icon) { SDL_Rect d = { tx, yy + (ph - 3 - 30) / 2, 30, 30 }; SDL_SetTextureColorMod(icoSmile, (tc >> 16) & 255, (tc >> 8) & 255, tc & 255); SDL_RenderCopy(R, icoSmile, NULL, &d); tx += icon + 6; }
  if (style == 0 || style == 4) { Tex *sh = label(f, 0x2e5a00, txt); SDL_SetTextureAlphaMod(sh->t, 110); draw_tex(sh, tx, ty + 1); SDL_SetTextureAlphaMod(sh->t, 255); }
  draw_tex(t, tx, ty);
}
static int btn_w(const char *txt, int small) { return label_w(small ? F_NAME : F_BOLD, txt) + (small ? 30 : 40); }
static int yeah_w(int n) { char b[32]; snprintf(b, sizeof b, "Yeah! %d", n); return label_w(F_BOLD, b) + 36 + 36; }

static void badge(int x, int y, const char *txt, uint32_t bg) {
  Tex *t = label(F_BADGE, C_WHITE, txt);
  fillr(x, y, t->w + 22, 25, bg);
  draw_tex(t, x + 11, y + (25 - t->h) / 2);
}
static int badge_w(const char *txt) { return label_w(F_BADGE, txt) + 22; }

/* ------------------------------------------------------------------ time: UTC from the server's Date header */
static volatile long serverDelta; static volatile int serverKnown;
static long days_from_civil(int y, int m, int d) { y -= m <= 2; long era = (y >= 0 ? y : y - 399) / 400; long yoe = y - era * 400; long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; return era * 146097 + doe - 719468; }
static void parse_date_header(const char *h) {
  static const char *mons = "JanFebMarAprMayJunJulAugSepOctNovDec";
  int d, y, H, M, S; char mon[4];
  const char *p = strchr(h, ','); if (!p) return;
  if (sscanf(p + 1, " %d %3s %d %d:%d:%d", &d, mon, &y, &H, &M, &S) != 6) return;
  const char *mp = strstr(mons, mon); if (!mp) return;
  long t = days_from_civil(y, (int)((mp - mons) / 3) + 1, d) * 86400L + H * 3600 + M * 60 + S;
  serverDelta = t - (long)time(NULL); serverKnown = 1;
}
static long utc_now(void) { return (long)time(NULL) + (serverKnown ? serverDelta : 0); }
/* console clock is local time; its difference to UTC (rounded to 15 min) is the time zone */
static long local_offset(void) { if (!serverKnown) return 3600; long d = -serverDelta; return ((d + (d >= 0 ? 450 : -450)) / 900) * 900; }
static int nth_sunday(int y, int m, int n) { /* day of month of the n-th Sunday (n = -1: last) */
  long first = days_from_civil(y, m, 1); int wd = (int)((first + 4) % 7); if (wd < 0) wd += 7;   /* 0 = Sunday */
  int d1 = 1 + (7 - wd) % 7;
  if (n > 0) return d1 + (n - 1) * 7;
  int dim = (int)(days_from_civil(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - first);
  while (d1 + 7 <= dim) d1 += 7;
  return d1;
}
static long tz_offset(int zone, long utc) {
  long days = utc / 86400; int y = 1970 + (int)(days / 365.2425); if (days_from_civil(y, 1, 1) > days) y--; if (days_from_civil(y + 1, 1, 1) <= days) y++;
  switch (zone) {
  case 0: { long a = days_from_civil(y, 3, nth_sunday(y, 3, 2)) * 86400 + 7 * 3600, b = days_from_civil(y, 11, nth_sunday(y, 11, 1)) * 86400 + 6 * 3600; return utc >= a && utc < b ? -4 * 3600 : -5 * 3600; }
  case 1: return -3 * 3600;
  case 2: return 3600;
  case 3: { long a = days_from_civil(y, 3, nth_sunday(y, 3, -1)) * 86400 + 3600, b = days_from_civil(y, 10, nth_sunday(y, 10, -1)) * 86400 + 3600; return utc >= a && utc < b ? 2 * 3600 : 3600; }
  default: return 9 * 3600;
  }
}
static void fmt_hm(long t, char *o, size_t n) { long s = ((t % 86400) + 86400) % 86400; snprintf(o, n, "%02ld:%02ld", s / 3600, (s / 60) % 60); }

/* ------------------------------------------------------------------ hero: globe + world clocks + people (website hero()) */
static const float LAND[][1 + 2 * 80] = {
#include "land.inc"
};
static SDL_Texture *globeTex; static int globeR = 100; static float globeLon = 12; static uint32_t globeNext;
static void globe_draw(void) {
  int size = globeR * 2, Rr = globeR;
  Cv c = cv_new(size, size);
  const float D = 3.14159265f / 180, f0 = 14 * D;
  /* ocean radial gradient: #74d6f8 at (0.7R,0.6R) -> #1b9fdc */
  for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
    float d = sqrtf((x + .5f - Rr) * (x + .5f - Rr) + (y + .5f - Rr) * (y + .5f - Rr));
    float a = Rr - 1 - d + 0.5f; if (a <= 0) continue; if (a > 1) a = 1;
    float g = sqrtf((x - Rr * 0.7f) * (x - Rr * 0.7f) + (y - Rr * 0.6f) * (y - Rr * 0.6f)) / (Rr * 1.0f);
    cv_blend(&c, x, y, lerp_col(0xff74d6f8, 0xff1b9fdc, g), a);
  }
  float pts[2 * 80];
  for (int pi = 0; pi < (int)(sizeof LAND / sizeof LAND[0]); pi++) {
    const float *poly = LAND[pi];
    int n = (int)poly[0], any = 0;
    for (int i = 0; i < n; i++) {
      float lo = poly[1 + i * 2], la = poly[2 + i * 2];
      float l = (lo - globeLon) * D, f = la * D;
      float cc = sinf(f0) * sinf(f) + cosf(f0) * cosf(f) * cosf(l);
      float x = cosf(f) * sinf(l), y = cosf(f0) * sinf(f) - sinf(f0) * cosf(f) * cosf(l);
      if (cc >= 0) any = 1; else { float nn = sqrtf(x * x + y * y); if (nn < 1e-6f) nn = 1; x /= nn; y /= nn; }
      pts[i * 2] = Rr + x * Rr; pts[i * 2 + 1] = Rr - y * Rr;
    }
    if (!any) continue;
    cv_poly(&c, pts, n, 0xffa4e25a); cv_poly(&c, pts, n, 0xffa4e25a);
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; cv_line(&c, pts[i * 2], pts[i * 2 + 1], pts[j * 2], pts[j * 2 + 1], 1.5f, 0xff7cc83a); }
  }
  /* clip to circle + white ring */
  for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
    float d = sqrtf((x + .5f - Rr) * (x + .5f - Rr) + (y + .5f - Rr) * (y + .5f - Rr));
    uint8_t *p = c.p + ((size_t)y * size + x) * 4;
    if (d > Rr - 0.5f) p[3] = 0;
    float ring = 1.6f - fabsf(d - (Rr - 2.5f)); if (ring > 0) cv_blend(&c, x, y, 0xe6ffffff, ring > 1 ? 1 : ring);
  }
  if (globeTex) SDL_DestroyTexture(globeTex);
  globeTex = cv_tex(&c);
  cv_free(&c);
}

/* figures (website figure()): the 2D body recoloured to the shirt, cube head with the avatar letter */
static const uint32_t AVC[8] = { 0xff004d, 0xffa300, 0x00e436, 0x29adff, 0x4b2d7f, 0x2ab7ca, 0xd291bc, 0x99e550 };
static uint32_t name_hash(const char *u) { uint32_t n = 0; for (const unsigned char *p = (const unsigned char *)(u ? u : "?"); *p; p++) n = n * 31 + *p; return n; }
static SDL_Texture *make_figure_f(const char *name, const char *user, int shirt, int fh, SDL_Surface *face);
static SDL_Texture *make_figure(const char *name, const char *user, int shirt, int fh) { return make_figure_f(name, user, shirt, fh, NULL); }
static SDL_Texture *make_figure_f(const char *name, const char *user, int shirt, int fh, SDL_Surface *face) {
  int fw = fh * 52 / 82;
  float k = fh / 82.0f;
  Cv c = cv_new(fw, fh);
  uint32_t t = SHIRT_COLS[((shirt % 12) + 12) % 12];
  float tr = (t >> 16) & 255, tg = (t >> 8) & 255, tb = t & 255;
  /* body image placed at x 1.8, y 31.6, size 48.4 x 49.8 in the 52x82 box */
  float bx = 1.8f * k, by = 31.6f * k, bw = 48.4f * k, bh = 49.8f * k;
  for (int y = (int)by; y < (int)(by + bh) && y < fh; y++) for (int x = (int)bx; x < (int)(bx + bw) && x < fw; x++) {
    int sx = (int)((x - bx) / bw * BODY_W), sy = (int)((y - by) / bh * BODY_H);
    if (sx < 0 || sy < 0 || sx >= BODY_W || sy >= BODY_H) continue;
    const unsigned char *s = BODY_RGBA + (sy * BODY_W + sx) * 4;
    float r = s[0], g = s[1], b = s[2];
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), sat = mx > 0 ? (mx - mn) / mx : 0;
    if (!(sat < 0.28f || r < g || r < b)) {
      float f = (0.299f * r + 0.587f * g + 0.114f * b) / 158;
      if (f <= 1) { r = tr * f; g = tg * f; b = tb * f; }
      else { float w2 = fminf(1, (f - 1) * 0.9f); r = tr + (255 - tr) * w2; g = tg + (255 - tg) * w2; b = tb + (255 - tb) * w2; }
    }
    cv_blend(&c, x, y, 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b, s[3] / 255.0f);
  }
  float top[8] = { 15 * k, 11 * k, 19 * k, 7 * k, 41 * k, 7 * k, 37 * k, 11 * k }, side[8] = { 37 * k, 11 * k, 41 * k, 7 * k, 41 * k, 29 * k, 37 * k, 33 * k };
  cv_poly(&c, top, 4, 0xffefefef); cv_poly(&c, top, 4, 0xffefefef);
  cv_poly(&c, side, 4, 0xff9a9aa3); cv_poly(&c, side, 4, 0xff9a9aa3);
  uint32_t av = user ? (0xff000000u | AVC[name_hash(user) % 8]) : 0xffb8b8b8;
  cv_rrect1(&c, 15 * k, 11 * k, 22 * k, 22 * k, 0, av);
  for (int i = 0; i < 4; i++) { cv_line(&c, top[i * 2], top[i * 2 + 1], top[(i * 2 + 2) % 8], top[(i * 2 + 3) % 8], 1.2f, 0xff2b2b33); cv_line(&c, side[i * 2], side[i * 2 + 1], side[(i * 2 + 2) % 8], side[(i * 2 + 3) % 8], 1.2f, 0xff2b2b33); }
  float rx[5] = { 15 * k, 37 * k, 37 * k, 15 * k, 15 * k }, ry[5] = { 11 * k, 11 * k, 33 * k, 33 * k, 11 * k };
  for (int i = 0; i < 4; i++) cv_line(&c, rx[i], ry[i], rx[i + 1], ry[i + 1], 1.2f, 0xff2b2b33);
  if (face && face->w > 0) {   /* avatar picture on the cube's front (cover crop) */
    int fx0 = (int)(15 * k), fy0 = (int)(11 * k), fs = (int)(22 * k);
    int side = face->w < face->h ? face->w : face->h, ox = (face->w - side) / 2, oy = (face->h - side) / 2;
    for (int y = 0; y < fs; y++) for (int x = 0; x < fs; x++) {
      const uint8_t *sp = (const uint8_t *)face->pixels + (oy + y * side / fs) * face->pitch + (ox + x * side / fs) * 4;
      cv_blend(&c, fx0 + x, fy0 + y, 0xff000000u | ((uint32_t)sp[0] << 16) | ((uint32_t)sp[1] << 8) | sp[2], 1);
    }
    for (int i = 0; i < 4; i++) cv_line(&c, rx[i], ry[i], rx[i + 1], ry[i + 1], 1.2f, 0xff2b2b33);
    SDL_Texture *tx = cv_tex(&c); cv_free(&c); return tx;
  }
  /* letter */
  char ini[8] = "?";
  if (name && *name) { int L = 1; unsigned char ch = (unsigned char)name[0]; if (ch >= 0xF0) L = 4; else if (ch >= 0xE0) L = 3; else if (ch >= 0xC0) L = 2; memcpy(ini, name, (size_t)L); ini[L] = 0; if (L == 1 && ini[0] >= 'a' && ini[0] <= 'z') ini[0] -= 32; }
  TTF_Font *lf = font_get(0, (int)(14 * k * 1.15f), 1);
  SDL_Surface *ls = lf ? TTF_RenderUTF8_Blended(lf, ini, (SDL_Color){ 255, 255, 255, 255 }) : NULL;
  if (ls) {
    SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormatFrom(c.p, c.w, c.h, 32, c.w * 4, SDL_PIXELFORMAT_RGBA32);
    SDL_Rect d = { (int)(26 * k) - ls->w / 2, (int)(22 * k) - ls->h / 2, 0, 0 };
    SDL_SetSurfaceBlendMode(ls, SDL_BLENDMODE_BLEND);
    SDL_BlitSurface(ls, NULL, dst, &d);
    SDL_FreeSurface(dst); SDL_FreeSurface(ls);
  }
  SDL_Texture *tx = cv_tex(&c);
  cv_free(&c);
  return tx;
}
typedef struct { char *name, *user, *avatar; int shirt, faceDone; SDL_Texture *fig; } HeroP;
static HeroP heroP[5]; static int heroN;
static const char *CITY_N[5] = { "USA", "BRAZIL", "AFRICA", "GERMANY", "JAPAN" };
static const float CITY_A[5] = { 300, 218, 152, 22, 88 };
static void people_cb(int serial, long code, const char *body, const char *err, int extra) {
  (void)serial; (void)err; (void)extra;
  if (code != 200 || !body) return;
  cJSON *j = cJSON_Parse(body), *p, *arr = j ? cJSON_GetObjectItem(j, "people") : NULL;
  for (int i = 0; i < heroN; i++) { free(heroP[i].name); free(heroP[i].user); free(heroP[i].avatar); if (heroP[i].fig) SDL_DestroyTexture(heroP[i].fig); }
  heroN = 0;
  cJSON_ArrayForEach(p, arr) {
    if (heroN >= 5) break;
    const char *d = jstr(p, "display"), *u = jstr(p, "username");
    heroP[heroN].name = xstrdup(d && *d ? d : u); heroP[heroN].user = xstrdup(u); heroP[heroN].shirt = (int)jnum(p, "shirt"); heroP[heroN].fig = NULL;
    heroP[heroN].avatar = xstrdup(jstr(p, "avatar")); heroP[heroN].faceDone = 0;
    heroN++;
  }
  cJSON_Delete(j);
}
static SDL_Texture *figEmpty;
static void draw_hero(Item *it, int X, int Y) {
  /* card: radial highlight + light-blue gradient + gloss, like .page.wide .hero */
  if (!it->bgTex) {
    Cv c = cv_new(it->w + 40, it->h + 40);
    int ox = 20, oy = 14;
    cv_shadow(&c, ox, oy + 8, it->w, it->h, S14(22), 26, 0x40146ea0);
    cv_rrect1(&c, ox, oy, it->w, it->h, S14(22), 0xffffffff);
    Stop st[3] = { { 0, 0xff8fdcff }, { .55f, 0xffc9f0ff }, { 1, 0xffe6fbff } };
    cv_rrect(&c, ox + 1, oy + 1, it->w - 2, it->h - 2, S14(22) - 1, st, 3, 180);
    for (int y = 0; y < it->h; y++) for (int x = 0; x < it->w; x++) {
      float ex = (x - it->w / 2.0f) / (it->w * 0.6f), ey = y / (it->h * 0.6f);
      float d = sqrtf(ex * ex + ey * ey);
      if (d < 1 && sd_rrect(ox + x + .5f, oy + y + .5f, ox + 1, oy + 1, it->w - 2, it->h - 2, S14(22)) < 0) cv_blend(&c, ox + x, oy + y, 0xccffffff, (1 - d));
    }
    it->bgTex = cv_tex(&c);
    cv_free(&c);
  }
  { SDL_Rect d = { X - 20, Y - 14, it->w + 40, it->h + 40 }; SDL_RenderCopy(R, it->bgTex, NULL, &d); }
  Tex *t = label(F_TITLE, 0x1f8a00, "Hello from around the world!");
  Tex *ts = label(F_TITLE, 0xffffff, "Hello from around the world!");
  draw_tex(ts, X + (it->w - t->w) / 2, Y + 18 + 2);
  draw_tex(t, X + (it->w - t->w) / 2, Y + 18);
  uint32_t now = SDL_GetTicks();
  if (!globeTex || now > globeNext) { globeLon = fmodf(globeLon + 0.5f, 360); globe_draw(); globeNext = now + 120; }
  int cx = X + it->w / 2, cy = Y + 108 + (it->h - 108) / 2;
  int fh = (int)(globeR * 0.8f), fw = fh * 52 / 82;
  if (globeTex) { SDL_Rect d = { cx - globeR, cy - globeR, globeR * 2, globeR * 2 }; SDL_RenderCopy(R, globeTex, NULL, &d); }
  for (int i = 0; i < 5; i++) {
    float th = CITY_A[i] * 3.14159265f / 180;
    float fx = cx + (globeR - 3) * sinf(th), fy = cy - (globeR - 3) * cosf(th);
    SDL_Texture *ft = NULL;
    if (i < heroN) {
      HeroP *hp = &heroP[i];
      if (hp->avatar && *hp->avatar && !hp->faceDone) { ImgE *e = img_get(hp->avatar); if (e && e->s) { if (hp->fig) SDL_DestroyTexture(hp->fig); hp->fig = make_figure_f(hp->name, hp->user, hp->shirt, fh, e->s); hp->faceDone = 1; } }
      if (!hp->fig) hp->fig = make_figure(hp->name, hp->user, hp->shirt, fh);
      ft = hp->fig;
    }
    else { if (!figEmpty) figEmpty = make_figure(NULL, NULL, 0, fh); ft = figEmpty; }
    /* rotate around the feet so the figure stands on the globe */
    SDL_Rect d = { (int)(fx - fw / 2.0f), (int)(fy - fh), fw, fh };
    SDL_Point piv = { fw / 2, fh };
    if (ft) SDL_RenderCopyEx(R, ft, NULL, &d, CITY_A[i], &piv, SDL_FLIP_NONE);
    /* clock labels around */
    float hw = 58 * 1.1f, hh = 26 * 1.1f, dist = globeR + fh + 10 + fabsf(sinf(th)) * hw + fabsf(cosf(th)) * hh;
    int lx = (int)(cx + dist * sinf(th)), ly = (int)(cy - dist * cosf(th));
    if (lx < X + 90) lx = X + 90; if (lx > X + it->w - 90) lx = X + it->w - 90;
    if (ly < Y + 92) ly = Y + 92; if (ly > Y + it->h - 46) ly = Y + it->h - 46;
    long utc = utc_now(); char tb[16]; fmt_hm(utc + tz_offset(i, utc), tb, sizeof tb);
    Tex *nt = label(F_TINY, 0x2d5d78, CITY_N[i]); Tex *tt = label(F_H, 0x1d3f52, tb);
    draw_tex(nt, lx - nt->w / 2, ly - 22);
    fillr(lx - 40, ly - 2, 80, 2, C_GREEN);
    draw_tex(tt, lx - tt->w / 2, ly + 2);
  }
  { long utc = utc_now(); char tb[16]; fmt_hm(utc + local_offset(), tb, sizeof tb);
    Tex *b = label(F_BIG, C_WHITE, tb), *bs = label(F_BIG, 0x0b4a78, tb);
    SDL_SetTextureAlphaMod(bs->t, 120); draw_tex(bs, cx - b->w / 2 + 1, cy - b->h / 2 - 8 + 2); SDL_SetTextureAlphaMod(bs->t, 255);
    draw_tex(b, cx - b->w / 2, cy - b->h / 2 - 8);
    Tex *sm = label(F_TINY, 0xe8f6ff, "YOUR TIME"); draw_tex(sm, cx - sm->w / 2, cy + b->h / 2 - 8); }
}

/* ------------------------------------------------------------------ 3D plaza item */
static P3Buf p3buf; static SDL_Texture *p3tex_out;
static void plaza_setup_people(Screen *s) {
  for (int i = 0; i < p3ntex; i++) free(p3tex[i].px);
  p3ntex = 0;
  p3_build_world();
  for (int i = 0; i < s->np; i++) {
    Person *q = &s->pp[i];
    /* face texture: avatar colour + letter */
    if (p3ntex < 64) {
      P3Tex *T = &p3tex[p3ntex]; T->w = T->h = 32; T->px = malloc(32 * 32 * 4);
      uint32_t col = 0xff000000u | AVC[name_hash(q->user) % 8];
      for (int k = 0; k < 32 * 32; k++) T->px[k] = col;
      char ini[8] = "?"; if (q->name && *q->name) { int L = 1; unsigned char ch = (unsigned char)q->name[0]; if (ch >= 0xF0) L = 4; else if (ch >= 0xE0) L = 3; else if (ch >= 0xC0) L = 2; memcpy(ini, q->name, (size_t)L); ini[L] = 0; if (L == 1 && ini[0] >= 'a' && ini[0] <= 'z') ini[0] -= 32; }
      TTF_Font *lf = font_get(0, 22, 1);
      SDL_Surface *ls = lf ? TTF_RenderUTF8_Blended(lf, ini, (SDL_Color){ 255, 255, 255, 255 }) : NULL;
      if (ls) {
        SDL_Surface *cs = SDL_ConvertSurfaceFormat(ls, SDL_PIXELFORMAT_RGBA32, 0);
        if (cs) {
          int ox = 16 - cs->w / 2, oy = 17 - cs->h / 2;
          for (int y = 0; y < cs->h; y++) for (int x = 0; x < cs->w; x++) {
            int tx = ox + x, ty = oy + y; if (tx < 0 || ty < 0 || tx >= 32 || ty >= 32) continue;
            uint8_t *sp = (uint8_t *)cs->pixels + y * cs->pitch + x * 4; float a = sp[3] / 255.0f;
            uint32_t d0 = T->px[ty * 32 + tx]; int r = (int)(((d0 >> 16) & 255) * (1 - a) + 255 * a), g = (int)(((d0 >> 8) & 255) * (1 - a) + 255 * a), b = (int)((d0 & 255) * (1 - a) + 255 * a);
            T->px[ty * 32 + tx] = 0xff000000u | (uint32_t)(r << 16 | g << 8 | b);
          }
          SDL_FreeSurface(cs);
        }
        SDL_FreeSurface(ls);
      }
      q->tex = p3ntex++;
    } else q->tex = -1;
    float a = (rand() % 1000) / 1000.0f * 6.2831853f, d = 6 + (rand() % 1000) / 1000.0f * 24;
    q->w.pos = v3(sinf(a) * d, 0, cosf(a) * d); q->w.heading = (rand() % 628) / 100.0f;
    q->w.speed = 1.3f + (rand() % 100) / 100.0f * 1.1f; q->w.phase = (rand() % 600) / 100.0f; q->w.wait = (rand() % 300) / 100.0f;
    p3_pick_wp(&q->w);
  }
}
static int plaza_visible(const Screen *s, const Person *q) {
  int fm = s->filter == 1 ? 1 : s->filter == 2 ? 2 : s->filter == 3 ? 4 : 7;
  return fm == 7 || (q->tags & fm);
}
static float tagX[64], tagY[64]; static int tagVis[64];
static void draw_plaza(Screen *s, Item *it, int X, int Y, int focus) {
  int W = 400, H = 224;
  if (!p3buf.px) { p3buf.w = W; p3buf.h = H; p3buf.px = malloc((size_t)W * H * 4); p3buf.zb = malloc((size_t)W * H * sizeof(float)); }
  if (!p3tex_out) p3tex_out = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
  static uint32_t last; static int frameCnt, skip = 2; uint32_t now = SDL_GetTicks(); float dt = last ? (now - last) / 1000.0f : 0.016f; if (dt > 0.15f) dt = 0.15f;
  frameCnt++;
  if ((frameCnt % skip) && p3tex_out && last) {   /* the 3D view is rendered every 2nd or 3rd frame, depending on speed */
    card(X, Y, it->w, it->h);
    SDL_Rect d0 = { X + 2, Y + 2, it->w - 4, it->h - 4 }; SDL_RenderCopy(R, p3tex_out, NULL, &d0);
    goto tags;
  }
  last = now;
  if (s->autoRot) s->az += dt * 0.05f;
  if (s->polar < 0.35f) s->polar = 0.35f; if (s->polar > 1.45f) s->polar = 1.45f;
  if (s->dist < 16) s->dist = 16; if (s->dist > 150) s->dist = 150;
  uint64_t t0 = SDL_GetPerformanceCounter();
  p3_build_world();
  if (!p3world.pre) { p3_precompute(&p3world); p3_precompute(&p3cloud); }
  p3_set_camera(s->az, s->polar, s->dist, (float)W / H);
  /* sky above the horizon, sea below it (the sea is too big to draw as triangles) */
  {
    V3 fwh = vnorm(v3(p3fw.x, 0, p3fw.z));
    V3 far = vadd(p3eye, vmul(fwh, 600)); far.y = -7;
    float hx, hy = 0; V3 fc = p3_cam(far);
    int hor = p3_project(fc, W, H, &hx, &hy) ? (int)hy : -1;
    uint32_t sea = p3_shade(0x5db7e6, v3(0, 1, 0), 0);
    for (int y = 0; y < H; y++) {
      uint32_t c;
      if (y < hor) c = 0xffa9dcf6;
      else { float k = hor >= 0 ? fminf(1, (y - hor) / (H * 0.35f)) : 1; c = p3_fog(sea, 0.55f * (1 - k)); }
      uint32_t *row = p3buf.px + y * W; for (int x = 0; x < W; x++) row[x] = c;
    }
  }
  memset(p3buf.zb, 0, sizeof(float) * (size_t)W * H);
  p3_draw_mesh(&p3buf, &p3world, v3(0, 0, 0));
  for (int i = 0; i < 7; i++) { p3clouds[i].x += dt * (0.8f + i * 0.15f); if (p3clouds[i].x > 120) p3clouds[i].x = -120; p3_draw_mesh(&p3buf, &p3cloud, p3clouds[i]); }
  /* people walk */
  static Mesh chars; chars.n = 0;
  for (int i = 0; i < s->np && i < 64; i++) {
    Person *q = &s->pp[i];
    tagVis[i] = 0;
    if (!plaza_visible(s, q)) continue;
    Walker *w = &q->w;
    if (q->avatar && *q->avatar && !q->faceDone && q->tex >= 0) {   /* avatar picture on the cube head */
      ImgE *e = img_get(q->avatar);
      if (e && e->s) {
        P3Tex *T = &p3tex[q->tex]; SDL_Surface *f = e->s;
        int side = f->w < f->h ? f->w : f->h, ox = (f->w - side) / 2, oy = (f->h - side) / 2;
        for (int y = 0; y < T->h; y++) for (int x = 0; x < T->w; x++) {
          const uint8_t *sp = (const uint8_t *)f->pixels + (oy + y * side / T->h) * f->pitch + (ox + x * side / T->w) * 4;
          T->px[y * T->w + x] = 0xff000000u | ((uint32_t)sp[0] << 16) | ((uint32_t)sp[1] << 8) | sp[2];
        }
        q->faceDone = 1;
      }
    }
    float amount = 1;
    if (w->wait > 0) { w->wait -= dt; w->phase += dt * 1.5f; amount = 0.05f; }
    else {
      float dx = w->wx - w->pos.x, dz = w->wz - w->pos.z, d = sqrtf(dx * dx + dz * dz);
      if (d < 0.5f) { w->wait = 1 + (rand() % 400) / 100.0f; p3_pick_wp(w); }
      else {
        float want = atan2f(dx, dz), diff = want - w->heading; diff = atan2f(sinf(diff), cosf(diff));
        w->heading += diff * fminf(1, dt * 6);
        float step = w->speed * dt * fmaxf(0.2f, 1 - fabsf(diff));
        w->pos.x += sinf(w->heading) * step; w->pos.z += cosf(w->heading) * step;
        w->phase += dt * w->speed * 4.2f;
      }
    }
    uint32_t pants[4] = { 0x454b5c, 0x2f3a56, 0x5a4a3a, 0x3b4a3f };
    p3_character(&chars, w->pos, w->heading, w->phase, amount, SHIRT_COLS[((q->shirt % 12) + 12) % 12], pants[name_hash(q->user) % 4], q->tex, 1.8f);
    V3 c = p3_cam(v3(w->pos.x, 3.0f * 1.8f + 0.6f, w->pos.z));
    float sx, sy;
    if (p3_project(c, it->w, it->h, &sx, &sy) && sx > -40 && sx < it->w + 40 && sy > 0 && sy < it->h) { tagVis[i] = 1; tagX[i] = sx; tagY[i] = sy; }
  }
  p3_draw_mesh(&p3buf, &chars, v3(0, 0, 0));
  SDL_UpdateTexture(p3tex_out, NULL, p3buf.px, W * 4);
  { double ms = (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency(); skip = ms > 22 ? 3 : 2; }
  SDL_SetTextureScaleMode(p3tex_out, SDL_ScaleModeLinear);
  /* rounded card around (.plaza-card, radius 18) */
  card(X, Y, it->w, it->h);
  SDL_Rect d = { X + 2, Y + 2, it->w - 4, it->h - 4 };
  SDL_RenderCopy(R, p3tex_out, NULL, &d);
tags:
  for (int i = 0; i < s->np && i < 64; i++) {
    if (!tagVis[i]) continue;
    Person *q = &s->pp[i];
    int sel = focus && i == s->psel;
    int tw = label_w(F_TINY, q->name) + 16, tx = X + (int)tagX[i] - tw / 2, ty = Y + (int)tagY[i] - 30;
    fillr(tx - 1, ty - 1, tw + 2, 28, 0x55000000 >> 0 & 0x777777);
    fillr(tx, ty, tw, 26, sel ? C_GREEN : (q->tags & 1) ? 0xfff0a8 : C_WHITE);
    draw_label(F_TINY, sel ? C_WHITE : 0x222222, q->name, tx + tw / 2, ty + 4, 1);
  }
  if (!s->np) draw_label(F_BOLD, C_SEC2, s->loading ? "Loading the plaza..." : "Nobody is here yet.", X + it->w / 2, Y + it->h / 2, 1);
}

/* ------------------------------------------------------------------ drawing items */
static uint32_t av_color(const char *u) { return PAL[AV_IDX[name_hash(u) % 8]]; }
/* .av: avatar square with 2px border (red ring + dot when the person posted in the last day, like .av.fresh) */
static const char *avUrl;   /* picture for the next draw_avatar call (set by draw_avatar_u) */
static void draw_avatar_x(const char *name, const char *user, int x, int y, int s, int fresh) {
  const char *url = avUrl; avUrl = NULL;
  if (fresh) { fillr(x - 4, y - 4, s + 8, s + 8, C_RED); fillr(x - 2, y - 2, s + 4, s + 4, C_WHITE); }
  fillr(x, y, s, s, fresh ? C_RED : C_LINE);
  ImgE *pic = url && *url ? img_get(url) : NULL;
  if (pic) { fillr(x + 2, y + 2, s - 4, s - 4, C_WHITE); img_draw_cover(pic, x + 2, y + 2, s - 4, s - 4);
    if (fresh) { int r = s >= 60 ? 9 : 7; fillr(x + s - r - 1, y - 3, r + 4, r + 4, C_WHITE); fillr(x + s - r + 1, y - 1, r, r, C_RED); }
    return; }
  fillr(x + 2, y + 2, s - 4, s - 4, av_color(user));
  char ini[8] = "?";
  if (name && *name) {
    int L = 1; unsigned char c = (unsigned char)name[0];
    if (c >= 0xF0) L = 4; else if (c >= 0xE0) L = 3; else if (c >= 0xC0) L = 2;
    memcpy(ini, name, (size_t)L); ini[L] = 0;
    if (L == 1 && ini[0] >= 'a' && ini[0] <= 'z') ini[0] -= 32;
  }
  TTF_Font *f = s >= 90 ? F_HUGE : s >= 60 ? F_TITLE : s >= 44 ? F_NAME : F_TINY;
  if (s >= 90) f = F_BIG;
  Tex *t = label(f, C_WHITE, ini);
  if (t->t) draw_tex(t, x + (s - t->w) / 2, y + (s - t->h) / 2);
  if (fresh) { int r = s >= 60 ? 9 : 7; fillr(x + s - r - 1, y - 3, r + 4, r + 4, C_WHITE); fillr(x + s - r + 1, y - 1, r, r, C_RED); }
}
static void draw_avatar(const char *name, const char *user, int x, int y, int s) { draw_avatar_x(name, user, x, y, s, 0); }
static int is_fresh(double lastPost) { return lastPost > 0 && (double)utc_now() * 1000.0 - lastPost < 86400000.0; }
static void draw_drawing(Item *it, int x, int y) {
  if (!it->ndr) return;
  int f = 0;
  if (it->ndr > 1) f = (int)((SDL_GetTicks() / 125) % 4);   /* website: 4 frames at 8 fps */
  SDL_Rect d = { x, y, it->dw, it->dh };
  fillr(x - 1, y - 1, it->dw + 2, it->dh + 2, 0xcfe6f1);
  SDL_RenderCopy(R, it->dr[f < it->ndr ? f : 0], NULL, &d);
}
static void draw_rich(Item *it, int x, int y) {
  if (!it->rich) return;
  SDL_Rect d = { x - 4, y - 4, it->rw, it->rh };
  SDL_RenderCopy(R, it->rich, NULL, &d);
}
static void sec_icon(SDL_Texture *ic, int x, int y) { if (!ic) return; SDL_Rect d = { x, y, 28, 28 }; SDL_SetTextureColorMod(ic, 255, 255, 255); SDL_RenderCopy(R, ic, NULL, &d); }

static void draw_item(Screen *s, Item *it, int X, int Y, int focus) {
  char buf[128];
  switch (it->kind) {
  case K_POST: case K_FULLPOST: {
    int full = it->kind == K_FULLPOST;
    if (!full) card(X, Y, it->w, it->h);
    if (focus) focus_ring(X, Y, it->w, full ? it->h - PC_PAD : it->h);
    int fresh = is_fresh(it->lastPost);
    avUrl = it->avatar; draw_avatar_x(it->name, it->user, X + PC_PAD + 3, Y + PC_PAD, PC_AV, fresh);
    it->avR = (SDL_Rect){ PC_PAD, PC_PAD, PC_AV + 6, PC_AV + 6 };
    int bx = X + PC_BUBX, bw = it->w - PC_BUBX - PC_PAD, by = Y + PC_PAD, bh = it->h - 2 * PC_PAD;
    nine_draw(&NBUB, bx, by, bw, bh);
    /* the little arrow of the bubble (rotated square in css) */
    for (int i = 0; i < 9; i++) { setc(0xcfe6f1); SDL_RenderDrawLine(R, bx - 8 + i, by + 28 - i, bx - 8 + i, by + 28 + i); }
    for (int i = 1; i < 9; i++) { setc(0xf3fbff); SDL_RenderDrawLine(R, bx - 7 + i, by + 29 - i, bx - 7 + i, by + 27 + i); }
    int tx = bx + BUB_PX, ty = by + BUB_PY;
    draw_tex(&it->tName, tx, ty);
    snprintf(buf, sizeof buf, "@%s", it->user ? it->user : "");
    draw_label(F_SMALL, C_SOFT, buf, tx + it->tName.w + 10, ty + 3, 0);
    fmt_ago(it->created, buf, sizeof buf);
    draw_label(F_SMALL, C_SOFT, buf, bx + bw - BUB_PX, ty + 3, 2);
    ty += 30 + 4;
    if (it->cname && *it->cname) badge(tx, ty, it->cname, C_ORANGE); else badge(tx, ty, "Profile", C_BLUE);
    ty += 25;
    if (it->rich) { ty += 8; draw_rich(it, tx, ty); ty += it->richH; }
    for (int k = 0; k < it->npics; k++) {
      ty += 10;
      int inner = bw - 2 * BUB_PX;
      ImgE *e = img_get(it->pics[k]);
      if (e) {
        int dw = e->w * 2 < inner ? e->w * 2 : inner, dh = dw * e->h / (e->w ? e->w : 1);
        if (dh > 520) { dh = 520; dw = dh * e->w / (e->h ? e->h : 1); }
        if (dh != it->picH[k]) { it->h += dh - it->picH[k]; it->picH[k] = dh; }
        SDL_Rect d = { tx, ty, dw, dh }; SDL_RenderCopy(R, e->t, NULL, &d);
      } else {
        ImgE *fe = img_find(it->pics[k]);
        if (fe && fe->state == 3) { if (it->picH[k] != 30) { it->h += 30 - it->picH[k]; it->picH[k] = 30; } draw_label(F_SMALL, C_SOFT, "[image unavailable]", tx, ty + 4, 0); }
        else { boxr(tx, ty, inner < 480 ? inner : 480, it->picH[k], 0xf4f9fc, 0xcfe6f1, 1); draw_label(F_SMALL, C_SOFT, "Loading picture...", tx + 16, ty + it->picH[k] / 2 - 10, 0); }
      }
      ty += it->picH[k];
    }
    if (it->ndr) { ty += 10; draw_drawing(it, tx, ty); ty += it->dh; }
    ty += 14;
    snprintf(buf, sizeof buf, "Yeah! %d", it->yeahs);
    int yw = yeah_w(it->yeahs);
    draw_btn(tx, ty, yw, 44, buf, it->mine ? 3 : 2, 0);
    it->yeahR = (SDL_Rect){ tx - X, ty - Y, yw, 44 };
    if (it->replies) snprintf(buf, sizeof buf, it->replies == 1 ? "1 reply" : "%d replies", it->replies); else snprintf(buf, sizeof buf, "Reply");
    int lw = draw_label(F_SMALL, C_SOFT, buf, tx + yw + 16, ty + 11, 0);
    fillr(tx + yw + 16, ty + 11 + 22, lw, 1, C_SOFT);
    break; }
  case K_REPLY: {
    boxr(X, Y, it->w, it->h, C_REPLY, C_REPLY_L, 1);
    avUrl = it->avatar; draw_avatar_x(it->name, it->user, X + 14, Y + 11, 50, is_fresh(it->lastPost));
    it->avR = (SDL_Rect){ 14, 11, 50, 50 };
    int tx = X + 14 + 53 + 14, ty = Y + 11;
    draw_tex(&it->tName, tx, ty);
    fmt_ago(it->created, buf, sizeof buf);
    draw_label(F_SMALL, C_SOFT, buf, X + it->w - 14, ty + 3, 2);
    ty += 30;
    if (it->rich) { ty += 4; draw_rich(it, tx, ty); ty += it->richH; }
    if (it->ndr) { ty += 8; draw_drawing(it, tx, ty); }
    break; }
  case K_SEP:
    fillr(X, Y + 8, it->w, 1, C_REPLY_L);
    break;
  case K_COMM: {
    card(X, Y, it->w, it->h);
    if (focus) focus_ring(X, Y, it->w, it->h);
    avUrl = it->avatar; draw_avatar(it->name, it->cid, X + 16, Y + 14, 56);
    draw_tex(&it->tName, X + 88, Y + 14);
    badge(X + 88 + it->tName.w + 10, Y + 16, it->age && !strcmp(it->age, "14") ? "14+" : it->age && !strcmp(it->age, "16") ? "16+" : it->age && !strcmp(it->age, "18") ? "18+" : "All ages", C_GREEN);
    snprintf(buf, sizeof buf, it->posts == 1 ? "1 post" : "%d posts", it->posts);
    draw_label(F_SMALL, C_SOFT, buf, X + 88, Y + 46, 0);
    draw_label(F_H, 0xa0a8ad, ">", X + it->w - 24, Y + 26, 1);
    break; }
  case K_BANNER: {
    card(X, Y, it->w, it->h);
    if (focus) focus_ring(X, Y, it->w, it->h);
    int bh = S14(190);
    if (!it->bgTex) {
      Cv c = cv_new(it->w, bh);
      Stop st[2] = { { 0, 0xff7fe02a }, { 1, 0xff4bb800 } };
      cv_rrect(&c, 0, 0, it->w, bh + 30, S14(14), st, 2, 180);
      for (int y = bh; y < bh; y++) ;
      it->bgTex = cv_tex(&c); cv_free(&c);
    }
    SDL_Rect d = { X, Y, it->w, bh }; SDL_RenderCopy(R, it->bgTex, NULL, &d);
    { ImgE *th = it->avatar && *it->avatar ? img_get(it->avatar) : NULL; if (th) img_draw_cover(th, X + 1, Y + 1, it->w - 2, bh - 1); }
    if (it->tName.t) {
      SDL_SetTextureColorMod(it->tName.t, 0, 0, 0); SDL_SetTextureAlphaMod(it->tName.t, 46);
      draw_tex(&it->tName, X + (it->w - it->tName.w) / 2, Y + (bh - it->tName.h) / 2 + 4);
      SDL_SetTextureColorMod(it->tName.t, 255, 255, 255); SDL_SetTextureAlphaMod(it->tName.t, 255);
      draw_tex(&it->tName, X + (it->w - it->tName.w) / 2, Y + (bh - it->tName.h) / 2);
    }
    int fy = Y + bh + 14;
    avUrl = it->avatar; draw_avatar(it->name, it->cid, X + 18, fy, 56);
    int nw = draw_label(F_NAME, C_INK, it->name, X + 90, fy + 2, 0);
    badge(X + 90 + nw + 10, fy + 4, "All ages", C_GREEN);
    snprintf(buf, sizeof buf, it->posts == 1 ? "1 post" : "%d posts", it->posts);
    draw_label(F_SMALL, C_SOFT, buf, X + 90, fy + 32, 0);
    draw_label(F_H, 0xa0a8ad, ">", X + it->w - 24, fy + 14, 1);
    break; }
  case K_CHEAD: {
    card(X, Y, it->w, it->h);
    fillr(X + 20, Y + 20, 100, 100, C_GREEN);
    { ImgE *th = it->avatar && *it->avatar ? img_get(it->avatar) : NULL; if (th) { img_draw_cover(th, X + 20, Y + 20, 100, 100); goto chead_text; } }
    Tex *l = label(F_HUGE, C_WHITE, it->name && *it->name ? (char[2]){ (char)((it->name[0] >= 'a' && it->name[0] <= 'z') ? it->name[0] - 32 : it->name[0]), 0 } : "?");
    draw_tex(l, X + 70 - l->w / 2, Y + 70 - l->h / 2);
  chead_text:;
    int tx = X + 140, ty = Y + 20;
    draw_tex(&it->tName, tx, ty); ty += it->tName.h + 6;
    badge(tx, ty, it->age && !strcmp(it->age, "14") ? "14+" : it->age && !strcmp(it->age, "16") ? "16+" : it->age && !strcmp(it->age, "18") ? "18+" : "All ages", it->age && !strcmp(it->age, "18") ? C_RED : it->age && !strcmp(it->age, "16") ? C_ORANGE : it->age && !strcmp(it->age, "14") ? C_BLUE : C_GREEN);
    ty += 31;
    draw_tex(&it->tBody, tx, ty);
    if (it->msg && *it->msg) {
      int my = Y + it->h - (16 + 22 + 6 + it->tTitle.h + 18) - 1;
      fillr(X + 1, my, it->w - 2, 1, C_REPLY_L);
      fillr(X + 1, my + 1, it->w - 2, Y + it->h - my - 16, 0xfafafa);
      draw_label(F_TINY, C_SOFT, "MESSAGE FROM THE CREATOR", X + 22, my + 16, 0);
      draw_tex(&it->tTitle, X + 22, my + 16 + 22 + 6);
    }
    break; }
  case K_HEAD:
    card(X, Y, it->w, it->h);
    draw_tex(&it->tName, X + 22, Y + 22);
    draw_tex(&it->tBody, X + 22, Y + 22 + it->tName.h + 8);
    break;
  case K_SEC: {
    int ix = X + 2;
    SDL_Texture *ic = it->icon == 1 ? icoSmile : it->icon == 2 ? icoPencil : it->icon == 3 ? icoTree : it->icon == 4 ? icoGames : NULL;
    if (ic) { if (it->icon == 1) SDL_SetTextureColorMod(ic, 0x14, 0x5d, 0x86); SDL_Rect d = { ix, Y + 2, 28, 28 }; SDL_RenderCopy(R, ic, NULL, &d); SDL_SetTextureColorMod(ic, 255, 255, 255); ix += 36; }
    if (it->tName.t) { SDL_SetTextureColorMod(it->tName.t, 255, 255, 255); }
    Tex *w = label(F_H, C_WHITE, it->title); draw_tex(w, ix, Y + 1);
    draw_tex(&it->tName, ix, Y);
    break; }
  case K_INFO:
    if (it->tBody.t) draw_tex(&it->tBody, X + 4, Y + 10);
    break;
  case K_EMPTY:
    card(X, Y, it->w, it->h);
    if (it->tBody.t) draw_tex(&it->tBody, X + (it->w - it->tBody.w) / 2, Y + (it->h - it->tBody.h) / 2);
    break;
  case K_PROMPT: {
    card(X, Y, it->w, it->h);
    if (focus) focus_ring(X, Y, it->w, it->h);
    if (it->act == 1) {   /* .card.composer: avatar, fake input, Draw + Write */
      avUrl = me.avatar; draw_avatar(me.display, me.username, X + 20, Y + 20, 66);
      boxr(X + 104, Y + 20, it->w - 124, 54, 0xffffff, 0xbdbdbd, 1);
      draw_label(F_BODYS, 0x8a9399, it->body, X + 120, Y + 34, 0);
      int ww = btn_w("Write", 0), dw = btn_w("Draw", 0) + 30;
      draw_btn(X + it->w - 20 - ww, Y + 88, ww, 48, "Write", 0, 0);
      draw_btn(X + it->w - 20 - ww - 12 - dw, Y + 88, dw, 48, "Draw", 1, 0);
      it->btnR = (SDL_Rect){ it->w - 20 - ww - 12 - dw, 88, dw, 48 };
      break;
    }
    draw_tex(&it->tBody, X + 22, Y + (it->h - it->tBody.h) / 2);
    const char *bt = it->title ? it->title : "Sign in";
    int bw = btn_w(bt, 1);
    draw_btn(X + it->w - 22 - bw, Y + (it->h - 38) / 2, bw, 38, bt, 0, 0);
    break; }
  case K_CNEW: {
    /* dashed box (.c-new) */
    for (int x = X; x < X + it->w; x += 10) { fillr(x, Y, 5, 2, 0xbdbdbd); fillr(x, Y + it->h - 2, 5, 2, 0xbdbdbd); }
    for (int y = Y; y < Y + it->h; y += 10) { fillr(X, y, 2, 5, 0xbdbdbd); fillr(X + it->w - 2, y, 2, 5, 0xbdbdbd); }
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, 250, 250, 250, 150); SDL_Rect in = { X + 2, Y + 2, it->w - 4, it->h - 4 }; SDL_RenderFillRect(R, &in);
    SDL_Rect pd = { X + 18, Y + (it->h - 40) / 2, 40, 40 }; SDL_RenderCopy(R, icoPlus, NULL, &pd);
    draw_label(F_NAME, 0x444444, it->title ? it->title : "Start a community", X + 74, Y + 18, 0);
    draw_label(F_SMALL, 0x666666, it->body ? it->body : "Send in your idea for approval", X + 74, Y + 44, 0);
    break; }
  case K_FEAT: {
    card(X, Y, it->w, it->h);
    draw_drawing(it, X + 14, Y + 14);
    avUrl = it->avatar; draw_avatar(it->name, it->user, X + 20, Y + 20, 34);
    snprintf(buf, sizeof buf, "%s · %s", it->cname && *it->cname ? it->cname : "Profile", it->name ? it->name : "");
    draw_label(F_SMALL, C_SOFT, buf, X + 14, Y + 14 + it->dh + 10, 0);
    break; }
  case K_MORE:
    if (focus) focus_ring(X + it->w / 2 - 110, Y + 8, 220, 48);
    draw_btn(X + it->w / 2 - 110, Y + 8, 220, 48, s->loading ? "Loading..." : "Show more", 1, 0);
    break;
  case K_ACTION: {
    const char *t = it->title ? it->title : "";
    int bw = btn_w(t, 0); if (it->act == A_SEND || it->act == A_UPDATE) bw = it->w;
    int bx = it->act == A_SEND || it->act == A_UPDATE ? X : X;
    draw_btn(bx, Y + 8, bw, 48, t, it->act == A_SIGNOUT ? 1 : it->act == A_MODE ? 1 : 0, focus);
    break; }
  case K_CHIPS: {
    static const char *CH[4] = { "Everyone", "Popular", "Newest", "Active now" };
    int x = X;
    for (int i = 0; i < 4; i++) {
      int w = label_w(F_NAME, CH[i]) + 34;
      pill_draw(s->filter == i ? &PCHIP_ON : &PCHIP, x, Y + 2, w);
      draw_label(F_NAME, s->filter == i ? C_WHITE : 0x222222, CH[i], x + w / 2, Y + 2 + 9, 1);
      x += w + 12;
    }
    if (focus) focus_ring(X, Y + 2, x - X - 12, 42);
    break; }
  case K_FIELD:
    card(X, Y, it->w, it->h);
    if (focus) focus_ring(X, Y, it->w, it->h);
    draw_label(F_NAME, C_SEC2, it->title, X + 22, Y + 18, 0);
    boxr(X + 22, Y + 56, it->w - 44, it->h - 56 - 18, 0xffffff, 0xbdbdbd, 1);
    if (it->tBody.t) draw_tex_clip(&it->tBody, X + 34, Y + 66, it->h - 56 - 36);
    else draw_label(F_BODYS, 0xa0a8ad, "Tap here to type", X + 34, Y + 66, 0);
    break;
  case K_DRAWF:
    card(X, Y, it->w, it->h);
    if (focus) focus_ring(X, Y, it->w, it->h);
    draw_label(F_NAME, C_SEC2, it->title, X + 22, Y + 18, 0);
    if (it->ndr) draw_drawing(it, X + 22, Y + 56);
    else { boxr(X + 22, Y + 56, it->dw, it->dh, 0xffffff, 0xbdbdbd, 1); draw_label(F_NAME, 0xa0a8ad, "Tap to draw", X + 22 + it->dw / 2, Y + 56 + it->dh / 2 - 14, 1); }
    break;
  case K_WIG: case K_WIGFULL: {
    int fullw = it->kind == K_WIGFULL;
    card(X, Y, it->w, it->h);
    if (focus) focus_ring(X, Y, it->w, it->h);
    int dx = X + (it->w - it->dw) / 2;
    draw_drawing(it, dx, Y + 14);
    int ty = Y + 14 + it->dh + 10, lx = fullw ? dx : X + 14;
    draw_tex(&it->tTitle, lx, ty);
    ty += it->tTitle.h + 4;
    snprintf(buf, sizeof buf, "by %s", it->name ? it->name : "?");
    draw_label(F_SMALL, C_SOFT, buf, lx, ty, 0);
    ty += 36;
    snprintf(buf, sizeof buf, "Yeah! %d", it->yeahs);
    int yw = yeah_w(it->yeahs);
    draw_btn(lx, ty, yw, 44, buf, it->mine ? 3 : 2, 0);
    it->yeahR = (SDL_Rect){ lx - X, ty - Y, yw, 44 };
    break; }
  case K_DCELL: {
    card(X, Y, it->w, it->h);
    if (focus) focus_ring(X, Y, it->w, it->h);
    draw_drawing(it, X + 12, Y + 12);
    int fy = Y + 12 + it->dh + 10;
    avUrl = it->avatar; draw_avatar(it->name, it->user, X + 12, fy, 30);
    char nm[48]; snprintf(nm, sizeof nm, "%.14s", it->name ? it->name : "");
    draw_label(F_SMALL, C_INK, nm, X + 50, fy + 3, 0);
    snprintf(buf, sizeof buf, "Yeah! %d", it->yeahs);
    draw_label(F_SMALL, C_YEAH_T, buf, X + it->w - 12, fy + 3, 2);
    break; }
  case K_PROFILE: {
    card(X, Y, it->w, it->h);
    /* character figure on the left like the 3D character on the website */
    static SDL_Texture *pf; static char pfUser[32]; static int pfFace;
    ImgE *fe = it->avatar && *it->avatar ? img_get(it->avatar) : NULL;
    if (!pf || strcmp(pfUser, it->user ? it->user : "") || (fe && !pfFace)) {
      if (pf) SDL_DestroyTexture(pf);
      pf = make_figure_f(it->name, it->user, it->icon, 230, fe ? fe->s : NULL); pfFace = fe != NULL;
      snprintf(pfUser, sizeof pfUser, "%s", it->user ? it->user : "");
    }
    if (pf) { SDL_Rect d = { X + 40, Y + 22, 230 * 52 / 82, 230 }; SDL_RenderCopy(R, pf, NULL, &d); }
    int tx = X + 260, ty = Y + 24;
    draw_tex(&it->tName, tx, ty); ty += it->tName.h + 4;
    snprintf(buf, sizeof buf, "@%s", it->user ? it->user : "");
    draw_label(F_BODYS, C_SOFT, buf, tx, ty, 0); ty += 28;
    if (it->tBody.t) { ty += 10; draw_tex(&it->tBody, tx, ty); ty += it->tBody.h; }
    ty += 8;
    char since[48]; fmt_ago(it->created, since, sizeof since);
    if (it->posts == 1) snprintf(buf, sizeof buf, "1 post   ·   joined %s", since); else snprintf(buf, sizeof buf, "%d posts   ·   joined %s", it->posts, since);
    draw_label(F_SMALL, C_SOFT, buf, tx, ty, 0);
    break; }
  case K_GAME: {
    card(X, Y, it->w, it->h);
    if (focus) focus_ring(X, Y, it->w, it->h);
    int ultra = it->id == 1, aw = 210, ah = 140, ax = X + 17, ay = Y + (it->h - ah) / 2;
    if (!it->bgTex) {
      Cv c = cv_new(aw, ah);
      Stop su[2] = { { 0, 0xff5bd1ff }, { 1, 0xff6fe36a } }, uu[3] = { { 0, 0xffff4fa3 }, { .5f, 0xffffb02e }, { 1, 0xff7a5cff } };
      cv_rrect(&c, 0, 0, aw, ah, 11, ultra ? uu : su, ultra ? 3 : 2, 135);
      SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormatFrom(c.p, c.w, c.h, 32, c.w * 4, SDL_PIXELFORMAT_RGBA32);
      const char *l1 = ultra ? "ULTRA" : "Super", *l2 = "Wiggle";
      SDL_Surface *a1 = TTF_RenderUTF8_Blended(F_BIG, l1, (SDL_Color){ 255, 255, 255, 255 }), *a2 = TTF_RenderUTF8_Blended(F_BIG, l2, (SDL_Color){ 255, 255, 255, 255 });
      SDL_Surface *s1 = TTF_RenderUTF8_Blended(F_BIG, l1, (SDL_Color){ 0, 60, 90, 255 }), *s2 = TTF_RenderUTF8_Blended(F_BIG, l2, (SDL_Color){ 0, 60, 90, 255 });
      if (a1 && a2 && s1 && s2) {
        SDL_SetSurfaceAlphaMod(s1, 70); SDL_SetSurfaceAlphaMod(s2, 70);
        SDL_Rect d1 = { (aw - a1->w) / 2, ah / 2 - a1->h + 4, 0, 0 }, d2 = { (aw - a2->w) / 2, ah / 2 - 4, 0, 0 };
        SDL_Rect e1 = { d1.x, d1.y + 2, 0, 0 }, e2 = { d2.x, d2.y + 2, 0, 0 };
        SDL_BlitSurface(s1, NULL, dst, &e1); SDL_BlitSurface(s2, NULL, dst, &e2);
        SDL_BlitSurface(a1, NULL, dst, &d1); SDL_BlitSurface(a2, NULL, dst, &d2);
      }
      if (a1) SDL_FreeSurface(a1); if (a2) SDL_FreeSurface(a2); if (s1) SDL_FreeSurface(s1); if (s2) SDL_FreeSurface(s2);
      SDL_FreeSurface(dst);
      it->bgTex = cv_tex(&c); cv_free(&c);
    }
    /* @keyframes gcw: rotate -3deg .. 3deg, scale 1.04 */
    float ph = (SDL_GetTicks() % (ultra ? 350 : 1800)) / (ultra ? 350.0f : 1800.0f);
    float ang = -3 * cosf(ph * 6.2831853f), sc = 1 + 0.02f * (1 - cosf(ph * 6.2831853f));
    SDL_Rect d = { ax + (int)(aw * (1 - sc) / 2), ay + (int)(ah * (1 - sc) / 2), (int)(aw * sc), (int)(ah * sc) };
    SDL_RenderCopyEx(R, it->bgTex, NULL, &d, ang, NULL, SDL_FLIP_NONE);
    int tx = X + 17 + aw + 20, ty = Y + 18;
    draw_tex(&it->tName, tx, ty); ty += it->tName.h + 8;
    draw_tex(&it->tBody, tx, ty); ty += it->tBody.h + 10;
    const char *bt = ultra ? "Play if you dare" : "Play";
    draw_btn(tx, ty, btn_w(bt, 1), 38, bt, 0, 0);
    break; }
  case K_LTABS: {
    card(X, Y, it->w, it->h + 6);
    int hw = it->w / 2;
    fillr(X + 1, Y + 1, it->w - 2, it->h - 2, 0xffffff);
    fillr(X + hw, Y + 1, hw - 1, it->h - 2, s->mode ? 0xffffff : 0xf0f0f0);
    if (s->mode) fillr(X + 1, Y + 1, hw - 1, it->h - 2, 0xf0f0f0);
    fillr(s->mode ? X + hw : X + 1, Y + 1, hw - 1, 4, C_GREEN);
    draw_label(F_NAME, s->mode ? 0x777777 : C_GREEN_D, "Sign in", X + hw / 2, Y + 18, 1);
    draw_label(F_NAME, s->mode ? C_GREEN_D : 0x777777, "Create account", X + hw + hw / 2, Y + 18, 1);
    break; }
  case K_HERO:
    draw_hero(it, X, Y);
    break;
  case K_PLAZA:
    draw_plaza(s, it, X, Y, focus);
    break;
  }
}

/* ------------------------------------------------------------------ chrome: header, footer */
typedef struct { int x, w; } TabR;
static TabR tabR[NTABS]; static int signinX, signinW, noteX;
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
/* same header as the website: logo, tabs Home / Communities / Plaza / Games / Sign in|Profile, rules icon, Sign in button */
static void draw_header(void) {
  { SDL_Rect d = { 0, 0, SW, TOPH + 14 }; SDL_RenderCopy(R, headTex, NULL, &d); }
  if (logoTex) { SDL_Rect d = { 40, (TOPH - LOGO_H) / 2, LOGO_W, LOGO_H }; SDL_RenderCopy(R, logoTex, NULL, &d); }
  const char *names[NTABS] = { "Home", "Communities", "Plaza", "Games", "Sign in" };
#if STAGE >= 2
  if (me.id) names[4] = "Profile";
#endif
  int w = 104, x = 540;
  for (int i = 0; i < NTABS; i++) {
    int tw = i == 1 ? 128 : w;
    tabR[i] = (TabR){ x, tw };
    int on = curTab == i;
    uint32_t c = on ? C_GREEN : 0x6b6b6b;
    if (iconTex[i]) { SDL_SetTextureColorMod(iconTex[i], (c >> 16) & 255, (c >> 8) & 255, c & 255); SDL_Rect d = { x + tw / 2 - 18, 8, 36, 36 }; SDL_RenderCopy(R, iconTex[i], NULL, &d); }
    draw_label(F_TINY, c, names[i], x + tw / 2, 47, 1);
    if (on) fillr(x + 6, TOPH - 4, tw - 12, 4, C_GREEN);
    x += tw;
  }
  /* music on/off + rules icon */
  noteX = x + 10;
  { SDL_Rect d = { noteX, 22, 32, 32 }; SDL_SetTextureAlphaMod(icoNote, auMusic ? 255 : 110); SDL_RenderCopy(R, icoNote, NULL, &d); SDL_SetTextureAlphaMod(icoNote, 255);
    if (!auMusic) { setc(0xe5322d); for (int k = -1; k <= 1; k++) SDL_RenderDrawLine(R, noteX + 2 + k, 52, noteX + 30 + k, 24); } }
  { SDL_Rect d = { x + 52, 23, 26, 30 }; SDL_RenderCopy(R, icoDoc, NULL, &d); }
#if STAGE >= 2
  if (!me.id) { signinW = 100; signinX = SW - 24 - signinW; draw_btn(signinX, 19, signinW, 38, "Sign in", 0, 0); }
  else signinW = 0;
#else
  signinW = 0;
#endif
}
static void draw_footer(const char *hint) {
  SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(R, 255, 255, 255, 210);
  SDL_Rect r = { 0, SH - FOOTH, SW, FOOTH };
  SDL_RenderFillRect(R, &r);
  draw_label(F_SMALL, 0x55707f, hint, 24, SH - FOOTH + 6, 0);
  char ver[64]; snprintf(ver, sizeof ver, "Aeroverse for Wii U %s (build %d)", APP_VERSION, APP_BUILD);
  draw_label(F_SMALL, 0x8aa0ad, ver, SW - 24, SH - FOOTH + 6, 2);
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
  au_init();
  WIN = SDL_CreateWindow("Aeroverse", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SW, SH, 0);
  R = SDL_CreateRenderer(WIN, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!R) R = SDL_CreateRenderer(WIN, -1, 0);
  SDL_RenderSetLogicalSize(R, SW, SH);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  TTF_Init();
#define OPENF(file, sz) TTF_OpenFontRW(SDL_RWFromConstMem(file, (int)file##_size), 1, sz)
  F_SMALL = OPENF(nunito_reg_ttf, 18);   /* 13px */
  F_BODYS = OPENF(nunito_reg_ttf, 21);   /* 15px */
  F_BODY = OPENF(nunito_reg_ttf, 22);    /* 16px */
  F_TINY = OPENF(nunito_bold_ttf, 15);   /* 11px bold */
  F_BADGE = OPENF(nunito_bold_ttf, 17);  /* 12px bold */
  F_NAME = OPENF(nunito_bold_ttf, 21);   /* 15px bold */
  F_BOLD = OPENF(nunito_bold_ttf, 22);
  F_H = OPENF(nunito_bold_ttf, 26);      /* h-sec 19px */
  F_TITLE = OPENF(nunito_bold_ttf, 31);
  F_BIG = OPENF(nunito_bold_ttf, 36);
  F_LOGO = OPENF(nunito_bold_ttf, 39);   /* logo 28px */
  F_HUGE = OPENF(nunito_bold_ttf, 76);   /* banner 56px */
  postfonts_init(postfonts_bin, (int)postfonts_bin_size);
  make_bg();
  make_logo();
  make_icons();
  make_styles();

  curl_global_init(CURL_GLOBAL_DEFAULT);
  netMx = SDL_CreateMutex(); netCv = SDL_CreateCond();
  SDL_Thread *nt = SDL_CreateThreadWithStackSize(net_thread, "net", 512 * 1024, NULL);
  img_init();

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
    img_poll();

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
    { static int wasModal; int modal = ed.open + kb.open * 2 + pe.open * 4; if (wasModal && modal != wasModal) swallowTouch = 1; wasModal = modal; }
#endif
    if (swallowTouch) { touchTapX = -1; touchFresh = 0; if (!tDown) swallowTouch = 0; }
    if (touchTapX >= 0) sfx(1);
    if (trig & VPAD_BUTTON_A) sfx(1);
    /* key repeat for the D-Pad */
    uint32_t dirs = hold & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT);
    if (trig & dirs) holdT = SDL_GetTicks() + 380;
    else if (dirs && SDL_GetTicks() > holdT) { trig |= dirs; holdT = SDL_GetTicks() + 90; }

    Screen *s = cur();
    const char *hint = "Touch or D-Pad to choose   A: open   B: back   L/R: tabs   Y: refresh   HOME: exit";
#if STAGE >= 2
    if (kb.open) {
      kb_input(trig, touchTapX, touchTapY);
    } else if (ed.open) {
      ed_touch(touchX, touchY, tDown, touchFresh);
      ed_buttons(trig);
    } else if (pe.open) {
      if (touchTapX >= 0) pe_touch(touchTapX, touchTapY);
      pe_buttons(trig);
    } else
#endif
    if (s) {
      /* touch scrolling with momentum */
      static int plazaDrag, touchLastX;
      if (touchFresh) {
        plazaDrag = 0; touchLastX = touchX;
        for (int i = 0; i < s->n; i++) if (s->it[i].kind == K_PLAZA && inr(touchX, touchY + (int)s->scroll, s->it[i].x, s->it[i].y, s->it[i].w, s->it[i].h)) plazaDrag = 1;
      }
      if (tDown && touchDown) {
        int dy = touchY - touchLastY, dx = touchX - touchLastX;
        if (abs(touchY - touchStartY) > 8 || abs(touchX - touchStartX) > 8) touchMoved += abs(dy) + abs(dx) + 1;
        if (plazaDrag) { if (touchMoved >= 18) { s->az -= dx * 0.006f; s->polar -= dy * 0.005f; s->autoRot = 0; } }
        else if (touchMoved >= 18 && touchStartY > TOPH) { s->scroll -= dy; s->vel = (float)-dy; }
        touchLastY = touchY; touchLastX = touchX;
      } else if (!tDown) { s->scroll += s->vel; s->vel *= 0.92f; if (fabsf(s->vel) < 0.3f) s->vel = 0; }
      float sy = got ? st.leftStick.y : 0;
      if (fabsf(sy) > 0.15f) s->scroll -= sy * 22.0f;
      if (s->kind == S_PLAZA && got) {   /* right stick looks around the plaza */
        if (fabsf(st.rightStick.x) > 0.15f) { s->az -= st.rightStick.x * 0.04f; s->autoRot = 0; }
        if (fabsf(st.rightStick.y) > 0.15f) { s->dist -= st.rightStick.y * 1.5f; s->autoRot = 0; }
      } else if (got && fabsf(st.rightStick.y) > 0.15f) s->scroll -= st.rightStick.y * 22.0f;

      if (touchTapX >= 0) {
        if (touchTapY < TOPH) {
          for (int i = 0; i < NTABS; i++) if (touchTapX >= tabR[i].x && touchTapX < tabR[i].x + tabR[i].w) go_tab(i);
          if (touchTapX >= noteX - 6 && touchTapX < noteX + 40) { auMusic = !auMusic; au_save(); toast(auMusic ? "Music on" : "Music off"); }
#if STAGE >= 2
          if (signinW && touchTapX >= signinX && touchTapX < signinX + signinW) open_login();
#endif
        } else {
          int cy = touchTapY + (int)s->scroll;
          Item *hit = NULL; int hitSide = 0, hitI = -1;
          for (int i = 0; i < s->n && !hit; i++) if (inr(touchTapX, cy, s->it[i].x, s->it[i].y, s->it[i].w, s->it[i].h)) { hit = &s->it[i]; hitI = i; }
          for (int i = 0; i < s->nside && !hit; i++) if (inr(touchTapX, cy, s->side[i].x, s->side[i].y, s->side[i].w, s->side[i].h)) { hit = &s->side[i]; hitSide = 1; }
          if (hit) {
            Item *it = hit;
            if (!hitSide && it->sel_ok) s->sel = hitI;
            int lx = touchTapX - it->x, ly = cy - it->y;
            if ((it->kind == K_POST || it->kind == K_FULLPOST || it->kind == K_REPLY) && it->avR.w && inr(lx, ly, it->avR.x, it->avR.y, it->avR.w, it->avR.h)) open_user(it->user);
            else if (it->kind == K_PROFILE || it->kind == K_SEC || it->kind == K_CHEAD || it->kind == K_SEP || it->kind == K_HERO) { /* nothing */ }
#if STAGE >= 2
            else if (it->kind == K_LTABS) { s->mode = lx >= it->w / 2; rebuild_compose(s); }
#endif
            else if (it->kind == K_CHIPS) {
              static const char *CH[4] = { "Everyone", "Popular", "Newest", "Active now" };
              int x = 0;
              for (int k = 0; k < 4; k++) { int w = label_w(F_NAME, CH[k]) + 34; if (lx >= x && lx < x + w) { s->filter = k; s->psel = 0; } x += w + 12; }
            } else if (it->kind == K_PLAZA) {
              int best = -1; float bd = 60 * 60;
              for (int k = 0; k < s->np && k < 64; k++) {
                if (!tagVis[k]) continue;
                float dx = lx - tagX[k], dy = ly - (tagY[k] + 40);
                if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = k; }
              }
              if (best >= 0) { s->psel = best; open_user(s->pp[best].user); }
            }
#if STAGE >= 2
            else if ((it->kind == K_POST || it->kind == K_WIG || it->kind == K_FULLPOST || it->kind == K_WIGFULL) && it->yeahR.w && inr(lx, ly, it->yeahR.x, it->yeahR.y, it->yeahR.w, it->yeahR.h)) do_yeah(s, it);
            else if (it->kind == K_FULLPOST || it->kind == K_WIGFULL) { /* only the Yeah button reacts */ }
#endif
#if STAGE >= 2
            else if (it->kind == K_PROMPT && it->act == 1 && me.id && inr(lx, ly, it->btnR.x, it->btnR.y, it->btnR.w, it->btnR.h)) pe_open(s, 1);
#endif
            else if (it->sel_ok || it->kind == K_FEAT || it->kind == K_CNEW || it->kind == K_INFO) open_item(s, it);
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
    SDL_SetRenderDrawBlendMode(R, SDL_BLENDMODE_BLEND);
    if (bgTex) SDL_RenderCopy(R, bgTex, NULL, NULL); else fillr(0, 0, SW, SH, C_BG);
#if STAGE >= 2
    if (ed.open && !s) { ed_draw(); goto overlays; }
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
      if (s->kind == S_POST && s->n) card(CX, post_wrap_top - off, CW, post_wrap_bot - post_wrap_top);
      for (int i = 0; i < s->n; i++) {
        Item *it = &s->it[i];
        int Y = it->y - off;
        if (Y + it->h < TOPH - 30 || Y > SH + 30) continue;
        draw_item(s, it, it->x, Y, i == s->sel && it->sel_ok);
      }
      for (int i = 0; i < s->nside; i++) {
        Item *it = &s->side[i];
        int Y = it->y - off;
        if (Y + it->h < TOPH - 30 || Y > SH + 30) continue;
        draw_item(s, it, it->x, Y, 0);
      }
      int endY = s->contentH - 50 - off;
      if (s->loading) draw_label(F_BOLD, C_SEC2, "Loading...", colx(s) + colw(s) / 2, endY - 4, 1);
      if (s->err[0]) {
        boxr(colx(s), endY - 10, colw(s), 64, 0xffe8e8, 0xdd9999, 1);
        char eb[260]; snprintf(eb, sizeof eb, "%s   (press Y to try again)", s->err);
        draw_label(F_SMALL, C_RED, eb, colx(s) + 16, endY + 10, 0);
      }
      SDL_RenderSetClipRect(R, NULL);
    }
    draw_header();
    draw_footer(hint);
#if STAGE >= 2
    if (pe.open) pe_draw();
    if (ed.open) ed_draw();
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
