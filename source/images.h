/*
 * images.h - avatars, community pictures and [img=...] pictures in posts.
 * A second network thread downloads and decodes images (WebP/JPEG/PNG with SDL2_image when available,
 * otherwise JPEG/PNG with stb_image); the main thread turns them into textures. Small LRU cache.
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#include "stb_image.h"
#ifdef HAVE_WEBP
#include <webp/decode.h>
#endif

typedef struct { char url[200]; SDL_Texture *t; SDL_Surface *s; int w, h, state; uint32_t used; } ImgE;   /* state 0 free, 1 queued, 2 ok, 3 failed */
#define NIMG 96
static ImgE imgs[NIMG];
static uint32_t imgTick;
static SDL_mutex *imgMx; static SDL_cond *imgCv;
static char imgQ[32][200]; static int imgQh, imgQt;
typedef struct { char url[200]; SDL_Surface *s; } ImgDone;
static ImgDone imgD[32]; static int imgDh, imgDt;

static SDL_Surface *img_decode(const unsigned char *data, size_t len) {
  SDL_Surface *out = NULL;
#ifdef HAVE_WEBP
  if (len > 12 && !memcmp(data, "RIFF", 4) && !memcmp(data + 8, "WEBP", 4)) {
    int w = 0, h = 0;
    uint8_t *px = WebPDecodeRGBA(data, len, &w, &h);
    if (!px) return NULL;
    out = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (out) for (int y = 0; y < h; y++) memcpy((uint8_t *)out->pixels + y * out->pitch, px + (size_t)y * w * 4, (size_t)w * 4);
    WebPFree(px);
    return out;
  }
#endif
  int w, h, n;
  unsigned char *px = stbi_load_from_memory(data, (int)len, &w, &h, &n, 4);
  if (!px) return NULL;
  out = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
  if (out) for (int y = 0; y < h; y++) memcpy((uint8_t *)out->pixels + y * out->pitch, px + (size_t)y * w * 4, (size_t)w * 4);
  stbi_image_free(px);
  return out;
}
/* big pictures are scaled down before they become textures */
static SDL_Surface *img_shrink(SDL_Surface *s, int maxDim) {
  if (!s || (s->w <= maxDim && s->h <= maxDim)) return s;
  float k = (float)maxDim / (s->w > s->h ? s->w : s->h);
  int w = (int)(s->w * k), h = (int)(s->h * k); if (w < 1) w = 1; if (h < 1) h = 1;
  SDL_Surface *d = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
  if (!d) return s;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    int sx = x * s->w / w, sy = y * s->h / h;
    memcpy((uint8_t *)d->pixels + y * d->pitch + x * 4, (uint8_t *)s->pixels + sy * s->pitch + sx * 4, 4);
  }
  SDL_FreeSurface(s);
  return d;
}
static int img_thread(void *u) {
  (void)u;
  CURL *c = curl_easy_init();
  for (;;) {
    SDL_LockMutex(imgMx);
    while (imgQh == imgQt && !netQuit) SDL_CondWait(imgCv, imgMx);
    if (netQuit) { SDL_UnlockMutex(imgMx); break; }
    char url[200]; snprintf(url, sizeof url, "%s", imgQ[imgQt]); imgQt = (imgQt + 1) % 32;
    SDL_UnlockMutex(imgMx);
    Buf b = { 0 };
    long code = 0;
    if (c) {
      curl_easy_reset(c);
      struct curl_blob blob; blob.data = (void *)cacert_pem; blob.len = cacert_pem_size; blob.flags = CURL_BLOB_COPY;
      curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &blob);
      char full[260];
      if (url[0] == '/') snprintf(full, sizeof full, "%s%s", API, url); else snprintf(full, sizeof full, "%s", url);
      curl_easy_setopt(c, CURLOPT_URL, full);
      curl_easy_setopt(c, CURLOPT_USERAGENT, "AeroverseWiiU/" APP_VERSION);
      curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
      curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
      curl_easy_setopt(c, CURLOPT_TIMEOUT, 40L);
      curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, wr_cb);
      curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
      curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
      curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, prog_cb);
      curl_easy_perform(c);
      curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    }
    SDL_Surface *s = NULL;
    if (code == 200 && b.p && b.n > 8 && b.n < 12 * 1024 * 1024) s = img_shrink(img_decode((unsigned char *)b.p, b.n), 640);
    free(b.p);
    SDL_LockMutex(imgMx);
    if ((imgDh + 1) % 32 != imgDt) { snprintf(imgD[imgDh].url, sizeof imgD[0].url, "%s", url); imgD[imgDh].s = s; imgDh = (imgDh + 1) % 32; }
    else if (s) SDL_FreeSurface(s);
    SDL_UnlockMutex(imgMx);
  }
  if (c) curl_easy_cleanup(c);
  return 0;
}
static void img_init(void) {
  imgMx = SDL_CreateMutex(); imgCv = SDL_CreateCond();
  SDL_CreateThreadWithStackSize(img_thread, "img", 512 * 1024, NULL);
}
static ImgE *img_find(const char *url) { for (int i = 0; i < NIMG; i++) if (imgs[i].state && !strcmp(imgs[i].url, url)) return &imgs[i]; return NULL; }
/* main thread: textures for finished downloads */
static int imgChanged;
static void img_poll(void) {
  for (;;) {
    SDL_LockMutex(imgMx);
    if (imgDh == imgDt) { SDL_UnlockMutex(imgMx); return; }
    ImgDone d = imgD[imgDt]; imgDt = (imgDt + 1) % 32;
    SDL_UnlockMutex(imgMx);
    ImgE *e = img_find(d.url);
    if (!e) { if (d.s) SDL_FreeSurface(d.s); continue; }
    if (d.s) { e->s = d.s; e->w = d.s->w; e->h = d.s->h; e->t = SDL_CreateTextureFromSurface(R, d.s); if (e->t) SDL_SetTextureScaleMode(e->t, SDL_ScaleModeLinear); e->state = e->t ? 2 : 3; }
    else e->state = 3;
    imgChanged = 1;
  }
}
/* returns the picture if it is ready; starts the download the first time */
static ImgE *img_get(const char *url) {
  if (!url || !*url || strlen(url) >= 200) return NULL;
  imgTick++;
  ImgE *e = img_find(url);
  if (e) { e->used = imgTick; return e->state == 2 ? e : NULL; }
  int freeI = -1, old = -1;
  for (int i = 0; i < NIMG; i++) {
    if (!imgs[i].state) { freeI = i; break; }
    if (imgs[i].state >= 2 && (old < 0 || imgs[i].used < imgs[old].used)) old = i;
  }
  int i = freeI >= 0 ? freeI : old;
  if (i < 0) return NULL;
  if (imgs[i].t) SDL_DestroyTexture(imgs[i].t);
  if (imgs[i].s) SDL_FreeSurface(imgs[i].s);
  memset(&imgs[i], 0, sizeof imgs[i]);
  snprintf(imgs[i].url, sizeof imgs[i].url, "%s", url);
  imgs[i].state = 1; imgs[i].used = imgTick;
  SDL_LockMutex(imgMx);
  if ((imgQh + 1) % 32 != imgQt) { snprintf(imgQ[imgQh], sizeof imgQ[0], "%s", url); imgQh = (imgQh + 1) % 32; SDL_CondSignal(imgCv); }
  else imgs[i].state = 0;   /* queue full: try again later */
  SDL_UnlockMutex(imgMx);
  return NULL;
}
/* draw a picture like object-fit: cover into a square/rect */
static void img_draw_cover(ImgE *e, int x, int y, int w, int h) {
  float sa = (float)e->w / e->h, da = (float)w / h;
  SDL_Rect src;
  if (sa > da) { src.h = e->h; src.w = (int)(e->h * da); src.x = (e->w - src.w) / 2; src.y = 0; }
  else { src.w = e->w; src.h = (int)(e->w / da); src.x = 0; src.y = (e->h - src.h) / 2; }
  SDL_Rect d = { x, y, w, h };
  SDL_RenderCopy(R, e->t, &src, &d);
}
/* allowed picture hosts for [img=...] (same list as the website) */
static int img_host_ok(const char *u) {
  static const char *H[] = { "i.imgur.com", "i.postimg.cc", "files.catbox.moe", "i.ibb.co", "upload.wikimedia.org", "i.redd.it", "media.tenor.com", "i.giphy.com", "media.giphy.com" };
  if (strncmp(u, "https://", 8)) return 0;
  const char *h = u + 8; size_t L = strcspn(h, "/:?#");
  for (size_t i = 0; i < sizeof H / sizeof H[0]; i++) if (strlen(H[i]) == L && !strncmp(h, H[i], L)) return 1;
  return 0;
}
