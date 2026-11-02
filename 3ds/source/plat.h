/*
 * plat.h - the small part that is different on a real 3DS and on the PC test build:
 * screens, buttons/touch, the system keyboard, threads and file paths.
 */
enum { KEY_A_ = 1, KEY_B_ = 2, KEY_X_ = 4, KEY_Y_ = 8, KEY_L_ = 16, KEY_R_ = 32, KEY_UP_ = 64, KEY_DOWN_ = 128, KEY_LEFT_ = 256, KEY_RIGHT_ = 512,
       KEY_START_ = 1024, KEY_SELECT_ = 2048, KEY_ZL_ = 4096, KEY_ZR_ = 8192 };
typedef struct { uint32_t down, held; int touch, tx, ty; float cy, csx, csy; } Input;   /* cy: circle pad y, csx/csy: C-stick of the New 3DS (-1..1) */

#ifdef __3DS__
#include <3ds.h>
#include <malloc.h>
#define DATA_DIR "sdmc:/3ds/aeroverse/"
#define APP_PATH_DEFAULT "sdmc:/3ds/aeroverse.3dsx"
static u32 *socBuf;
static uint32_t plat_ms(void) { return (uint32_t)osGetTime(); }
static int plat_init(void) {
  gfxInitDefault();
  gfxSetDoubleBuffering(GFX_TOP, true); gfxSetDoubleBuffering(GFX_BOTTOM, true);
  socBuf = memalign(0x1000, 0x100000);
  if (socBuf && socInit(socBuf, 0x100000) != 0) { free(socBuf); socBuf = NULL; }
  romfsInit();
  irrstInit();   /* C-stick and ZL/ZR on the New 3DS (fails quietly on the old 3DS) */
  osSetSpeedupEnable(true);   /* New 3DS: faster CPU */
  mkdir("sdmc:/3ds", 0777); mkdir(DATA_DIR, 0777);
  return 1;
}
static void plat_exit(void) { irrstExit(); if (socBuf) socExit(); gfxExit(); }
static int plat_running(void) { return aptMainLoop(); }
static void plat_input(Input *in) {
  hidScanInput();
  u32 d = hidKeysDown(), h = hidKeysHeld();
  static const u32 M[14][2] = { { KEY_A, KEY_A_ }, { KEY_B, KEY_B_ }, { KEY_X, KEY_X_ }, { KEY_Y, KEY_Y_ }, { KEY_L, KEY_L_ }, { KEY_R, KEY_R_ },
    { KEY_DUP, KEY_UP_ }, { KEY_DDOWN, KEY_DOWN_ }, { KEY_DLEFT, KEY_LEFT_ }, { KEY_DRIGHT, KEY_RIGHT_ }, { KEY_START, KEY_START_ }, { KEY_SELECT, KEY_SELECT_ },
    { KEY_ZL, KEY_ZL_ }, { KEY_ZR, KEY_ZR_ } };
  in->down = in->held = 0;
  for (int i = 0; i < 14; i++) { if (d & M[i][0]) in->down |= M[i][1]; if (h & M[i][0]) in->held |= M[i][1]; }
  in->touch = (h & KEY_TOUCH) ? 1 : 0;
  if (in->touch) { touchPosition tp; hidTouchRead(&tp); in->tx = tp.px; in->ty = tp.py; }
  circlePosition cp; hidCircleRead(&cp);
  in->cy = cp.dy / 156.0f;
  irrstScanInput();
  circlePosition cs; irrstCstickRead(&cs);
  in->csx = cs.dx / 146.0f; in->csy = cs.dy / 146.0f;
  if (in->csx > 1) in->csx = 1;
  if (in->csx < -1) in->csx = -1;
  if (in->csy > 1) in->csy = 1;
  if (in->csy < -1) in->csy = -1;
  if (in->cy > 1) in->cy = 1;
  if (in->cy < -1) in->cy = -1;
}
/* screens are rotated in memory: 240 pixels per column, BGR */
static void blit_screen(gfxScreen_t scr, const Cv *c) {
  u16 fw, fh;
  u8 *fb = gfxGetFramebuffer(scr, GFX_LEFT, &fw, &fh);
  for (int x = 0; x < c->w; x++) {
    u8 *col = fb + (size_t)x * 240 * 3;
    for (int y = 0; y < 240; y++) {
      uint32_t p = c->px[y * c->w + x];
      u8 *o = col + (239 - y) * 3;
      o[0] = p & 255; o[1] = (p >> 8) & 255; o[2] = (p >> 16) & 255;
    }
  }
}
static void plat_present(const Cv *top, const Cv *bot) {
  blit_screen(GFX_TOP, top); blit_screen(GFX_BOTTOM, bot);
  gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
}
static void plat_vsync(void) { gspWaitForVBlank(); }
/* the system keyboard; returns 1 when the person pressed OK */
static int plat_keyboard(const char *hint, const char *init, char *out, int max, int password, int multiline) {
  SwkbdState sw;
  swkbdInit(&sw, SWKBD_TYPE_NORMAL, 2, max > 1 ? max - 1 : 1);
  swkbdSetHintText(&sw, hint);
  if (init && *init && !password) swkbdSetInitialText(&sw, init);
  if (password) swkbdSetPasswordMode(&sw, SWKBD_PASSWORD_HIDE_DELAY);
  if (multiline) swkbdSetFeatures(&sw, SWKBD_MULTILINE);
  swkbdSetButton(&sw, SWKBD_BUTTON_LEFT, "Cancel", false);
  swkbdSetButton(&sw, SWKBD_BUTTON_RIGHT, "OK", true);
  char *buf = malloc((size_t)max + 4);
  SwkbdButton b = swkbdInputText(&sw, buf, (size_t)max);
  int ok = b == SWKBD_BUTTON_RIGHT;
  if (ok) snprintf(out, (size_t)max, "%s", buf);
  free(buf);
  return ok;
}
typedef LightLock Mx;
static void mx_init(Mx *m) { LightLock_Init(m); }
static void mx_lock(Mx *m) { LightLock_Lock(m); }
static void mx_unlock(Mx *m) { LightLock_Unlock(m); }
static Thread thr[4]; static int thrN;
static void thread_start(void (*fn)(void *), int stack) {
  s32 prio = 0x30; svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
  if (thrN < 4) { thr[thrN] = threadCreate(fn, NULL, (size_t)stack, prio + 1, -2, false); if (thr[thrN]) thrN++; }
}
/* wait for the network threads before closing sockets (otherwise the 3DS shows an exception screen on exit) */
static void threads_join(void) { for (int i = 0; i < thrN; i++) { threadJoin(thr[i], 3000000000ULL); threadFree(thr[i]); } thrN = 0; }
static void thread_sleep_ms(int ms) { svcSleepThread((s64)ms * 1000000LL); }

#else  /* ------------------------------------------------ PC test build (SDL2): top screen above the bottom one */
#include <SDL.h>
#include <sys/stat.h>
#define DATA_DIR "host_sd/"
#define APP_PATH_DEFAULT "host_sd/aeroverse.3dsx"
static SDL_Window *hWin; static SDL_Renderer *hR; static SDL_Texture *hTop, *hBot;
static int hRun = 1;
static char hsCmd[512][160]; static int hsN, hsI, hsWait, hsTouchLeft, hsTX, hsTY, hsTX1, hsTY1, hsTLen; static char hsShot[64];
static char hsKb[32][200]; static int hsKbN, hsKbI;
static uint32_t plat_ms(void) { return SDL_GetTicks(); }
static int plat_init(void) {
  SDL_Init(SDL_INIT_VIDEO);
  hWin = SDL_CreateWindow("Aeroverse 3DS (PC test)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 400, 480, 0);
  hR = SDL_CreateRenderer(hWin, -1, SDL_RENDERER_SOFTWARE);
  hTop = SDL_CreateTexture(hR, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 400, 240);
  hBot = SDL_CreateTexture(hR, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);
  mkdir(DATA_DIR, 0777);
  const char *sc = getenv("HOST_SCRIPT");
  if (sc) { FILE *f = fopen(sc, "r"); if (f) { while (hsN < 512 && fgets(hsCmd[hsN], 160, f)) { hsCmd[hsN][strcspn(hsCmd[hsN], "\r\n")] = 0; hsN++; } fclose(f); } }
  return 1;
}
static void plat_exit(void) { SDL_Quit(); }
static int plat_running(void) { return hRun; }
static void plat_input(Input *in) {
  SDL_Event e;
  static uint32_t prevHeld;
  uint32_t held = 0;
  while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) hRun = 0;
  const Uint8 *ks = SDL_GetKeyboardState(NULL);
  if (ks[SDL_SCANCODE_Z]) held |= KEY_A_;
  if (ks[SDL_SCANCODE_X]) held |= KEY_B_;
  if (ks[SDL_SCANCODE_A]) held |= KEY_Y_;
  if (ks[SDL_SCANCODE_S]) held |= KEY_X_;
  if (ks[SDL_SCANCODE_Q]) held |= KEY_L_;
  if (ks[SDL_SCANCODE_W]) held |= KEY_R_;
  if (ks[SDL_SCANCODE_UP]) held |= KEY_UP_;
  if (ks[SDL_SCANCODE_DOWN]) held |= KEY_DOWN_;
  if (ks[SDL_SCANCODE_LEFT]) held |= KEY_LEFT_;
  if (ks[SDL_SCANCODE_RIGHT]) held |= KEY_RIGHT_;
  in->touch = 0; in->cy = 0; in->csx = 0; in->csy = 0;
  int mx, my; uint32_t mb = SDL_GetMouseState(&mx, &my);
  if ((mb & SDL_BUTTON_LMASK) && my >= 240 && mx >= 40 && mx < 360) { in->touch = 1; in->tx = mx - 40; in->ty = my - 240; }
  /* scripted input for automatic tests */
  uint32_t scr = 0;
  if (hsWait > 0) hsWait--;
  else if (hsTouchLeft <= 0 && hsI < hsN) {
    char *l = hsCmd[hsI++]; int a, b, c2, d, fr; char nm[64]; unsigned bt;
    if (sscanf(l, "wait %d", &a) == 1) hsWait = a;
    else if (sscanf(l, "tap %d %d", &a, &b) == 2) { hsTX = hsTX1 = a; hsTY = hsTY1 = b; hsTLen = hsTouchLeft = 4; hsWait = 3; }
    else if (sscanf(l, "drag %d %d %d %d %d", &a, &b, &c2, &d, &fr) == 5) { hsTX = a; hsTY = b; hsTX1 = c2; hsTY1 = d; hsTLen = hsTouchLeft = fr; hsWait = 3; }
    else if (sscanf(l, "btn %x", &bt) == 1) { scr = bt; hsWait = 3; }
    else if (!strncmp(l, "kb ", 3)) { if (hsKbN < 32) snprintf(hsKb[hsKbN++], 200, "%s", l + 3); }
    else if (sscanf(l, "shot %63s", nm) == 1) snprintf(hsShot, sizeof hsShot, "%s", nm);
    else if (!strncmp(l, "quit", 4)) hRun = 0;
  }
  if (hsTouchLeft > 0) {
    int k = hsTLen - hsTouchLeft; float t = hsTLen > 1 ? (float)k / (hsTLen - 1) : 0;
    in->touch = 1; in->tx = (int)(hsTX + (hsTX1 - hsTX) * t); in->ty = (int)(hsTY + (hsTY1 - hsTY) * t);
    hsTouchLeft--;
  }
  held |= scr;
  in->held = held; in->down = held & ~prevHeld; prevHeld = held & ~scr;
}
static void plat_present(const Cv *top, const Cv *bot) {
  SDL_UpdateTexture(hTop, NULL, top->px, top->w * 4);
  SDL_UpdateTexture(hBot, NULL, bot->px, bot->w * 4);
  SDL_SetRenderDrawColor(hR, 20, 20, 20, 255); SDL_RenderClear(hR);
  SDL_Rect a = { 0, 0, 400, 240 }, b = { 40, 240, 320, 240 };
  SDL_RenderCopy(hR, hTop, NULL, &a); SDL_RenderCopy(hR, hBot, NULL, &b);
  if (hsShot[0]) {
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 400, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(hR, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    char p[128]; snprintf(p, sizeof p, "/tmp/shots3ds/%s.bmp", hsShot); SDL_SaveBMP(s, p); SDL_FreeSurface(s); hsShot[0] = 0;
  }
  SDL_RenderPresent(hR);
  SDL_Delay(16);
}
static void plat_vsync(void) { SDL_Delay(16); }
static int plat_keyboard(const char *hint, const char *init, char *out, int max, int password, int multiline) {
  (void)hint; (void)init; (void)password; (void)multiline;
  if (hsKbI < hsKbN) { snprintf(out, (size_t)max, "%s", hsKb[hsKbI++]); for (char *p = out; *p; p++) if (*p == '|') *p = '\n'; return 1; }
  return 0;
}
typedef SDL_mutex *Mx;
static void mx_init(Mx *m) { *m = SDL_CreateMutex(); }
static void mx_lock(Mx *m) { SDL_LockMutex(*m); }
static void mx_unlock(Mx *m) { SDL_UnlockMutex(*m); }
static void (*thrFn[8])(void *); static int thrN; static SDL_Thread *thrH[8];
static int thr_tramp(void *u) { thrFn[(intptr_t)u](NULL); return 0; }
static void thread_start(void (*fn)(void *), int stack) { (void)stack; thrFn[thrN] = fn; thrH[thrN] = SDL_CreateThread(thr_tramp, "t", (void *)(intptr_t)thrN); thrN++; }
static void threads_join(void) { for (int i = 0; i < thrN; i++) SDL_WaitThread(thrH[i], NULL); thrN = 0; }
static void thread_sleep_ms(int ms) { SDL_Delay((Uint32)ms); }
#endif
