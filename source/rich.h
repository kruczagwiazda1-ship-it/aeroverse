/*
 * rich.h - renders post text exactly like the website's richText() + applyPostStyle():
 * **bold** *italic* [i]..[/i] __underline__ ~~strike~~ [color=#hex] [glow=#hex] [grad=#a,#b] [font=N] [size=N] [img=..]
 * lines: "# heading", "> quote", "- list", "---" rule. Post styles: colour/theme background, 29 fonts, 3 sizes, align, frame.
 */

/* fonts used by post styles, same order as POST_FONTS on the website (packed in data/postfonts.bin) */
#define NPOSTFONTS 29
static const unsigned char *pfData[NPOSTFONTS]; static int pfLen[NPOSTFONTS];
static void postfonts_init(const unsigned char *bin, int len) {
  if (len < 8 || memcmp(bin, "PFNT", 4)) return;
  int n = (bin[4] << 24) | (bin[5] << 16) | (bin[6] << 8) | bin[7];
  for (int i = 0; i < n && i < NPOSTFONTS; i++) {
    const unsigned char *e = bin + 8 + i * 8;
    int off = (e[0] << 24) | (e[1] << 16) | (e[2] << 8) | e[3], l = (e[4] << 24) | (e[5] << 16) | (e[6] << 8) | e[7];
    if (off + l <= len) { pfData[i] = bin + off; pfLen[i] = l; }
  }
}
typedef struct { int font, px, bold; TTF_Font *f; uint32_t used; } FontC;
#define NFONTC 48
static FontC fontc[NFONTC];
static uint32_t fontTick;
extern const unsigned char nunito_bold_ttf[]; extern const unsigned int nunito_bold_ttf_size;
static TTF_Font *font_get(int font, int px, int bold) {
  if (font < 0 || font >= NPOSTFONTS || !pfData[font]) font = 0;
  if (px < 8) px = 8; if (px > 120) px = 120;
  int useBoldFile = bold && font == 0;   /* Nunito has a real 800 weight */
  fontTick++;
  int freeI = -1, old = 0;
  for (int i = 0; i < NFONTC; i++) {
    if (!fontc[i].f) { if (freeI < 0) freeI = i; continue; }
    if (fontc[i].font == font && fontc[i].px == px && fontc[i].bold == useBoldFile) { fontc[i].used = fontTick; return fontc[i].f; }
    if (fontc[i].used < fontc[old].used) old = i;
  }
  int i = freeI >= 0 ? freeI : old;
  if (fontc[i].f) TTF_CloseFont(fontc[i].f);
  TTF_Font *f = useBoldFile ? TTF_OpenFontRW(SDL_RWFromConstMem(nunito_bold_ttf, (int)nunito_bold_ttf_size), 1, px)
                            : TTF_OpenFontRW(SDL_RWFromConstMem(pfData[font] ? pfData[font] : nunito_bold_ttf, pfData[font] ? pfLen[font] : (int)nunito_bold_ttf_size), 1, px);
  fontc[i].font = font; fontc[i].px = px; fontc[i].bold = useBoldFile; fontc[i].f = f; fontc[i].used = fontTick;
  return f;
}

/* ---------- inline parsing into styled runs ---------- */
typedef struct { int font; float em; uint8_t b, i, u, s; uint32_t color; int hasColor; uint32_t glow; int hasGlow; uint32_t g1, g2; int hasGrad; } RStyle;
typedef struct { char *txt; RStyle st; } Run;
typedef struct { Run *r; int n, cap; } Runs;
/* drop emoji and joiners: the fonts have no emoji glyphs (they would show as boxes) */
static size_t strip_emoji(char *d, const char *s, size_t len) {
  size_t o = 0;
  for (size_t i = 0; i < len;) {
    unsigned char c = (unsigned char)s[i];
    if (c >= 0xF0 && i + 3 < len + 0) { i += 4; continue; }
    if (c >= 0xF0) { i = len; continue; }
    if (c == 0xE2 && i + 2 < len) { unsigned cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); if ((cp >= 0x2600 && cp <= 0x27BF) || cp == 0x200D || (cp >= 0x2B00 && cp <= 0x2BFF)) { i += 3; continue; } }
    if (c == 0xEF && i + 2 < len && (unsigned char)s[i + 1] == 0xB8 && (unsigned char)s[i + 2] == 0x8F) { i += 3; continue; }   /* U+FE0F */
    d[o++] = s[i++];
  }
  return o;
}
static void runs_add(Runs *rs, const char *s0, size_t len0, const RStyle *st) {
  if (!len0) return;
  char tmpbuf[4096]; char *heap = NULL; char *s = tmpbuf;
  if (len0 >= sizeof tmpbuf) s = heap = malloc(len0 + 1);
  size_t len = strip_emoji(s, s0, len0);
  if (!len) { free(heap); return; }
  if (rs->n == rs->cap) { rs->cap = rs->cap ? rs->cap * 2 : 32; rs->r = realloc(rs->r, sizeof(Run) * (size_t)rs->cap); }
  Run *r = &rs->r[rs->n++]; r->txt = malloc(len + 1); memcpy(r->txt, s, len); r->txt[len] = 0; r->st = *st;
  free(heap);
}
static void runs_free(Runs *rs) { for (int i = 0; i < rs->n; i++) free(rs->r[i].txt); free(rs->r); memset(rs, 0, sizeof *rs); }

static int is_sp(char c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r'; }
static uint32_t parse_hex6(const char *p) { uint32_t v = 0; for (int k = 0; k < 6; k++) { int h = hexv(p[k]); if (h < 0) return 0; v = v * 16 + (uint32_t)h; } return v; }

/* add text with stray closing tags removed */
static void add_plain(Runs *rs, const char *s, size_t len, const RStyle *st) {
  size_t i = 0, start = 0;
  static const char *cl[6] = { "[/color]", "[/glow]", "[/font]", "[/size]", "[/grad]", "[/i]" };
  while (i < len) {
    int hit = 0;
    if (s[i] == '[') for (int k = 0; k < 6; k++) { size_t L = strlen(cl[k]); if (i + L <= len && !strncmp(s + i, cl[k], L)) { runs_add(rs, s + start, i - start, st); i += L; start = i; hit = 1; break; } }
    if (!hit) i++;
  }
  runs_add(rs, s + start, len - start, st);
}
/* balanced closing tag for name in s[0..len) */
static long find_close(const char *s, size_t len, const char *name) {
  int depth = 1; size_t nl = strlen(name);
  for (size_t i = 0; i < len; i++) {
    if (s[i] != '[') continue;
    if (s[i + 1] == '/' && i + 2 + nl < len + 1 && !strncmp(s + i + 2, name, nl) && s[i + 2 + nl] == ']') { if (--depth == 0) return (long)i; }
    else if (!strncmp(s + i + 1, name, nl) && (s[i + 1 + nl] == ']' || s[i + 1 + nl] == '=')) depth++;
  }
  return -1;
}

/* try to match one inline token at s; returns kind (0 none) and fills inner range / value */
enum { T_NONE, T_B, T_IMG, T_I, T_U, T_S, T_TAG, T_GRAD, T_ITAG };
static int match_at(const char *s, size_t len, size_t *used, const char **in, size_t *inLen, char *tag, char *val) {
  /* **b** */
  if (len >= 5 && s[0] == '*' && s[1] == '*' && !is_sp(s[2])) {
    for (size_t j = 2; j + 1 < len; j++) { if (s[j] == '\n') break; if (s[j] == '*' && s[j + 1] == '*' && !is_sp(s[j - 1]) && j > 2) { *in = s + 2; *inLen = j - 2; *used = j + 2; return T_B; } }
    if (len >= 5 && s[2] != '*' && s[3] == '*' && s[4] == '*') { *in = s + 2; *inLen = 1; *used = 5; return T_B; }
  }
  if (!strncmp(s, "[img=https://", 13)) { const char *e = memchr(s, ']', len); if (e) { int ok = 1; for (const char *p = s + 5; p < e; p++) if (is_sp(*p)) ok = 0; if (ok) { *used = (size_t)(e - s) + 1; return T_IMG; } } }
  if (len >= 3 && s[0] == '*' && s[1] != '*' && !is_sp(s[1])) {
    for (size_t j = 1; j < len; j++) { if (s[j] == '\n') break; if (s[j] == '*') { if (!is_sp(s[j - 1])) { *in = s + 1; *inLen = j - 1; *used = j + 1; return T_I; } break; } }
  }
  if (len >= 5 && s[0] == '_' && s[1] == '_' && !is_sp(s[2])) {
    for (size_t j = 3; j + 1 < len; j++) { if (s[j] == '\n') break; if (s[j] == '_' && s[j + 1] == '_' && !is_sp(s[j - 1])) { *in = s + 2; *inLen = j - 2; *used = j + 2; return T_U; } }
  }
  if (len >= 5 && s[0] == '~' && s[1] == '~' && !is_sp(s[2])) {
    for (size_t j = 3; j + 1 < len; j++) { if (s[j] == '\n') break; if (s[j] == '~' && s[j + 1] == '~' && !is_sp(s[j - 1])) { *in = s + 2; *inLen = j - 2; *used = j + 2; return T_S; } }
  }
  if (s[0] == '[') {
    static const char *tags[4] = { "color", "glow", "font", "size" };
    for (int k = 0; k < 4; k++) {
      size_t tl = strlen(tags[k]);
      if (!strncmp(s + 1, tags[k], tl) && s[1 + tl] == '=') {
        const char *v = s + 2 + tl, *e = memchr(v, ']', len - (size_t)(v - s));
        if (!e) break;
        size_t vl = (size_t)(e - v);
        int ok = (vl == 7 && v[0] == '#' && parse_hex6(v + 1) != 0) || (vl == 7 && v[0] == '#' && !strncmp(v + 1, "000000", 6)) || (vl >= 1 && vl <= 2 && v[0] >= '0' && v[0] <= '9' && (vl == 1 || (v[1] >= '0' && v[1] <= '9')));
        if (!ok) break;
        strcpy(tag, tags[k]); memcpy(val, v, vl); val[vl] = 0;
        *used = (size_t)(e - s) + 1;
        return T_TAG;
      }
    }
    if (!strncmp(s, "[grad=#", 7) && len > 22 && s[14] == ',' && s[15] == '#' && s[22] == ']' && parse_hex6(s + 8) + 1 && parse_hex6(s + 16) + 1) {
      memcpy(val, s + 6, 15); val[15] = 0; strcpy(tag, "grad"); *used = 23; return T_GRAD;
    }
    if (!strncmp(s, "[i]", 3)) { strcpy(tag, "i"); *used = 3; return T_ITAG; }
  }
  return T_NONE;
}

static void inline_rich(Runs *rs, const char *s, size_t len, RStyle st, int depth) {
  size_t i = 0, start = 0;
  while (i < len) {
    char c = s[i];
    if (depth < 8 && (c == '*' || c == '[' || c == '_' || c == '~')) {
      size_t used = 0, inLen = 0; const char *in = NULL; char tag[16] = "", val[24] = "";
      int k = match_at(s + i, len - i, &used, &in, &inLen, tag, val);
      if (k != T_NONE) {
        add_plain(rs, s + start, i - start, &st);
        RStyle ns = st;
        if (k == T_IMG) { char u[200]; size_t L = used - 6 < 199 ? used - 6 : 199; memcpy(u, s + i + 5, L); u[L] = 0; if (!img_host_ok(u)) runs_add(rs, s + i, used, &st); i += used; start = i; continue; }
        if (k == T_B) ns.b = 1; else if (k == T_I) ns.i = 1; else if (k == T_U) ns.u = 1; else if (k == T_S) ns.s = 1;
        if (k == T_TAG || k == T_GRAD || k == T_ITAG) {
          const char *after = s + i + used; size_t alen = len - i - used;
          long ci = find_close(after, alen, tag);
          int bad = 0;
          if (k == T_TAG) {
            if (!strcmp(tag, "font") && atoi(val) > NPOSTFONTS - 1) bad = 1;
            if (!strcmp(tag, "size") && atoi(val) > 4) bad = 1;
            if ((!strcmp(tag, "font") || !strcmp(tag, "size")) && val[0] == '#') bad = 1;
            if ((!strcmp(tag, "color") || !strcmp(tag, "glow")) && val[0] != '#') bad = 1;
          }
          if (ci < 0 || bad) { if (bad) add_plain(rs, s + i, used, &st); i += used; start = i; continue; }
          in = after; inLen = (size_t)ci; used = used + (size_t)ci + strlen(tag) + 3;
          if (k == T_ITAG) ns.i = 1;
          else if (k == T_GRAD) { ns.hasGrad = 1; ns.g1 = parse_hex6(val + 1); ns.g2 = parse_hex6(val + 9); ns.b = 1; }
          else if (!strcmp(tag, "color")) { ns.hasColor = 1; ns.color = parse_hex6(val + 1); }
          else if (!strcmp(tag, "glow")) { ns.hasGlow = 1; ns.glow = parse_hex6(val + 1); ns.hasColor = 1; ns.color = 0xffffff; }
          else if (!strcmp(tag, "font")) { int f = atoi(val); if (f > 0) ns.font = f; }
          else if (!strcmp(tag, "size")) { static const float SE[5] = { 0.75f, 1, 1.35f, 1.8f, 2.5f }; ns.em = st.em * SE[atoi(val)]; }
        }
        inline_rich(rs, in, inLen, ns, depth + 1);
        i += used; start = i;
        continue;
      }
    }
    i++;
  }
  add_plain(rs, s + start, len - start, &st);
}

/* ---------- layout + render ---------- */
typedef struct { int kind; Runs runs; } RBlock;   /* kind 0 text, 1 heading, 2 quote, 3 list item, 4 rule */
typedef struct { const char *txt; size_t len; RStyle st; TTF_Font *f; int w, h, asc; int space; } Piece;

static TTF_Font *style_font(const RStyle *st, int basePx) {
  int px = (int)(basePx * st->em + 0.5f);
  return font_get(st->font, px, st->b);
}
static void blit_text(SDL_Surface *dst, TTF_Font *f, const char *txt, const RStyle *st, uint32_t baseCol, int x, int y, int textShadow, uint32_t shadowCol) {
  int fs = TTF_STYLE_NORMAL;
  if (st->b && !(st->font == 0)) fs |= TTF_STYLE_BOLD;
  if (st->i) fs |= TTF_STYLE_ITALIC;
  if (st->u) fs |= TTF_STYLE_UNDERLINE;
  if (st->s) fs |= TTF_STYLE_STRIKETHROUGH;
  TTF_SetFontStyle(f, fs);
  uint32_t col = st->hasColor ? st->color : baseCol;
  SDL_Color c = { (col >> 16) & 255, (col >> 8) & 255, col & 255, 255 };
  if (st->hasGlow) {
    SDL_Color g = { (st->glow >> 16) & 255, (st->glow >> 8) & 255, st->glow & 255, 255 };
    SDL_Surface *gs = TTF_RenderUTF8_Blended(f, txt, g);
    if (gs) {
      SDL_SetSurfaceBlendMode(gs, SDL_BLENDMODE_BLEND);
      static const int off[12][2] = { { -3, 0 }, { 3, 0 }, { 0, -3 }, { 0, 3 }, { -2, -2 }, { 2, 2 }, { -2, 2 }, { 2, -2 }, { -5, 0 }, { 5, 0 }, { 0, -5 }, { 0, 5 } };
      for (int k = 0; k < 12; k++) { SDL_SetSurfaceAlphaMod(gs, k < 8 ? 90 : 50); SDL_Rect d = { x + off[k][0], y + off[k][1], 0, 0 }; SDL_BlitSurface(gs, NULL, dst, &d); }
      SDL_FreeSurface(gs);
    }
  }
  if (textShadow) {
    SDL_Color sc = { (shadowCol >> 16) & 255, (shadowCol >> 8) & 255, shadowCol & 255, 255 };
    SDL_Surface *ss = TTF_RenderUTF8_Blended(f, txt, sc);
    if (ss) { SDL_SetSurfaceBlendMode(ss, SDL_BLENDMODE_BLEND); SDL_SetSurfaceAlphaMod(ss, (shadowCol >> 24) & 255); SDL_Rect d = { x, y + 1, 0, 0 }; SDL_BlitSurface(ss, NULL, dst, &d); SDL_FreeSurface(ss); }
  }
  SDL_Surface *ts = TTF_RenderUTF8_Blended(f, txt, st->hasGrad ? (SDL_Color){ 255, 255, 255, 255 } : c);
  if (ts) {
    if (st->hasGrad) {
      SDL_Surface *cs = SDL_ConvertSurfaceFormat(ts, SDL_PIXELFORMAT_RGBA32, 0);
      if (cs) {
        SDL_LockSurface(cs);
        for (int yy = 0; yy < cs->h; yy++) { uint8_t *row = (uint8_t *)cs->pixels + yy * cs->pitch; for (int xx = 0; xx < cs->w; xx++) {
          uint32_t g = lerp_col(st->g1 | 0xff000000u, st->g2 | 0xff000000u, cs->w > 1 ? (float)xx / (cs->w - 1) : 0);
          row[xx * 4] = (g >> 16) & 255; row[xx * 4 + 1] = (g >> 8) & 255; row[xx * 4 + 2] = g & 255; } }
        SDL_UnlockSurface(cs);
        SDL_FreeSurface(ts); ts = cs;
      }
    }
    SDL_SetSurfaceBlendMode(ts, SDL_BLENDMODE_BLEND);
    SDL_Rect d = { x, y, 0, 0 };
    SDL_BlitSurface(ts, NULL, dst, &d);
    SDL_FreeSurface(ts);
  }
  TTF_SetFontStyle(f, TTF_STYLE_NORMAL);
}

/* POST_COLORS from the website: background (solid or theme index >= 12) and text colour */
static const uint32_t PC_BG[12] = { 0, 0xfff3a8, 0xffd0e1, 0xcdf5d4, 0xcfeaff, 0xe3d6ff, 0xffd9a8, 0x2b2d3a, 0xe5322d, 0x5ac800, 0x1b8fd3, 0x234a3a };
static const uint32_t PC_FG[20] = { 0x323232, 0x3a3300, 0x4a1030, 0x0d3b1a, 0x0b2e4f, 0x2a1458, 0x4a2300, 0xf4f4f8, 0xffffff, 0xffffff, 0xffffff, 0xf1f1e6,
  0xffffff, 0x0b2a45, 0x0b3a14, 0xffffff, 0xffffff, 0xe8f4ff, 0x073a63, 0x4a3300 };
static const int PSIZE[3] = { 14, 17, 24 };

static void theme_bg(Cv *c, int theme, float x, float y, float w, float h, float r) {
  Stop s[4]; int n = 0; float deg = 180;
  switch (theme) {
  case 12: s[0] = (Stop){ 0, 0xff0b74c9 }; s[1] = (Stop){ .35f, 0xff2fa6ee }; s[2] = (Stop){ .75f, 0xff86d6ff }; s[3] = (Stop){ 1, 0xffd6f5ff }; n = 4; break;
  case 13: s[0] = (Stop){ 0, 0xebffffff }; s[1] = (Stop){ 1, 0xccdef0fc }; n = 2; break;
  case 14: s[0] = (Stop){ 0, 0xff3fae4a }; s[1] = (Stop){ .55f, 0xff8fe06a }; s[2] = (Stop){ 1, 0xffd9f7a8 }; n = 3; break;
  case 15: s[0] = (Stop){ 0, 0xff7cc4f5 }; s[1] = (Stop){ .5f, 0xff3b9be0 }; s[2] = (Stop){ .51f, 0xff1f80cc }; s[3] = (Stop){ 1, 0xff3fa2e6 }; n = 4; break;
  case 16: s[0] = (Stop){ 0, 0xffff9a3c }; s[1] = (Stop){ .6f, 0xffff5f8a }; s[2] = (Stop){ 1, 0xffa04bd6 }; n = 3; deg = 160; break;
  case 17: s[0] = (Stop){ 0, 0xff0a1f4d }; s[1] = (Stop){ 1, 0xff1b4a8a }; n = 2; break;
  case 18: s[0] = (Stop){ 0, 0xffbfeaff }; s[1] = (Stop){ 1, 0xff6cc3f2 }; n = 2; break;
  default: s[0] = (Stop){ 0, 0xfffff7c2 }; s[1] = (Stop){ .6f, 0xffffe27a }; s[2] = (Stop){ 1, 0xffffd24d }; n = 3; break;
  }
  cv_rrect(c, x, y, w, h, r, s, n, deg);
  if (theme == 17) { /* stars */
    for (float yy = y; yy < y + h; yy += S14(60)) for (float xx = x; xx < x + w; xx += S14(60)) cv_circle(c, xx + S14(12), yy + S14(18), 1.6f, 0x99ffffff);
    for (float yy = y; yy < y + h; yy += S14(90)) for (float xx = x; xx < x + w; xx += S14(90)) cv_circle(c, xx + S14(63), yy + S14(54), 1.5f, 0x88ffffff);
  }
  if (theme == 18) { /* bubbles */
    float bx[3] = { .18f, .80f, .55f }, by[3] = { .70f, .30f, .82f }, br[3] = { 7, 10, 5 };
    for (int k = 0; k < 3; k++) { float R2 = S14(br[k]); cv_circle(c, x + w * bx[k], y + h * by[k], R2 * 2, 0x26ffffff); cv_circle(c, x + w * bx[k], y + h * by[k], R2, 0xbfffffff); }
  }
}

/* renders body text with post style; returns a texture (caller frees). width = available width */
static SDL_Texture *rich_render(const char *body, const char *style, int width, int unstyledPx, uint32_t ink, int maxH, int *outW, int *outH, int *clipped) {
  int bg = 0, font = 0, size = 1, align = 0, deco = 0, styled = 0;
  if (style && *style) {
    int a = -1, b2 = -1, c2 = -1, d = 0, e = 0;
    int n = sscanf(style, "%d.%d.%d.%1d%1d", &a, &b2, &c2, &d, &e);
    if (n >= 3 && a >= 0 && a <= 19 && b2 >= 0 && b2 < NPOSTFONTS && c2 >= 0 && c2 <= 2) {
      bg = a; font = b2; size = c2; if (n == 5) { align = d; deco = e; }
      styled = bg || font || size != 1 || align || deco;
    }
  }
  int basePx = styled ? S14(PSIZE[size]) : unstyledPx;
  int padX = styled && (bg || deco) ? S14(16) : 0, padY = styled && (bg || deco) ? S14(14) : 0;
  if (styled && !bg && !deco) { padX = 0; padY = S14(4); }
  int innerW = width - 2 * padX;
  uint32_t fg = bg ? PC_FG[bg] : ink;
  int theme = bg >= 12;
  int tshadow = bg == 12 || bg == 15 || bg == 16 || bg == 17;
  uint32_t shCol = bg == 14 ? 0x80ffffff : 0x8c001e50;
  if (bg == 14) tshadow = 1;

  /* split into blocks by lines */
  static RBlock blk[400]; int nb = 0;
  const char *p = body ? body : "";
  RStyle base; memset(&base, 0, sizeof base); base.font = font; base.em = 1;
  /* collect plain lines joined with \n into one text block, like the website */
  char *plainBuf = malloc(strlen(p) + 2); size_t plen = 0;
  #define FLUSH_PLAIN() do { if (plen && nb < 400) { blk[nb].kind = 0; memset(&blk[nb].runs, 0, sizeof(Runs)); inline_rich(&blk[nb].runs, plainBuf, plen, base, 0); nb++; } plen = 0; } while (0)
  while (*p && nb < 398) {
    const char *e = strchr(p, '\n'); size_t L = e ? (size_t)(e - p) : strlen(p);
    if (L > 0 && p[L - 1] == '\r') L--;
    if (L >= 2 && p[0] == '-' && p[1] == ' ' && L > 2) { FLUSH_PLAIN(); blk[nb].kind = 3; memset(&blk[nb].runs, 0, sizeof(Runs)); inline_rich(&blk[nb].runs, p + 2, L - 2, base, 0); nb++; }
    else if (L >= 2 && p[0] == '>' && p[1] == ' ') {
      FLUSH_PLAIN();
      if (nb && blk[nb - 1].kind == 2) { RStyle nl = base; runs_add(&blk[nb - 1].runs, "\n", 1, &nl); inline_rich(&blk[nb - 1].runs, p + 2, L - 2, base, 0); }
      else { blk[nb].kind = 2; memset(&blk[nb].runs, 0, sizeof(Runs)); inline_rich(&blk[nb].runs, p + 2, L - 2, base, 0); nb++; }
    }
    else if (L >= 3 && !strncmp(p, "---", 3) && strspn(p, "-") >= 3 && strspn(p + strspn(p, "-"), " \t") + strspn(p, "-") == L) { FLUSH_PLAIN(); blk[nb].kind = 4; memset(&blk[nb].runs, 0, sizeof(Runs)); nb++; }
    else if (L >= 3 && p[0] == '#' && p[1] == ' ') { FLUSH_PLAIN(); RStyle hs = base; hs.b = 1; hs.em = 1.35f; blk[nb].kind = 1; memset(&blk[nb].runs, 0, sizeof(Runs)); inline_rich(&blk[nb].runs, p + 2, L - 2, hs, 0); nb++; }
    else { if (plen) plainBuf[plen++] = '\n'; memcpy(plainBuf + plen, p, L); plen += L; }
    if (!e) break;
    p = e + 1;
  }
  FLUSH_PLAIN();
  free(plainBuf);

  /* measure + lay out lines: pieces = words / spaces / newlines */
  typedef struct { int x, y, blk; Piece pc; } Placed;
  int capP = 2048; Placed *pl = malloc(sizeof(Placed) * (size_t)capP); int npl = 0;
  typedef struct { int y, h, w, first, last, blk; } Line;
  static Line lines[1200]; int nl = 0;
  int y = 0;
  float lh = styled && (font == 7 || font == 20) ? 1.7f : 1.45f;
  for (int bi = 0; bi < nb; bi++) {
    RBlock *b = &blk[bi];
    int indent = b->kind == 3 ? (int)(basePx * 1.4f) : b->kind == 2 ? S14(16) : 0;
    int avail = innerW - indent - (b->kind == 2 ? S14(12) : 0);
    if (b->kind == 4) { if (nl < 1200) { lines[nl] = (Line){ y, S14(4) + S14(16), 0, npl, npl - 1, bi }; nl++; } y += S14(4) + S14(16); continue; }
    if (b->kind == 2) y += S14(6);
    int lineStart = npl, x = 0, curH = (int)(basePx * lh * (b->kind == 1 ? 1.35f : 1));
    int lineY = y;
    #define END_LINE() do { if (nl < 1200) { lines[nl].y = lineY; lines[nl].h = curH; lines[nl].w = x; lines[nl].first = lineStart; lines[nl].last = npl - 1; lines[nl].blk = bi; nl++; } y = lineY + curH; lineY = y; x = 0; lineStart = npl; curH = (int)(basePx * lh); } while (0)
    for (int ri = 0; ri < b->runs.n; ri++) {
      Run *r = &b->runs.r[ri];
      TTF_Font *f = style_font(&r->st, basePx);
      if (!f) continue;
      int fh = (int)(TTF_FontHeight(f) * (lh / 1.2f));
      const char *s = r->txt;
      while (*s) {
        if (*s == '\n') { if (fh > curH) curH = fh; END_LINE(); s++; continue; }
        size_t L; int space = *s == ' ';
        if (space) L = strspn(s, " "); else { L = 0; while (s[L] && s[L] != ' ' && s[L] != '\n') L++; }
        char tmp[512]; size_t cl = L < 511 ? L : 511; memcpy(tmp, s, cl); tmp[cl] = 0;
        int tw = 0, th = 0;
        TTF_SetFontStyle(f, (r->st.b && r->st.font ? TTF_STYLE_BOLD : 0) | (r->st.i ? TTF_STYLE_ITALIC : 0));
        TTF_SizeUTF8(f, tmp, &tw, &th);
        TTF_SetFontStyle(f, TTF_STYLE_NORMAL);
        if (!space && x + tw > avail && x > 0) { END_LINE(); }
        if (space && x == 0) { s += L; continue; }   /* no leading spaces on wrapped lines */
        /* very long word: hard-break by characters */
        if (!space && tw > avail) {
          size_t k = 0;
          while (k < cl) {
            size_t m = k; int ww = 0; char part[512];
            while (m < cl) {
              size_t nx = m + 1; while (nx < cl && ((unsigned char)tmp[nx] & 0xC0) == 0x80) nx++;
              memcpy(part, tmp + k, nx - k); part[nx - k] = 0; TTF_SizeUTF8(f, part, &ww, NULL);
              if (ww > avail - x && m > k) break;
              m = nx;
            }
            if (npl == capP) { capP *= 2; pl = realloc(pl, sizeof(Placed) * (size_t)capP); }
            memcpy(part, tmp + k, m - k); part[m - k] = 0; TTF_SizeUTF8(f, part, &ww, NULL);
            pl[npl].x = x + indent; pl[npl].blk = bi; pl[npl].pc.txt = s + k; pl[npl].pc.len = m - k; pl[npl].pc.st = r->st; pl[npl].pc.f = f; pl[npl].pc.w = ww; pl[npl].pc.h = TTF_FontHeight(f); pl[npl].pc.asc = TTF_FontAscent(f); pl[npl].pc.space = 0;
            npl++; x += ww; if (fh > curH) curH = fh;
            k = m;
            if (k < cl) END_LINE();
          }
          s += L; continue;
        }
        if (npl == capP) { capP *= 2; pl = realloc(pl, sizeof(Placed) * (size_t)capP); }
        pl[npl].x = x + indent; pl[npl].blk = bi; pl[npl].pc.txt = s; pl[npl].pc.len = cl; pl[npl].pc.st = r->st; pl[npl].pc.f = f; pl[npl].pc.w = tw; pl[npl].pc.h = TTF_FontHeight(f); pl[npl].pc.asc = TTF_FontAscent(f); pl[npl].pc.space = space;
        npl++;
        x += tw; if (fh > curH) curH = fh;
        s += L;
      }
    }
    END_LINE();
    if (b->kind == 2) y += S14(6);
    if (b->kind == 1) y += S14(3);
    #undef END_LINE
  }
  int textH = y;
  int fullH = textH + 2 * padY;
  int H = fullH;
  *clipped = 0;
  if (maxH > 0 && H > maxH) { H = maxH; *clipped = 1; }
  if (H < 1) H = 1;
  /* background canvas */
  Cv cv = cv_new(width + 8, H + 8);
  int ox = 4, oy = 4;
  if (styled && (bg || deco)) {
    float r = theme ? S14(12) : (deco == 3 ? 0 : S14(3));
    if (theme) cv_shadow(&cv, ox, oy + 3, width, H, r, 8, 0x38003264);
    if (deco == 2) cv_shadow(&cv, ox, oy, width, H, r, 14, 0xbf00aaff);
    if (deco == 1) { cv_shadow(&cv, ox, oy + 3, width, H, r, 10, 0x4d003264); cv_rrect1(&cv, ox - 1, oy - 1, width + 2, H + 2, r + 1, 0xff6bb7e6); cv_rrect1(&cv, ox, oy, width, H, r, 0xffffffff); }
    if (theme && deco != 1) cv_rrect1(&cv, ox, oy, width, H, r, 0xd9ffffff);
    float bw = deco == 1 ? S14(3) : theme ? 1 : 0;
    if (theme) theme_bg(&cv, bg, ox + bw, oy + bw, width - 2 * bw, H - 2 * bw, r - bw > 0 ? r - bw : 0);
    else if (bg) cv_rrect1(&cv, ox + bw, oy + bw, width - 2 * bw, H - 2 * bw, r, 0xff000000u | PC_BG[bg]);
    else if (deco == 2 || deco == 3) cv_rrect1(&cv, ox, oy, width, H, r, 0x00ffffff);
  }
  SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormatFrom(cv.p, cv.w, cv.h, 32, cv.w * 4, SDL_PIXELFORMAT_RGBA32);
  SDL_SetSurfaceBlendMode(dst, SDL_BLENDMODE_BLEND);
  /* block decorations (quote bar, rule) */
  for (int li = 0; li < nl; li++) {
    RBlock *b = &blk[lines[li].blk];
    int ly = oy + padY + lines[li].y;
    if (ly > cv.h) break;
    if (b->kind == 4) {
      for (int xx = 0; xx < innerW; xx++) { float t = (float)xx / innerW; uint32_t c = t < 0.5f ? lerp_col(0x0000a0ff, 0xff39b8ff, t * 2) : lerp_col(0xff8fff5a, 0x008cff5a, (t - 0.5f) * 2);
        for (int yy = 0; yy < S14(4); yy++) cv_blend(&cv, ox + padX + xx, ly + S14(8) + yy, c, 1); }
    }
    if (b->kind == 2) {
      int top = ly - (li == 0 || lines[li - 1].blk != lines[li].blk ? S14(6) : 0), bot = ly + lines[li].h + (li + 1 >= nl || lines[li + 1].blk != lines[li].blk ? S14(6) : 0);
      for (int yy = top; yy < bot; yy++) for (int xx = 0; xx < innerW; xx++) cv_blend(&cv, ox + padX + xx, yy, xx < S14(4) ? 0xff0a84ff : 0x1a0a84ff, 1);
    }
    if (b->kind == 3 && lines[li].first <= lines[li].last && (li == 0 || lines[li - 1].blk != lines[li].blk)) {
      cv_circle(&cv, ox + padX + basePx * 0.6f, ly + lines[li].h * 0.5f, basePx * 0.16f, 0xff000000u | fg);
    }
  }
  for (int li = 0; li < nl; li++) {
    int ly = oy + padY + lines[li].y;
    if (ly > cv.h) break;
    int shift = 0;
    if (align == 1) shift = (innerW - lines[li].w) / 2; else if (align == 2) shift = innerW - lines[li].w;
    /* baseline align pieces on the line */
    int maxAsc = 0;
    for (int k = lines[li].first; k <= lines[li].last; k++) if (pl[k].pc.asc > maxAsc) maxAsc = pl[k].pc.asc;
    int lineInner = 0; for (int k = lines[li].first; k <= lines[li].last; k++) if (pl[k].pc.h > lineInner) lineInner = pl[k].pc.h;
    int top = ly + (lines[li].h - lineInner) / 2;
    for (int k = lines[li].first; k <= lines[li].last; k++) {
      Piece *pc = &pl[k].pc;
      if (pc->space && !pc->st.u && !pc->st.s) continue;
      char tmp[512]; size_t cl = pc->len < 511 ? pc->len : 511; memcpy(tmp, pc->txt, cl); tmp[cl] = 0;
      blit_text(dst, pc->f, tmp, &pc->st, fg, ox + padX + pl[k].x + shift, top + maxAsc - pc->asc, tshadow, shCol);
    }
  }
  SDL_FreeSurface(dst);
  for (int i = 0; i < nb; i++) runs_free(&blk[i].runs);
  free(pl);
  SDL_Texture *t = cv_tex(&cv);
  *outW = cv.w; *outH = cv.h;
  cv_free(&cv);
  return t;
}
