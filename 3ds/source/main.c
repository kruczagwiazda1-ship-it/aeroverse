/*
 * Aeroverse for Nintendo 3DS (Homebrew Launcher, .3dsx)
 * Same website, same account, same posts as the PC and Wii U versions.
 * Top screen: the selected post / drawing / community big. Bottom screen (touch): lists, buttons, drawing.
 */
#define APP_VERSION "1.3"
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
#include "plaza3d.h"

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
static uint32_t name_hash(const char *u) { uint32_t n = 0; for (const unsigned char *p = (const unsigned char *)(u ? u : "?"); *p; p++) n = n * 31 + *p; return n; }

static Cv TOP, BOT;
static Img bgTop, bgBot, logoImg, logoSm, logoMid, icons20[5];
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
static int rulesOk, ageOk;
static void save_settings(void) { FILE *f = fopen(SETTINGS_FILE, "w"); if (f) { fprintf(f, "rules=%d\nage=%d\n", rulesOk, ageOk); fclose(f); } }
static void load_settings(void) {
  FILE *f = fopen(SETTINGS_FILE, "r"); if (!f) return;
  char ln[64]; int v;
  while (fgets(ln, sizeof ln, f)) { if (sscanf(ln, "rules=%d", &v) == 1) rulesOk = v; else if (sscanf(ln, "age=%d", &v) == 1) ageOk = v; }
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

/* ------------------------------------------------------------------ screens and items
 * The page is one long column that continues from the top screen onto the bottom screen (like the 3DS browser):
 * the top screen shows the upper part, the bottom screen (touch) the lower part. */
enum { S_FEED, S_COMMS, S_COMM, S_POST, S_PLAZA, S_GAMES, S_GALLERY, S_USER, S_LOGIN, S_COMPOSE, S_RULES, S_NOTIF, S_REPORT, S_EDIT, S_AGE };
enum { K_POST, K_REPLY, K_COMM, K_SEC, K_INFO, K_ACTION, K_MORE, K_PERSON, K_GAME, K_WIG, K_FIELD, K_RULE, K_PROFILE, K_EMPTY,
       K_HERO, K_PROMPT, K_CHEAD, K_DCELL, K_NOTIF };
enum { A_NONE, A_SEND, A_SIGNOUT, A_RULESOK, A_UPDATE, A_MODE, A_DRAW, A_WIGTOG, A_RETRY, A_RULES, A_NOTIFS, A_EDIT, A_PASSWORD, A_DELETE, A_REPORT, A_REASON, A_SHIRT, A_AGEYES, A_AGENO };

#define COLW 312
#define TOP_Y0 26                 /* page area on the top screen: y 26..240 */
#define TOPV (240 - TOP_Y0)
#define TOP_X0 44
#define BOT_X0 4
#define TAB_Y 206                 /* bottom: page 0..178, action bar 178..206, tabs 206..240 */
#define BAR_Y 178
#define C_PAGE 0xe2f2fa
#define PIC_W 296
#define PIC_H 166

typedef struct {
  int kind, act, id, yeahs, replies, mine, posts, icon, tags, full;
  double created;
  char *name, *user, *cname, *cid, *body, *drawing, *title, *avatar, *age, *msg, *plain, *style, *platform;
  char *pics[3]; int npics;
  uint8_t *dpx; uint32_t dpal[32]; int wig;
  int x, y, w, h;
  Img cache;                       /* the item rendered once (fast scrolling) */
  int drx, dry, drw, drh;          /* where the (wiggling) drawing is inside the item */
} Item;
typedef struct Screen {
  int kind, serial, mode, tab;
  Item *it; int n, cap;
  int sel; float scroll, vel; int contentH;
  int loading, lastId, page, more;
  char err[160];
  char cid[64], cname[96];
  int postId;
  int ckind;
  char text[1024], text2[100], pass[128];
  uint8_t *draw; int drawWig;
  int filter;
  float az, polar, dist; int autoRot;
} Screen;
#define MAXS 24
static Screen *stack[MAXS]; static int depth;
static Screen *roots[5];
static int curTab;
static int serialCounter = 1;
static Screen *cur(void) { return depth ? stack[depth - 1] : NULL; }
static Screen *by_serial(int s) { for (int i = 0; i < depth; i++) if (stack[i]->serial == s) return stack[i]; for (int i = 0; i < 5; i++) if (roots[i] && roots[i]->serial == s) return roots[i]; return NULL; }
static int page_topv(const Screen *s) { return s && s->kind == S_PLAZA ? 0 : TOPV; }   /* the plaza uses the top screen for the 3D island */
static int page_h(const Screen *s) { return page_topv(s) + BAR_Y; }
static void item_uncache(Item *it) { img_free(&it->cache); }
static void item_free(Item *it) {
  free(it->name); free(it->user); free(it->cname); free(it->cid); free(it->body); free(it->drawing); free(it->title); free(it->avatar); free(it->age); free(it->msg); free(it->plain); free(it->style); free(it->platform); free(it->dpx);
  for (int i = 0; i < it->npics; i++) free(it->pics[i]);
  item_uncache(it);
}
static void screen_uncache(Screen *s) { if (s) for (int i = 0; i < s->n; i++) item_uncache(&s->it[i]); }
static Item *add_item(Screen *s, int kind) {
  if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 32; s->it = realloc(s->it, sizeof(Item) * (size_t)s->cap); }
  Item *it = &s->it[s->n++]; memset(it, 0, sizeof *it); it->kind = kind; it->w = COLW;
  return it;
}
static void clear_items(Screen *s, int from) { for (int i = from; i < s->n; i++) item_free(&s->it[i]); if (from < s->n) s->n = from; dirty = 1; }
static Screen *new_screen(int kind) { Screen *s = calloc(1, sizeof(Screen)); s->kind = kind; s->serial = serialCounter++; s->sel = -1; s->az = 0.6f; s->polar = 1.0f; s->dist = 70; s->autoRot = 1; return s; }
static void free_screen(Screen *s) { clear_items(s, 0); free(s->it); free(s->draw); free(s); }
static int is_root(Screen *s) { for (int i = 0; i < 5; i++) if (roots[i] == s) return 1; return 0; }
static void push(Screen *s) { if (depth < MAXS) stack[depth++] = s; else free_screen(s); dirty = 1; }
static void pop(void) { if (depth <= 1) return; Screen *s = stack[--depth]; if (!is_root(s)) free_screen(s); dirty = 1; }
static int unread;   /* notifications not read yet */
static int sel_ok(const Item *it) { return !(it->kind == K_SEC || it->kind == K_INFO || it->kind == K_EMPTY || it->kind == K_RULE || it->kind == K_MORE || it->kind == K_HERO || it->kind == K_PROFILE || it->kind == K_CHEAD); }

static void fill_post(Item *it, cJSON *p) {
  cJSON *a = cJSON_GetObjectItem(p, "author"), *c = cJSON_GetObjectItem(p, "community");
  it->id = (int)jnum(p, "id"); it->yeahs = (int)jnum(p, "yeahs"); it->replies = (int)jnum(p, "replies"); it->mine = jbool(p, "mine");
  it->created = jnum(p, "created_at");
  const char *disp = jstr(a, "display"), *un = jstr(a, "username");
  it->name = xstrdup(disp && *disp ? disp : un); it->user = xstrdup(un);
  it->cname = xstrdup(cJSON_IsObject(c) ? jstr(c, "name") : NULL);
  it->cid = xstrdup(cJSON_IsObject(c) ? jstr(c, "id") : NULL);
  it->body = xstrdup(jstr(p, "body"));
  it->plain = strip_markup(it->body, 3000);
  it->drawing = xstrdup(jstr(p, "drawing"));
  it->avatar = xstrdup(jstr(a, "avatar"));
  it->style = xstrdup(jstr(p, "style"));
  it->platform = xstrdup(jstr(p, "platform"));
  const char *b = it->body;
  while (b && (b = strstr(b, "[img=")) && it->npics < 3) {
    const char *e = strchr(b, ']'); if (!e) break;
    size_t L = (size_t)(e - b - 5);
    if (L < 190) { char u[200]; memcpy(u, b + 5, L); u[L] = 0; if (img_host_ok(u)) it->pics[it->npics++] = xstrdup(u); }
    b = e + 1;
  }
}
static Item *copy_post(Screen *s, const Item *it, int kind) {
  Item *f = add_item(s, kind);
  f->id = it->id; f->yeahs = it->yeahs; f->replies = it->replies; f->mine = it->mine; f->created = it->created;
  f->name = xstrdup(it->name); f->user = xstrdup(it->user); f->cname = xstrdup(it->cname); f->cid = xstrdup(it->cid); f->body = xstrdup(it->body);
  f->plain = xstrdup(it->plain); f->drawing = xstrdup(it->drawing); f->avatar = xstrdup(it->avatar); f->style = xstrdup(it->style); f->platform = xstrdup(it->platform);
  for (int i = 0; i < it->npics; i++) f->pics[f->npics++] = xstrdup(it->pics[i]);
  return f;
}
static void item_drawing(Item *it) {
  if (it->dpx || !it->drawing || !*it->drawing) return;
  it->dpx = malloc(DPIX);
  it->wig = decode_drawing(it->drawing, it->dpx, it->dpal);
  if (it->wig < 0) { free(it->dpx); it->dpx = NULL; }
}
/* where the post came from (the website fills "platform": pc, mobile, wiiu, 3ds) */
static const char *platform_name(const char *p) {
  if (!p || !*p) return NULL;
  if (!strcmp(p, "3ds")) return "Nintendo 3DS";
  if (!strcmp(p, "wiiu")) return "Wii U";
  if (!strcmp(p, "mobile")) return "Mobile";
  if (!strcmp(p, "pc")) return "PC";
  if (!strcmp(p, "lite")) return "Aeroverse Lite";
  return p;
}

/* ---- post styles from the website's NEW POST editor: "bg.font.size[.align deco]" (shown in Nunito here) */
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
  else if (ps.bg) rrect(c, x, y, w, h, ps.deco == 3 ? 0 : 5, PC_BG[ps.bg]);
}
static int styled_box(PStyle ps) { return ps.bg || ps.deco; }
static int post_font_px(PStyle ps, int full) { static const int S[3] = { 15, 17, 21 }; return S[ps.size] + (full && ps.size == 1 ? 1 : 0); }

/* ---- sizes (everything bigger and thicker than on the PC because the 3DS screens are small) */
#define PAD 8
static int post_lines_max(const Item *it) { return it->full ? 0 : (it->dpx || it->npics ? 3 : 8); }
static int post_text_h(const Item *it) {
  if (!it->plain || !*it->plain) return 0;
  PStyle ps = post_style(it->style); int fs = post_font_px(ps, it->full), lh = fs + fs / 3;
  int pad = styled_box(ps) ? 8 : 0;
  return text_wrap(NULL, FM, fs, 0, 0, it->w - 2 * PAD - 2 * pad, it->plain, 0, lh, post_lines_max(it)) + 2 * pad;
}
static void measure(Item *it) {
  switch (it->kind) {
  case K_POST: case K_REPLY: {
    item_drawing(it);
    int h = PAD + 34 + 6;
    int th = post_text_h(it); if (th) h += th + 6;
    h += it->npics * (PIC_H + 6);
    if (it->dpx) h += PIC_H + 6;
    h += 26 + PAD - 4;
    it->h = h; break; }
  case K_COMM: it->h = 64; break;
  case K_CHEAD: it->h = 16 + 56 + 8 + text_wrap(NULL, FM, 13, 0, 0, COLW - 24, it->body, 0, 17, 3) + (it->msg && *it->msg ? 26 + text_wrap(NULL, FM, 14, 0, 0, COLW - 24, it->msg, 0, 18, 4) : 0) + 12; break;
  case K_SEC: it->h = 24; break;
  case K_INFO: it->h = text_wrap(NULL, FM, 13, 0, 0, it->w - 8, it->body, 0, 18, 0) + 6; break;
  case K_EMPTY: it->h = text_wrap(NULL, FM, 14, 0, 0, it->w - 28, it->body, 0, 19, 0) + 34; break;
  case K_ACTION: it->h = 34; break;
  case K_MORE: it->h = 28; break;
  case K_PERSON: it->h = 50; break;
  case K_GAME: it->h = 100; break;
  case K_WIG: case K_DCELL: item_drawing(it); it->w = 100; it->h = 56 + 36; break;
  case K_FIELD: it->h = 54; break;
  case K_RULE: { it->h = 10 + 18 + text_wrap(NULL, FM, 13, 0, 0, it->w - 44, it->body, 0, 18, 0) + 10; break; }
  case K_PROFILE: it->h = PAD + 64 + PAD + (it->plain && *it->plain ? text_wrap(NULL, FM, 14, 0, 0, it->w - 2 * PAD, it->plain, 0, 18, 5) + 6 : 0); break;
  case K_HERO: it->h = 150; break;
  case K_PROMPT: it->h = it->act ? 92 : 56; break;
  case K_NOTIF: it->h = 58; break;
  }
  item_uncache(it);
}
static void layout(Screen *s) {
  int y = 6, col = 0, rowH = 0;
  for (int i = 0; i < s->n; i++) {
    Item *t = &s->it[i];
    if (t->kind == K_WIG || t->kind == K_DCELL) {
      t->x = col * 106; t->y = y; if (t->h > rowH) rowH = t->h;
      if (++col == 3) { col = 0; y += rowH + 6; rowH = 0; }
      continue;
    }
    if (col) { col = 0; y += rowH + 6; rowH = 0; }
    t->x = 0; t->y = y; y += t->h + (t->kind == K_SEC ? 2 : 8);
  }
  if (col) y += rowH + 6;
  s->contentH = y + 8;
}
static void add_sec(Screen *s, const char *t) { Item *h = add_item(s, K_SEC); h->title = xstrdup(t); measure(h); }
static void add_info(Screen *s, const char *t) { Item *h = add_item(s, K_INFO); h->body = xstrdup(t); measure(h); }
static void add_empty(Screen *s, const char *t) { Item *h = add_item(s, K_EMPTY); h->body = xstrdup(t); measure(h); }
static void add_action(Screen *s, int act, const char *t) { Item *h = add_item(s, K_ACTION); h->act = act; h->title = xstrdup(t); measure(h); }
static int starts_low(const Screen *s) { return s->kind == S_COMMS || s->kind == S_GAMES || s->kind == S_LOGIN || s->kind == S_COMPOSE || s->kind == S_RULES || s->kind == S_GALLERY || s->kind == S_REPORT || s->kind == S_EDIT || s->kind == S_AGE || s->kind == S_NOTIF; }
static void finish(Screen *s) {
  layout(s);
  int autoSel = s->kind == S_LOGIN || s->kind == S_COMPOSE || s->kind == S_RULES || s->kind == S_EDIT || s->kind == S_REPORT || s->kind == S_AGE;
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
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); if (!append) { add_empty(s, s->err); add_action(s, A_RETRY, "Try again"); } finish(s); return; }
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
/* Home: Most liked this week + Drawings of the week, then the latest posts (like the website) */
static void top_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)len; (void)err; (void)extra;
  Screen *s = by_serial(serial); if (!s) return;
  if (code == 200 && body) {
    cJSON *j = cJSON_Parse(body), *arr = j ? cJSON_GetObjectItem(j, "posts") : NULL, *p;
    add_sec(s, "Most liked this week");
    int n = 0;
    cJSON_ArrayForEach(p, arr) { Item *it = add_item(s, K_POST); fill_post(it, p); measure(it); n++; }
    if (!n) add_empty(s, "No liked posts this week yet. Give a Yeah! to something you like.");
    arr = j ? cJSON_GetObjectItem(j, "drawings") : NULL; n = 0;
    add_sec(s, "Drawings of the week");
    cJSON_ArrayForEach(p, arr) { if (n >= 6) break; Item *it = add_item(s, K_DCELL); fill_post(it, p); measure(it); n++; }
    if (!n) add_empty(s, "No liked drawings this week yet.");
    cJSON_Delete(j);
  }
  add_sec(s, "Latest posts");
  finish(s);
  net_send("GET", "/api/posts?limit=15", NULL, posts_cb, s->serial, 0);
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
    Item *it = add_item(s, K_COMM);
    it->cid = xstrdup(jstr(c, "id")); it->name = xstrdup(jstr(c, "name")); it->body = xstrdup(jstr(c, "description"));
    it->msg = xstrdup(jstr(c, "message")); it->avatar = xstrdup(jstr(c, "thumb")); it->age = xstrdup(age ? age : "all"); it->posts = (int)jnum(c, "posts");
    measure(it);
  }
  add_info(s, "Starting a new community is done on the website (it needs a picture).");
  cJSON_Delete(j);
  finish(s);
}
static void post_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)len; (void)extra;
  Screen *s = by_serial(serial); if (!s) return;
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); toast(s->err); return; }
  cJSON *j = cJSON_Parse(body), *p = j ? cJSON_GetObjectItem(j, "post") : NULL;
  if (cJSON_IsObject(p)) {
    if (!(s->n && s->it[0].kind == K_POST)) { add_item(s, K_POST); Item t = s->it[s->n - 1]; memmove(&s->it[1], &s->it[0], sizeof(Item) * (size_t)(s->n - 1)); s->it[0] = t; }
    int rep = s->it[0].replies;
    item_free(&s->it[0]); memset(&s->it[0], 0, sizeof(Item)); s->it[0].kind = K_POST; s->it[0].w = COLW; s->it[0].full = 1;
    fill_post(&s->it[0], p); if (!s->it[0].replies) s->it[0].replies = rep; measure(&s->it[0]);
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
  cJSON_ArrayForEach(r, arr) { Item *it = add_item(s, K_REPLY); it->w = COLW - 14; fill_post(it, r); measure(it); }
  if (!n) add_info(s, "No replies yet.");
  add_action(s, A_DRAW, me.id ? "Write a reply" : "Sign in to reply");
  if (me.id && keep) {
    if (s->it[0].user && !strcmp(s->it[0].user, me.username)) add_action(s, A_DELETE, "Delete this post");
    else add_action(s, A_REPORT, "Report this post");
  }
  if (keep) { s->it[0].replies = n; item_uncache(&s->it[0]); }
  cJSON_Delete(j);
  finish(s);
  for (int i = 0; i < s->n; i++) if (s->it[i].kind == K_REPLY) s->it[i].x = 14;
}
static void user_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)len; (void)extra;
  Screen *s = by_serial(serial); if (!s) return;
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); toast(s->err); return; }
  cJSON *j = cJSON_Parse(body), *u = j ? cJSON_GetObjectItem(j, "user") : NULL;
  Item tmpP; memset(&tmpP, 0, sizeof tmpP);
  Item *it = add_item(s, K_PROFILE);
  const char *disp = jstr(u, "display");
  it->name = xstrdup(disp && *disp ? disp : jstr(u, "username")); it->user = xstrdup(jstr(u, "username"));
  it->body = xstrdup(jstr(u, "bio")); it->plain = strip_markup(it->body, 600);
  it->posts = (int)jnum(u, "posts"); it->created = jnum(u, "created_at"); it->avatar = xstrdup(jstr(u, "avatar")); it->icon = (int)jnum(u, "shirt");
  measure(it);
  /* the profile card goes first, before posts that may have arrived already */
  if (s->n > 1) { Item t = s->it[s->n - 1]; memmove(&s->it[1], &s->it[0], sizeof(Item) * (size_t)(s->n - 1)); s->it[0] = t; }
  int at = 1;
  it = &s->it[0];   /* the card moved to the front */
  if (me.id && it->user && !strcmp(it->user, me.username)) {
    Item *w = add_item(s, K_PROMPT); w->act = 1; measure(w);
    { Item t = s->it[s->n - 1]; memmove(&s->it[at + 1], &s->it[at], sizeof(Item) * (size_t)(s->n - 1 - at)); s->it[at] = t; at++; }
    static const int ACTS[4] = { A_NOTIFS, A_EDIT, A_PASSWORD, A_SIGNOUT };
    static const char *AT[4] = { "Notifications", "Edit profile", "Change password", "Sign out" };
    for (int k = 0; k < 4; k++) {
      Item *a = add_item(s, K_ACTION); a->act = ACTS[k]; a->title = xstrdup(AT[k]); measure(a);
      Item t = s->it[s->n - 1]; memmove(&s->it[at + 1], &s->it[at], sizeof(Item) * (size_t)(s->n - 1 - at)); s->it[at] = t; at++;
    }
    Item *r = add_item(s, K_ACTION); r->act = A_RULES; r->title = xstrdup("Aeroverse Rules"); measure(r);
    { Item t = s->it[s->n - 1]; memmove(&s->it[at + 1], &s->it[at], sizeof(Item) * (size_t)(s->n - 1 - at)); s->it[at] = t; at++; }
  }
  (void)tmpP;
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
    it->title = xstrdup(jstr(e, "title")); it->drawing = xstrdup(jstr(e, "drawing")); it->platform = xstrdup(jstr(e, "platform"));
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

/* ---- plaza: the website's 3D island (plaza3d.h), drawn on the top screen */
typedef struct { char *name, *user, *avatar; int tags, shirt, tex, faceDone, item; Walker w; } Person;
static Person pp[48]; static int np;
static P3Buf p3buf;
static void face_tex(Person *q) {
  if (p3ntex >= 64) { q->tex = -1; return; }
  P3Tex *T = &p3tex[p3ntex]; T->w = T->h = 16; T->px = malloc(16 * 16 * 4);
  Cv tc; tc.px = T->px; tc.w = 16; tc.h = 16; cv_noclip(&tc);
  fill(&tc, 0, 0, 16, 16, AVC[name_hash(q->user) % 8]);
  char ini[8] = "?";
  if (q->name && *q->name) { int L = 1; unsigned char ch = (unsigned char)q->name[0]; if (ch >= 0xF0) L = 4; else if (ch >= 0xE0) L = 3; else if (ch >= 0xC0) L = 2; memcpy(ini, q->name, (size_t)L); ini[L] = 0; if (L == 1 && ini[0] >= 'a' && ini[0] <= 'z') ini[0] -= 32; }
  text_c(&tc, FB, 12, 8, 1, ini, C_WHITE);
  for (int k = 0; k < 256; k++) T->px[k] |= 0xff000000u;
  q->tex = p3ntex++;
}
static void plaza_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)len; (void)extra;
  Screen *s = by_serial(serial); if (!s) return;
  s->loading = 0; clear_items(s, 0);
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); add_empty(s, s->err); add_action(s, A_RETRY, "Try again"); finish(s); return; }
  for (int i = 0; i < np; i++) { free(pp[i].name); free(pp[i].user); free(pp[i].avatar); }
  for (int i = 0; i < p3ntex; i++) free(p3tex[i].px);
  np = 0; p3ntex = 0;
  p3_build_world();
  cJSON *j = cJSON_Parse(body), *arr = j ? cJSON_GetObjectItem(j, "people") : NULL, *p;
  static const char *FN[4] = { "Everyone", "Popular", "Newest", "Active now" };
  char t[64]; snprintf(t, sizeof t, "Plaza: %s", FN[s->filter & 3]); add_sec(s, t);
  cJSON_ArrayForEach(p, arr) {
    int tags = 0; cJSON *tg, *tl = cJSON_GetObjectItem(p, "tags");
    cJSON_ArrayForEach(tg, tl) if (cJSON_IsString(tg)) { if (!strcmp(tg->valuestring, "popular")) tags |= 1; if (!strcmp(tg->valuestring, "new")) tags |= 2; if (!strcmp(tg->valuestring, "active")) tags |= 4; }
    if (s->filter && !(tags & (1 << (s->filter - 1)))) continue;
    if (np >= 48) break;
    Item *it = add_item(s, K_PERSON);
    const char *disp = jstr(p, "display");
    it->name = xstrdup(disp && *disp ? disp : jstr(p, "username")); it->user = xstrdup(jstr(p, "username"));
    it->icon = (int)jnum(p, "shirt"); it->avatar = xstrdup(jstr(p, "avatar")); it->tags = tags;
    measure(it);
    Person *q = &pp[np++]; memset(q, 0, sizeof *q);
    q->name = xstrdup(it->name); q->user = xstrdup(it->user); q->avatar = xstrdup(it->avatar); q->shirt = it->icon; q->tags = tags; q->item = s->n - 1;
    face_tex(q);
    float a = (rand() % 1000) / 1000.0f * 6.2831853f, d = 6 + (rand() % 1000) / 1000.0f * 24;
    q->w.pos = v3(sinf(a) * d, 0, cosf(a) * d); q->w.heading = (rand() % 628) / 100.0f;
    q->w.speed = 1.3f + (rand() % 100) / 100.0f * 1.1f; q->w.phase = (rand() % 600) / 100.0f; q->w.wait = (rand() % 300) / 100.0f;
    p3_pick_wp(&q->w);
  }
  if (!np) add_empty(s, "Nobody here right now.");
  else add_info(s, "Circle Pad: look around. Tap a person to visit their page.");
  cJSON_Delete(j);
  finish(s);
}
static float tagX[48], tagY[48]; static int tagVis[48];
static void plaza_render(Screen *s, Cv *c) {
  const int W = 200, H = 107;   /* half resolution, shown twice as big */
  if (!p3buf.px) { p3buf.w = W; p3buf.h = H; p3buf.px = malloc((size_t)W * H * 4); p3buf.zb = malloc((size_t)W * H * sizeof(float)); }
  static uint32_t last; uint32_t now = nowMs; float dt = last ? (now - last) / 1000.0f : 0.03f; if (dt > 0.15f) dt = 0.15f; last = now;
  if (s->autoRot) s->az += dt * 0.06f;
  if (s->polar < 0.45f) s->polar = 0.45f;
  if (s->polar > 1.35f) s->polar = 1.35f;
  if (s->dist < 25) s->dist = 25;
  if (s->dist > 130) s->dist = 130;
  p3_build_world();
  if (!p3world.pre) { p3_precompute(&p3world); p3_precompute(&p3cloud); }
  p3_set_camera(s->az, s->polar, s->dist, (float)W / H);
  {
    V3 fwh = vnorm(v3(p3fw.x, 0, p3fw.z)), far = vadd(p3eye, vmul(fwh, 600)); far.y = -7;
    float hx, hy = 0; V3 fc = p3_cam(far);
    int hor = p3_project(fc, W, H, &hx, &hy) ? (int)hy : -1;
    uint32_t sea = p3_shade(0x5db7e6, v3(0, 1, 0), 0);
    for (int y = 0; y < H; y++) {
      uint32_t col;
      if (y < hor) col = 0xffa9dcf6; else { float k = hor >= 0 ? fminf(1, (y - hor) / (H * 0.35f)) : 1; col = p3_fog(sea, 0.55f * (1 - k)); }
      uint32_t *row = p3buf.px + y * W; for (int x = 0; x < W; x++) row[x] = col;
    }
  }
  memset(p3buf.zb, 0, sizeof(float) * (size_t)W * H);
  p3_draw_mesh(&p3buf, &p3world, v3(0, 0, 0));
  for (int i = 0; i < 4; i++) { p3clouds[i].x += dt * (0.8f + i * 0.15f); if (p3clouds[i].x > 120) p3clouds[i].x = -120; p3_draw_mesh(&p3buf, &p3cloud, p3clouds[i]); }
  static Mesh chars; chars.n = 0;
  for (int i = 0; i < np; i++) {
    Person *q = &pp[i]; Walker *w = &q->w;
    tagVis[i] = 0;
    if (q->avatar && *q->avatar && !q->faceDone && q->tex >= 0) {
      Pic *pic = pic_get(q->avatar);
      if (pic && pic->full.px) {
        P3Tex *T = &p3tex[q->tex]; Img *f = &pic->full;
        int side = f->w < f->h ? f->w : f->h, ox = (f->w - side) / 2, oy = (f->h - side) / 2;
        for (int y = 0; y < T->h; y++) for (int x = 0; x < T->w; x++) T->px[y * T->w + x] = 0xff000000u | f->px[(oy + y * side / T->h) * f->w + ox + x * side / T->w];
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
    V3 cc = p3_cam(v3(w->pos.x, 3.0f * 1.8f + 0.6f, w->pos.z));
    float sx, sy;
    if (p3_project(cc, 400, 214, &sx, &sy) && sx > -30 && sx < 430 && sy > 0 && sy < 214) { tagVis[i] = 1; tagX[i] = sx; tagY[i] = sy; }
  }
  p3_draw_mesh(&p3buf, &chars, v3(0, 0, 0));
  /* 2x upscale with a light blur between pixels */
  for (int y = 0; y < 214; y++) {
    const uint32_t *src = p3buf.px + (y / 2) * W;
    uint32_t *dst = c->px + (TOP_Y0 + y) * 400;
    for (int x = 0; x < 400; x++) {
      int sx = x >> 1;
      if ((x & 1) && sx + 1 < W) { uint32_t a = src[sx], b = src[sx + 1]; dst[x] = 0xff000000u | (((a & 0xfefefe) >> 1) + ((b & 0xfefefe) >> 1)); }
      else dst[x] = src[sx];
    }
  }
  /* name tags */
  for (int i = 0; i < np; i++) {
    if (!tagVis[i]) continue;
    Person *q = &pp[i];
    int sel = s->sel == q->item;
    int tw = text_w(FB, 10, q->name) + 10, tx = (int)tagX[i] - tw / 2, ty = TOP_Y0 + (int)tagY[i] - 18;
    rrect(c, tx, ty, tw, 15, 4, sel ? C_GREEN : (q->tags & 1) ? 0xfff0a8 : C_WHITE);
    text(c, FB, 10, tx + 5, ty + 1, q->name, sel ? C_WHITE : 0x222222);
  }
  if (!np) text_c(c, FB, 14, 200, 120, s->loading ? "Loading the plaza..." : "Nobody is here yet.", C_SEC2);
}

/* ---- notifications, reports, profile editing, age check (like the website) */
static void notif_count_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)serial; (void)len; (void)err; (void)extra;
  if (code != 200 || !body) return;
  cJSON *j = cJSON_Parse(body); int n = (int)jnum(j, "unread"); cJSON_Delete(j);
  if (n != unread) { unread = n; dirty = 1; }
}
static void notif_read_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) { (void)serial; (void)code; (void)body; (void)len; (void)err; (void)extra; }
static void notifs_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)len; (void)extra;
  Screen *s = by_serial(serial); if (!s) return;
  s->loading = 0; clear_items(s, 0);
  if (code != 200 || !body) { api_error(code, body, err, s->err, sizeof s->err); add_empty(s, s->err); finish(s); return; }
  add_sec(s, "Notifications");
  cJSON *j = cJSON_Parse(body), *arr = j ? cJSON_GetObjectItem(j, "notifications") : NULL, *n;
  int cnt = 0;
  cJSON_ArrayForEach(n, arr) {
    Item *it = add_item(s, K_NOTIF);
    cJSON *a = cJSON_GetObjectItem(n, "actor");
    it->id = (int)jnum(n, "post_id"); it->created = jnum(n, "created_at"); it->mine = !jbool(n, "read");
    const char *disp = jstr(a, "display"), *un = jstr(a, "username");
    it->name = xstrdup(disp && *disp ? disp : un); it->user = xstrdup(un); it->avatar = xstrdup(jstr(a, "avatar"));
    it->title = xstrdup(jstr(n, "type"));
    const char *sn = jstr(n, "snippet");
    it->plain = sn && *sn ? strip_markup(sn, 120) : xstrdup(jbool(n, "has_drawing") ? "Your drawing" : "");
    measure(it); cnt++;
  }
  if (!cnt) add_empty(s, "No notifications yet. When someone gives your post a Yeah! or replies, it shows up here.");
  cJSON_Delete(j);
  finish(s);
  net_send("POST", "/api/notifications/read", "{}", notif_read_cb, 0, 0);
  unread = 0;
}
static void report_items(Screen *s) {
  add_sec(s, "Report this post");
  add_info(s, "Why should the admin look at it? Tap a reason.");
  static const char *R[7] = { "piracy", "modding", "nsfw", "swearing", "harassment", "spam", "other" };
  static const char *RN[7] = { "Piracy or ROMs", "Console modding help", "NSFW or gore", "Swearing", "Harassment or bullying", "Spam or scam", "Something else" };
  for (int i = 0; i < 7; i++) { Item *a = add_item(s, K_ACTION); a->act = A_REASON; a->title = xstrdup(RN[i]); a->msg = xstrdup(R[i]); measure(a); }
}
static const char *SHIRT_N[12] = { "Red", "Orange", "Yellow", "Green", "Blue", "Purple", "Pink", "Teal", "Brown", "Navy", "White", "Grey" };
static void edit_items(Screen *s) {
  add_sec(s, "Edit profile");
  Item *f = add_item(s, K_FIELD); f->act = 5; f->title = xstrdup("Display name"); f->body = xstrdup(s->cname); measure(f);
  Item *b = add_item(s, K_FIELD); b->act = 6; b->title = xstrdup("Bio"); b->body = xstrdup(s->text); measure(b);
  char t[64]; snprintf(t, sizeof t, "Shirt colour: %s (tap to change)", SHIRT_N[s->filter % 12]);
  Item *sh = add_item(s, K_ACTION); sh->act = A_SHIRT; sh->title = xstrdup(t); measure(sh);
  add_info(s, "Your picture (avatar) can be changed on the website.");
  add_action(s, A_SEND, s->loading ? "Saving..." : "Save");
}
static void age_items(Screen *s) {
  char t[160];
  snprintf(t, sizeof t, "%s is rated %d+", s->cname, s->filter); add_sec(s, t);
  static const char *INFO[3] = { "Mild language and mild themes. No NSFW.", "Stronger language and more mature topics. No NSFW.", "Adult conversation and strong language. No NSFW." };
  add_info(s, INFO[s->filter >= 18 ? 2 : s->filter >= 16 ? 1 : 0]);
  snprintf(t, sizeof t, "Yes, I am %d or older", s->filter); add_action(s, A_AGEYES, t);
  add_action(s, A_AGENO, "No, go back");
}
static void open_post_id(int id);
static void add_game(Screen *s, int mode, const char *title, const char *body) { Item *g = add_item(s, K_GAME); g->id = mode; g->title = xstrdup(title); g->body = xstrdup(body); measure(g); }
static void add_rule(Screen *s, int num, const char *title, const char *text, int instant) { Item *r = add_item(s, K_RULE); r->id = num; r->title = xstrdup(title); r->body = xstrdup(text); r->act = instant; measure(r); }
static void rules_items(Screen *s) {
  add_sec(s, s->mode ? "Welcome! Please read the rules" : "Aeroverse Rules");
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
  add_sec(s, "If you break the rules");
  add_info(s, "The admin can delete your posts, ban you for a while (1 hour up to 30 days) or ban you for good. Piracy and console modding mean an instant ban. The admin's decision is final.");
}
static void rebuild_form(Screen *s);
static void load_screen(Screen *s, int append) {
  char path[300];
  if (s->kind == S_LOGIN || s->kind == S_COMPOSE || s->kind == S_EDIT) { rebuild_form(s); return; }
  if (!append) { clear_items(s, 0); s->scroll = starts_low(s) ? (float)-page_topv(s) : 0; s->vel = 0; s->sel = -1; s->lastId = 0; s->page = 0; s->more = 0; s->serial = serialCounter++; }
  s->loading = 1; s->err[0] = 0;
  switch (s->kind) {
  case S_FEED:
    if (append) { snprintf(path, sizeof path, "/api/posts?limit=15&before=%d", s->lastId); net_send("GET", path, NULL, posts_cb, s->serial, 1); break; }
    { Item *h = add_item(s, K_HERO); measure(h); }
    if (updateBuild > 0) add_action(s, A_UPDATE, "New version - tap to update");
    { Item *p = add_item(s, K_PROMPT); p->act = me.id ? 1 : 0; measure(p); }
    net_send("GET", "/api/top", NULL, top_cb, s->serial, 0);
    break;
  case S_COMM:
    if (append) snprintf(path, sizeof path, "/api/posts?limit=15&community=%s&before=%d", s->cid, s->lastId);
    else {
      Item *h = add_item(s, K_CHEAD); h->name = xstrdup(s->cname); h->body = xstrdup(s->text); h->msg = xstrdup(s->text2); h->avatar = xstrdup(s->pass); { char ag[8]; snprintf(ag, sizeof ag, "%d", s->filter); h->age = xstrdup(ag); } measure(h);
      Item *p = add_item(s, K_PROMPT); p->act = me.id ? 2 : 0; measure(p);
      add_sec(s, "Posts");
      snprintf(path, sizeof path, "/api/posts?limit=15&community=%s", s->cid);
    }
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
    else {
      add_sec(s, s->mode ? "Ultra Wiggle Paint" : "Super Wiggle Paint");
      add_info(s, s->mode ? "Everything is 3 times faster. The buttons spin and change colour too. Sometimes your last move disappears, and sometimes a page pops up to bug you. Good luck!" : "Draw anything while the whole canvas shakes, flips upside down and changes colour. Say what it is and send it to the gallery.");
      add_action(s, A_DRAW, "Draw!");
      add_sec(s, "Gallery");
    }
    snprintf(path, sizeof path, "/api/wiggle?mode=%s&page=%d", s->mode ? "ultra" : "super", s->page);
    net_send("GET", path, NULL, wiggle_cb, s->serial, append);
    break;
  case S_GAMES:
    s->loading = 0;
    add_sec(s, "Games & Challenges");
    add_game(s, 0, "Super Wiggle Paint", "Draw anything while the whole canvas shakes, flips upside down and changes colour. Say what it is and send it to the gallery.");
    add_game(s, 1, "Ultra Wiggle Paint", "Everything is 3 times faster. The buttons spin and change colour too. Sometimes your last move disappears, and sometimes a page pops up to bug you. Good luck!");
    add_empty(s, "More challenges are on the way.");
    break;
  case S_RULES: s->loading = 0; rules_items(s); break;
  case S_NOTIF: net_send("GET", "/api/notifications", NULL, notifs_cb, s->serial, 0); break;
  case S_REPORT: s->loading = 0; report_items(s); break;
  case S_AGE: s->loading = 0; age_items(s); break;
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
  if (!s->n) s->scroll = (float)-page_topv(s);
  clear_items(s, 0);
  if (s->kind == S_EDIT) { edit_items(s); finish(s); if (keepSel >= 0 && keepSel < s->n && sel_ok(&s->it[keepSel])) s->sel = keepSel; return; }
  if (s->kind == S_LOGIN) {
    add_sec(s, s->mode ? "Create an account" : "Sign in to Aeroverse");
    add_info(s, "Use the same account as on the website and the Wii U. Tap a field to type with the keyboard.");
    field(s, 1, "Username", s->text2);
    if (s->mode) field(s, 3, "Display name (optional)", s->cname);
    char m[110]; int n = utf8_len(s->pass); if (n > 100) n = 100; memset(m, '*', (size_t)n); m[n] = 0;
    field(s, 2, s->mode ? "Password (8 or more characters)" : "Password", m);
    if (s->mode) { add_info(s, "By creating an account you accept the Aeroverse rules."); add_action(s, A_RULES, "Read the rules"); }
    add_action(s, A_SEND, s->loading ? "..." : s->mode ? "Create account" : "Sign in");
    add_action(s, A_MODE, s->mode ? "I already have an account" : "Create a new account");
  } else {
    add_sec(s, s->ckind == 1 ? "Write a reply" : "New post");
    if (s->ckind == 0 && s->cname[0]) { char t[140]; snprintf(t, sizeof t, "Posting in %s", s->cname); add_info(s, t); }
    if (s->ckind == 0 && !s->cid[0]) add_info(s, "Your post shows up on Home and on your profile.");
    field(s, 1, s->ckind == 1 ? "Your reply (tap to type)" : "Your post (tap to type)", s->text);
    field(s, 4, s->draw ? "Drawing (tap to change)" : "Add a drawing (optional)", s->draw ? (s->drawWig ? "Drawing with wiggle" : "Drawing") : "Tap to draw with the stylus");
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
  if (s->kind == S_EDIT) {
    toast("Profile saved!"); net_send("GET", "/api/me", NULL, me_cb, 0, 0);
    pop(); Screen *c = cur(); if (c) load_screen(c, 0);
    return;
  }
  if (s->kind == S_LOGIN) {   /* the Profile tab turns into the profile when /api/me answers (main loop) */
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
  if (s->kind == S_EDIT) {
    cJSON_AddStringToObject(o, "display", s->cname); cJSON_AddStringToObject(o, "bio", s->text); cJSON_AddNumberToObject(o, "shirt", s->filter % 12);
    char *b = cJSON_PrintUnformatted(o);
    net_send("POST", "/api/me", b, sent_cb, s->serial, 0);
    free(b);
  } else if (s->kind == S_LOGIN) {
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
  for (int i = 0; i < s->n; i++) { Item *it = &s->it[i]; if (it->id == id && (it->kind == K_POST || it->kind == K_REPLY || it->kind == K_WIG)) { it->yeahs = (int)jnum(j, "yeahs"); it->mine = jbool(j, "mine"); item_uncache(it); } }
  cJSON_Delete(j);
  dirty = 1;
}
static void do_yeah(Screen *s, Item *it) {
  if (!it || !(it->kind == K_POST || it->kind == K_REPLY || it->kind == K_WIG)) return;
  if (!me.id) { toast("Sign in to give a Yeah!"); return; }
  char path[64];
  if (it->kind == K_WIG) snprintf(path, sizeof path, "/api/wiggle/%d/yeah", it->id); else snprintf(path, sizeof path, "/api/posts/%d/yeah", it->id);
  it->mine = !it->mine; it->yeahs += it->mine ? 1 : -1; item_uncache(it);
  net_send("POST", path, "{}", yeah_cb, s->serial, it->id);
  dirty = 1;
}
static void sent_cb2_reply(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)len; (void)extra;
  if (code != 200) { char e[200]; api_error(code, body, err, e, sizeof e); toast(e); return; }
  toast("Reply deleted.");
  Screen *s = by_serial(serial); if (s) { char path[64]; snprintf(path, sizeof path, "/api/posts/%d/replies", s->postId); net_send("GET", path, NULL, replies_cb, s->serial, 0); }
}
static void signout_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) { (void)serial; (void)code; (void)body; (void)len; (void)err; (void)extra; }

/* ------------------------------------------------------------------ drawing editor (bottom screen), Super / Ultra Wiggle Paint chaos */
typedef struct {
  int on, challenge, ultra, started;
  uint8_t px[DPIX];
  uint8_t *undo[24]; int nundo;
  int color, size, tool, wig, strength;   /* tool 0 pen 1 eraser 2 fill */
  int lastX, lastY, drawing;
  Screen *target;
  /* chaos, like the website's Super Wiggle: wigLive waves, flips, hue / invert / sepia / tint, jelly, shaking */
  int fx, fy, inv, sep, tint; float hue, sat, sc, supPh, K; uint32_t chaosAt, lastTick;
  /* Ultra: the toolbar spins and changes colour, buttons swap places, annoyance page */
  int uiFlip, uiMirror; float uiHue; int perm[8]; int annoy, tabsOpened;
  char title[64];
} Editor;
static Editor ed;
#define ED_TOOLY 180
static void ed_snap(void) {
  if (ed.nundo == 24) { free(ed.undo[0]); memmove(ed.undo, ed.undo + 1, sizeof(ed.undo[0]) * 23); ed.nundo--; }
  ed.undo[ed.nundo] = malloc(DPIX); memcpy(ed.undo[ed.nundo], ed.px, DPIX); ed.nundo++;
}
static void ed_undo(void) { if (!ed.nundo) return; ed.nundo--; memcpy(ed.px, ed.undo[ed.nundo], DPIX); free(ed.undo[ed.nundo]); dirty = 1; }
static void ed_chaos_reset(void) { ed.fx = ed.fy = ed.inv = ed.sep = 0; ed.hue = 0; ed.sat = 1; ed.sc = 1; ed.tint = -1; ed.uiFlip = ed.uiMirror = 0; ed.uiHue = 0; for (int i = 0; i < 8; i++) ed.perm[i] = i; }
static void ed_open(Screen *target, int challenge, int ultra) {
  for (int i = 0; i < ed.nundo; i++) free(ed.undo[i]);
  memset(&ed, 0, sizeof ed);
  ed.on = 1; ed.target = target; ed.challenge = challenge; ed.ultra = ultra; ed.color = 5; ed.size = 2; ed.strength = 3; ed.K = ultra ? 3 : 1;
  ed_chaos_reset();
  if (target && target->draw) { memcpy(ed.px, target->draw, DPIX); ed.wig = target->drawWig ? 1 : 0; if (target->drawWig) ed.strength = target->drawWig; }
  dirty = 1;
}
static void ed_close(void) { for (int i = 0; i < ed.nundo; i++) free(ed.undo[i]); ed.nundo = 0; ed.on = 0; dirty = 1; swallow = 1; screen_uncache(cur()); }
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
/* the website's CSS filters: hue-rotate, invert, saturate, sepia, tint */
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
  if (tint >= 0) {
    float h = tint / 60.0f, C = (1 - fabsf(2 * 0.58f - 1)) * 0.95f, X = C * (1 - fabsf(fmodf(h, 2) - 1)), m = 0.58f - C / 2, tr, tg, tb;
    int i = (int)h % 6;
    if (i == 0) { tr = C; tg = X; tb = 0; } else if (i == 1) { tr = X; tg = C; tb = 0; } else if (i == 2) { tr = 0; tg = C; tb = X; } else if (i == 3) { tr = 0; tg = X; tb = C; } else if (i == 4) { tr = X; tg = 0; tb = C; } else { tr = C; tg = 0; tb = X; }
    r *= tr + m; g *= tg + m; b *= tb + m;
  }
  if (r < 0) r = 0;
  if (g < 0) g = 0;
  if (b < 0) b = 0;
  if (r > 1) r = 1;
  if (g > 1) g = 1;
  if (b > 1) b = 1;
  return ((uint32_t)(r * 255) << 16) | ((uint32_t)(g * 255) << 8) | (uint32_t)(b * 255);
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
    int r = rand() % 10;
    ed.uiFlip = r < 2;            /* toolbar upside down */
    ed.uiMirror = r >= 2 && r < 5; /* toolbar mirrored */
    ed.uiHue = (float)(rand() % 360);
    for (int i = 0; i < 8; i++) ed.perm[i] = i;
    if (rand() % 3 == 0) for (int i = 7; i > 0; i--) { int j = rand() % (i + 1), t = ed.perm[i]; ed.perm[i] = ed.perm[j]; ed.perm[j] = t; }
    if (!ed.drawing && ed.nundo && (rand() % 1000) < 40) { ed_undo(); toast("Oops! Your last move vanished."); }
  }
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
    if (!plat_keyboard("What did you draw?", ed.title, t, 48, 0, 0)) { swallow = 1; return; }
    swallow = 1;
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
static const char *ED_TOOLS[8] = { "Pen", "Eraser", "Fill", "Size", "Undo", "Wiggle", "Cancel", "Done" };
static void ed_tick(void) {
  if (!(ed.challenge && ed.started)) return;
  if (nowMs - ed.lastTick >= 50) { ed.supPh += 0.55f * ed.K; ed.lastTick = nowMs; }
  if (nowMs > ed.chaosAt) { ed_chaos_pick(); ed.chaosAt = nowMs + (uint32_t)((450 + rand() % 600) / ed.K); }
}
/* canvas transform: jelly (superjelly keyframes) + scale + flips + shake; screen -> canvas */
static float edJa, edJsx = 1, edJsy = 1; static int edShx, edShy;
static void ed_xform_update(void) {
  edJa = 0; edJsx = edJsy = 1; edShx = edShy = 0;
  if (!(ed.challenge && ed.started)) return;
  float t = fmodf(nowMs / (ed.ultra ? 300.0f : 900.0f), 1.0f);
  static const float KX[5] = { 1, 1.08f, 0.93f, 1.04f, 1 }, KY[5] = { 1, 0.92f, 1.07f, 0.97f, 1 };
  int seg = (int)(t * 4); float f = t * 4 - seg;
  edJsx = (KX[seg] + (KX[seg + 1] - KX[seg]) * f) * ed.sc; edJsy = (KY[seg] + (KY[seg + 1] - KY[seg]) * f) * ed.sc;
  int k = ed.ultra ? 6 : 3;
  edShx = (int)(sinf(nowMs / (ed.ultra ? 50.0f : 130.0f)) * k); edShy = (int)(cosf(nowMs / (ed.ultra ? 65.0f : 160.0f)) * k);
}
static int ed_to_canvas(int x, int y, int *cx, int *cy) {
  float fx = (x - 160 - edShx) / edJsx + 160, fy = (y - 90 - edShy) / edJsy + 90;
  int ix = (int)fx, iy = (int)fy;
  if (ed.fx) ix = DW - 1 - ix;
  if (ed.fy) iy = DH - 1 - iy;
  *cx = ix; *cy = iy;
  return ix >= 0 && iy >= 0 && ix < DW && iy < DH;
}
/* toolbar layout with Ultra's swaps: button slot -> tool */
static void ed_tool_press(int b) {
  switch (b) {
  case 0: ed.tool = 0; break;
  case 1: ed.tool = 1; break;
  case 2: ed.tool = 2; break;
  case 3: ed.size = ed.size == 1 ? 2 : ed.size == 2 ? 4 : ed.size == 4 ? 7 : 1; break;
  case 4: ed_undo(); break;
  case 5: if (ed.challenge) toast("Wiggle is always on in Wiggle Paint!"); else if (!ed.wig) ed.wig = 1; else if (ed.strength < 5) ed.strength++; else { ed.wig = 0; ed.strength = 3; } break;
  case 6: ed_close(); break;
  case 7: ed_done(); break;
  }
}
static void ed_touch(Input *in) {
  static int wasTouch;
  if (!in->touch) { if (wasTouch) ed.drawing = 0; wasTouch = 0; return; }
  int fresh = !wasTouch; wasTouch = 1;
  int tx = in->tx, ty = in->ty;
  if (ed.annoy) {   /* the annoyance page covers the bottom screen */
    if (fresh && tx >= 60 && tx < 260 && ty >= 170 && ty < 204) { ed.annoy = 0; dirty = 1; }
    return;
  }
  if (ty < ED_TOOLY) {
    int cx, cy; ed_to_canvas(tx, ty, &cx, &cy);
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx >= DW) cx = DW - 1;
    if (cy >= DH) cy = DH - 1;
    if (ed.challenge && !ed.started) { ed.started = 1; ed.chaosAt = nowMs + 250; }
    if (ed.ultra && ed.challenge && fresh && ed.tabsOpened < 2 && (rand() % 100) < 4) { ed.tabsOpened++; ed.annoy = 1; dirty = 1; return; }
    if (ed.tool == 2) { if (fresh) { ed_snap(); ed_flood(cx, cy); } }
    else if (fresh) { ed_snap(); ed_dot(cx, cy); ed.drawing = 1; }
    else if (ed.drawing) ed_line(ed.lastX, ed.lastY, cx, cy);
    ed.lastX = cx; ed.lastY = cy;
    dirty = 1;
    return;
  }
  if (!fresh) return;
  /* Ultra: the toolbar may be upside down or mirrored */
  int lx = tx, ly = ty - ED_TOOLY;
  if (ed.uiFlip) { lx = 319 - lx; ly = 59 - ly; }
  else if (ed.uiMirror) lx = 319 - lx;
  if (ly < 30) {
    int row = (ly - 2) / 14, col = lx / 20;
    if (row < 0) row = 0;
    if (row > 1) row = 1;
    if (col > 15) col = 15;
    ed.color = row * 16 + col; if (ed.tool == 1) ed.tool = 0;
  } else {
    int slot = lx / 40; if (slot > 7) slot = 7;
    ed_tool_press(ed.perm[slot]);
  }
  dirty = 1;
}
static void ed_draw_bottom(void) {
  Cv *c = &BOT;
  ed_xform_update();
  int chaos = ed.challenge && ed.started;
  uint32_t pal[32];
  for (int i = 0; i < 32; i++) pal[i] = chaos ? css_filter(PAL[i], ed.hue, ed.inv, ed.sat, ed.sep, ed.tint) : PAL[i];
  float amp = 0, phX = 0, phY = 0;
  if (chaos) { amp = (1.2f + ed.strength * 0.7f) * (ed.ultra ? 1.4f : 1); phX = ed.supPh; phY = ed.supPh * 1.3f; }   /* wigLive() */
  else if (ed.wig && !ed.drawing) { int fr = (int)((nowMs / 140) % 4); amp = ed.strength * 1.1f; phX = fr * 1.7f; phY = fr * 2.2f; }
  fill(c, 0, 0, 320, ED_TOOLY, chaos ? 0x8494a0 : 0x9aa9b2);
  /* inverse-map every screen pixel into the canvas (scale/jelly, flips, shake, waves) */
  static int colX[320], rowY[ED_TOOLY], eox[DH], eoy[DW];
  for (int y = 0; y < DH; y++) eox[y] = amp ? (int)lroundf(amp * sinf(y / 6.0f + phX)) : 0;
  for (int x = 0; x < DW; x++) eoy[x] = amp ? (int)lroundf(amp * sinf(x / 7.0f + phY)) : 0;
  for (int x = 0; x < 320; x++) { int v = (int)floorf((x - 160 - edShx) / edJsx + 160); if (ed.fx && v >= 0 && v < DW) v = DW - 1 - v; colX[x] = v; }
  for (int y = 0; y < ED_TOOLY; y++) { int v = (int)floorf((y - 90 - edShy) / edJsy + 90); if (ed.fy && v >= 0 && v < DH) v = DH - 1 - v; rowY[y] = v; }
  for (int y = 0; y < ED_TOOLY; y++) {
    int sy = rowY[y]; if (sy < 0 || sy >= DH) continue;
    uint32_t *row = &c->px[y * 320];
    for (int x = 0; x < 320; x++) {
      int sx = colX[x]; if (sx < 0 || sx >= DW) continue;
      int qx = sx + eox[sy], qy = sy + eoy[sx];
      if (qx < 0) qx = 0;
      if (qx >= DW) qx = DW - 1;
      if (qy < 0) qy = 0;
      if (qy >= DH) qy = DH - 1;
      row[x] = 0xff000000u | pal[ed.px[qy * DW + qx] & 31];
    }
  }
  /* toolbar, drawn into its own canvas so Ultra can turn it around and recolour it */
  static uint32_t tbuf[320 * 60]; Cv tb; tb.px = tbuf; tb.w = 320; tb.h = 60; cv_noclip(&tb);
  vgrad(&tb, 0, 0, 320, 60, 0xf4fafd, 0xd6e8f1);
  fill(&tb, 0, 0, 320, 1, C_LINE);
  for (int i = 0; i < 32; i++) {
    int x = (i % 16) * 20, y = 2 + (i / 16) * 14;
    fill(&tb, x + 1, y, 18, 13, PAL[i]);
    if (i == ed.color && ed.tool != 1) { fill(&tb, x, y - 1, 20, 2, C_INK); fill(&tb, x, y + 12, 20, 2, C_INK); fill(&tb, x, y, 2, 13, C_INK); fill(&tb, x + 18, y, 2, 13, C_INK); }
  }
  for (int slot = 0; slot < 8; slot++) {
    int b = ed.perm[slot];
    int x = slot * 40 + 2, y = 31;
    int on = (b == ed.tool && b < 3) || (b == 5 && (ed.wig || ed.challenge));
    uint32_t bg = b == 7 ? C_GREEN : on ? C_BLUE : 0xffffff;
    rrect_border(&tb, x, y, 36, 27, 7, b == 7 ? C_GREEN_D : on ? 0x0b5f99 : C_LINE, bg);
    char lab[16]; snprintf(lab, sizeof lab, "%s", ED_TOOLS[b]);
    if (b == 3) snprintf(lab, sizeof lab, "Size %d", ed.size);
    if (b == 5) { if (ed.challenge) snprintf(lab, sizeof lab, "Wiggle"); else if (ed.wig) snprintf(lab, sizeof lab, "Wig %d", ed.strength); else snprintf(lab, sizeof lab, "Wig off"); }
    uint32_t fg = (b == 7 || on) ? C_WHITE : C_INK;
    text_c(&tb, FB, 10, x + 18, y + 7, lab, fg);
  }
  int ultraUI = ed.ultra && ed.started;
  for (int y = 0; y < 60; y++) for (int x = 0; x < 320; x++) {
    int sx = x, sy = y;
    if (ultraUI && ed.uiFlip) { sx = 319 - x; sy = 59 - y; } else if (ultraUI && ed.uiMirror) sx = 319 - x;
    uint32_t p = tbuf[sy * 320 + sx];
    if (ultraUI) p = css_filter(p, ed.uiHue, 0, 1.6f, 0, -1);
    c->px[(ED_TOOLY + y) * 320 + x] = 0xff000000u | p;
  }
  if (ed.annoy) {
    fill(c, 0, 0, 320, 240, 0xd0101828);
    rrect_border(c, 14, 20, 292, 196, 12, C_RED, 0xfff6f0);
    text_c(c, FB, 16, 160, 34, "Ultra Wiggle Paint", C_RED);
    text_wrap(c, FM, 14, 30, 62, 260, "This is the Ultra Wiggle Paint annoyance. Please close this page and go back to the previous one. Your drawing is waiting for you there.", C_INK, 18, 0);
    rrect(c, 60, 170, 200, 34, 17, C_GREEN_D); rrect_g(c, 61, 171, 198, 32, 16, 0x7ee03a, 0x3f9a00);
    text_c(c, FB, 14, 160, 178, "Close this page", C_WHITE);
  }
}
static void ed_draw_top(void) {
  Cv *c = &TOP;
  memcpy(c->px, bgTop.px, (size_t)400 * 240 * 4);
  vgrad(c, 0, 0, 400, 25, 0xffffff, 0xe3f3fb); fill(c, 0, 25, 400, 1, 0x8fd4f2);
  img_draw(c, &logoSm, 6, 3, 100, 20, 0, 255);
  const char *t = ed.challenge ? (ed.ultra ? "Ultra Wiggle Paint" : "Super Wiggle Paint") : "Drawing";
  text(c, FB, 14, 394 - text_w(FB, 14, t), 4, t, ed.ultra ? 0x7a3ce0 : C_SEC2);
  int frame = (int)((nowMs / 140) % 4);
  shadow(c, 38, 32, 324, 184, 6);
  rrect(c, 38, 32, 324, 184, 6, C_WHITE);
  draw_drawing(c, ed.px, PAL, ed.challenge ? 3 : ed.strength, (ed.challenge || ed.wig) ? frame : -1, 40, 34, 320, 180);
  const char *help = ed.challenge ? (ed.started ? "Keep drawing! X: done   L: undo   B: cancel" : "Touch the bottom screen to start! The chaos begins...") : "Draw with the stylus.  X: done   L: undo   B: cancel";
  text_c(c, FB, 11, 200, 222, help, ed.ultra ? 0x7a3ce0 : C_SEC2);
  if (ed.ultra && ed.started) { int w = text_w(FB, 13, "ULTRA!") + 16; rrect(c, 354 - w / 2, 40, w, 20, 10, 0x7a3ce0); text_c(c, FB, 13, 354, 42, "ULTRA!", C_WHITE); }
}

/* ------------------------------------------------------------------ drawing the page */
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
static void card(Cv *c, int x, int y, int w, int h) { shadow(c, x, y, w, h, 10); rrect(c, x, y, w, h, 10, C_WHITE); }
static void pill(Cv *c, int x, int y, int w, int h, const char *t, int style) {   /* 0 green, 1 white, 2 yeah, 3 yeah on, 4 blue, 5 purple */
  uint32_t top, bot, border, fg;
  switch (style) {
  case 0: top = 0x7ee03a; bot = 0x3f9a00; border = 0x2f7a00; fg = C_WHITE; break;
  case 2: top = 0xfffbea; bot = 0xffefb8; border = 0xf0cf73; fg = C_YEAH_T; break;
  case 3: top = 0xffe27a; bot = 0xffc928; border = 0xd9a400; fg = 0x4a3200; break;
  case 4: top = 0x55b8f0; bot = 0x1b8fd3; border = 0x0b5f99; fg = C_WHITE; break;
  case 5: top = 0xb07cf0; bot = 0x7a3ce0; border = 0x5a24b0; fg = C_WHITE; break;
  default: top = 0xffffff; bot = 0xe6f1f6; border = 0xb7cbd6; fg = C_SEC2; break;
  }
  rrect(c, x, y, w, h, h / 2, border);
  rrect_g(c, x + 1, y + 1, w - 2, h - 2, h / 2 - 1, top, bot);
  fill(c, x + h / 2, y + 2, w - h, 1, 0x70ffffff);
  int fs = h >= 28 ? 14 : h >= 22 ? 12 : 11;
  text_c(c, FB, fs, x + w / 2, y + (h - fs) / 2 - 1, t, fg);
}
static void yeah_btn(Cv *c, int x, int y, int n, int on) {
  char b[32]; snprintf(b, sizeof b, "Yeah! %d", n);
  int w = text_w(FB, 12, b) + 20;
  pill(c, x, y, w, 22, b, on ? 3 : 2);
}
/* static part of an item (cached); dynamic parts (wiggling drawings) are drawn every frame on top */
static void draw_item(Cv *c, Screen *s, Item *it, int X, int Y) {
  char buf[220];
  it->drw = 0;
  switch (it->kind) {
  case K_POST: case K_REPLY: {
    card(c, X, Y, it->w, it->h);
    int av = it->kind == K_REPLY ? 30 : 34;
    draw_avatar(c, it->name, it->user, it->avatar, X + PAD, Y + PAD, av);
    text_fit(c, FB, 15, X + PAD + av + 8, Y + PAD - 2, it->name, C_INK, it->w - av - 2 * PAD - 8);
    char ago[40]; fmt_ago(it->created, ago, sizeof ago);
    const char *pf = platform_name(it->platform);
    if (it->cname && *it->cname && s->kind != S_COMM) snprintf(buf, sizeof buf, "%s - %s%s%s", it->cname, ago, pf ? " - " : "", pf ? pf : "");
    else snprintf(buf, sizeof buf, "%s%s%s", ago, pf ? " - from " : "", pf ? pf : "");
    text_fit(c, FM, 12, X + PAD + av + 8, Y + PAD + 17, buf, C_SOFT, it->w - av - 2 * PAD - 8);
    int y = Y + PAD + 34 + 6;
    int th = post_text_h(it);
    if (th) {
      PStyle ps = post_style(it->style); int fs = post_font_px(ps, it->full), lh = fs + fs / 3, pad = styled_box(ps) ? 8 : 0;
      if (pad) style_box(c, ps, X + PAD - 2, y - 2, it->w - 2 * PAD + 4, th + 2);
      text_wrap(c, ps.size == 2 ? FB : FM, fs, X + PAD + pad, y + pad, it->w - 2 * PAD - 2 * pad, it->plain, ps.bg ? PC_FG[ps.bg] : C_INK, lh, post_lines_max(it));
      y += th + 6;
    }
    for (int i = 0; i < it->npics; i++) {
      int pw = it->w - 2 * PAD, ph = PIC_H;
      fill(c, X + PAD, y, pw, ph, 0xeef4f7);
      Pic *p = pic_get2(it->pics[i], 1);
      if (p && p->full.px) {
        float k = fminf((float)pw / p->full.w, (float)ph / p->full.h); int dw = (int)(p->full.w * k), dh = (int)(p->full.h * k);
        img_draw(c, &p->full, X + PAD + (pw - dw) / 2, y + (ph - dh) / 2, dw, dh, 0, 255);
      } else text_c(c, FM, 13, X + it->w / 2, y + ph / 2 - 8, p == NULL && pic_find(it->pics[i]) && pic_find(it->pics[i])->state == 3 ? "[image unavailable]" : "Loading picture...", C_SOFT);
      y += ph + 6;
    }
    if (it->dpx) {
      int dw = it->w - 2 * PAD, dh = dw * 9 / 16;
      fill(c, X + PAD - 1, y - 1, dw + 2, dh + 2, C_LINE);
      draw_drawing(c, it->dpx, it->dpal, it->wig, -1, X + PAD, y, dw, dh);
      if (it->wig > 0) { it->drx = PAD; it->dry = y - Y; it->drw = dw; it->drh = dh; }
      y += dh + 6;
    }
    int fy = Y + it->h - PAD - 22;
    yeah_btn(c, X + PAD, fy, it->yeahs, it->mine);
    if (it->kind == K_POST) { snprintf(buf, sizeof buf, it->replies == 1 ? "1 reply" : "%d replies", it->replies); text(c, FB, 13, X + PAD + 104, fy + 3, buf, C_SEC2); }
    break; }
  case K_COMM: {
    card(c, X, Y, it->w, it->h);
    Pic *p = it->avatar && *it->avatar ? pic_get(it->avatar) : NULL;
    if (p) pic_draw_sq(c, p, X + 8, Y + 8, 48);
    else { fill(c, X + 8, Y + 8, 48, 48, C_GREEN); char l[2] = { it->name && *it->name ? it->name[0] : '?', 0 }; text_c(c, FB, 28, X + 32, Y + 16, l, C_WHITE); }
    text_fit(c, FB, 16, X + 64, Y + 6, it->name, C_INK, it->w - 72);
    text_fit(c, FM, 13, X + 64, Y + 26, it->body, C_SOFT, it->w - 72);
    snprintf(buf, sizeof buf, "%d posts", it->posts); text(c, FB, 11, X + 64, Y + 43, buf, C_GREEN_D);
    { int ag = it->age ? atoi(it->age) : 0; if (ag) { char al[8]; snprintf(al, sizeof al, "%d+", ag); uint32_t ac = ag >= 18 ? C_RED : ag >= 16 ? C_ORANGE : C_BLUE; rrect(c, X + it->w - 40, Y + 42, 32, 15, 3, ac); text_c(c, FB, 10, X + it->w - 24, Y + 43, al, C_WHITE); } }
    break; }
  case K_CHEAD: {
    card(c, X, Y, it->w, it->h);
    Pic *p = it->avatar && *it->avatar ? pic_get(it->avatar) : NULL;
    if (p) pic_draw_sq(c, p, X + 12, Y + 12, 56);
    else { fill(c, X + 12, Y + 12, 56, 56, C_GREEN); char l[2] = { it->name && *it->name ? it->name[0] : '?', 0 }; text_c(c, FB, 32, X + 40, Y + 22, l, C_WHITE); }
    text_fit(c, FB, 18, X + 78, Y + 12, it->name, C_INK, it->w - 90);
    { int ag = it->age ? atoi(it->age) : 0; const char *al = ag >= 18 ? "18+" : ag >= 16 ? "16+" : ag >= 14 ? "14+" : "All ages"; uint32_t ac = ag >= 18 ? C_RED : ag >= 16 ? C_ORANGE : ag >= 14 ? C_BLUE : C_GREEN;
      rrect(c, X + 78, Y + 38, 60, 18, 3, ac); text_c(c, FB, 11, X + 108, Y + 40, al, C_WHITE); }
    int y = Y + 76;
    y += text_wrap(c, FM, 13, X + 12, y, it->w - 24, it->body, C_SOFT, 17, 3);
    if (it->msg && *it->msg) { fill(c, X, y + 6, it->w, 1, 0xe3ecf0); text(c, FB, 10, X + 12, y + 12, "MESSAGE FROM THE CREATOR", C_SOFT); text_wrap(c, FM, 14, X + 12, y + 28, it->w - 24, it->msg, C_INK, 18, 4); }
    break; }
  case K_SEC: {
    static const char *SEC_ICON[] = { "" };
    (void)SEC_ICON;
    text(c, FB, 17, X + 3, Y + 3, it->title, 0xffffff);
    text(c, FB, 17, X + 2, Y + 2, it->title, C_SEC2);
    break; }
  case K_INFO: text_wrap(c, FM, 13, X + 4, Y + 2, it->w - 8, it->body, C_SEC2, 18, 0); break;
  case K_EMPTY: card(c, X, Y, it->w, it->h); text_wrap(c, FM, 14, X + 14, Y + 17, it->w - 28, it->body, C_SOFT, 19, 0); break;
  case K_ACTION: pill(c, X + 8, Y + 2, it->w - 16, 30, it->title, it->act == A_SIGNOUT || it->act == A_MODE || it->act == A_RULES || it->act == A_WIGTOG || it->act == A_SHIRT || it->act == A_AGENO || it->act == A_PASSWORD ? 1 : it->act == A_UPDATE ? 4 : (s->kind == S_GALLERY && s->mode) ? 5 : 0); break;
  case K_MORE: text_c(c, FB, 12, X + it->w / 2, Y + 7, "Loading more...", C_SEC2); break;
  case K_PERSON: {
    card(c, X, Y, it->w, it->h);
    draw_avatar(c, it->name, it->user, it->avatar, X + 8, Y + 7, 36);
    text_fit(c, FB, 15, X + 52, Y + 6, it->name, C_INK, 140);
    snprintf(buf, sizeof buf, "@%s", it->user ? it->user : ""); text_fit(c, FM, 12, X + 52, Y + 25, buf, C_SOFT, 140);
    int bx = X + it->w - 8;
    static const char *TN[3] = { "Popular", "New", "Active" }; static const uint32_t TC[3] = { C_ORANGE, C_BLUE, C_GREEN };
    for (int k = 2; k >= 0; k--) if (it->tags & (1 << k)) { int w = text_w(FB, 10, TN[k]) + 12; bx -= w; rrect(c, bx, Y + 16, w, 18, 9, TC[k]); text(c, FB, 10, bx + 6, Y + 18, TN[k], C_WHITE); bx -= 4; }
    break; }
  case K_GAME: {
    card(c, X, Y, it->w, it->h);
    if (it->id) rrect_g(c, X + 8, Y + 8, 80, 80, 12, 0xff6a3d, 0x9b3fd6); else rrect_g(c, X + 8, Y + 8, 80, 80, 12, 0x86e3ff, 0x2ab76a);
    text_c(c, FB, 15, X + 48, Y + 28, it->id ? "ULTRA" : "Super", C_WHITE);
    text_c(c, FB, 15, X + 48, Y + 46, "Wiggle", C_WHITE);
    text(c, FB, 15, X + 96, Y + 8, it->title, C_INK);
    text_wrap(c, FM, 12, X + 96, Y + 28, it->w - 104, it->body, C_SOFT, 15, 4);
    break; }
  case K_WIG: case K_DCELL: {
    card(c, X, Y, it->w, it->h);
    if (it->dpx) {
      draw_drawing(c, it->dpx, it->dpal, it->kind == K_WIG ? 3 : it->wig, -1, X + 1, Y + 1, 98, 55);
      if (it->kind == K_WIG || it->wig > 0) { it->drx = 1; it->dry = 1; it->drw = 98; it->drh = 55; }
    }
    text_fit(c, FB, 11, X + 5, Y + 59, it->kind == K_WIG ? it->title : it->name, C_INK, 90);
    snprintf(buf, sizeof buf, "Yeah! %d", it->yeahs); text(c, FB, 10, X + 5, Y + 75, buf, it->mine ? 0xd9a400 : C_YEAH_T);
    break; }
  case K_FIELD: {
    text(c, FB, 13, X + 4, Y - 1, it->title, C_SEC2);
    rrect_border(c, X, Y + 17, it->w, 34, 8, C_LINE, C_WHITE);
    const char *v = it->body && *it->body ? it->body : "";
    if (*v) text_fit(c, FM, 15, X + 10, Y + 24, v, C_INK, it->w - 20); else text(c, FM, 15, X + 10, Y + 24, "(tap to type)", 0xa8b4ba);
    break; }
  case K_RULE: {
    card(c, X, Y, it->w, it->h);
    rrect(c, X + 8, Y + 10, 22, 20, 4, C_SEC2);
    snprintf(buf, sizeof buf, "%d", it->id); text_c(c, FB, 12, X + 19, Y + 12, buf, C_WHITE);
    text_fit(c, FB, 14, X + 36, Y + 9, it->title, C_INK, it->act ? it->w - 130 : it->w - 44);
    if (it->act) { int w = text_w(FB, 10, "Instant ban") + 10; rrect(c, X + it->w - w - 8, Y + 10, w, 16, 4, C_RED); text(c, FB, 10, X + it->w - w - 3, Y + 12, "Instant ban", C_WHITE); }
    text_wrap(c, FM, 13, X + 36, Y + 30, it->w - 44, it->body, C_SOFT, 18, 0);
    break; }
  case K_PROFILE: {
    card(c, X, Y, it->w, it->h);
    draw_avatar(c, it->name, it->user, it->avatar, X + PAD, Y + PAD, 64);
    text_fit(c, FB, 18, X + 82, Y + 10, it->name, C_INK, it->w - 90);
    snprintf(buf, sizeof buf, "@%s", it->user ? it->user : ""); text_fit(c, FM, 13, X + 82, Y + 34, buf, C_SOFT, it->w - 90);
    snprintf(buf, sizeof buf, "%d posts", it->posts); text(c, FB, 12, X + 82, Y + 52, buf, C_GREEN_D);
    if (it->plain && *it->plain) text_wrap(c, FM, 14, X + PAD, Y + PAD + 64 + 6, it->w - 2 * PAD, it->plain, C_INK, 18, 5);
    break; }
  case K_HERO: {
    shadow(c, X, Y, it->w, it->h, 12);
    rrect_g(c, X, Y, it->w, it->h, 12, 0xffffff, 0xdff2fc);
    img_draw(c, &logoMid, X + it->w / 2 - 90, Y + 12, 180, 36, 0, 255);
    text_c(c, FB, 15, X + it->w / 2, Y + 56, "Hello from around the world!", C_SEC2);
    static const char *CITY[5] = { "USA", "Brazil", "UK", "Germany", "Japan" };
    long utc = utc_now();
    for (int i = 0; i < 5; i++) {
      int cx = X + 32 + i * 62;
      extern long tz_offset(int zone, long utc);
      long t = utc + tz_offset(i, utc);
      char hm[16]; long sec = ((t % 86400) + 86400) % 86400; snprintf(hm, sizeof hm, "%02ld:%02ld", sec / 3600, (sec / 60) % 60);
      text_c(c, FB, 10, cx, Y + 84, CITY[i], C_SOFT);
      fill(c, cx - 22, Y + 99, 44, 2, C_GREEN);
      text_c(c, FB, 16, cx, Y + 103, hm, C_INK);
    }
    text_c(c, FM, 12, X + it->w / 2, Y + 128, me.id > 0 ? "Welcome back! Scroll down for the latest posts." : "Sign in on the Profile tab to post and draw.", C_SEC2);
    break; }
  case K_NOTIF: {
    card(c, X, Y, it->w, it->h);
    if (it->mine) circle(c, X + it->w - 12, Y + 12, 5, C_RED);
    draw_avatar(c, it->name, it->user, it->avatar, X + PAD, Y + PAD, 40);
    snprintf(buf, sizeof buf, "%s %s", it->name ? it->name : "", it->title && !strcmp(it->title, "reply") ? "replied to your post" : "gave your post a Yeah!");
    text_fit(c, FB, 13, X + 56, Y + 7, buf, C_INK, it->w - 76);
    char ago[40]; fmt_ago(it->created, ago, sizeof ago);
    if (it->plain && *it->plain) snprintf(buf, sizeof buf, "\"%s\" - %s", it->plain, ago); else snprintf(buf, sizeof buf, "%s", ago);
    text_fit(c, FM, 12, X + 56, Y + 28, buf, C_SOFT, it->w - 64);
    break; }
  case K_PROMPT: {
    card(c, X, Y, it->w, it->h);
    if (!it->act) {
      text_fit(c, FM, 14, X + 12, Y + 18, "Sign in to post and draw.", C_SOFT, 180);
      pill(c, X + it->w - 104, Y + 13, 92, 30, "Sign in", 0);
    } else {
      draw_avatar(c, me.display, me.username, me.avatar, X + PAD, Y + PAD, 36);
      rrect_border(c, X + 52, Y + 10, it->w - 60, 34, 8, 0xbdbdbd, C_WHITE);
      text_fit(c, FM, 14, X + 62, Y + 18, it->act == 2 ? "Share your thoughts in this community..." : "Write something on your page...", 0x8a9399, it->w - 80);
      pill(c, X + it->w - 196, Y + 54, 88, 30, "Draw", 1);
      pill(c, X + it->w - 100, Y + 54, 88, 30, "Write", 0);
    }
    break; }
  }
}
/* render once into a cache image (with the page colour behind), then blit */
static void item_render_cache(Screen *s, Item *it) {
  int w = it->w + 8, h = it->h + 8;
  Cv tmp; cv_init(&tmp, w, h);
  for (int i = 0; i < w * h; i++) tmp.px[i] = 0xff000000u | C_PAGE;
  draw_item(&tmp, s, it, 4, 4);
  it->cache.px = tmp.px; it->cache.w = w; it->cache.h = h;
}
static void blit_opaque(Cv *c, const Img *m, int dx, int dy) {
  int x0 = dx < c->cx0 ? c->cx0 : dx, y0 = dy < c->cy0 ? c->cy0 : dy, x1 = dx + m->w > c->cx1 ? c->cx1 : dx + m->w, y1 = dy + m->h > c->cy1 ? c->cy1 : dy + m->h;
  if (x0 >= x1) return;
  for (int y = y0; y < y1; y++) memcpy(&c->px[y * c->w + x0], &m->px[(y - dy) * m->w + (x0 - dx)], (size_t)(x1 - x0) * 4);
}
static void focus_ring(Cv *c, int x, int y, int w, int h) {
  uint32_t col = 0x1b8fd3;
  fill(c, x - 3, y - 3, w + 6, 3, col); fill(c, x - 3, y + h, w + 6, 3, col);
  fill(c, x - 3, y, 3, h, col); fill(c, x + w, y, 3, h, col);
}
static void draw_item_at(Cv *c, Screen *s, Item *it, int X, int Y, int focus, int frame) {
  if (!it->cache.px) item_render_cache(s, it);
  blit_opaque(c, &it->cache, X - 4, Y - 4);
  if (it->drw && frame >= 0) draw_drawing(c, it->dpx, it->dpal, it->kind == K_WIG ? 3 : it->wig, frame, X + it->drx, Y + it->dry, it->drw, it->drh);
  if (focus) focus_ring(c, X, Y, it->w, it->h);
}
/* above the start of the page (when it begins on the bottom screen) the top screen shows a banner for the screen */
static void top_banner(Screen *s, int off) {
  Cv *c = &TOP;
  int y0 = TOP_Y0 + off;   /* off: 0 when the page starts exactly at the bottom screen, negative when it moves up */
  cv_clip(c, 0, TOP_Y0, 400, TOPV);
  fill(c, 0, TOP_Y0, 400, TOPV, 0);
  memcpy(c->px + TOP_Y0 * 400, bgTop.px + TOP_Y0 * 400, (size_t)400 * TOPV * 4);
  shadow(c, 16, y0 + 10, 368, 194, 14);
  rrect_g(c, 16, y0 + 10, 368, 194, 14, 0xffffff, 0xdff2fc);
  const char *t = "", *d = "";
  switch (s->kind) {
  case S_COMMS: t = "Communities"; d = "Pick a community on the bottom screen to read its posts and write in it."; break;
  case S_GAMES: t = "Games & Challenges"; d = "Super and Ultra Wiggle Paint: draw on the bottom screen while everything shakes, flips and changes colour."; break;
  case S_LOGIN: t = s->mode ? "Create your account" : "Sign in to Aeroverse"; d = "Use the same account as on the website and the Wii U. Tap a field on the bottom screen to type."; break;
  case S_RULES: t = s->mode ? "Welcome to Aeroverse!" : "Aeroverse Rules"; d = s->mode ? "Please read the rules on the bottom screen and accept them to continue." : "Everyone who uses Aeroverse follows these rules."; break;
  case S_GALLERY: t = s->mode ? "Ultra Wiggle Paint" : "Super Wiggle Paint"; d = "Tap Draw! to play. Drawings from everybody are in the gallery below."; break;
  case S_COMPOSE: t = s->ckind == 1 ? "Write a reply" : "New post"; d = ""; break;
  case S_NOTIF: t = "Notifications"; d = "Yeah!s and replies to your posts. Tap one to open the post."; break;
  case S_REPORT: t = "Report"; d = "Tell the admin what is wrong. Reports are private."; break;
  case S_EDIT: t = "Edit profile"; d = "Change your name, bio and the colour of your shirt on the Plaza."; break;
  case S_AGE: t = "Age check"; d = "This community is for older players. Please confirm your age."; break;
  default: t = ""; d = ""; break;
  }
  if (s->kind == S_COMPOSE) {
    text(c, FB, 16, 32, y0 + 22, t, C_SEC2);
    int y = y0 + 48;
    if (s->text[0]) y += text_wrap(c, FM, 15, 32, y, 336, s->text, C_INK, 19, s->draw ? 2 : 7) + 6; else { text(c, FM, 15, 32, y, "(nothing written yet)", 0xa8b4ba); y += 24; }
    if (s->draw) { int dh = y0 + 196 - y, dw = dh * 16 / 9; if (dh > 20) draw_drawing(c, s->draw, PAL, s->drawWig, s->drawWig ? (int)((nowMs / 140) % 4) : -1, 200 - dw / 2, y, dw, dh); }
  } else if (s->kind == S_GAMES || s->kind == S_GALLERY) {
    rrect_g(c, 34, y0 + 34, 110, 110, 16, s->kind == S_GALLERY && s->mode ? 0xff6a3d : 0x86e3ff, s->kind == S_GALLERY && s->mode ? 0x9b3fd6 : 0x2ab76a);
    text_c(c, FB, 20, 89, y0 + 64, s->kind == S_GALLERY && s->mode ? "ULTRA" : "Super", C_WHITE); text_c(c, FB, 20, 89, y0 + 90, "Wiggle", C_WHITE);
    text_fit(c, FB, 18, 158, y0 + 36, t, C_INK, 214);
    text_wrap(c, FM, 14, 158, y0 + 64, 212, d, C_SOFT, 18, 7);
  } else {
    img_draw(c, &logoMid, 110, y0 + 26, 180, 36, 0, 255);
    text_c(c, FB, 18, 200, y0 + 78, t, C_SEC2);
    text_wrap(c, FM, 14, 40, y0 + 108, 320, d, C_INK, 18, 4);
  }
  text_c(c, FB, 11, 200, y0 + 182, "Scroll up to read more on this screen", 0x8aa0ad);
  cv_noclip(c);
}
/* the page: upper part on the top screen, lower part on the bottom screen */
static void draw_page(Screen *s) {
  int topv = page_topv(s), ph = page_h(s);
  int maxS = s->contentH - ph, low = starts_low(s) ? -topv : 0;
  if (maxS < low) maxS = low;
  if (s->scroll > maxS) s->scroll = (float)maxS;
  if (s->scroll < -topv) s->scroll = (float)-topv;
  int frame = (int)((nowMs / 140) % 4);
  if (topv) {
    fill(&TOP, TOP_X0 - 6, TOP_Y0, COLW + 12, TOPV, C_PAGE);
    if (s->scroll < 0) top_banner(s, (int)(-s->scroll) - topv);
    cv_clip(&TOP, TOP_X0 - 6, TOP_Y0, COLW + 12, TOPV);
  }
  fill(&BOT, 0, 0, 320, BAR_Y, C_PAGE);
  cv_clip(&BOT, 0, 0, 320, BAR_Y);
  for (int i = 0; i < s->n; i++) {
    Item *it = &s->it[i];
    int vy = it->y - (int)s->scroll;
    if (vy + it->h + 4 < 0 || vy - 4 > ph) continue;
    if (topv && vy < topv) draw_item_at(&TOP, s, it, TOP_X0 + it->x, TOP_Y0 + vy, i == s->sel, frame);
    if (vy + it->h > topv) draw_item_at(&BOT, s, it, BOT_X0 + it->x, vy - topv, i == s->sel, frame);
  }
  if (s->loading && s->n <= 3) text_c(&BOT, FB, 14, 160, 70, "Loading...", C_SEC2);
  /* scroll bar on the bottom screen */
  if (s->contentH > ph) { int bh = BAR_Y * ph / s->contentH; if (bh < 16) bh = 16; int by = (int)(s->scroll * (BAR_Y - bh) / (s->contentH - ph)); rrect(&BOT, 315, by, 4, bh, 2, 0x801b8fd3); }
  cv_noclip(&TOP); cv_noclip(&BOT);
}

/* ---- action bar (above the tabs) */
typedef struct { const char *t; int id; int style; } Btn;
enum { B_BACK = 1, B_WRITE, B_REFRESH, B_YEAH, B_REPLY, B_DRAW, B_FILTER, B_SEND, B_OPEN, B_ACCEPT, B_ALERTS };
static int bar_buttons(Screen *s, Btn *b) {
  int n = 0;
  if (depth > 1 && !(s->kind == S_RULES && s->mode)) b[n++] = (Btn){ "Back", B_BACK, 1 };
  Item *sel = s->sel >= 0 && s->sel < s->n ? &s->it[s->sel] : NULL;
  switch (s->kind) {
  case S_FEED: b[n++] = (Btn){ "Write", B_WRITE, 0 }; if (me.id) { static char nb[24]; snprintf(nb, sizeof nb, unread ? "Alerts (%d)" : "Alerts", unread); b[n++] = (Btn){ nb, B_ALERTS, unread ? 4 : 1 }; } b[n++] = (Btn){ "Refresh", B_REFRESH, 1 }; break;
  case S_NOTIF: b[n++] = (Btn){ "Refresh", B_REFRESH, 1 }; break;
  case S_COMMS: b[n++] = (Btn){ "Refresh", B_REFRESH, 1 }; break;
  case S_PLAZA: b[n++] = (Btn){ "Filter", B_FILTER, 1 }; b[n++] = (Btn){ "Refresh", B_REFRESH, 1 }; break;
  case S_COMM: b[n++] = (Btn){ "Write here", B_WRITE, 0 }; break;
  case S_POST: b[n++] = (Btn){ "Yeah!", B_YEAH, s->n && s->it[0].kind == K_POST && s->it[0].mine ? 3 : 2 }; b[n++] = (Btn){ "Reply", B_REPLY, 0 }; break;
  case S_GALLERY: b[n++] = (Btn){ "Draw!", B_DRAW, s->mode ? 5 : 0 }; if (sel && sel->kind == K_WIG) b[n++] = (Btn){ "Yeah!", B_YEAH, sel->mine ? 3 : 2 }; break;
  case S_USER: if (me.id && !strcmp(s->cid, me.username)) b[n++] = (Btn){ "Write", B_WRITE, 0 }; else b[n++] = (Btn){ "Refresh", B_REFRESH, 1 }; break;
  case S_COMPOSE: case S_EDIT: b[n++] = (Btn){ s->kind == S_EDIT ? "Save" : "Send", B_SEND, 0 }; break;
  case S_RULES: if (s->mode) b[n++] = (Btn){ "I accept the rules", B_ACCEPT, 0 }; break;
  default: break;
  }
  if (sel && (sel->kind == K_POST || sel->kind == K_DCELL) && s->kind != S_POST && n < 4) b[n++] = (Btn){ "Open", B_OPEN, 4 };
  return n;
}
static const char *TAB_N[5] = { "Home", "Communities", "Plaza", "Games", "Profile" };
static void draw_bars(Screen *s) {
  Cv *c = &BOT;
  vgrad(c, 0, BAR_Y, 320, TAB_Y - BAR_Y, 0xf7fcff, 0xe2f0f7);
  fill(c, 0, BAR_Y, 320, 1, C_LINE);
  if (s) {
    Btn b[5]; int n = bar_buttons(s, b);
    int w = n ? (312 - (n - 1) * 6) / n : 0;
    for (int i = 0; i < n; i++) pill(c, 4 + i * (w + 6), BAR_Y + 3, w, 22, b[i].t, b[i].style);
  }
  vgrad(c, 0, TAB_Y, 320, 240 - TAB_Y, 0xffffff, 0xeaf5fb);
  fill(c, 0, TAB_Y, 320, 1, 0x8fd4f2);
  for (int i = 0; i < 5; i++) {
    int x = i * 64, on = curTab == i;
    uint32_t col = on ? C_GREEN : 0x6b6b6b;
    img_draw(c, &icons20[i], x + 32 - 10, TAB_Y + 2, 20, 20, col, 255);
    const char *nm = (i == 4 && !me.id) ? "Sign in" : TAB_N[i];
    text_c(c, FB, 10, x + 32, TAB_Y + 21, nm, col);
    if (on) fill(c, x + 8, 237, 48, 3, C_GREEN);
  }
}

/* ---- top screen frame */
long tz_offset(int zone, long utc);
static int nth_sunday(int y, int m, int n) {
  long first = days_from_civil(y, m, 1); int wd = (int)((first + 4) % 7); if (wd < 0) wd += 7;
  int d1 = 1 + (7 - wd) % 7;
  if (n > 0) return d1 + (n - 1) * 7;
  int dim = (int)(days_from_civil(m == 12 ? y + 1 : y, m == 12 ? 1 : m + 1, 1) - first);
  while (d1 + 7 <= dim) d1 += 7;
  return d1;
}
long tz_offset(int zone, long utc) {
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
static void draw_header(Screen *s) {
  Cv *c = &TOP;
  vgrad(c, 0, 0, 400, 25, 0xffffff, 0xe3f3fb); fill(c, 0, 25, 400, 1, 0x8fd4f2);
  img_draw(c, &logoSm, 6, 3, 100, 20, 0, 255);
  static const char *SN[15] = { "Home", "Communities", "Community", "Post", "Plaza", "Games", "Gallery", "Profile", "Sign in", "Write", "Rules", "Notifications", "Report", "Edit profile", "Age check" };
  char title[120]; snprintf(title, sizeof title, "%s", s ? SN[s->kind] : "");
  if (s && (s->kind == S_COMM || s->kind == S_GALLERY) && s->cname[0]) snprintf(title, sizeof title, "%s", s->cname);
  if (s && s->kind == S_USER && s->n && s->it[0].kind == K_PROFILE && s->it[0].name) snprintf(title, sizeof title, "%s", s->it[0].name);
  int w = text_w(FB, 14, title); if (w > 230) w = 230;
  text_fit(c, FB, 14, 394 - w, 4, title, C_SEC2, 230);
  if (net_pending()) { int k = (int)(nowMs / 120) % 6; for (int i = 0; i < 6; i++) fill(c, 394 - w - 48 + i * 7, 10, 4, 4, i == k ? C_GREEN : 0xc8dfe8); }
  if (unread > 0) { char nb[8]; snprintf(nb, sizeof nb, "%d", unread > 99 ? 99 : unread); int bw = text_w(FB, 11, nb) + 10; rrect(c, 112, 5, bw, 16, 8, C_RED); text(c, FB, 11, 117, 6, nb, C_WHITE); }
  if (toastMsg[0] && nowMs < toastUntil) {
    int tw = text_w(FB, 12, toastMsg) + 28; if (tw > 390) tw = 390;
    rrect(c, 200 - tw / 2, 206, tw, 28, 14, 0xe62d5d78);
    text_fit(c, FB, 12, 200 - tw / 2 + 14, 212, toastMsg, C_WHITE, tw - 28);
  }
}
static void draw_all(void) {
  Screen *s = cur();
  memcpy(TOP.px, bgTop.px, (size_t)400 * 240 * 4);
  memcpy(BOT.px, bgBot.px, (size_t)320 * 240 * 4);
  if (s) {
    if (s->kind == S_PLAZA) plaza_render(s, &TOP);
    draw_page(s);
  }
  draw_bars(s);
  draw_header(s);
}

/* ------------------------------------------------------------------ actions */
static void pw_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)serial; (void)len; (void)extra;
  if (code != 200) { char e[200]; api_error(code, body, err, e, sizeof e); toast(e); return; }
  toast("Password changed!");
}
static void report_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)serial; (void)len; (void)extra;
  if (code != 200) { char e[200]; api_error(code, body, err, e, sizeof e); toast(e); return; }
  toast("Thanks! The admin will take a look.");
}
static void deleted_cb(int serial, long code, const char *body, size_t len, const char *err, int extra) {
  (void)serial; (void)len; (void)extra;
  if (code != 200) { char e[200]; api_error(code, body, err, e, sizeof e); toast(e); return; }
  toast("Deleted.");
  Screen *c = cur(); if (c && c->kind == S_POST) pop();
  c = cur(); if (c) load_screen(c, 0);
}
static void open_post_id(int id) {
  Screen *p = new_screen(S_POST); p->postId = id; push(p);
  Item *f = add_item(p, K_POST); f->full = 1; f->id = id; f->plain = xstrdup("Loading..."); measure(f);
  finish(p); p->sel = 0;
  char path[64];
  snprintf(path, sizeof path, "/api/posts/%d", id); net_send("GET", path, NULL, post_cb, p->serial, 0);
  snprintf(path, sizeof path, "/api/posts/%d/replies", id); p->loading = 1; net_send("GET", path, NULL, replies_cb, p->serial, 0);
}
static void go_tab(int t) {
  if (t < 0) t = 4;
  if (t > 4) t = 0;
  while (depth > 0) { Screen *s = stack[--depth]; if (!is_root(s)) free_screen(s); }
  curTab = t;
  if (t == 4 && (!roots[4] || (me.id && roots[4]->kind == S_LOGIN) || (!me.id && roots[4]->kind == S_USER))) { if (roots[4]) free_screen(roots[4]); roots[4] = make_profile_root(); }
  push(roots[t]);
  if (!roots[t]->n) load_screen(roots[t], 0);
  dirty = 1;
}
static void open_post(Screen *s, Item *it) {
  (void)s;
  Screen *p = new_screen(S_POST); p->postId = it->id; push(p);
  Item *f = copy_post(p, it, K_POST); f->full = 1; measure(f);
  snprintf(p->cname, sizeof p->cname, "%s", it->name ? it->name : "");
  finish(p); p->sel = 0;
  char path[64];
  snprintf(path, sizeof path, "/api/posts/%d", it->id); net_send("GET", path, NULL, post_cb, p->serial, 0);
  snprintf(path, sizeof path, "/api/posts/%d/replies", it->id); p->loading = 1; net_send("GET", path, NULL, replies_cb, p->serial, 0);
}
static void open_item(Screen *s, Item *it, int tx, int ty) {
  switch (it->kind) {
  case K_POST: if (s->kind != S_POST) open_post(s, it); break;
  case K_DCELL: open_post(s, it); break;
  case K_REPLY:
    if (me.id && it->user && !strcmp(it->user, me.username)) {
      static int armId; static uint32_t armT;
      if (armId == it->id && nowMs - armT < 4000) { char path[64]; snprintf(path, sizeof path, "/api/replies/%d", it->id); net_send("DELETE", path, NULL, sent_cb2_reply, s->serial, it->id); armId = 0; }
      else { armId = it->id; armT = nowMs; toast("Tap your reply again to delete it."); }
    } else if (me.id) { Screen *r = new_screen(S_REPORT); r->ckind = 1; r->postId = it->id; push(r); load_screen(r, 0); }
    break;
  case K_NOTIF: if (it->id) open_post_id(it->id); break;
  case K_COMM: { Screen *c = new_screen(S_COMM); snprintf(c->cid, sizeof c->cid, "%s", it->cid ? it->cid : ""); snprintf(c->cname, sizeof c->cname, "%s", it->name ? it->name : "");
    snprintf(c->text, sizeof c->text, "%s", it->body ? it->body : ""); snprintf(c->text2, sizeof c->text2, "%s", it->msg ? it->msg : ""); snprintf(c->pass, sizeof c->pass, "%s", it->avatar ? it->avatar : "");
    { Item *h0 = NULL; (void)h0; }
    c->filter = it->age ? atoi(it->age) : 0;
    if (c->filter > ageOk) { Screen *g = new_screen(S_AGE); g->filter = c->filter; snprintf(g->cname, sizeof g->cname, "%s", c->cname); free_screen(c); g->postId = 0;
      snprintf(g->cid, sizeof g->cid, "%s", it->cid ? it->cid : ""); snprintf(g->text, sizeof g->text, "%s", it->body ? it->body : ""); snprintf(g->text2, sizeof g->text2, "%s", it->msg ? it->msg : ""); snprintf(g->pass, sizeof g->pass, "%s", it->avatar ? it->avatar : "");
      push(g); load_screen(g, 0); break; }
    push(c); load_screen(c, 0); break; }
  case K_PERSON: { Screen *u = new_screen(S_USER); snprintf(u->cid, sizeof u->cid, "%s", it->user ? it->user : ""); push(u); load_screen(u, 0); break; }
  case K_GAME: { Screen *g = new_screen(S_GALLERY); g->mode = it->id; snprintf(g->cname, sizeof g->cname, "%s", it->title); push(g); load_screen(g, 0); break; }
  case K_WIG: do_yeah(s, it); break;
  case K_PROMPT:
    if (!it->act) go_tab(4);
    else if (tx >= 0 && ty >= 0 && ty >= it->y + 54 && tx < BOT_X0 + it->w - 104) { open_compose(0, s->kind == S_COMM ? s->cid : NULL, s->kind == S_COMM ? s->cname : NULL, 0); Screen *c = cur(); if (c && c->kind == S_COMPOSE) ed_open(c, 0, 0); }
    else open_compose(0, s->kind == S_COMM ? s->cid : NULL, s->kind == S_COMM ? s->cname : NULL, 0);
    break;
  case K_FIELD: {
    char tmp[1024];
    if (it->act == 1 && s->kind == S_LOGIN) { if (plat_keyboard("Username", s->text2, tmp, 32, 0, 0)) snprintf(s->text2, sizeof s->text2, "%s", tmp); }
    else if (it->act == 2) { if (plat_keyboard("Password", "", tmp, 100, 1, 0)) snprintf(s->pass, sizeof s->pass, "%s", tmp); }
    else if (it->act == 3) { if (plat_keyboard("Display name", s->cname, tmp, 40, 0, 0)) snprintf(s->cname, sizeof s->cname, "%s", tmp); }
    else if (it->act == 1) { if (plat_keyboard(s->ckind == 1 ? "Write a reply" : "Write a post", s->text, tmp, 1000, 0, 1)) snprintf(s->text, sizeof s->text, "%s", tmp); }
    else if (it->act == 4) { ed_open(s, 0, 0); return; }
    else if (it->act == 5) { if (plat_keyboard("Display name", s->cname, tmp, 24, 0, 0)) snprintf(s->cname, sizeof s->cname, "%s", tmp); }
    else if (it->act == 6) { if (plat_keyboard("Bio", s->text, tmp, 200, 0, 1)) snprintf(s->text, sizeof s->text, "%s", tmp); }
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
    case A_NOTIFS: { Screen *n = new_screen(S_NOTIF); push(n); load_screen(n, 0); break; }
    case A_EDIT: {
      Screen *e = new_screen(S_EDIT);
      Item *pr = s->n && s->it[0].kind == K_PROFILE ? &s->it[0] : NULL;
      snprintf(e->cname, sizeof e->cname, "%s", me.display); snprintf(e->text, sizeof e->text, "%s", pr && pr->body ? pr->body : ""); e->filter = pr ? ((pr->icon % 12) + 12) % 12 : 0;
      push(e); rebuild_form(e); break; }
    case A_SHIRT: s->filter = (s->filter + 1) % 12; rebuild_form(s); break;
    case A_PASSWORD: {
      char cur_[128], nx[128];
      if (!plat_keyboard("Your current password", "", cur_, 100, 1, 0)) { swallow = 1; break; }
      if (!plat_keyboard("New password (8 or more characters)", "", nx, 100, 1, 0)) { swallow = 1; break; }
      swallow = 1;
      cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "current", cur_); cJSON_AddStringToObject(o, "next", nx);
      char *b = cJSON_PrintUnformatted(o); net_send("POST", "/api/me/password", b, pw_cb, 0, 0); free(b); cJSON_Delete(o);
      memset(cur_, 0, sizeof cur_); memset(nx, 0, sizeof nx);
      toast("Changing the password..."); break; }
    case A_DELETE: {
      static uint32_t armT; static int armId;
      int pid = s->postId;
      if (armId == pid && nowMs - armT < 4000) { char path[64]; snprintf(path, sizeof path, "/api/posts/%d", pid); net_send("DELETE", path, NULL, deleted_cb, 0, 0); armId = 0; }
      else { armId = pid; armT = nowMs; toast("Tap Delete again to delete this post."); }
      break; }
    case A_REPORT: { Screen *r = new_screen(S_REPORT); r->ckind = 0; r->postId = s->postId; push(r); load_screen(r, 0); break; }
    case A_REASON: {
      cJSON *o = cJSON_CreateObject(); cJSON_AddStringToObject(o, "type", s->ckind == 1 ? "reply" : s->ckind == 2 ? "wiggle" : "post"); cJSON_AddNumberToObject(o, "id", s->postId); cJSON_AddStringToObject(o, "reason", it->msg ? it->msg : "other");
      char *b = cJSON_PrintUnformatted(o); net_send("POST", "/api/reports", b, report_cb, 0, 0); free(b); cJSON_Delete(o);
      pop(); break; }
    case A_AGEYES: {
      ageOk = s->filter > ageOk ? s->filter : ageOk; save_settings();
      Screen *c = new_screen(S_COMM); c->filter = s->filter;
      snprintf(c->cid, sizeof c->cid, "%s", s->cid); snprintf(c->cname, sizeof c->cname, "%s", s->cname); snprintf(c->text, sizeof c->text, "%s", s->text); snprintf(c->text2, sizeof c->text2, "%s", s->text2); snprintf(c->pass, sizeof c->pass, "%s", s->pass);
      pop(); push(c); load_screen(c, 0); break; }
    case A_AGENO: pop(); break;
    case A_DRAW:
      if (s->kind == S_GALLERY) { if (!me.id) toast("Sign in first (Profile tab)."); else ed_open(NULL, 1, s->mode); }
      else if (s->kind == S_POST) open_compose(1, NULL, s->cname, s->postId);
      break;
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
  case B_YEAH: if (sel && sel->kind != K_POST && s->kind == S_POST) do_yeah(s, sel); else if (s->kind == S_POST && s->n) do_yeah(s, &s->it[0]); else if (sel) do_yeah(s, sel); break;
  case B_REPLY: open_compose(1, NULL, s->cname, s->postId); break;
  case B_DRAW: if (!me.id) toast("Sign in first (Profile tab)."); else ed_open(NULL, 1, s->mode); break;
  case B_FILTER: s->filter = (s->filter + 1) % 4; load_screen(s, 0); break;
  case B_SEND: compose_send(s); break;
  case B_OPEN: if (sel) open_item(s, sel, -1, -1); break;
  case B_ACCEPT: rulesOk = 1; save_settings(); pop(); toast("Thanks! Have fun on Aeroverse."); break;
  case B_ALERTS: { Screen *nn = new_screen(S_NOTIF); push(nn); load_screen(nn, 0); break; }
  }
  dirty = 1;
}
static void ensure_visible(Screen *s) {
  if (s->sel < 0 || s->sel >= s->n) return;
  Item *it = &s->it[s->sel];
  int ph = page_h(s);
  if (it->y - s->scroll < 4) s->scroll = (float)(it->y - 4);
  if (s->scroll < -page_topv(s)) s->scroll = (float)-page_topv(s);
  else if (it->y + it->h - s->scroll > ph - 4) s->scroll = (float)(it->y + it->h - ph + 4);
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
    uint32_t col = lerpc(0x9fd8f4, 0xe6f6fd, y / 239.0f);
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
  logoSm = img_scale(&logoImg, 100, 20);
  logoMid = img_scale(&logoImg, 180, 36);
  for (int i = 0; i < 5; i++) { Img ic = img_from_rgba(ICONS_RGBA[i], ICON_SZ, ICON_SZ); icons20[i] = img_scale(&ic, 20, 20); img_free(&ic); }
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
  int touchDown = 0, tsx = 0, tsy = 0, tly = 0, moved = 0, lastMeId = 0, plazaFrame = 0;
  uint32_t holdT = 0;
  while (running && plat_running()) {
    nowMs = plat_ms(); frameNo++;
    plat_input(&in);
    net_poll(); pic_poll();
    if (picChanged) { picChanged = 0; screen_uncache(cur()); dirty = 1; }
    if (me.id != lastMeId) {   /* signed in / out: refresh Home and the Profile tab, but never close the rules page */
      lastMeId = me.id; dirty = 1;
      if (roots[0]) { if (cur() == roots[0]) load_screen(roots[0], 0); else { clear_items(roots[0], 0); roots[0]->serial = serialCounter++; roots[0]->loading = 0; } }
      if (curTab == 4 && cur() == roots[4]) go_tab(4);
    }
    if (in.down & KEY_START_) break;
    { static uint32_t nextPoll; if (me.id > 0 && nowMs > nextPoll) { nextPoll = nowMs + 60000; net_send("GET", "/api/notifications/count", NULL, notif_count_cb, 0, 0); } }
    if (swallow) { if (in.touch) in.touch = 0; else swallow = 0; }
    if (ed.on) {
      ed_tick();
      ed_touch(&in);
      if (ed.annoy) { if (in.down & (KEY_A_ | KEY_B_)) ed.annoy = 0; }
      else {
        if (in.down & (KEY_L_ | KEY_Y_)) ed_undo();
        if (in.down & KEY_X_) ed_done();
        if (in.down & KEY_B_) ed_close();
      }
      if (ed.on) { ed_draw_top(); ed_draw_bottom(); plat_present(&TOP, &BOT); continue; }
    }
    Screen *s = cur();
    if (s && !s->n && !s->loading && s->kind != S_LOGIN && s->kind != S_COMPOSE) load_screen(s, 0);
    if (s) {
      if (in.down & KEY_L_) go_tab(curTab - 1);
      else if (in.down & KEY_R_) go_tab(curTab + 1);
      else if (in.down & KEY_B_) { if (!(s->kind == S_RULES && s->mode)) pop(); }
      else if (in.down & KEY_DOWN_) { move_sel(s, 1); holdT = nowMs + 350; }
      else if (in.down & KEY_UP_) { move_sel(s, -1); holdT = nowMs + 350; }
      else if ((in.held & KEY_DOWN_) && nowMs > holdT) { move_sel(s, 1); holdT = nowMs + 90; }
      else if ((in.held & KEY_UP_) && nowMs > holdT) { move_sel(s, -1); holdT = nowMs + 90; }
      else if (in.down & KEY_A_) { if (s->sel >= 0) open_item(s, &s->it[s->sel], -1, -1); }
      else if (in.down & KEY_Y_) { if (s->kind == S_POST && s->n) do_yeah(s, s->sel > 0 ? &s->it[s->sel] : &s->it[0]); else if (s->sel >= 0) do_yeah(s, &s->it[s->sel]); }
      else if (in.down & KEY_X_) { Btn b[5]; int n = bar_buttons(s, b); for (int i = 0; i < n; i++) if (b[i].id != B_BACK) { bar_press(s, b[i].id); break; } }
      s = cur();
      if (s && in.touch) {
        if (!touchDown) { touchDown = 1; tsx = in.tx; tsy = in.ty; tly = in.ty; moved = 0; s->vel = 0; }
        else {
          int dy = in.ty - tly;
          if (abs(in.ty - tsy) > 6 || abs(in.tx - tsx) > 6) moved = 1;
          if (moved && tsy < BAR_Y && dy) { s->scroll -= dy; s->vel = (float)-dy; dirty = 1; }
          tly = in.ty;
        }
      } else if (s && touchDown) {
        touchDown = 0;
        if (!moved) {
          int tx = tsx, ty = tsy;
          if (ty >= TAB_Y) go_tab(tx / 64);
          else if (ty >= BAR_Y) {
            Btn b[5]; int n = bar_buttons(s, b);
            int w = n ? (312 - (n - 1) * 6) / n : 0;
            for (int i = 0; i < n; i++) if (tx >= 4 + i * (w + 6) && tx < 4 + i * (w + 6) + w) bar_press(s, b[i].id);
          } else {
            int cy = ty + (int)s->scroll + page_topv(s), cx = tx - BOT_X0;
            for (int i = 0; i < s->n; i++) {
              Item *it = &s->it[i];
              if (cx >= it->x && cx < it->x + it->w && cy >= it->y && cy < it->y + it->h) {
                if (!sel_ok(it)) break;
                int again = s->sel == i;
                s->sel = i; dirty = 1;
                if ((it->kind == K_POST || it->kind == K_REPLY) && cy >= it->y + it->h - PAD - 24 && cx < it->x + PAD + 96) { do_yeah(s, it); break; }
                if (it->kind == K_POST || it->kind == K_WIG || it->kind == K_DCELL) { if (again || it->kind == K_POST) { if (it->kind != K_WIG || again) open_item(s, it, cx, cy); } }
                else if (it->kind != K_REPLY) open_item(s, it, cx, cy);
                break;
              }
            }
          }
        }
      } else if (s && !in.touch) {
        if (fabsf(s->vel) > 0.3f) { s->scroll += s->vel; s->vel *= 0.92f; dirty = 1; } else s->vel = 0;
      }
      s = cur();
      if (s && s->kind == S_PLAZA) {
        if (fabsf(in.cy) > 0.15f) { s->polar -= in.cy * 0.02f; s->autoRot = 0; }
        if (in.held & KEY_LEFT_) { s->az -= 0.03f; s->autoRot = 0; }
        if (in.held & KEY_RIGHT_) { s->az += 0.03f; s->autoRot = 0; }
      } else if (s && fabsf(in.cy) > 0.15f) { s->scroll -= in.cy * 7; dirty = 1; }
      if (s && s->more && !s->loading && s->scroll + page_h(s) > s->contentH - 60) load_screen(s, 1);
    }
    s = cur();
    int anim = 0;
    if (s) for (int i = 0; i < s->n && !anim; i++) { Item *it = &s->it[i]; int vy = it->y - (int)s->scroll; if (it->drw && vy + it->h > 0 && vy < page_h(s)) anim = 1; }
    if (s && s->kind == S_PLAZA) { if (++plazaFrame % 2 == 0) dirty = 1; }   /* 3D island: every 2nd frame */
    else if (anim && frameNo % 8 == 0) dirty = 1;                               /* wiggling drawings */
    if (frameNo % 60 == 0) dirty = 1;                                           /* clocks, toasts */
    if (toastMsg[0] && nowMs > toastUntil && nowMs < toastUntil + 100) dirty = 1;
#ifndef __3DS__
    if (hsShot[0]) dirty = 1;
#endif
    if (dirty) { draw_all(); dirty = 0; plat_present(&TOP, &BOT); }
    else plat_vsync();
  }
  netQuit = 1;
  threads_join();
  curl_global_cleanup();
  plat_exit();
  return 0;
}
