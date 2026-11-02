/*
 * net.h - background network thread (libcurl + mbedTLS, same as the Wii U app) and the picture loader.
 */
#include <curl/curl.h>

typedef void (*NetCb)(int serial, long code, const char *body, size_t len, const char *err, int extra);
typedef struct { char method[8]; char url[420]; char *body; NetCb cb; int serial, extra; int file; } NetReq;
typedef struct { NetCb cb; int serial, extra; long code; char *body; size_t len; char err[160]; } NetRes;
#define NQ 16
static NetReq reqQ[NQ]; static int reqHead, reqTail;
static NetRes resQ[NQ]; static int resHead, resTail;
static Mx netMx;
static volatile int netQuit, netBusy;
static char sessionTok[96];
static long serverDelta; static int serverKnown;

typedef struct { char *p; size_t n; } Buf;
static size_t wr_cb(void *d, size_t sz, size_t nm, void *u) {
  Buf *b = u; size_t n = sz * nm;
  if (b->n + n > 12 * 1024 * 1024) return 0;
  char *np = realloc(b->p, b->n + n + 1); if (!np) return 0;
  b->p = np; memcpy(b->p + b->n, d, n); b->n += n; b->p[b->n] = 0;
  return n;
}
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
static size_t hdr_cb(char *d, size_t sz, size_t nm, void *u) {
  size_t n = sz * nm; (void)u;
  if (n > 10 && !strncasecmp(d, "date:", 5)) { char tmp[80]; size_t l = n < 79 ? n : 79; memcpy(tmp, d, l); tmp[l] = 0; parse_date_header(tmp); }
  if (n > 16 && !strncasecmp(d, "set-cookie:", 11)) {
    const char *p = d + 11; while (*p == ' ') p++;
    if (!strncmp(p, "sid=", 4)) {
      p += 4; char tok[96]; int k = 0;
      while (k < 95 && p < d + n && *p != ';' && *p != '\r' && *p != '\n') tok[k++] = *p++;
      tok[k] = 0;
      mx_lock(&netMx); snprintf(sessionTok, sizeof sessionTok, "%s", tok); mx_unlock(&netMx);
    }
  }
  return n;
}
static int prog_cb(void *u, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) { (void)u; (void)a; (void)b; (void)c; (void)d; return netQuit ? 1 : 0; }
static void curl_common(CURL *c, const char *url, Buf *b, char *err) {
  struct curl_blob blob; blob.data = (void *)cacert_pem; blob.len = cacert_pem_size; blob.flags = CURL_BLOB_COPY;
  curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &blob);
  curl_easy_setopt(c, CURLOPT_URL, url);
  curl_easy_setopt(c, CURLOPT_USERAGENT, "Aeroverse3DS/" APP_VERSION);
  curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
  curl_easy_setopt(c, CURLOPT_TIMEOUT, strstr(url, ".3dsx") ? 600L : 45L);
  if (err) curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wr_cb);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, b);
  curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, prog_cb);
  curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
}
static void net_thread(void *u) {
  (void)u;
  CURL *c = curl_easy_init();
  while (!netQuit) {
    mx_lock(&netMx);
    if (reqHead == reqTail) { mx_unlock(&netMx); thread_sleep_ms(8); continue; }
    NetReq rq = reqQ[reqTail]; reqTail = (reqTail + 1) % NQ;
    char tok[96]; snprintf(tok, sizeof tok, "%s", sessionTok);
    netBusy = 1;
    mx_unlock(&netMx);
    Buf b = { 0 };
    char err[CURL_ERROR_SIZE]; err[0] = 0;
    long code = 0; CURLcode rc = CURLE_FAILED_INIT;
    if (c) {
      curl_easy_reset(c);
      curl_common(c, rq.url, &b, err);
      curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, hdr_cb);
      curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
      struct curl_slist *hl = NULL; char ck[128];
      if (tok[0] && !strncmp(rq.url, API, strlen(API))) { snprintf(ck, sizeof ck, "Cookie: sid=%s", tok); hl = curl_slist_append(hl, ck); }
      hl = curl_slist_append(hl, "X-Requested-With: aeroverse");
      if (!rq.file) hl = curl_slist_append(hl, "Accept: application/json");
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
    mx_lock(&netMx);
    NetRes *r = &resQ[resHead];
    r->cb = rq.cb; r->serial = rq.serial; r->extra = rq.extra; r->code = code; r->body = b.p; r->len = b.n;
    snprintf(r->err, sizeof r->err, "%s", rc == CURLE_OK ? "" : (err[0] ? err : curl_easy_strerror(rc)));
    resHead = (resHead + 1) % NQ;
    netBusy = 0;
    mx_unlock(&netMx);
  }
  if (c) curl_easy_cleanup(c);
}
static char *xstrdup(const char *s) { if (!s) return NULL; size_t n = strlen(s); char *d = malloc(n + 1); memcpy(d, s, n + 1); return d; }
static void net_send2(const char *method, const char *path, const char *body, NetCb cb, int serial, int extra, int file) {
  mx_lock(&netMx);
  if ((reqHead + 1) % NQ == reqTail) { mx_unlock(&netMx); return; }
  NetReq *q = &reqQ[reqHead];
  snprintf(q->method, sizeof q->method, "%s", method);
  if (!strncmp(path, "http", 4)) snprintf(q->url, sizeof q->url, "%s", path); else snprintf(q->url, sizeof q->url, "%s%s", API, path);
  q->body = body ? xstrdup(body) : NULL;
  q->cb = cb; q->serial = serial; q->extra = extra; q->file = file;
  reqHead = (reqHead + 1) % NQ;
  mx_unlock(&netMx);
}
static void net_send(const char *method, const char *path, const char *body, NetCb cb, int serial, int extra) { net_send2(method, path, body, cb, serial, extra, 0); }
static void net_poll(void) {
  for (;;) {
    mx_lock(&netMx);
    if (resHead == resTail) { mx_unlock(&netMx); return; }
    NetRes r = resQ[resTail]; resTail = (resTail + 1) % NQ;
    mx_unlock(&netMx);
    if (r.cb) r.cb(r.serial, r.code, r.body, r.len, r.err, r.extra);
    free(r.body);
  }
}
static int net_pending(void) { mx_lock(&netMx); int p = netBusy || reqHead != reqTail; mx_unlock(&netMx); return p; }

/* ------------------------------------------------------------------ pictures (avatars, community pictures) */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#include "stb_image.h"
#ifdef HAVE_WEBP
#include <webp/decode.h>
#endif
typedef struct { char url[200]; Img full, v[2]; int state; uint32_t used; } Pic;   /* state 0 free, 1 loading, 2 ok, 3 failed */
#define NPIC 40
#define PIC_MAX 128
static Pic pics[NPIC];
static uint32_t picTick;
static Mx picMx;
static char picQ[24][200]; static int picQh, picQt;
typedef struct { char url[200]; Img m; } PicDone;
static PicDone picD[24]; static int picDh, picDt;
static int picChanged;

static Img pic_decode(const unsigned char *data, size_t len) {
  Img out = { 0 };
  int w = 0, h = 0;
  uint8_t *px = NULL;
#ifdef HAVE_WEBP
  if (len > 12 && !memcmp(data, "RIFF", 4) && !memcmp(data + 8, "WEBP", 4)) {
    px = WebPDecodeRGBA(data, len, &w, &h);
    if (!px) return out;
    out = img_from_rgba(px, w, h); WebPFree(px);
    goto shrink;
  }
#endif
  { int n; px = stbi_load_from_memory(data, (int)len, &w, &h, &n, 4); }
  if (!px) return out;
  out = img_from_rgba(px, w, h); stbi_image_free(px);
#ifdef HAVE_WEBP
shrink:
#endif
  if (out.w > PIC_MAX || out.h > PIC_MAX) {
    float k = (float)PIC_MAX / (out.w > out.h ? out.w : out.h);
    int nw = (int)(out.w * k), nh = (int)(out.h * k);
    Img s = img_scale(&out, nw < 1 ? 1 : nw, nh < 1 ? 1 : nh); img_free(&out); out = s;
  }
  return out;
}
static void pic_thread(void *u) {
  (void)u;
  CURL *c = curl_easy_init();
  while (!netQuit) {
    mx_lock(&picMx);
    if (picQh == picQt) { mx_unlock(&picMx); thread_sleep_ms(15); continue; }
    char url[200]; snprintf(url, sizeof url, "%s", picQ[picQt]); picQt = (picQt + 1) % 24;
    mx_unlock(&picMx);
    Buf b = { 0 }; long code = 0;
    if (c) {
      curl_easy_reset(c);
      char full[300];
      if (url[0] == '/') snprintf(full, sizeof full, "%s%s", API, url); else snprintf(full, sizeof full, "%s", url);
      curl_common(c, full, &b, NULL);
      curl_easy_perform(c);
      curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    }
    Img m = { 0 };
    if (code == 200 && b.p && b.n > 8) m = pic_decode((unsigned char *)b.p, b.n);
    free(b.p);
    mx_lock(&picMx);
    if ((picDh + 1) % 24 != picDt) { snprintf(picD[picDh].url, sizeof picD[0].url, "%s", url); picD[picDh].m = m; picDh = (picDh + 1) % 24; }
    else img_free(&m);
    mx_unlock(&picMx);
  }
}
static Pic *pic_find(const char *url) { for (int i = 0; i < NPIC; i++) if (pics[i].state && !strcmp(pics[i].url, url)) return &pics[i]; return NULL; }
static void pic_poll(void) {
  for (;;) {
    mx_lock(&picMx);
    if (picDh == picDt) { mx_unlock(&picMx); return; }
    PicDone d = picD[picDt]; picDt = (picDt + 1) % 24;
    mx_unlock(&picMx);
    Pic *p = pic_find(d.url);
    if (!p) { img_free(&d.m); continue; }
    if (d.m.px) { p->full = d.m; p->state = 2; } else p->state = 3;
    picChanged = 1;
  }
}
static Pic *pic_get(const char *url) {
  if (!url || !*url || strlen(url) >= 200) return NULL;
  picTick++;
  Pic *p = pic_find(url);
  if (p) { p->used = picTick; return p->state == 2 ? p : NULL; }
  int freeI = -1, old = -1;
  for (int i = 0; i < NPIC; i++) {
    if (!pics[i].state) { freeI = i; break; }
    if (pics[i].state >= 2 && (old < 0 || pics[i].used < pics[old].used)) old = i;
  }
  int i = freeI >= 0 ? freeI : old;
  if (i < 0) return NULL;
  img_free(&pics[i].full); img_free(&pics[i].v[0]); img_free(&pics[i].v[1]);
  memset(&pics[i], 0, sizeof pics[i]);
  snprintf(pics[i].url, sizeof pics[i].url, "%s", url);
  pics[i].state = 1; pics[i].used = picTick;
  mx_lock(&picMx);
  if ((picQh + 1) % 24 != picQt) { snprintf(picQ[picQh], sizeof picQ[0], "%s", url); picQh = (picQh + 1) % 24; }
  else pics[i].state = 0;
  mx_unlock(&picMx);
  return NULL;
}
/* square picture at size s (smoothly scaled, two sizes kept per picture) */
static void pic_draw_sq(Cv *c, Pic *p, int x, int y, int s) {
  Img *v = NULL;
  for (int k = 0; k < 2; k++) if (p->v[k].px && p->v[k].w == s) v = &p->v[k];
  if (!v) {
    int k = p->v[0].px ? (p->v[1].px ? (int)(picTick & 1) : 1) : 0;
    img_free(&p->v[k]);
    /* crop to a square first (object-fit: cover) */
    int side = p->full.w < p->full.h ? p->full.w : p->full.h;
    Img sq = { malloc((size_t)side * side * 4), side, side };
    int ox = (p->full.w - side) / 2, oy = (p->full.h - side) / 2;
    for (int yy = 0; yy < side; yy++) memcpy(sq.px + yy * side, p->full.px + (yy + oy) * p->full.w + ox, (size_t)side * 4);
    p->v[k] = img_scale(&sq, s, s); img_free(&sq);
    v = &p->v[k];
  }
  img_draw(c, v, x, y, s, s, 0, 255);
}
static void net_init(void) {
  mx_init(&netMx); mx_init(&picMx);
  curl_global_init(CURL_GLOBAL_DEFAULT);
  thread_start(net_thread, 256 * 1024);
  thread_start(pic_thread, 256 * 1024);
}
