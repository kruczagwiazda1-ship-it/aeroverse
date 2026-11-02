/*
 * Aeroverse for Nintendo 3DS (Homebrew Launcher, .3dsx)
 * Same website, same account, same posts as the PC and Wii U versions.
 * Top screen: the selected post / drawing / community big. Bottom screen (touch): lists, buttons, drawing.
 */
#define APP_VERSION "1.0"
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
#ifndef API
#define API "https://aeroverse.lukwszystkorobicz.pl"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>
#include "cJSON.h"
#include "cacert_pem.h"
#include "nunito_reg_ttf.h"
#include "nunito_bold_ttf.h"
#include "logo_img.h"
#include "icons_img.h"
#include "body_img.h"
#include "gfx.h"
#include "plat.h"
#include "net.h"

#define LOGIN_FILE DATA_DIR "login.txt"
#define SETTINGS_FILE DATA_DIR "settings.txt"

/* ------------------------------------------------------------------ colours (website app.css) */
#define C_INK 0x323232
#define C_SOFT 0x7a7a7a
#define C_LINE 0xc9d6dd
#define C_GREEN 0x5ac800
#define C_GREEN_D 0x3f9a00
#define C_BLUE 0x1b8fd3
#define C_SEC2 0x145d86
#define C_RED 0xe5322d
#define C_ORANGE 0xf09a1a
#define C_WHITE 0xffffff
#define C_YEAH 0xfff8e1
#define C_YEAH_T 0x9a6a00
#define C_YEAH_ON 0xffd54a

static const char *PALHEX[32] = { "ffffff","c2c3c7","83769c","5f574f","1d2b53","000000","7e2553","ab5236","ff004d","ff77a8","ffccaa","ffa300","ffec27","00e436","008751","29adff",
  "065ab5","4b2d7f","ff6e59","f0b27a","a8e6cf","2ab7ca","d291bc","8d5524","3f3f74","639bff","99e550","37946e","fbf236","d95763","663931","222034" };
static uint32_t PAL[32];
static const uint32_t AVC[8] = { 0xff004d, 0xffa300, 0x00e436, 0x29adff, 0x4b2d7f, 0x2ab7ca, 0xd291bc, 0x99e550 };
static const uint32_t SHIRT_COLS[12] = { 0xe5322d, 0xf09a1a, 0xf5d90a, 0x5ac800, 0x1b8fd3, 0x4b2d7f, 0xff77a8, 0x00b7a8, 0x8d5524, 0x222034, 0xffffff, 0x8a8a8a };
static uint32_t name_hash(const char *u) { uint32_t n = 0; for (const unsigned char *p = (const unsigned char *)(u ? u : "?"); *p; p++) n = n * 31 + *p; return n; }

static Cv TOP, BOT;
static Img bgTop, bgBot, logoImg, logoSm, icons[5], bodyImg;
static uint32_t frameNo, nowMs;
static int running = 1;
static int dirty = 1;   /* redraw needed */
static int swallow;     /* ignore the touch until the stylus is lifted (after the editor or keyboard closes) */

/* ------------------------------------------------------------------ small helpers */
static const char *jstr(cJSON *o, const char *k) { cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL; return cJSON_IsString(v) ? v->valuestring : NULL; }
static double jnum(cJSON *o, const char *k) { cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL; return cJSON_IsNumber(v) ? v->valuedouble : 0; }
static int jbool(cJSON *o, const char *k) { cJSON *v = o ? cJSON_GetObjectItem(o, k) : NULL; return cJSON_IsTrue(v) || (cJSON_IsNumber(v) && v->valueint); }
static int utf8_len(const char *s) { int n = 0; for (; s && *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++; return n; }

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
  static const char *tags[] = { "color", "glow", "font", "size", "grad", "i", "b", "u", "s", "center", "big", "small", "rainbow", "shake", "wave" };
  while (i < n && j < cap) {
    if (s[i] == '[') {
      const char *e = strchr(s + i, ']');
      if (e && e - (s + i) < 60) {
        size_t L = (size_t)(e - (s + i)) + 1;
        if (!strncmp(s + i, "[img=", 5)) { memcpy(o + j, "[picture]", 9); j += 9; i += L; continue; }
        const char *p = s + i + 1; if (*p == '/') p++;
        int hit = 0;
        for (int k = 0; k < (int)(sizeof tags / sizeof tags[0]); k++) { size_t tl = strlen(tags[k]); if (!strncmp(p, tags[k], tl) && (p[tl] == '=' || p[tl] == ']')) { hit = 1; break; } }
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

/* ------------------------------------------------------------------ drawings (320x180 RLE, same as the website) */
#define DW 320
#define DH 180
#define DPIX (DW * DH)
static int b32v(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'v') return c - 'a' + 10; return -1; }
static int hexv(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; }
static int decode_drawing(const char *code, uint8_t *px, uint32_t *pal) {
  for (int i = 0; i < 32; i++) pal[i] = PAL[i];
  memset(px, 0, DPIX);
  if (!code || !*code) return -1;
  const char *p = code;
  int wig = 0;
  if (*p == '~') { p++; wig = 2; if (*p >= '1' && *p <= '9') { wig = *p - '0'; p++; } }
  if (*p == '{') {
    p++; int i = 0;
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
    int c = b32v(*p); if (c < 0) break;
    p++;
    char *end; long n = strtol(p, &end, 36);
    if (end == p || n <= 0 || o + n > DPIX) break;
    memset(px + o, c, (size_t)n); o += (int)n; p = end;
    if (*p == ',') p++;
  }
  return wig;
}
static char *encode_drawing(const uint8_t *px, int wig) {
  static const char *B32 = "0123456789abcdefghijklmnopqrstuv";
  size_t cap = 64 * 1024, n = 0;
  char *o = malloc(cap);
  if (wig) { o[n++] = '~'; if (wig != 2) o[n++] = (char)('0' + wig); }
  size_t head = n; int i = 0;
  while (i < DPIX) {
    int c = px[i] & 31, j = i;
    while (j < DPIX && (px[j] & 31) == c) j++;
    int len = j - i; char num[8]; int k = 0;
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
/* draw a drawing (with the website's wiggle: wavy displacement, 4 frames) scaled to w x h */
static void draw_drawing(Cv *c, const uint8_t *px, const uint32_t *pal, int wig, int frame, int x, int y, int w, int h) {
  float amp = frame >= 0 ? (wig ? wig : 2) * 1.1f : 0, phX = frame * 1.7f, phY = frame * 2.2f;
  int x0 = x < c->cx0 ? c->cx0 : x, y0 = y < c->cy0 ? c->cy0 : y, x1 = x + w > c->cx1 ? c->cx1 : x + w, y1 = y + h > c->cy1 ? c->cy1 : y + h;
  if (x0 >= x1 || y0 >= y1) return;
  /* the wiggle offsets depend only on the source row / column: work them out once (sinf per pixel is slow on a 3DS) */
  static int colSx[400], colOy[400];
  for (int xx = x0; xx < x1; xx++) { int sx = (xx - x) * DW / w; colSx[xx - x0] = sx; colOy[xx - x0] = amp ? (int)lroundf(amp * sinf(sx / 7.0f + phY)) : 0; }
  for (int yy = y0; yy < y1; yy++) {
    int sy0 = (yy - y) * DH / h;
    int ox = amp ? (int)lroundf(amp * sinf(sy0 / 6.0f + phX)) : 0;
    uint32_t *row = &c->px[yy * c->w];
    for (int xx = x0; xx < x1; xx++) {
      int sx = colSx[xx - x0] + ox, sy = sy0 + colOy[xx - x0];
      if (sx < 0) sx = 0;
      if (sx >= DW) sx = DW - 1;
      if (sy < 0) sy = 0;
      if (sy >= DH) sy = DH - 1;
      row[xx] = 0xff000000u | pal[px[sy * DW + sx] & 31];
    }
  }
}

/* ------------------------------------------------------------------ session */
typedef struct { int id; char username[24]; char display[48]; char avatar[80]; } Me;
static Me me;
static char toastMsg[200]; static uint32_t toastUntil;
static void toast(const char *m) { snprintf(toastMsg, sizeof toastMsg, "%s", m); toastUntil = nowMs + 3200; dirty = 1; }
static void api_error(long code, const char *body, const char *err, char *out, size_t n) {
  if (err && *err) { snprintf(out, n, "No connection: %s", err); return; }
  cJSON *j = body ? cJSON_Parse(body) : NULL;
  cJSON *e = j ? cJSON_GetObjectItem(j, "error") : NULL;
  if (cJSON_IsString(e)) snprintf(out, n, "%s", e->valuestring); else snprintf(out, n, "Server error (%ld)", code);
  cJSON_Delete(j);
}
static void save_token(void) { FILE *f = fopen(LOGIN_FILE, "w"); if (!f) return; mx_lock(&netMx); fprintf(f, "%s\n", sessionTok); mx_unlock(&netMx); fclose(f); }
static void load_token(void) {
  FILE *f = fopen(LOGIN_FILE, "r"); if (!f) return;
  char b[96] = { 0 };
  if (fgets(b, sizeof b, f)) {
    b[strcspn(b, "\r\n")] = 0;
    int ok = strlen(b) >= 16;
    for (char *p = b; *p; p++) if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) ok = 0;
    if (ok) snprintf(sessionTok, sizeof sessionTok, "%s", b);
  }
  fclose(f);
}
static void forget_token(void) { mx_lock(&netMx); sessionTok[0] = 0; mx_unlock(&netMx); remove(LOGIN_FILE); }
static int rulesOk;
static void save_settings(void) { FILE *f = fopen(SETTINGS_FILE, "w"); if (f) { fprintf(f, "rules=%d\n", rulesOk); fclose(f); } }
static void load_settings(void) {
  FILE *f = fopen(SETTINGS_FILE, "r"); if (!f) return;
  char ln[64]; int v;
  while (fgets(ln, sizeof ln, f)) if (sscanf(ln, "rules=%d", &v) == 1) rulesOk = v;
  fclose(f);
}
static void me_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)serial; (void)len; (void)err; (void)extra;
  if (code != 200 || !body) return;
  cJSON *j = cJSON_Parse(body), *u = j ? cJSON_GetObjectItem(j, "user") : NULL;
  memset(&me, 0, sizeof me);
  if (cJSON_IsObject(u)) {
    me.id = (int)jnum(u, "id");
    snprintf(me.username, sizeof me.username, "%s", jstr(u, "username") ? jstr(u, "username") : "");
    snprintf(me.display, sizeof me.display, "%s", jstr(u, "display") ? jstr(u, "display") : me.username);
    snprintf(me.avatar, sizeof me.avatar, "%s", jstr(u, "avatar") ? jstr(u, "avatar") : "");
  } else if (sessionTok[0]) forget_token();
  cJSON_Delete(j);
  dirty = 1;
}

/* ------------------------------------------------------------------ self update (GitHub releases "3ds-build-N") */
static int updateBuild, updating;
static char updateUrl[300];
static char appPath[256] = APP_PATH_DEFAULT;
static void update_check_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)serial; (void)len; (void)err; (void)extra;
  if (code != 200 || !body) return;
  cJSON *j = cJSON_Parse(body), *r;
  int best = 0;
  cJSON_ArrayForEach(r, j) {
    const char *tag = jstr(r, "tag_name");
    if (!tag || strncmp(tag, "3ds-build-", 10)) continue;
    int b = atoi(tag + 10);
    if (b <= best) continue;
    cJSON *a, *assets = cJSON_GetObjectItem(r, "assets");
    cJSON_ArrayForEach(a, assets) {
      const char *nm = jstr(a, "name"), *u = jstr(a, "browser_download_url");
      if (nm && u && !strcmp(nm, "aeroverse.3dsx")) { best = b; snprintf(updateUrl, sizeof updateUrl, "%s", u); }
    }
  }
  cJSON_Delete(j);
  if (best > APP_BUILD && APP_BUILD > 0) { updateBuild = best; toast("A new version of Aeroverse is available! See Home."); }
}
static void update_check(void) {
  remove(DATA_DIR "update.part");
  if (!UPDATE_REPO[0]) return;
  net_send2("GET", "https://api.github.com/repos/" UPDATE_REPO "/releases?per_page=30", NULL, update_check_cb, 0, 0, 1);
}
static void update_dl_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)serial; (void)extra;
  updating = 0;
  if (code != 200 || !body || len < 50000 || memcmp(body, "3DSX", 4)) { char m[200]; snprintf(m, sizeof m, "Update failed: %s", err && *err ? err : "download error"); toast(m); return; }
  FILE *f = fopen(DATA_DIR "update.part", "wb");
  if (!f) { toast("Update failed: cannot write to the SD card."); return; }
  size_t w = fwrite(body, 1, len, f);
  int ok = fclose(f) == 0 && w == len;
  if (!ok) { remove(DATA_DIR "update.part"); toast("Update failed: SD card full?"); return; }
  /* the .3dsx is not locked while it runs, so it can simply be replaced */
  remove(appPath);
  if (rename(DATA_DIR "update.part", appPath) != 0) { toast("Update failed: cannot replace the app file."); return; }
  updateBuild = -1;
  toast("Updated! Press START and open Aeroverse again.");
}
static void update_start(void) {
  if (updating || updateBuild <= 0 || !updateUrl[0]) return;
  updating = 1;
  toast("Downloading the update... keep the app open.");
  net_send2("GET", updateUrl, NULL, update_dl_cb, 0, 0, 1);
}

/* ------------------------------------------------------------------ screens and list items */
enum { S_FEED, S_COMMS, S_COMM, S_POST, S_PLAZA, S_GAMES, S_GALLERY, S_USER, S_LOGIN, S_COMPOSE, S_RULES };
enum { K_POST, K_REPLY, K_COMM, K_SEC, K_INFO, K_ACTION, K_MORE, K_PERSON, K_GAME, K_WIG, K_FIELD, K_RULE, K_PROFILE, K_EMPTY };
enum { A_NONE, A_SEND, A_SIGNOUT, A_RULESOK, A_UPDATE, A_MODE, A_DRAW, A_WIGTOG, A_RETRY, A_RULES };

typedef struct {
  int kind, act, id, yeahs, replies, mine, posts, icon, tags;
  double created;
  char *name, *user, *cname, *cid, *body, *drawing, *title, *avatar, *age, *msg, *plain, *style;
  uint8_t *dpx; uint32_t dpal[32]; int wig;
  int x, y, w, h;
} Item;
#define LIST_H 186
#define BAR_Y 186
#define TAB_Y 212
typedef struct Screen {
  int kind, serial, mode, tab;
  Item *it; int n, cap;
  int sel; float scroll, vel; int contentH;
  int loading, lastId, page, more;
  char err[160];
  char cid[64], cname[96];
  int postId;
  /* compose / login */
  int ckind;           /* 0 post, 1 reply, 2 wiggle entry */
  char text[1024], text2[100], pass[128];
  uint8_t *draw; int drawWig;
  /* plaza */
  int filter;
} Screen;
#define MAXS 24
static Screen *stack[MAXS]; static int depth;
static Screen *roots[5];
static int curTab;
static int serialCounter = 1;
static Screen *cur(void) { return depth ? stack[depth - 1] : NULL; }
static Screen *by_serial(int s) { for (int i = 0; i < depth; i++) if (stack[i]->serial == s) return stack[i]; for (int i = 0; i < 5; i++) if (roots[i] && roots[i]->serial == s) return roots[i]; return NULL; }
static void item_free(Item *it) {
  free(it->name); free(it->user); free(it->cname); free(it->cid); free(it->body); free(it->drawing); free(it->title); free(it->avatar); free(it->age); free(it->msg); free(it->plain); free(it->style); free(it->dpx);
}
static Item *add_item(Screen *s, int kind) {
  if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 32; s->it = realloc(s->it, sizeof(Item) * (size_t)s->cap); }
  Item *it = &s->it[s->n++]; memset(it, 0, sizeof *it); it->kind = kind; it->w = 304;
  return it;
}
static void clear_items(Screen *s, int from) { for (int i = from; i < s->n; i++) item_free(&s->it[i]); if (from < s->n) s->n = from; dirty = 1; }
static Screen *new_screen(int kind) { Screen *s = calloc(1, sizeof(Screen)); s->kind = kind; s->serial = serialCounter++; s->sel = -1; return s; }
static void free_screen(Screen *s) { clear_items(s, 0); free(s->it); free(s->draw); free(s); }
static void push(Screen *s) { if (depth < MAXS) stack[depth++] = s; else free_screen(s); dirty = 1; }
static void pop(void) { if (depth <= 1) return; Screen *s = stack[--depth]; int root = 0; for (int i = 0; i < 5; i++) if (roots[i] == s) root = 1; if (!root) free_screen(s); dirty = 1; }
static int sel_ok(const Item *it) { return !(it->kind == K_SEC || it->kind == K_INFO || it->kind == K_EMPTY || it->kind == K_RULE || it->kind == K_MORE); }

static void fill_post(Item *it, cJSON *p) {
  cJSON *a = cJSON_GetObjectItem(p, "author"), *c = cJSON_GetObjectItem(p, "community");
  it->id = (int)jnum(p, "id"); it->yeahs = (int)jnum(p, "yeahs"); it->replies = (int)jnum(p, "replies"); it->mine = jbool(p, "mine");
  it->created = jnum(p, "created_at");
  const char *disp = jstr(a, "display"), *un = jstr(a, "username");
  it->name = xstrdup(disp && *disp ? disp : un); it->user = xstrdup(un);
  it->cname = xstrdup(cJSON_IsObject(c) ? jstr(c, "name") : NULL);
  it->cid = xstrdup(cJSON_IsObject(c) ? jstr(c, "id") : NULL);
  it->body = xstrdup(jstr(p, "body"));
  it->plain = strip_markup(it->body, 2000);
  it->drawing = xstrdup(jstr(p, "drawing"));
  it->avatar = xstrdup(jstr(a, "avatar"));
  it->style = xstrdup(jstr(p, "style"));
}
static void item_drawing(Item *it) {
  if (it->dpx || !it->drawing || !*it->drawing) return;
  it->dpx = malloc(DPIX);
  it->wig = decode_drawing(it->drawing, it->dpx, it->dpal);
  if (it->wig < 0) { free(it->dpx); it->dpx = NULL; }
}

/* ---- post styles from the website's NEW POST editor: "bg.font.size[.align deco]" (fonts are shown in Nunito here) */
static const uint32_t PC_BG[12] = { 0, 0xfff3a8, 0xffd0e1, 0xcdf5d4, 0xcfeaff, 0xe3d6ff, 0xffd9a8, 0x2b2d3a, 0xe5322d, 0x5ac800, 0x1b8fd3, 0x234a3a };
static const uint32_t PC_FG[20] = { 0x323232, 0x3a3300, 0x4a1030, 0x0d3b1a, 0x0b2e4f, 0x2a1458, 0x4a2300, 0xf4f4f8, 0xffffff, 0xffffff, 0xffffff, 0xf1f1e6,
  0xffffff, 0x0b2a45, 0x0b3a14, 0xffffff, 0xffffff, 0xe8f4ff, 0x073a63, 0x4a3300 };
static const uint32_t TH_A[8] = { 0x0b74c9, 0xffffff, 0x3fae4a, 0x7cc4f5, 0xff9a3c, 0x0a1f4d, 0xbfeaff, 0xfff7c2 };
static const uint32_t TH_B[8] = { 0xb6e8ff, 0xdef0fc, 0xd9f7a8, 0x2f8fd8, 0xa04bd6, 0x1b4a8a, 0x6cc3f2, 0xffd24d };
typedef struct { int bg, size, align, deco; } PStyle;
static PStyle post_style(const char *style) {
  PStyle ps = { 0, 1, 0, 0 };
  if (style && *style) {
    int a = -1, b = -1, c = -1, d = 0, e = 0;
    int n = sscanf(style, "%d.%d.%d.%1d%1d", &a, &b, &c, &d, &e);
    if (n >= 3 && a >= 0 && a <= 19 && c >= 0 && c <= 2) { ps.bg = a; ps.size = c; if (n == 5) { ps.align = d; ps.deco = e; } }
  }
  return ps;
}
static void style_box(Cv *c, PStyle ps, int x, int y, int w, int h) {
  if (ps.deco == 1) { rrect(c, x - 2, y - 2, w + 4, h + 4, 8, 0x6bb7e6); rrect(c, x, y, w, h, 6, C_WHITE); }
  if (ps.deco == 2) rrect(c, x - 3, y - 3, w + 6, h + 6, 10, 0x6000aaff);
  if (ps.bg >= 12) rrect_g(c, x, y, w, h, 8, TH_A[ps.bg - 12], TH_B[ps.bg - 12]);
  else if (ps.bg) rrect(c, x, y, w, h, ps.deco == 3 ? 0 : 4, PC_BG[ps.bg]);
}
static int styled_box(PStyle ps) { return ps.bg || ps.deco; }

/* ---- measuring items (bottom list, width 304) */
#define LX 8
static int post_text_lines(Item *it) { return it->plain && *it->plain ? (it->dpx ? 1 : 3) : 0; }
static void measure(Item *it) {
  switch (it->kind) {
  case K_POST: case K_REPLY: {
    item_drawing(it);
    int h = 6 + 24 + 4;
    int tl = post_text_lines(it);
    if (tl) h += text_wrap(NULL, FR, 11, 0, 0, it->w - 16, it->plain, 0, 14, tl) + 2;
    if (it->dpx) h += 72 + 4;
    h += 16;
    it->h = h; break; }
  case K_COMM: it->h = 48; break;
  case K_SEC: it->h = 20; break;
  case K_INFO: it->h = text_wrap(NULL, FR, 10, 0, 0, it->w - 8, it->body, 0, 13, 0) + 6; break;
  case K_EMPTY: it->h = text_wrap(NULL, FR, 10, 0, 0, it->w - 24, it->body, 0, 13, 0) + 30; break;
  case K_ACTION: it->h = 30; break;
  case K_MORE: it->h = 26; break;
  case K_PERSON: it->h = 40; break;
  case K_GAME: it->h = 70; break;
  case K_WIG: item_drawing(it); it->w = 98; it->h = 56 + 30; break;
  case K_FIELD: it->h = 44; break;
  case K_RULE: { int tw = it->w - 40; it->h = 8 + 14 + text_wrap(NULL, FR, 10, 0, 0, tw, it->body, 0, 12, 0) + 8; break; }
  case K_PROFILE: it->h = 8 + 50 + (it->body && *it->body ? text_wrap(NULL, FR, 10, 0, 0, it->w - 16, it->plain, 0, 13, 3) + 4 : 0) + 8; break;
  }
}
static void layout(Screen *s) {
  int y = 6, col = 0, rowH = 0;
  for (int i = 0; i < s->n; i++) {
    Item *t = &s->it[i];
    if (t->kind == K_WIG) {
      t->x = LX + col * 103; t->y = y; if (t->h > rowH) rowH = t->h;
      if (++col == 3) { col = 0; y += rowH + 5; rowH = 0; }
      continue;
    }
    if (col) { col = 0; y += rowH + 5; rowH = 0; }
    t->x = LX; t->y = y; y += t->h + (t->kind == K_SEC ? 2 : 6);
  }
  if (col) y += rowH + 5;
  s->contentH = y + 6;
}
static void add_sec(Screen *s, const char *t) { Item *h = add_item(s, K_SEC); h->title = xstrdup(t); measure(h); }
static void add_info(Screen *s, const char *t) { Item *h = add_item(s, K_INFO); h->body = xstrdup(t); measure(h); }
static void add_empty(Screen *s, const char *t) { Item *h = add_item(s, K_EMPTY); h->body = xstrdup(t); measure(h); }
static void add_action(Screen *s, int act, const char *t) { Item *h = add_item(s, K_ACTION); h->act = act; h->title = xstrdup(t); measure(h); }
static void finish(Screen *s) {
  layout(s);
  int autoSel = s->kind == S_LOGIN || s->kind == S_COMPOSE || s->kind == S_RULES || s->kind == S_POST || s->kind == S_GALLERY;
  if (!autoSel) { if (s->sel >= s->n || (s->sel >= 0 && !sel_ok(&s->it[s->sel]))) s->sel = -1; dirty = 1; return; }
  if (s->sel < 0 || s->sel >= s->n || !sel_ok(&s->it[s->sel])) { s->sel = -1; for (int i = 0; i < s->n; i++) if (sel_ok(&s->it[i])) { s->sel = i; break; } }
  dirty = 1;
}

/* ------------------------------------------------------------------ loading */
static void load_screen(Screen *s, int append);
static void posts_cb(int serial, long code, const char *body, size_t len, const char *err, int append) {
  (void)len;
  Screen *s = by_serial(serial); if (!s) return;
  s->loading = 0;
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); if (!append) { clear_items(s, 0); add_empty(s, s->err); add_action(s, A_RETRY, "Try again"); } finish(s); return; }
  s->err[0] = 0;
  cJSON *j = cJSON_Parse(body), *arr = j ? cJSON_GetObjectItem(j, "posts") : NULL, *p;
  if (s->n && s->it[s->n - 1].kind == K_MORE) clear_items(s, s->n - 1);
  int cnt = 0;
  cJSON_ArrayForEach(p, arr) { Item *it = add_item(s, K_POST); fill_post(it, p); measure(it); s->lastId = it->id; cnt++; }
  if (!append && cnt == 0) add_empty(s, s->kind == S_FEED ? "Nobody has posted yet." : "No posts here yet. Be the first!");
  s->more = cnt >= 15;
  if (s->more) { Item *m = add_item(s, K_MORE); measure(m); }
  cJSON_Delete(j);
  finish(s);
}
static void comms_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)len; (void)extra;
  Screen *s = by_serial(serial); if (!s) return;
  s->loading = 0; clear_items(s, 0);
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); add_empty(s, s->err); add_action(s, A_RETRY, "Try again"); finish(s); return; }
  cJSON *j = cJSON_Parse(body), *arr = j ? cJSON_GetObjectItem(j, "communities") : NULL, *c;
  add_sec(s, "Communities");
  cJSON_ArrayForEach(c, arr) {
    const char *age = jstr(c, "age");
    if (age && strcmp(age, "all")) continue;   /* all-ages communities only (age check is on the website) */
    Item *it = add_item(s, K_COMM);
    it->cid = xstrdup(jstr(c, "id")); it->name = xstrdup(jstr(c, "name")); it->body = xstrdup(jstr(c, "description"));
    it->msg = xstrdup(jstr(c, "message")); it->avatar = xstrdup(jstr(c, "thumb")); it->age = xstrdup(age ? age : "all"); it->posts = (int)jnum(c, "posts");
    measure(it);
  }
  add_info(s, "New communities and communities for older ages are on the website.");
  cJSON_Delete(j);
  finish(s);
}
static void post_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)len; (void)extra;
  Screen *s = by_serial(serial); if (!s) return;
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); toast(s->err); return; }
  cJSON *j = cJSON_Parse(body), *p = j ? cJSON_GetObjectItem(j, "post") : NULL;
  if (cJSON_IsObject(p)) {
    Item tmp; memset(&tmp, 0, sizeof tmp);
    if (s->n && s->it[0].kind == K_POST) { item_free(&s->it[0]); memset(&s->it[0], 0, sizeof(Item)); s->it[0].kind = K_POST; s->it[0].w = 304; fill_post(&s->it[0], p); measure(&s->it[0]); }
    else { Item *f = add_item(s, K_POST); fill_post(f, p); measure(f); }
    snprintf(s->cname, sizeof s->cname, "%s", s->it[0].name ? s->it[0].name : "");
  }
  cJSON_Delete(j);
  finish(s);
}
static void replies_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)len; (void)extra;
  Screen *s = by_serial(serial); if (!s) return;
  s->loading = 0;
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); toast(s->err); return; }
  int keep = (s->n && s->it[0].kind == K_POST) ? 1 : 0;
  clear_items(s, keep);
  cJSON *j = cJSON_Parse(body), *arr = j ? cJSON_GetObjectItem(j, "replies") : NULL, *r;
  int n = cJSON_GetArraySize(arr);
  char t[48]; snprintf(t, sizeof t, n == 1 ? "1 reply" : "%d replies", n);
  add_sec(s, t);
  cJSON_ArrayForEach(r, arr) { Item *it = add_item(s, K_REPLY); fill_post(it, r); measure(it); }
  if (!n) add_info(s, "No replies yet.");
  if (keep) s->it[0].replies = n;
  cJSON_Delete(j);
  finish(s);
}
static void user_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)len; (void)extra;
  Screen *s = by_serial(serial); if (!s) return;
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); toast(s->err); return; }
  cJSON *j = cJSON_Parse(body), *u = j ? cJSON_GetObjectItem(j, "user") : NULL;
  Item *it = add_item(s, K_PROFILE);
  const char *disp = jstr(u, "display");
  it->name = xstrdup(disp && *disp ? disp : jstr(u, "username")); it->user = xstrdup(jstr(u, "username"));
  it->body = xstrdup(jstr(u, "bio")); it->plain = strip_markup(it->body, 600);
  it->posts = (int)jnum(u, "posts"); it->created = jnum(u, "created_at"); it->avatar = xstrdup(jstr(u, "avatar")); it->icon = (int)jnum(u, "shirt");
  measure(it);
  /* the profile card must be first: move it in front of posts that may have arrived already */
  if (s->n > 1) { Item tmp = s->it[s->n - 1]; memmove(&s->it[1], &s->it[0], sizeof(Item) * (size_t)(s->n - 1)); s->it[0] = tmp; }
  if (me.id && it->user && !strcmp(it->user, me.username)) {
    Item *a = add_item(s, K_ACTION); a->act = A_SIGNOUT; a->title = xstrdup("Sign out"); measure(a);
    Item tmp = s->it[s->n - 1]; memmove(&s->it[2], &s->it[1], sizeof(Item) * (size_t)(s->n - 2)); s->it[1] = tmp;
  }
  cJSON_Delete(j);
  finish(s);
}
static void wiggle_cb(int serial, long code, const char *body, size_t len, const char *err, int append) {
  (void)len;
  Screen *s = by_serial(serial); if (!s) return;
  s->loading = 0;
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); toast(s->err); return; }
  if (s->n && s->it[s->n - 1].kind == K_MORE) clear_items(s, s->n - 1);
  cJSON *j = cJSON_Parse(body), *arr = j ? cJSON_GetObjectItem(j, "entries") : NULL, *e;
  int cnt = 0;
  cJSON_ArrayForEach(e, arr) {
    Item *it = add_item(s, K_WIG);
    cJSON *a = cJSON_GetObjectItem(e, "author");
    it->id = (int)jnum(e, "id"); it->yeahs = (int)jnum(e, "yeahs"); it->mine = jbool(e, "mine"); it->created = jnum(e, "created_at");
    it->title = xstrdup(jstr(e, "title")); it->drawing = xstrdup(jstr(e, "drawing"));
    const char *disp = jstr(a, "display"), *un = jstr(a, "username");
    it->name = xstrdup(disp && *disp ? disp : un); it->user = xstrdup(un); it->avatar = xstrdup(jstr(a, "avatar"));
    measure(it); cnt++;
  }
  if (!append && !cnt) add_empty(s, "No drawings yet. Be the first!");
  s->more = jbool(j, "more");
  if (s->more) { Item *m = add_item(s, K_MORE); measure(m); }
  cJSON_Delete(j);
  finish(s);
}
/* plaza people walk around a little island on the top screen */
typedef struct { float x, y, tx, ty, ph; int dir; } Walk;
static Walk walkers[60];
static void plaza_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)len; (void)extra;
  Screen *s = by_serial(serial); if (!s) return;
  s->loading = 0; clear_items(s, 0);
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); add_empty(s, s->err); add_action(s, A_RETRY, "Try again"); finish(s); return; }
  cJSON *j = cJSON_Parse(body), *arr = j ? cJSON_GetObjectItem(j, "people") : NULL, *p;
  static const char *FN[4] = { "Everyone", "Popular", "Newest", "Active now" };
  char t[64]; snprintf(t, sizeof t, "Plaza - %s", FN[s->filter & 3]); add_sec(s, t);
  int n = 0;
  cJSON_ArrayForEach(p, arr) {
    int tags = 0; cJSON *tg, *tl = cJSON_GetObjectItem(p, "tags");
    cJSON_ArrayForEach(tg, tl) if (cJSON_IsString(tg)) { if (!strcmp(tg->valuestring, "popular")) tags |= 1; if (!strcmp(tg->valuestring, "new")) tags |= 2; if (!strcmp(tg->valuestring, "active")) tags |= 4; }
    if (s->filter && !(tags & (1 << (s->filter - 1)))) continue;
    if (n >= 60) break;
    Item *it = add_item(s, K_PERSON);
    const char *disp = jstr(p, "display");
    it->name = xstrdup(disp && *disp ? disp : jstr(p, "username")); it->user = xstrdup(jstr(p, "username"));
    it->icon = (int)jnum(p, "shirt"); it->avatar = xstrdup(jstr(p, "avatar")); it->tags = tags;
    measure(it);
    uint32_t h = name_hash(it->user);
    walkers[n].x = walkers[n].tx = 60 + (h % 280); walkers[n].y = walkers[n].ty = 100 + ((h >> 9) % 100); walkers[n].ph = (h % 100) / 10.0f;
    n++;
  }
  if (!n) add_empty(s, "Nobody here right now.");
  cJSON_Delete(j);
  finish(s);
}
static void add_game(Screen *s, int mode, const char *title, const char *body) { Item *g = add_item(s, K_GAME); g->id = mode; g->title = xstrdup(title); g->body = xstrdup(body); measure(g); }
static void add_rule(Screen *s, int num, const char *title, const char *text, int instant) { Item *r = add_item(s, K_RULE); r->id = num; r->title = xstrdup(title); r->body = xstrdup(text); r->act = instant; measure(r); }
static void rules_items(Screen *s) {
  add_sec(s, "Aeroverse Rules");
  add_rule(s, 1, "No piracy and no ROMs", "We do not share pirated games, ROMs, cracks or links to them, and we do not ask for them. This leads to an instant ban.", 1);
  add_rule(s, 2, "No console modding help", "We do not help with and do not talk about modding the Switch, Switch 2, PS5 or Xbox Series X|S. This leads to an instant ban.", 1);
  add_rule(s, 3, "No swearing in All ages communities", "Keep your language clean in every community rated All ages. Higher-rated communities allow stronger language as their rating says, but never to attack someone.", 0);
  add_rule(s, 4, "No NSFW and no gore", "No sexual content, nudity, graphic violence or gore anywhere on Aeroverse, in any age rating, in text or in drawings. Inappropriate posts are deleted.", 0);
  add_rule(s, 5, "Be kind", "No harassment, bullying, threats, hate speech or picking on people. Disagree politely.", 0);
  add_rule(s, 6, "Protect privacy", "Do not post addresses, phone numbers, real names or photos of other people. Think twice before sharing your own.", 0);
  add_rule(s, 7, "No spam and no scams", "No ads, no flooding, no phishing and no suspicious links.", 0);
  add_rule(s, 8, "Post in the right place", "Stay on the topic of the community and respect its age rating.", 0);
  add_rule(s, 9, "One honest account", "Do not pretend to be the admin or someone else, and do not make new accounts to get around a ban.", 0);
  if (s->mode) add_action(s, A_RULESOK, "I have read the rules and accept");
  add_info(s, "If you break the rules, the admin can delete your posts or ban you (1 hour to 30 days, or for good). Piracy and console modding mean an instant ban.");
}
static void rebuild_form(Screen *s);
static void load_screen(Screen *s, int append) {
  char path[300];
  if (s->kind == S_LOGIN || s->kind == S_COMPOSE) { rebuild_form(s); return; }
  if (!append) { clear_items(s, 0); s->scroll = 0; s->vel = 0; s->sel = -1; s->lastId = 0; s->page = 0; s->more = 0; }
  s->loading = 1; s->err[0] = 0;
  switch (s->kind) {
  case S_FEED:
    if (!append) { if (updateBuild > 0) add_action(s, A_UPDATE, "New version - tap to update"); add_sec(s, "Latest posts"); }
    if (append) snprintf(path, sizeof path, "/api/posts?limit=15&before=%d", s->lastId); else snprintf(path, sizeof path, "/api/posts?limit=15");
    net_send("GET", path, NULL, posts_cb, s->serial, append);
    break;
  case S_COMM:
    if (append) snprintf(path, sizeof path, "/api/posts?limit=15&community=%s&before=%d", s->cid, s->lastId); else snprintf(path, sizeof path, "/api/posts?limit=15&community=%s", s->cid);
    net_send("GET", path, NULL, posts_cb, s->serial, append);
    break;
  case S_USER:
    if (append) snprintf(path, sizeof path, "/api/posts?limit=15&user=%s&before=%d", s->cid, s->lastId);
    else { char up[100]; snprintf(up, sizeof up, "/api/users/%s", s->cid); net_send("GET", up, NULL, user_cb, s->serial, 0); add_sec(s, "Posts"); snprintf(path, sizeof path, "/api/posts?limit=15&user=%s", s->cid); }
    net_send("GET", path, NULL, posts_cb, s->serial, append);
    break;
  case S_COMMS: net_send("GET", "/api/communities", NULL, comms_cb, s->serial, 0); break;
  case S_POST:
    snprintf(path, sizeof path, "/api/posts/%d", s->postId); net_send("GET", path, NULL, post_cb, s->serial, 0);
    snprintf(path, sizeof path, "/api/posts/%d/replies", s->postId); net_send("GET", path, NULL, replies_cb, s->serial, 0);
    break;
  case S_PLAZA: net_send("GET", "/api/plaza", NULL, plaza_cb, s->serial, 0); break;
  case S_GALLERY:
    if (append) s->page++;
    snprintf(path, sizeof path, "/api/wiggle?mode=%s&page=%d", s->mode ? "ultra" : "super", s->page);
    net_send("GET", path, NULL, wiggle_cb, s->serial, append);
    break;
  case S_GAMES:
    s->loading = 0;
    add_sec(s, "Games & Challenges");
    add_game(s, 0, "Super Wiggle Paint", "Draw anything while the whole canvas shakes, flips upside down and changes colour. Say what it is and send it to the gallery.");
    add_game(s, 1, "Ultra Wiggle Paint", "Everything is 3 times faster. Sometimes your last move disappears, and sometimes a box pops up to bug you. Good luck!");
    add_info(s, "Tap a game to see its gallery, then tap Draw! to play. You draw on this screen with the stylus.");
    break;
  case S_RULES: s->loading = 0; rules_items(s); break;
  default: s->loading = 0; break;
  }
  finish(s);
}

/* ------------------------------------------------------------------ forms: sign in, new post / reply */
static Screen *make_profile_root(void) {
  if (me.id) { Screen *u = new_screen(S_USER); snprintf(u->cid, sizeof u->cid, "%s", me.username); u->tab = 4; return u; }
  Screen *l = new_screen(S_LOGIN); l->tab = 4; return l;
}
static void field(Screen *s, int act, const char *label, const char *value) { Item *f = add_item(s, K_FIELD); f->act = act; f->title = xstrdup(label); f->body = xstrdup(value); measure(f); }
static void rebuild_form(Screen *s) {
  int keepSel = s->sel;
  clear_items(s, 0);
  if (s->kind == S_LOGIN) {
    add_sec(s, s->mode ? "Create an account" : "Sign in to Aeroverse");
    field(s, 1, "Username", s->text2);
    if (s->mode) field(s, 3, "Display name (optional)", s->cname);
    char m[110]; int n = utf8_len(s->pass); if (n > 100) n = 100; memset(m, '*', (size_t)n); m[n] = 0;
    field(s, 2, s->mode ? "Password (8 or more characters)" : "Password", m);
    if (s->mode) { add_info(s, "By creating an account you accept the Aeroverse rules."); add_action(s, A_RULES, "Read the rules"); }
    add_action(s, A_SEND, s->loading ? "..." : s->mode ? "Create account" : "Sign in");
    add_action(s, A_MODE, s->mode ? "I already have an account" : "Create a new account");
  } else {
    add_sec(s, s->ckind == 1 ? "Reply" : s->cid[0] ? "New post in a community" : "New post");
    if (s->ckind == 0 && s->cname[0]) { char t[140]; snprintf(t, sizeof t, "Posting in %s", s->cname); add_info(s, t); }
    if (s->ckind == 0 && !s->cid[0]) add_info(s, "Your post shows up on Home and on your profile.");
    field(s, 1, s->ckind == 1 ? "Your reply (tap to type)" : "Your post (tap to type)", s->text);
    field(s, 4, s->draw ? "Drawing (tap to change)" : "Add a drawing (optional)", s->draw ? (s->drawWig ? "Drawing with wiggle" : "Drawing") : "Tap to draw on this screen");
    if (s->draw) add_action(s, A_WIGTOG, s->drawWig ? "Wiggle: on (tap to turn off)" : "Wiggle: off (tap to turn on)");
    add_action(s, A_SEND, s->loading ? "Sending..." : "Send");
  }
  finish(s);
  if (keepSel >= 0 && keepSel < s->n && sel_ok(&s->it[keepSel])) s->sel = keepSel;
}
static void sent_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)len; (void)extra;
  Screen *s = by_serial(serial); if (!s) return;
  s->loading = 0;
  if (code != 200) { char e[200]; api_error(code, body, err, e, sizeof e); toast(e); rebuild_form(s); return; }
  if (s->kind == S_LOGIN) {
    /* the Profile tab switches to the profile when /api/me answers (see the main loop) */
    save_token(); net_send("GET", "/api/me", NULL, me_cb, 0, 0); toast("Signed in!");
    rebuild_form(s);
    return;
  }
  toast(s->ckind == 1 ? "Reply sent!" : "Posted!");
  pop();
  Screen *c = cur(); if (c) load_screen(c, 0);
  if (roots[0] && roots[0] != c) clear_items(roots[0], 0);
}
static void compose_send(Screen *s) {
  if (s->loading) return;
  cJSON *o = cJSON_CreateObject();
  if (s->kind == S_LOGIN) {
    if (!s->text2[0] || !s->pass[0]) { toast("Type your username and password."); cJSON_Delete(o); return; }
    cJSON_AddStringToObject(o, "username", s->text2); cJSON_AddStringToObject(o, "password", s->pass);
    if (s->mode) { cJSON_AddStringToObject(o, "display", s->cname[0] ? s->cname : s->text2); cJSON_AddTrueToObject(o, "rules"); }
    char *b = cJSON_PrintUnformatted(o);
    net_send("POST", s->mode ? "/api/register" : "/api/login", b, sent_cb, s->serial, 0);
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
  rebuild_form(s);
}
static void open_compose(int ckind, const char *cid, const char *cname, int postId) {
  if (!me.id) { toast("Sign in first (Profile tab)."); return; }
  Screen *c = new_screen(S_COMPOSE); c->ckind = ckind; c->postId = postId;
  snprintf(c->cid, sizeof c->cid, "%s", cid ? cid : ""); snprintf(c->cname, sizeof c->cname, "%s", cname ? cname : "");
  push(c); rebuild_form(c);
}
static void yeah_cb(int serial, long code, const char *body, size_t len, const char *err, int id) {
  (void)len;
  Screen *s = by_serial(serial);
  if (code != 200) { char e[200]; api_error(code, body, err, e, sizeof e); toast(e); return; }
  if (!s) return;
  cJSON *j = cJSON_Parse(body);
  for (int i = 0; i < s->n; i++) { Item *it = &s->it[i]; if (it->id == id && (it->kind == K_POST || it->kind == K_REPLY || it->kind == K_WIG)) { it->yeahs = (int)jnum(j, "yeahs"); it->mine = jbool(j, "mine"); } }
  cJSON_Delete(j);
  dirty = 1;
}
static void do_yeah(Screen *s, Item *it) {
  if (!it || !(it->kind == K_POST || it->kind == K_REPLY || it->kind == K_WIG)) return;
  if (!me.id) { toast("Sign in to give a Yeah!"); return; }
  char path[64];
  if (it->kind == K_WIG) snprintf(path, sizeof path, "/api/wiggle/%d/yeah", it->id); else snprintf(path, sizeof path, "/api/posts/%d/yeah", it->id);
  it->mine = !it->mine; it->yeahs += it->mine ? 1 : -1;
  net_send("POST", path, "{}", yeah_cb, s->serial, it->id);
  dirty = 1;
}
static void signout_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) { (void)serial; (void)code; (void)body; (void)len; (void)err; (void)extra; }

/* ------------------------------------------------------------------ drawing editor (bottom screen) */
typedef struct {
  int on, challenge, ultra;
  uint8_t px[DPIX];
  uint8_t *undo[24]; int nundo;
  int color, size, tool, wig, strength;   /* tool 0 pen 1 eraser 2 fill */
  int lastX, lastY, drawing;
  Screen *target;
  /* chaos (Super / Ultra Wiggle) */
  int fx, fy, shx, shy, invert; uint32_t tint; uint32_t nextChaos; int annoy; uint32_t annoyUntil;
  char title[64];
} Editor;
static Editor ed;
#define ED_TOOLY 180
static void ed_snap(void) {
  if (ed.nundo == 24) { free(ed.undo[0]); memmove(ed.undo, ed.undo + 1, sizeof(ed.undo[0]) * 23); ed.nundo--; }
  ed.undo[ed.nundo] = malloc(DPIX); memcpy(ed.undo[ed.nundo], ed.px, DPIX); ed.nundo++;
}
static void ed_undo(void) { if (!ed.nundo) return; ed.nundo--; memcpy(ed.px, ed.undo[ed.nundo], DPIX); free(ed.undo[ed.nundo]); dirty = 1; }
static void ed_open(Screen *target, int challenge, int ultra) {
  for (int i = 0; i < ed.nundo; i++) free(ed.undo[i]);
  memset(&ed, 0, sizeof ed);
  ed.on = 1; ed.target = target; ed.challenge = challenge; ed.ultra = ultra; ed.color = 5; ed.size = 2; ed.strength = 3;
  if (target && target->draw) { memcpy(ed.px, target->draw, DPIX); ed.wig = target->drawWig ? 1 : 0; if (target->drawWig) ed.strength = target->drawWig; }
  ed.nextChaos = nowMs + 1500;
  dirty = 1;
}
static void ed_close(void) { for (int i = 0; i < ed.nundo; i++) free(ed.undo[i]); ed.nundo = 0; ed.on = 0; dirty = 1; swallow = 1; }
static void ed_dot(int cx, int cy) {
  int r = ed.size, c = ed.tool == 1 ? 0 : ed.color;
  for (int y = cy - r + 1; y < cy + r; y++) for (int x = cx - r + 1; x < cx + r; x++) {
    if (x < 0 || y < 0 || x >= DW || y >= DH) continue;
    if (r > 1 && (x - cx) * (x - cx) + (y - cy) * (y - cy) > (r - 1) * (r - 1) + 1) continue;
    ed.px[y * DW + x] = (uint8_t)c;
  }
}
static void ed_line(int x0, int y0, int x1, int y1) {
  int n = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
  for (int i = 0; i <= n; i++) ed_dot(x0 + (n ? (x1 - x0) * i / n : 0), y0 + (n ? (y1 - y0) * i / n : 0));
}
static void ed_flood(int x, int y) {
  int target = ed.px[y * DW + x], rep = ed.color;
  if (target == rep) return;
  int *st = malloc(sizeof(int) * DPIX), n = 0;
  st[n++] = y * DW + x;
  while (n) {
    int p = st[--n];
    if (ed.px[p] != target) continue;
    ed.px[p] = (uint8_t)rep;
    int px = p % DW, py = p / DW;
    if (px > 0 && n < DPIX) st[n++] = p - 1;
    if (px < DW - 1 && n < DPIX) st[n++] = p + 1;
    if (py > 0 && n < DPIX) st[n++] = p - DW;
    if (py < DH - 1 && n < DPIX) st[n++] = p + DW;
  }
  free(st);
}
static void ed_send_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)serial; (void)len; (void)extra;
  if (code != 200) { char e[200]; api_error(code, body, err, e, sizeof e); toast(e); return; }
  toast("Sent! Your drawing is in the gallery.");
  ed_close();
  Screen *c = cur(); if (c && c->kind == S_GALLERY) load_screen(c, 0);
}
static void ed_done(void) {
  int empty = 1;
  for (int i = 0; i < DPIX; i++) if (ed.px[i]) { empty = 0; break; }
  if (empty) { toast("The canvas is empty. Draw something first."); return; }
  if (ed.challenge) {
    char t[64] = "";
    if (!plat_keyboard("What did you draw?", ed.title, t, 48, 0, 0)) return;
    char *p = t; while (*p == ' ') p++;
    if (utf8_len(p) < 2) { toast("Write what you drew (at least 2 letters)."); return; }
    snprintf(ed.title, sizeof ed.title, "%s", p);
    char *code = encode_drawing(ed.px, 3);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "title", p); cJSON_AddStringToObject(o, "drawing", code); cJSON_AddStringToObject(o, "mode", ed.ultra ? "ultra" : "super");
    char *b = cJSON_PrintUnformatted(o);
    net_send("POST", "/api/wiggle", b, ed_send_cb, 0, 0);
    free(b); free(code); cJSON_Delete(o);
    toast("Sending...");
    return;
  }
  Screen *s = ed.target;
  if (s && by_serial(s->serial)) {
    if (!s->draw) s->draw = malloc(DPIX);
    memcpy(s->draw, ed.px, DPIX); s->drawWig = ed.wig ? ed.strength : 0;
    rebuild_form(s);
  }
  ed_close();
}
/* toolbar: row 1 = 32 colours (2 x 16), row 2 = tools */
static const char *ED_TOOLS[8] = { "Pen", "Eraser", "Fill", "Size", "Undo", "Wiggle", "Cancel", "Done" };
static void ed_chaos(void) {
  if (!ed.challenge) return;
  if (nowMs < ed.nextChaos) return;
  int speed = ed.ultra ? 3 : 1;
  ed.nextChaos = nowMs + (uint32_t)((1200 + rand() % 1800) / speed);
  int r = rand() % 6;
  if (r == 0) ed.fx = !ed.fx;
  else if (r == 1) ed.fy = !ed.fy;
  else if (r == 2) { static const uint32_t T[5] = { 0, 0xff8080, 0x80ff80, 0x8080ff, 0xffff60 }; ed.tint = T[rand() % 5]; }
  else if (r == 3) ed.invert = !ed.invert;
  else { ed.fx = 0; ed.fy = 0; ed.tint = 0; ed.invert = 0; }
  if (ed.ultra && rand() % 5 == 0 && ed.nundo) { ed_undo(); toast("Oops! Your last move disappeared."); }
  if (ed.ultra && rand() % 7 == 0) { ed.annoy = 1 + rand() % 3; ed.annoyUntil = nowMs + 2500; }
  dirty = 1;
}
static void ed_touch(Input *in) {
  static int wasTouch;
  if (!in->touch) { if (wasTouch) { ed.drawing = 0; } wasTouch = 0; return; }
  int fresh = !wasTouch; wasTouch = 1;
  int tx = in->tx, ty = in->ty;
  if (ty < ED_TOOLY) {
    /* map through the chaos transform */
    int cx = tx - ed.shx, cy = ty - ed.shy;
    if (ed.fx) cx = DW - 1 - cx;
    if (ed.fy) cy = DH - 1 - cy;
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx >= DW) cx = DW - 1;
    if (cy >= DH) cy = DH - 1;
    if (ed.tool == 2) { if (fresh) { ed_snap(); ed_flood(cx, cy); } }
    else if (fresh) { ed_snap(); ed_dot(cx, cy); ed.drawing = 1; }
    else if (ed.drawing) ed_line(ed.lastX, ed.lastY, cx, cy);
    ed.lastX = cx; ed.lastY = cy;
    dirty = 1;
    return;
  }
  if (!fresh) return;
  if (ty < 210) {   /* colours */
    int row = (ty - ED_TOOLY - 2) / 14, col = tx / 20;
    if (row < 0) row = 0;
    if (row > 1) row = 1;
    if (col > 15) col = 15;
    ed.color = row * 16 + col; if (ed.tool == 1) ed.tool = 0;
  } else {
    int b = tx / 40;
    switch (b) {
    case 0: ed.tool = 0; break;
    case 1: ed.tool = 1; break;
    case 2: ed.tool = 2; break;
    case 3: ed.size = ed.size == 1 ? 2 : ed.size == 2 ? 4 : ed.size == 4 ? 7 : 1; break;
    case 4: ed_undo(); break;
    case 5: if (ed.challenge) toast("Wiggle is always on in Wiggle Paint!"); else if (!ed.wig) { ed.wig = 1; } else if (ed.strength < 5) ed.strength++; else { ed.wig = 0; ed.strength = 3; } break;
    case 6: ed_close(); break;
    case 7: ed_done(); break;
    }
  }
  dirty = 1;
}
static void ed_draw_bottom(void) {
  Cv *c = &BOT;
  fill(c, 0, 0, 320, 240, 0xdfe9ee);
  /* canvas with chaos */
  int frame = (int)((nowMs / (ed.ultra ? 70 : 140)) % 4);
  if (ed.challenge) { int k = ed.ultra ? 5 : 3; ed.shx = (int)(sinf(nowMs / (ed.ultra ? 60.0f : 140.0f)) * k); ed.shy = (int)(cosf(nowMs / (ed.ultra ? 75.0f : 170.0f)) * k); }
  else { ed.shx = ed.shy = 0; }
  cv_clip(c, 0, 0, 320, ED_TOOLY);
  fill(c, 0, 0, 320, ED_TOOLY, 0x9aa9b2);
  float amp = ed.challenge ? 2.2f : ed.wig ? ed.strength * 0.6f : 0;
  static int eox[DH], eoy[DW];
  for (int y = 0; y < DH; y++) eox[y] = amp ? (int)(amp * sinf(y / 6.0f + frame * 1.7f)) : 0;
  for (int x = 0; x < DW; x++) eoy[x] = amp ? (int)(amp * sinf(x / 7.0f + frame * 2.2f)) : 0;
  for (int y = 0; y < DH; y++) {
    int sy = ed.fy ? DH - 1 - y : y;
    int py = y + ed.shy;
    if (py < 0 || py >= ED_TOOLY) continue;
    uint32_t *row = &c->px[py * 320];
    for (int x = 0; x < DW; x++) {
      int sx = ed.fx ? DW - 1 - x : x;
      int qx = sx + eox[sy], qy = sy + eoy[sx];
      if (qx < 0) qx = 0;
      if (qx >= DW) qx = DW - 1;
      if (qy < 0) qy = 0;
      if (qy >= DH) qy = DH - 1;
      uint32_t col = PAL[ed.px[qy * DW + qx] & 31];
      if (ed.invert) col = 0xffffff - col;
      if (ed.tint) col = (((((col >> 16) & 255) * ((ed.tint >> 16) & 255)) / 255) << 16) | (((((col >> 8) & 255) * ((ed.tint >> 8) & 255)) / 255) << 8) | (((col & 255) * (ed.tint & 255)) / 255);
      int px = x + ed.shx;
      if (px >= 0 && px < 320) row[px] = 0xff000000u | col;
    }
  }
  cv_noclip(c);
  /* toolbar */
  vgrad(c, 0, ED_TOOLY, 320, 60, 0xf4fafd, 0xd6e8f1);
  fill(c, 0, ED_TOOLY, 320, 1, C_LINE);
  for (int i = 0; i < 32; i++) {
    int x = (i % 16) * 20, y = ED_TOOLY + 2 + (i / 16) * 14;
    fill(c, x + 1, y, 18, 13, PAL[i]);
    if (i == ed.color && ed.tool != 1) { fill(c, x, y - 1, 20, 2, C_INK); fill(c, x, y + 12, 20, 2, C_INK); fill(c, x, y, 2, 13, C_INK); fill(c, x + 18, y, 2, 13, C_INK); }
  }
  for (int b = 0; b < 8; b++) {
    int x = b * 40 + 2, y = 212;
    int on = (b == ed.tool && b < 3) || (b == 5 && (ed.wig || ed.challenge));
    uint32_t bg = b == 7 ? C_GREEN : b == 6 ? 0xffffff : on ? C_BLUE : 0xffffff;
    rrect_border(c, x, y, 36, 25, 6, b == 7 ? C_GREEN_D : on ? 0x0b5f99 : C_LINE, bg);
    char lab[16]; snprintf(lab, sizeof lab, "%s", ED_TOOLS[b]);
    if (b == 3) snprintf(lab, sizeof lab, "Size %d", ed.size);
    if (b == 5) { if (ed.challenge) snprintf(lab, sizeof lab, "Wiggle"); else if (ed.wig) snprintf(lab, sizeof lab, "Wig %d", ed.strength); else snprintf(lab, sizeof lab, "Wig off"); }
    uint32_t fg = (b == 7 || on) ? C_WHITE : C_INK;
    int w = text_w(FB, 9, lab);
    text(c, FB, 9, x + 18 - w / 2, y + 7, lab, fg);
  }
}
static void ed_draw_top(void) {
  Cv *c = &TOP;
  memcpy(c->px, bgTop.px, (size_t)400 * 240 * 4);
  vgrad(c, 0, 0, 400, 24, 0xffffff, 0xe3f3fb); fill(c, 0, 24, 400, 1, 0x8fd4f2);
  img_draw(c, &logoSm, 6, 3, 89, 18, 0, 255);
  const char *t = ed.challenge ? (ed.ultra ? "Ultra Wiggle Paint" : "Super Wiggle Paint") : "Drawing";
  text(c, FB, 12, 394 - text_w(FB, 12, t), 5, t, C_SEC2);
  /* live preview with the website's wiggle */
  int frame = (int)((nowMs / 140) % 4);
  shadow(c, 38, 32, 324, 184, 6);
  rrect(c, 38, 32, 324, 184, 6, C_WHITE);
  uint32_t pal[32]; memcpy(pal, PAL, sizeof pal);
  draw_drawing(c, ed.px, pal, ed.challenge ? 3 : ed.strength, (ed.challenge || ed.wig) ? frame : -1, 40, 34, 320, 180);
  const char *help = ed.challenge ? "Draw on the bottom screen! L: undo  X: done  B: cancel" : "Draw with the stylus.  L: undo  X: done  B: cancel";
  text_c(c, FR, 10, 200, 222, help, C_SEC2);
  if (ed.annoy && nowMs < ed.annoyUntil) {
    static const char *AN[3] = { "Are you sure that is a good drawing?", "Did you know? You can draw faster!", "Your canvas is shaking. This is normal." };
    rrect_border(c, 60, 80, 280, 70, 10, 0xe5322d, 0xfff6f0);
    text_c(c, FB, 12, 200, 92, "Ultra Wiggle says:", C_RED);
    text_c(c, FR, 11, 200, 114, AN[(ed.annoy - 1) % 3], C_INK);
  } else ed.annoy = 0;
}

/* ------------------------------------------------------------------ drawing the UI */
static void draw_avatar(Cv *c, const char *name, const char *user, const char *url, int x, int y, int s) {
  fill(c, x, y, s, s, C_LINE);
  Pic *p = url && *url ? pic_get(url) : NULL;
  if (p) { fill(c, x + 1, y + 1, s - 2, s - 2, C_WHITE); pic_draw_sq(c, p, x + 1, y + 1, s - 2); return; }
  fill(c, x + 1, y + 1, s - 2, s - 2, AVC[name_hash(user) % 8]);
  char ini[8] = "?";
  if (name && *name) {
    int L = 1; unsigned char ch = (unsigned char)name[0];
    if (ch >= 0xF0) L = 4; else if (ch >= 0xE0) L = 3; else if (ch >= 0xC0) L = 2;
    memcpy(ini, name, (size_t)L); ini[L] = 0;
    if (L == 1 && ini[0] >= 'a' && ini[0] <= 'z') ini[0] -= 32;
  }
  int fs = s * 6 / 10;
  text_c(c, FB, fs, x + s / 2, y + (s - fs) / 2 - fs / 8, ini, C_WHITE);
}
static void card(Cv *c, int x, int y, int w, int h, int focus) {
  shadow(c, x, y, w, h, 8);
  rrect(c, x, y, w, h, 8, C_WHITE);
  if (focus) { rrect(c, x - 2, y - 2, w + 4, h + 4, 10, 0x9c1b8fd3); rrect(c, x, y, w, h, 8, C_WHITE); }
}
static void pill(Cv *c, int x, int y, int w, int h, const char *t, int style, int focus) {   /* style 0 green, 1 white, 2 yeah, 3 yeah on, 4 blue */
  if (focus) rrect(c, x - 2, y - 2, w + 4, h + 4, h / 2 + 2, 0xb01b8fd3);
  uint32_t top, bot, border, fg;
  switch (style) {
  case 0: top = 0x7ee03a; bot = 0x3f9a00; border = 0x2f7a00; fg = C_WHITE; break;
  case 2: top = 0xfffbea; bot = 0xffefb8; border = 0xf0cf73; fg = C_YEAH_T; break;
  case 3: top = 0xffe27a; bot = 0xffc928; border = 0xd9a400; fg = 0x4a3200; break;
  case 4: top = 0x55b8f0; bot = 0x1b8fd3; border = 0x0b5f99; fg = C_WHITE; break;
  default: top = 0xffffff; bot = 0xe6f1f6; border = 0xb7cbd6; fg = C_SEC2; break;
  }
  rrect(c, x, y, w, h, h / 2, border);
  rrect_g(c, x + 1, y + 1, w - 2, h - 2, h / 2 - 1, top, bot);
  fill(c, x + h / 2, y + 2, w - h, 1, 0x60ffffff);
  int fs = h >= 26 ? 12 : 10;
  text_c(c, FB, fs, x + w / 2, y + (h - fs) / 2 - 1, t, fg);
}
static void draw_item(Cv *c, Screen *s, Item *it, int X, int Y, int focus) {
  char buf[200];
  switch (it->kind) {
  case K_POST: case K_REPLY: {
    card(c, X, Y, it->w, it->h, focus);
    if (it->kind == K_REPLY) fill(c, X, Y + 6, 3, it->h - 12, 0xb7d7e8);
    draw_avatar(c, it->name, it->user, it->avatar, X + 6, Y + 6, 24);
    text_fit(c, FB, 11, X + 36, Y + 5, it->name, C_INK, 160);
    char ago[40]; fmt_ago(it->created, ago, sizeof ago);
    if (it->cname && *it->cname && s->kind != S_COMM) snprintf(buf, sizeof buf, "%s - %s", it->cname, ago); else snprintf(buf, sizeof buf, "%s", ago);
    text_fit(c, FR, 9, X + 36, Y + 19, buf, C_SOFT, it->w - 44);
    int y = Y + 34;
    int tl = post_text_lines(it);
    if (tl) {
      PStyle ps = post_style(it->style);
      int th = text_wrap(NULL, FR, 11, 0, 0, it->w - 16, it->plain, 0, 14, tl);
      if (styled_box(ps)) style_box(c, ps, X + 5, y - 2, it->w - 10, th + 3);
      y += text_wrap(c, ps.size == 2 ? FB : FR, 11, X + 8, y, it->w - 16, it->plain, ps.bg ? PC_FG[ps.bg] : C_INK, 14, tl) + 2;
    }
    if (it->dpx) {
      int fr = it->wig > 0 ? (int)((nowMs / 140) % 4) : -1;
      fill(c, X + 7, y - 1, 130, 75, C_LINE);
      draw_drawing(c, it->dpx, it->dpal, it->wig, fr, X + 8, y, 128, 72);
      y += 76;
    }
    /* footer: Yeah! + replies */
    int fy = Y + it->h - 18;
    snprintf(buf, sizeof buf, "Yeah! %d", it->yeahs);
    int yw = text_w(FB, 9, buf) + 14;
    rrect_border(c, X + 8, fy, yw, 14, 7, it->mine ? 0xd9a400 : 0xf0cf73, it->mine ? C_YEAH_ON : C_YEAH);
    text(c, FB, 9, X + 15, fy + 2, buf, it->mine ? 0x4a3200 : C_YEAH_T);
    if (it->kind == K_POST) { snprintf(buf, sizeof buf, it->replies == 1 ? "1 reply" : "%d replies", it->replies); text(c, FR, 9, X + 14 + yw, fy + 2, buf, C_SOFT); }
    break; }
  case K_COMM: {
    card(c, X, Y, it->w, it->h, focus);
    Pic *p = it->avatar && *it->avatar ? pic_get(it->avatar) : NULL;
    if (p) pic_draw_sq(c, p, X + 6, Y + 6, 36);
    else { fill(c, X + 6, Y + 6, 36, 36, C_GREEN); char l[2] = { it->name && *it->name ? it->name[0] : '?', 0 }; text_c(c, FB, 22, X + 24, Y + 11, l, C_WHITE); }
    text_fit(c, FB, 12, X + 50, Y + 6, it->name, C_INK, it->w - 58);
    text_fit(c, FR, 9, X + 50, Y + 22, it->body, C_SOFT, it->w - 58);
    snprintf(buf, sizeof buf, "%d posts", it->posts); text(c, FR, 9, X + 50, Y + 34, buf, C_GREEN_D);
    break; }
  case K_SEC:
    text(c, FB, 12, X + 2, Y + 3, it->title, C_SEC2);
    break;
  case K_INFO: text_wrap(c, FR, 10, X + 4, Y + 2, it->w - 8, it->body, C_SEC2, 13, 0); break;
  case K_EMPTY:
    card(c, X, Y, it->w, it->h, 0);
    text_wrap(c, FR, 10, X + 12, Y + 15, it->w - 24, it->body, C_SOFT, 13, 0);
    break;
  case K_ACTION:
    pill(c, X + 10, Y + 2, it->w - 20, 26, it->title, it->act == A_SIGNOUT || it->act == A_MODE || it->act == A_RULES || it->act == A_WIGTOG ? 1 : it->act == A_UPDATE ? 4 : 0, focus);
    break;
  case K_MORE:
    text_c(c, FR, 10, X + it->w / 2, Y + 7, s->loading ? "Loading..." : "Scroll down for more", C_SEC2);
    break;
  case K_PERSON: {
    card(c, X, Y, it->w, it->h, focus);
    draw_avatar(c, it->name, it->user, it->avatar, X + 6, Y + 6, 28);
    text_fit(c, FB, 12, X + 42, Y + 6, it->name, C_INK, 150);
    snprintf(buf, sizeof buf, "@%s", it->user ? it->user : ""); text_fit(c, FR, 9, X + 42, Y + 22, buf, C_SOFT, 150);
    int bx = X + it->w - 8;
    static const char *TN[3] = { "Popular", "New", "Active" }; static const uint32_t TC[3] = { C_ORANGE, C_BLUE, C_GREEN };
    for (int k = 2; k >= 0; k--) if (it->tags & (1 << k)) { int w = text_w(FB, 9, TN[k]) + 10; bx -= w; rrect(c, bx, Y + 13, w, 14, 7, TC[k]); text(c, FB, 9, bx + 5, Y + 15, TN[k], C_WHITE); bx -= 4; }
    break; }
  case K_GAME: {
    card(c, X, Y, it->w, it->h, focus);
    if (it->id) rrect_g(c, X + 6, Y + 6, 58, 58, 8, 0xff6a3d, 0x9b3fd6); else rrect_g(c, X + 6, Y + 6, 58, 58, 8, 0x86e3ff, 0x2ab76a);
    text_c(c, FB, 11, X + 35, Y + 20, it->id ? "ULTRA" : "Super", C_WHITE);
    text_c(c, FB, 11, X + 35, Y + 34, "Wiggle", C_WHITE);
    text(c, FB, 12, X + 72, Y + 6, it->title, C_INK);
    text_wrap(c, FR, 9, X + 72, Y + 22, it->w - 80, it->body, C_SOFT, 11, 4);
    break; }
  case K_WIG: {
    card(c, X, Y, it->w, it->h, focus);
    if (it->dpx) draw_drawing(c, it->dpx, it->dpal, 3, (int)((nowMs / 140) % 4), X + 1, Y + 1, 96, 54);
    text_fit(c, FB, 9, X + 4, Y + 58, it->title, C_INK, 90);
    snprintf(buf, sizeof buf, "Yeah! %d", it->yeahs); text(c, FR, 9, X + 4, Y + 71, buf, it->mine ? 0xd9a400 : C_SOFT);
    break; }
  case K_FIELD: {
    text(c, FB, 9, X + 4, Y, it->title, C_SEC2);
    if (focus) rrect(c, X - 2, Y + 11, it->w + 4, 32, 9, 0xb01b8fd3);
    rrect_border(c, X, Y + 13, it->w, 28, 7, C_LINE, C_WHITE);
    const char *v = it->body && *it->body ? it->body : "";
    if (*v) text_fit(c, FR, 11, X + 8, Y + 19, v, C_INK, it->w - 16); else text(c, FR, 11, X + 8, Y + 19, it->act == 4 ? "" : "(tap to type)", 0xa8b4ba);
    break; }
  case K_RULE: {
    card(c, X, Y, it->w, it->h, 0);
    rrect(c, X + 8, Y + 8, 18, 16, 3, C_SEC2);
    snprintf(buf, sizeof buf, "%d", it->id); text_c(c, FB, 10, X + 17, Y + 9, buf, C_WHITE);
    text_fit(c, FB, 10, X + 32, Y + 8, it->title, C_INK, it->act ? 180 : it->w - 40);
    if (it->act) { int w = text_w(FB, 8, "Instant ban") + 8; rrect(c, X + it->w - w - 6, Y + 8, w, 13, 3, C_RED); text(c, FB, 8, X + it->w - w - 2, Y + 10, "Instant ban", C_WHITE); }
    text_wrap(c, FR, 10, X + 32, Y + 22, it->w - 40, it->body, C_SOFT, 12, 0);
    break; }
  case K_PROFILE: {
    card(c, X, Y, it->w, it->h, 0);
    draw_avatar(c, it->name, it->user, it->avatar, X + 8, Y + 8, 48);
    text_fit(c, FB, 14, X + 64, Y + 8, it->name, C_INK, it->w - 72);
    snprintf(buf, sizeof buf, "@%s - %d posts", it->user ? it->user : "", it->posts); text_fit(c, FR, 10, X + 64, Y + 28, buf, C_SOFT, it->w - 72);
    if (it->plain && *it->plain) text_wrap(c, FR, 10, X + 8, Y + 62, it->w - 16, it->plain, C_INK, 13, 3);
    break; }
  }
}

/* ---- action bar (above the tabs) */
typedef struct { const char *t; int id; int style; } Btn;
enum { B_BACK = 1, B_WRITE, B_REFRESH, B_YEAH, B_REPLY, B_DRAW, B_FILTER, B_SEND, B_OPEN, B_ACCEPT, B_PROFILE, B_COMMUNITY };
static int bar_buttons(Screen *s, Btn *b) {
  int n = 0;
  if (depth > 1 && s->kind != S_RULES) b[n++] = (Btn){ "Back", B_BACK, 1 };
  Item *sel = s->sel >= 0 && s->sel < s->n ? &s->it[s->sel] : NULL;
  switch (s->kind) {
  case S_FEED: b[n++] = (Btn){ "Write", B_WRITE, 0 }; b[n++] = (Btn){ "Refresh", B_REFRESH, 1 }; break;
  case S_COMMS: case S_PLAZA: if (s->kind == S_PLAZA) b[n++] = (Btn){ "Filter", B_FILTER, 1 }; b[n++] = (Btn){ "Refresh", B_REFRESH, 1 }; break;
  case S_COMM: b[n++] = (Btn){ "Write here", B_WRITE, 0 }; break;
  case S_POST: b[n++] = (Btn){ "Yeah!", B_YEAH, sel && (sel->kind == K_POST || sel->kind == K_REPLY) && sel->mine ? 3 : 2 }; b[n++] = (Btn){ "Reply", B_REPLY, 0 }; break;
  case S_GALLERY: b[n++] = (Btn){ "Draw!", B_DRAW, 0 }; if (sel && sel->kind == K_WIG) b[n++] = (Btn){ "Yeah!", B_YEAH, sel->mine ? 3 : 2 }; break;
  case S_USER: if (me.id && !strcmp(s->cid, me.username)) b[n++] = (Btn){ "Write", B_WRITE, 0 }; else b[n++] = (Btn){ "Refresh", B_REFRESH, 1 }; break;
  case S_COMPOSE: b[n++] = (Btn){ "Send", B_SEND, 0 }; break;
  case S_RULES: if (s->mode) b[n++] = (Btn){ "Accept", B_ACCEPT, 0 }; else b[n++] = (Btn){ "Back", B_BACK, 1 }; break;
  default: break;
  }
  if (sel && (sel->kind == K_POST || sel->kind == K_REPLY) && s->kind != S_POST && n < 4) b[n++] = (Btn){ sel->kind == K_POST ? "Open" : "Yeah!", sel->kind == K_POST ? B_OPEN : B_YEAH, 1 };
  return n;
}
static const char *TAB_N[5] = { "Home", "Communities", "Plaza", "Games", "Profile" };
static void draw_bottom(void) {
  Cv *c = &BOT;
  Screen *s = cur();
  memcpy(c->px, bgBot.px, (size_t)320 * 240 * 4);
  if (s) {
    if (s->scroll > s->contentH - LIST_H) s->scroll = (float)(s->contentH - LIST_H);
    if (s->scroll < 0) s->scroll = 0;
    cv_clip(c, 0, 0, 320, LIST_H);
    for (int i = 0; i < s->n; i++) {
      Item *it = &s->it[i];
      int y = it->y - (int)s->scroll;
      if (y + it->h < 0 || y > LIST_H) continue;
      draw_item(c, s, it, it->x, y, i == s->sel);
    }
    if (s->loading && s->n <= 1) text_c(c, FB, 12, 160, 80, "Loading...", C_SEC2);
    /* scroll bar */
    if (s->contentH > LIST_H) { int bh = LIST_H * LIST_H / s->contentH; if (bh < 16) bh = 16; int by = (int)(s->scroll * (LIST_H - bh) / (s->contentH - LIST_H)); rrect(c, 315, by, 4, bh, 2, 0x701b8fd3); }
    cv_noclip(c);
    /* action bar */
    vgrad(c, 0, BAR_Y, 320, TAB_Y - BAR_Y, 0xf7fcff, 0xe2f0f7);
    fill(c, 0, BAR_Y, 320, 1, C_LINE);
    Btn b[5]; int n = bar_buttons(s, b);
    int w = n ? (312 - (n - 1) * 6) / n : 0;
    for (int i = 0; i < n; i++) pill(c, 4 + i * (w + 6), BAR_Y + 3, w, 20, b[i].t, b[i].style, 0);
  }
  /* tabs */
  vgrad(c, 0, TAB_Y, 320, 240 - TAB_Y, 0xffffff, 0xeaf5fb);
  fill(c, 0, TAB_Y, 320, 1, 0x8fd4f2);
  for (int i = 0; i < 5; i++) {
    int x = i * 64, on = curTab == i;
    uint32_t col = on ? C_GREEN : 0x6b6b6b;
    img_draw(c, &icons[i], x + 32 - 8, TAB_Y + 2, 16, 16, col, 255);
    const char *nm = (i == 4 && !me.id) ? "Sign in" : TAB_N[i];
    text_c(c, on ? FB : FR, 8, x + 32, TAB_Y + 18, nm, col);
    if (on) fill(c, x + 8, 238, 48, 2, C_GREEN);
  }
}

/* ---- top screen */
static long tz_offset(int zone, long utc);
static void top_header(Cv *c, const char *title) {
  vgrad(c, 0, 0, 400, 24, 0xffffff, 0xe3f3fb); fill(c, 0, 24, 400, 1, 0x8fd4f2);
  img_draw(c, &logoSm, 6, 3, 89, 18, 0, 255);
  int w = text_w(FB, 12, title);
  text(c, FB, 12, 394 - w, 5, title, C_SEC2);
  if (net_pending()) { int k = (int)(nowMs / 120) % 6; for (int i = 0; i < 6; i++) fill(c, 394 - w - 46 + i * 7, 10, 4, 4, i == k ? C_GREEN : 0xc8dfe8); }
}
static void top_post(Cv *c, Item *it, int big) {
  char buf[200], ago[40];
  int x = 10, y = 30, w = 380, h = 204;
  card(c, x, y, w, h, 0);
  draw_avatar(c, it->name, it->user, it->avatar, x + 8, y + 8, 32);
  text_fit(c, FB, 13, x + 48, y + 7, it->name, C_INK, 200);
  fmt_ago(it->created, ago, sizeof ago);
  if (it->cname && *it->cname) snprintf(buf, sizeof buf, "@%s - %s - %s", it->user ? it->user : "", it->cname, ago); else snprintf(buf, sizeof buf, "@%s - %s", it->user ? it->user : "", ago);
  text_fit(c, FR, 10, x + 48, y + 24, buf, C_SOFT, w - 56);
  snprintf(buf, sizeof buf, "Yeah! %d", it->yeahs);
  int yw = text_w(FB, 10, buf) + 16;
  rrect_border(c, x + w - yw - 8, y + 8, yw, 18, 9, it->mine ? 0xd9a400 : 0xf0cf73, it->mine ? C_YEAH_ON : C_YEAH);
  text(c, FB, 10, x + w - yw, y + 11, buf, it->mine ? 0x4a3200 : C_YEAH_T);
  int ty = y + 46;
  cv_clip(c, x, y, w, h - 4);
  if (it->dpx) {
    int fr = it->wig > 0 ? (int)((nowMs / 140) % 4) : -1;
    if (it->plain && *it->plain) { text_fit(c, FR, 11, x + 10, ty - 4, it->plain, C_INK, w - 20); ty += 12; }
    int dh = y + h - 6 - ty, dw = dh * 16 / 9;
    if (dw > w - 16) { dw = w - 16; dh = dw * 9 / 16; }
    fill(c, x + (w - dw) / 2 - 1, ty - 1, dw + 2, dh + 2, C_LINE);
    draw_drawing(c, it->dpx, it->dpal, it->wig, fr, x + (w - dw) / 2, ty, dw, dh);
  } else if (it->plain) {
    /* biggest size that fits, starting from the post's size (Small / Normal / Big) */
    PStyle ps = post_style(it->style);
    static const int SZ[3][4] = { { 14, 13, 12, 11 }, { 20, 16, 14, 12 }, { 26, 20, 16, 13 } };
    int avail = y + h - 8 - ty, pad = styled_box(ps) ? 10 : 0, fs = 12, lh = 15, tw = w - 24 - 2 * pad;
    for (int k = 0; k < 4; k++) { fs = SZ[ps.size][k]; lh = fs * 5 / 4 + 1; if (text_wrap(NULL, FR, fs, 0, 0, tw, it->plain, 0, lh, 0) + 2 * pad <= avail || k == 3) break; }
    int th = text_wrap(NULL, FR, fs, 0, 0, tw, it->plain, 0, lh, 0) + 2 * pad;
    if (th > avail) th = avail;
    if (styled_box(ps)) style_box(c, ps, x + 10, ty - 2, w - 20, th);
    int font = ps.size == 2 ? FB : FR;
    uint32_t fg = ps.bg ? PC_FG[ps.bg] : C_INK;
    if (ps.align == 1) {   /* centred lines */
      int lines = (th - 2 * pad) / lh, yy = ty + pad;
      char *copy = xstrdup(it->plain), *save = NULL;
      for (char *ln = strtok_r(copy, "\n", &save); ln && lines-- > 0; ln = strtok_r(NULL, "\n", &save)) yy += 0, text_c(c, font, fs, x + w / 2, yy, ln, fg), yy += lh;
      free(copy);
    } else text_wrap(c, font, fs, x + 12 + pad, ty + pad, tw, it->plain, fg, lh, (th - 2 * pad) / lh);
  }
  cv_noclip(c);
}
static void top_welcome(Cv *c) {
  shadow(c, 20, 34, 360, 196, 12);
  rrect_g(c, 20, 34, 360, 196, 12, 0xffffff, 0xeaf7fd);
  img_draw(c, &logoImg, 200 - 119, 50, 238, 48, 0, 255);
  text_c(c, FB, 14, 200, 108, "Hello from around the world!", C_SEC2);
  static const char *CITY[5] = { "USA", "Brazil", "UK", "Germany", "Japan" };
  long utc = utc_now();
  for (int i = 0; i < 5; i++) {
    int cx = 56 + i * 72;
    long t = utc + tz_offset(i, utc);
    char hm[16]; long sec = ((t % 86400) + 86400) % 86400; snprintf(hm, sizeof hm, "%02ld:%02ld", sec / 3600, (sec / 60) % 60);
    text_c(c, FB, 9, cx, 136, CITY[i], C_SOFT);
    fill(c, cx - 20, 149, 40, 2, C_GREEN);
    text_c(c, FB, 15, cx, 154, hm, C_INK);
  }
  if (me.id > 0) { char b[120]; snprintf(b, sizeof b, "Signed in as %s", me.display); text_c(c, FR, 11, 200, 190, b, C_SEC2); }
  else text_c(c, FR, 11, 200, 190, "Sign in on the Profile tab to post and draw.", C_SEC2);
  text_c(c, FR, 9, 200, 210, "Aeroverse for 3DS " APP_VERSION, 0x8aa0ad);
}
static int nth_sunday(int y, int m, int n) {
  long first = days_from_civil(y, m, 1); int wd = (int)((first + 4) % 7); if (wd < 0) wd += 7;
  int d1 = 1 + (7 - wd) % 7;
  if (n > 0) return d1 + (n - 1) * 7;
  int dim = (int)(days_from_civil(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - first);
  while (d1 + 7 <= dim) d1 += 7;
  return d1;
}
static long tz_offset(int zone, long utc) {
  long days = utc / 86400; int y = 1970 + (int)(days / 365.2425); if (days_from_civil(y, 1, 1) > days) y--; if (days_from_civil(y + 1, 1, 1) <= days) y++;
  long eu0 = days_from_civil(y, 3, nth_sunday(y, 3, -1)) * 86400 + 3600, eu1 = days_from_civil(y, 10, nth_sunday(y, 10, -1)) * 86400 + 3600;
  int euS = utc >= eu0 && utc < eu1;
  switch (zone) {
  case 0: { long a = days_from_civil(y, 3, nth_sunday(y, 3, 2)) * 86400 + 7 * 3600, b = days_from_civil(y, 11, nth_sunday(y, 11, 1)) * 86400 + 6 * 3600; return utc >= a && utc < b ? -4 * 3600 : -5 * 3600; }
  case 1: return -3 * 3600;
  case 2: return euS ? 3600 : 0;
  case 3: return euS ? 7200 : 3600;
  default: return 9 * 3600;
  }
}
/* plaza: island with walking people */
static void top_plaza(Cv *c, Screen *s) {
  vgrad(c, 0, 25, 400, 215, 0x9fdcf7, 0x2a9fd8);
  /* island */
  for (int y = 0; y < 150; y++) {
    float ny = (y - 75) / 75.0f; float hw = 180 * sqrtf(1 - ny * ny);
    fill(c, (int)(200 - hw - 6), 68 + y, (int)(hw * 2 + 12), 1, 0xf2dfa6);
    fill(c, (int)(200 - hw), 68 + y, (int)(hw * 2), 1, lerpc(0x8bd85a, 0x5fb63a, (float)y / 150));
  }
  circle(c, 200, 143, 26, 0xe8e2d0); circle(c, 200, 143, 15, 0x7fd0f5); circle(c, 200, 140, 4, 0xffffff);
  static const int TR[8][2] = { { 70, 110 }, { 330, 115 }, { 100, 180 }, { 300, 185 }, { 140, 85 }, { 260, 82 }, { 60, 150 }, { 345, 152 } };
  for (int i = 0; i < 8; i++) { circle(c, TR[i][0], TR[i][1] + 10, 4, 0x8a5a2b); circle(c, TR[i][0], TR[i][1], 12, 0x2f9a3a); circle(c, TR[i][0] - 3, TR[i][1] - 4, 6, 0x48b850); }
  /* people sorted by y */
  int idx[60], n = 0;
  for (int i = 0; i < s->n && n < 60; i++) if (s->it[i].kind == K_PERSON) idx[n++] = i;
  int order[60]; for (int i = 0; i < n; i++) order[i] = i;
  for (int i = 1; i < n; i++) { int k = order[i], j = i - 1; while (j >= 0 && walkers[order[j]].y > walkers[k].y) { order[j + 1] = order[j]; j--; } order[j + 1] = k; }
  for (int oi = 0; oi < n; oi++) {
    int k = order[oi];
    Item *it = &s->it[idx[k]];
    Walk *w = &walkers[k];
    int sel = idx[k] == s->sel;
    int bob = (int)(fabsf(sinf(w->ph * 2)) * 2);
    int px = (int)w->x, py = (int)w->y - bob;
    circle(c, px, (int)w->y + 1, 7, 0x30000000);
    img_draw(c, &bodyImg, px - 9, py - 16, 18, 18, SHIRT_COLS[((it->icon % 12) + 12) % 12] | 0x404040, 255);
    draw_avatar(c, it->name, it->user, it->avatar, px - 7, py - 30, 14);
    if (sel) {
      int tw = text_w(FB, 10, it->name ? it->name : "") + 12;
      rrect(c, px - tw / 2, py - 48, tw, 16, 8, C_WHITE);
      text_c(c, FB, 10, px, py - 46, it->name, C_INK);
    }
  }
  text(c, FB, 10, 8, 224, "Touch a person on the bottom screen to visit them.", C_WHITE);
}
static void plaza_step(void) {
  for (int i = 0; i < 60; i++) {
    Walk *w = &walkers[i];
    float dx = w->tx - w->x, dy = w->ty - w->y, d = sqrtf(dx * dx + dy * dy);
    if (d < 1.5f) {
      if (rand() % 80 == 0) {
        float a = (rand() % 628) / 100.0f, r = 20 + rand() % 50;
        w->tx = w->x + cosf(a) * r; w->ty = w->y + sinf(a) * r * 0.6f;
        float ny = (w->ty - 143) / 70.0f; if (ny > 0.95f) w->ty = 143 + 66; if (ny < -0.95f) w->ty = 143 - 66;
        float hw = 165 * sqrtf(fmaxf(0, 1 - ((w->ty - 143) / 75.0f) * ((w->ty - 143) / 75.0f)));
        if (w->tx < 200 - hw) w->tx = 200 - hw;
        if (w->tx > 200 + hw) w->tx = 200 + hw;
        if ((w->tx - 200) * (w->tx - 200) + (w->ty - 143) * (w->ty - 143) < 34 * 34) w->tx += w->tx < 200 ? -40 : 40;
      }
      continue;
    }
    w->x += dx / d * 0.6f; w->y += dy / d * 0.6f; w->ph += 0.15f;
  }
}
static void draw_top(void) {
  Cv *c = &TOP;
  memcpy(c->px, bgTop.px, (size_t)400 * 240 * 4);
  Screen *s = cur();
  static const char *SN[11] = { "Home", "Communities", "Community", "Post", "Plaza", "Games", "Gallery", "Profile", "Sign in", "Write", "Rules" };
  char title[120]; snprintf(title, sizeof title, "%s", s ? SN[s->kind] : "");
  if (s && (s->kind == S_COMM || s->kind == S_GALLERY) && s->cname[0]) snprintf(title, sizeof title, "%s", s->cname);
  if (s && s->kind == S_USER && s->n && s->it[0].kind == K_PROFILE && s->it[0].name) snprintf(title, sizeof title, "%s", s->it[0].name);
  Item *sel = s && s->sel >= 0 && s->sel < s->n ? &s->it[s->sel] : NULL;
  if (!s) { top_header(c, ""); return; }
  if (s->kind == S_PLAZA) { top_plaza(c, s); top_header(c, title); }
  else {
    top_header(c, title);
    if (sel && (sel->kind == K_POST || sel->kind == K_REPLY)) top_post(c, sel, s->kind == S_POST);
    else if (sel && sel->kind == K_WIG) {
      card(c, 30, 30, 340, 206, 0);
      if (sel->dpx) draw_drawing(c, sel->dpx, sel->dpal, 3, (int)((nowMs / 140) % 4), 40, 36, 320, 180);
      char b[200]; snprintf(b, sizeof b, "\"%s\" by %s - Yeah! %d", sel->title ? sel->title : "", sel->name ? sel->name : "", sel->yeahs);
      text_fit(c, FB, 11, 40, 218, b, C_INK, 320);
    } else if (sel && sel->kind == K_COMM) {
      card(c, 14, 32, 372, 200, 0);
      Pic *p = sel->avatar && *sel->avatar ? pic_get(sel->avatar) : NULL;
      if (p) pic_draw_sq(c, p, 26, 44, 72); else { fill(c, 26, 44, 72, 72, C_GREEN); char l[2] = { sel->name && *sel->name ? sel->name[0] : '?', 0 }; text_c(c, FB, 44, 62, 52, l, C_WHITE); }
      text_fit(c, FB, 18, 110, 44, sel->name, C_INK, 266);
      rrect(c, 110, 70, 56, 16, 3, C_GREEN); text_c(c, FB, 10, 138, 72, "All ages", C_WHITE);
      char b[64]; snprintf(b, sizeof b, "%d posts", sel->posts); text(c, FR, 11, 174, 71, b, C_SOFT);
      text_wrap(c, FR, 11, 110, 92, 266, sel->body, C_SOFT, 14, 2);
      if (sel->msg && *sel->msg) { fill(c, 14, 128, 372, 1, 0xe3ecf0); text(c, FB, 9, 26, 136, "MESSAGE FROM THE CREATOR", C_SOFT); text_wrap(c, FR, 12, 26, 150, 348, sel->msg, C_INK, 16, 4); }
    } else if (sel && sel->kind == K_GAME) {
      card(c, 14, 32, 372, 200, 0);
      if (sel->id) rrect_g(c, 26, 44, 100, 100, 14, 0xff6a3d, 0x9b3fd6); else rrect_g(c, 26, 44, 100, 100, 14, 0x86e3ff, 0x2ab76a);
      text_c(c, FB, 18, 76, 70, sel->id ? "ULTRA" : "Super", C_WHITE); text_c(c, FB, 18, 76, 94, "Wiggle", C_WHITE);
      text_fit(c, FB, 18, 138, 46, sel->title, C_INK, 240);
      text_wrap(c, FR, 12, 138, 72, 238, sel->body, C_SOFT, 16, 8);
    } else if (s->kind == S_LOGIN || s->kind == S_RULES) {
      card(c, 20, 34, 360, 196, 12);
      img_draw(c, &logoImg, 200 - 119, 52, 238, 48, 0, 255);
      if (s->kind == S_RULES) { text_c(c, FB, 15, 200, 116, s->mode ? "Welcome to Aeroverse!" : "Aeroverse Rules", C_SEC2); text_wrap(c, FR, 12, 44, 142, 312, s->mode ? "Please read the rules on the bottom screen and accept them to continue. You can read them again any time from the Profile tab." : "Everyone who uses Aeroverse follows these rules.", C_INK, 16, 4); }
      else { text_c(c, FB, 15, 200, 116, s->mode ? "Create your Aeroverse account" : "Sign in to Aeroverse", C_SEC2); text_wrap(c, FR, 12, 44, 142, 312, "Use the same account as on the website and the Wii U. Tap a field on the bottom screen to type with the keyboard.", C_INK, 16, 4); }
    } else if (s->kind == S_COMPOSE) {
      card(c, 14, 32, 372, 200, 0);
      text(c, FB, 13, 26, 42, s->ckind == 1 ? "Your reply" : "Your post", C_SEC2);
      int y = 64;
      if (s->text[0]) y += text_wrap(c, FR, 12, 26, y, 348, s->text, C_INK, 16, s->draw ? 2 : 9) + 6; else { text(c, FR, 12, 26, y, "(nothing written yet)", 0xa8b4ba); y += 22; }
      if (s->draw) { uint32_t pal[32]; memcpy(pal, PAL, sizeof pal); int dh = 228 - y, dw = dh * 16 / 9; draw_drawing(c, s->draw, pal, s->drawWig, s->drawWig ? (int)((nowMs / 140) % 4) : -1, 200 - dw / 2, y, dw, dh); }
    } else if (sel && sel->kind == K_PERSON) {
      top_welcome(c);
    } else if (s->kind == S_USER && s->n && s->it[0].kind == K_PROFILE) {
      Item *p = &s->it[0];
      card(c, 14, 32, 372, 200, 0);
      draw_avatar(c, p->name, p->user, p->avatar, 28, 46, 80);
      text_fit(c, FB, 20, 122, 48, p->name, C_INK, 250);
      char b[100]; snprintf(b, sizeof b, "@%s", p->user ? p->user : ""); text(c, FR, 12, 122, 76, b, C_SOFT);
      snprintf(b, sizeof b, "%d posts", p->posts); text(c, FB, 12, 122, 96, b, C_GREEN_D);
      if (p->plain) text_wrap(c, FR, 12, 28, 140, 344, p->plain, C_INK, 16, 5);
    } else top_welcome(c);
  }
  /* toast */
  if (toastMsg[0] && nowMs < toastUntil) {
    int w = text_w(FB, 11, toastMsg) + 24; if (w > 390) w = 390;
    rrect(c, 200 - w / 2, 206, w, 26, 13, 0xe62d5d78);
    text_fit(c, FB, 11, 200 - w / 2 + 12, 212, toastMsg, C_WHITE, w - 24);
  }
}

/* ------------------------------------------------------------------ actions */
static void go_tab(int t) {
  if (t < 0) t = 4;
  if (t > 4) t = 0;
  while (depth > 0) { Screen *s = stack[--depth]; int root = 0; for (int i = 0; i < 5; i++) if (roots[i] == s) root = 1; if (!root) free_screen(s); }
  curTab = t;
  if (t == 4 && (!roots[4] || (me.id && roots[4]->kind == S_LOGIN) || (!me.id && roots[4]->kind == S_USER))) { if (roots[4]) free_screen(roots[4]); roots[4] = make_profile_root(); roots[4]->n = 0; }
  push(roots[t]);
  if (!roots[t]->n) load_screen(roots[t], 0);
}
static void open_item(Screen *s, Item *it) {
  switch (it->kind) {
  case K_POST:
    if (s->kind == S_POST) return;
    { Screen *p = new_screen(S_POST); p->postId = it->id; push(p);
      Item *f = add_item(p, K_POST); f->id = it->id; f->yeahs = it->yeahs; f->replies = it->replies; f->mine = it->mine; f->created = it->created;
      f->name = xstrdup(it->name); f->user = xstrdup(it->user); f->cname = xstrdup(it->cname); f->cid = xstrdup(it->cid); f->body = xstrdup(it->body); f->plain = xstrdup(it->plain); f->drawing = xstrdup(it->drawing); f->avatar = xstrdup(it->avatar); f->style = xstrdup(it->style);
      measure(f); finish(p);
      char path[64]; snprintf(path, sizeof path, "/api/posts/%d/replies", it->id); p->loading = 1; net_send("GET", path, NULL, replies_cb, p->serial, 0); }
    break;
  case K_REPLY: do_yeah(s, it); break;
  case K_COMM: { Screen *c = new_screen(S_COMM); snprintf(c->cid, sizeof c->cid, "%s", it->cid ? it->cid : ""); snprintf(c->cname, sizeof c->cname, "%s", it->name ? it->name : ""); push(c); load_screen(c, 0); break; }
  case K_PERSON: { Screen *u = new_screen(S_USER); snprintf(u->cid, sizeof u->cid, "%s", it->user ? it->user : ""); push(u); load_screen(u, 0); break; }
  case K_GAME: { Screen *g = new_screen(S_GALLERY); g->mode = it->id; snprintf(g->cname, sizeof g->cname, "%s", it->title); push(g); load_screen(g, 0); break; }
  case K_WIG: do_yeah(s, it); break;
  case K_FIELD: {
    char tmp[1024];
    if (it->act == 1 && s->kind == S_LOGIN) { if (plat_keyboard("Username", s->text2, tmp, 32, 0, 0)) snprintf(s->text2, sizeof s->text2, "%s", tmp); }
    else if (it->act == 2) { if (plat_keyboard("Password", "", tmp, 100, 1, 0)) snprintf(s->pass, sizeof s->pass, "%s", tmp); }
    else if (it->act == 3) { if (plat_keyboard("Display name", s->cname, tmp, 40, 0, 0)) snprintf(s->cname, sizeof s->cname, "%s", tmp); }
    else if (it->act == 1) { if (plat_keyboard(s->ckind == 1 ? "Write a reply" : "Write a post", s->text, tmp, 1000, 0, 1)) snprintf(s->text, sizeof s->text, "%s", tmp); }
    else if (it->act == 4) { ed_open(s, 0, 0); return; }
    swallow = 1;
    rebuild_form(s);
    break; }
  case K_ACTION:
    switch (it->act) {
    case A_SEND: compose_send(s); break;
    case A_SIGNOUT: net_send("POST", "/api/logout", "{}", signout_cb, 0, 0); forget_token(); memset(&me, 0, sizeof me); toast("Signed out."); go_tab(4); break;
    case A_RULESOK: rulesOk = 1; save_settings(); pop(); toast("Thanks! Have fun on Aeroverse."); break;
    case A_RULES: { Screen *r = new_screen(S_RULES); push(r); load_screen(r, 0); break; }
    case A_UPDATE: update_start(); break;
    case A_MODE: s->mode = !s->mode; s->sel = -1; rebuild_form(s); break;
    case A_WIGTOG: s->drawWig = s->drawWig ? 0 : 3; rebuild_form(s); break;
    case A_RETRY: load_screen(s, 0); break;
    }
    break;
  }
  dirty = 1;
}
static void bar_press(Screen *s, int id) {
  Item *sel = s->sel >= 0 && s->sel < s->n ? &s->it[s->sel] : NULL;
  switch (id) {
  case B_BACK: pop(); break;
  case B_WRITE: open_compose(0, s->kind == S_COMM ? s->cid : NULL, s->kind == S_COMM ? s->cname : NULL, 0); break;
  case B_REFRESH: load_screen(s, 0); break;
  case B_YEAH: if (sel) do_yeah(s, sel); else if (s->n) do_yeah(s, &s->it[0]); break;
  case B_REPLY: open_compose(1, NULL, s->cname, s->postId); break;
  case B_DRAW: if (!me.id) toast("Sign in first (Profile tab)."); else ed_open(NULL, 1, s->mode); break;
  case B_FILTER: s->filter = (s->filter + 1) % 4; load_screen(s, 0); break;
  case B_SEND: compose_send(s); break;
  case B_OPEN: if (sel) open_item(s, sel); break;
  case B_ACCEPT: rulesOk = 1; save_settings(); pop(); toast("Thanks! Have fun on Aeroverse."); break;
  }
  dirty = 1;
}
static void ensure_visible(Screen *s) {
  if (s->sel < 0 || s->sel >= s->n) return;
  Item *it = &s->it[s->sel];
  if (it->y - s->scroll < 4) s->scroll = (float)(it->y - 4);
  else if (it->y + it->h - s->scroll > LIST_H - 4) s->scroll = (float)(it->y + it->h - LIST_H + 4);
}
static void move_sel(Screen *s, int d) {
  int i = s->sel;
  for (int k = 0; k < s->n; k++) { i += d; if (i < 0 || i >= s->n) return; if (sel_ok(&s->it[i])) { s->sel = i; ensure_visible(s); dirty = 1; return; } }
}

/* ------------------------------------------------------------------ main */
static void make_backgrounds(void) {
  bgTop.w = 400; bgTop.h = 240; bgTop.px = malloc(400 * 240 * 4);
  bgBot.w = 320; bgBot.h = 240; bgBot.px = malloc(320 * 240 * 4);
  Img *b[2] = { &bgTop, &bgBot };
  for (int k = 0; k < 2; k++) for (int y = 0; y < 240; y++) for (int x = 0; x < b[k]->w; x++) {
    uint32_t col = lerpc(0xbfe7f8, 0xeef9fe, y / 239.0f);
    if ((x % 14 == 0) && (y % 14 == 0)) col = lerpc(col, 0xffffff, 0.7f);
    b[k]->px[y * b[k]->w + x] = 0xff000000u | col;
  }
}
int main(int argc, char **argv) {
  plat_init();
  if (argc > 0 && argv && argv[0] && !strncmp(argv[0], "sdmc:/", 6) && strstr(argv[0], ".3dsx")) snprintf(appPath, sizeof appPath, "%s", argv[0]);
  srand((unsigned)time(NULL));
  for (int i = 0; i < 32; i++) PAL[i] = (uint32_t)strtoul(PALHEX[i], NULL, 16);
  cv_init(&TOP, 400, 240); cv_init(&BOT, 320, 240);
  text_init(nunito_reg_ttf, nunito_bold_ttf);
  logoImg = img_from_rgba(LOGO_RGBA, LOGO_W, LOGO_H);
  logoSm = img_scale(&logoImg, 89, 18);
  for (int i = 0; i < 5; i++) { Img ic = img_from_rgba(ICONS_RGBA[i], ICON_SZ, ICON_SZ); icons[i] = img_scale(&ic, 16, 16); img_free(&ic); }
  { Img b = img_from_rgba(BODY_RGBA, BODY_W, BODY_H); bodyImg = img_scale(&b, 18, 18); img_free(&b); }
  make_backgrounds();
  load_settings();
  load_token();
  net_init();
  if (sessionTok[0]) net_send("GET", "/api/me", NULL, me_cb, 0, 0);
  for (int i = 0; i < 4; i++) { roots[i] = new_screen(i == 0 ? S_FEED : i == 1 ? S_COMMS : i == 2 ? S_PLAZA : S_GAMES); roots[i]->tab = i; }
  roots[4] = make_profile_root();
  go_tab(0);
  if (!rulesOk) { Screen *r = new_screen(S_RULES); r->mode = 1; push(r); load_screen(r, 0); }
  update_check();

  Input in; memset(&in, 0, sizeof in);
  int touchDown = 0, tsx = 0, tsy = 0, tly = 0, moved = 0, lastMeId = 0;
  uint32_t holdT = 0;
  while (running && plat_running()) {
    nowMs = plat_ms(); frameNo++;
    plat_input(&in);
    net_poll(); pic_poll();
    if (picChanged) { picChanged = 0; dirty = 1; }
    if (me.id != lastMeId) { lastMeId = me.id; dirty = 1; if (curTab == 4) go_tab(4); }
    if (in.down & KEY_START_) break;
    if (swallow) { if (in.touch) in.touch = 0; else swallow = 0; }
    Screen *s = cur();
    if (ed.on) {
      ed_chaos();
      ed_touch(&in);
      if (in.down & (KEY_L_ | KEY_Y_)) ed_undo();
      if (in.down & KEY_X_) ed_done();
      if (in.down & KEY_B_) ed_close();
      ed_draw_top(); ed_draw_bottom();
      plat_present(&TOP, &BOT);
      continue;
    }
    if (s) {
      /* buttons */
      if (in.down & KEY_L_) go_tab(curTab - 1);
      else if (in.down & KEY_R_) go_tab(curTab + 1);
      else if (in.down & KEY_B_) { if (s->kind != S_RULES || !s->mode) pop(); }
      else if (in.down & KEY_DOWN_) { move_sel(s, 1); holdT = nowMs + 350; }
      else if (in.down & KEY_UP_) { move_sel(s, -1); holdT = nowMs + 350; }
      else if ((in.held & KEY_DOWN_) && nowMs > holdT) { move_sel(s, 1); holdT = nowMs + 90; }
      else if ((in.held & KEY_UP_) && nowMs > holdT) { move_sel(s, -1); holdT = nowMs + 90; }
      else if (in.down & KEY_A_) { if (s->sel >= 0) open_item(s, &s->it[s->sel]); }
      else if (in.down & KEY_Y_) { if (s->sel >= 0) do_yeah(s, &s->it[s->sel]); }
      else if (in.down & KEY_X_) { Btn b[5]; int n = bar_buttons(s, b); for (int i = 0; i < n; i++) if (b[i].id != B_BACK) { bar_press(s, b[i].id); break; } }
      s = cur();
      /* touch */
      if (s && in.touch) {
        if (!touchDown) { touchDown = 1; tsx = in.tx; tsy = in.ty; tly = in.ty; moved = 0; s->vel = 0; }
        else {
          int dy = in.ty - tly;
          if (abs(in.ty - tsy) > 6 || abs(in.tx - tsx) > 6) moved = 1;
          if (moved && tsy < LIST_H) { s->scroll -= dy; s->vel = (float)-dy; dirty = 1; }
          tly = in.ty;
        }
      } else if (s && touchDown) {
        touchDown = 0;
        if (!moved) {
          int tx = tsx, ty = tsy;
          if (ty >= TAB_Y) { go_tab(tx / 64); }
          else if (ty >= BAR_Y) {
            Btn b[5]; int n = bar_buttons(s, b);
            int w = n ? (312 - (n - 1) * 6) / n : 0;
            for (int i = 0; i < n; i++) if (tx >= 4 + i * (w + 6) && tx < 4 + i * (w + 6) + w) bar_press(s, b[i].id);
          } else {
            int cy = ty + (int)s->scroll;
            for (int i = 0; i < s->n; i++) {
              Item *it = &s->it[i];
              if (tx >= it->x && tx < it->x + it->w && cy >= it->y && cy < it->y + it->h) {
                if (!sel_ok(it)) break;
                int again = s->sel == i;
                s->sel = i; dirty = 1;
                /* posts: first tap shows it on the top screen, second tap opens it; Yeah! button in the card */
                if ((it->kind == K_POST || it->kind == K_REPLY) && cy >= it->y + it->h - 18 && tx < it->x + 80) { do_yeah(s, it); break; }
                if (it->kind == K_POST || it->kind == K_REPLY || it->kind == K_WIG) { if (again && it->kind == K_POST) open_item(s, it); }
                else open_item(s, it);
                break;
              }
            }
          }
        }
      } else if (s && !in.touch) {
        if (fabsf(s->vel) > 0.3f) { s->scroll += s->vel; s->vel *= 0.92f; dirty = 1; } else s->vel = 0;
      }
      s = cur();
      if (s && fabsf(in.cy) > 0.15f) { s->scroll -= in.cy * 6; dirty = 1; }
      /* load more at the end of the list */
      if (s && s->more && !s->loading && s->scroll + LIST_H > s->contentH - 40) load_screen(s, 1);
    }
    if (cur() && cur()->kind == S_PLAZA) { plaza_step(); dirty = 1; }
    if (frameNo % 8 == 0) dirty = 1;   /* wiggle animation, clocks, toasts */
    if (dirty) { draw_top(); draw_bottom(); dirty = 0; }
    plat_present(&TOP, &BOT);
  }
  netQuit = 1;
  plat_exit();
  return 0;
}
