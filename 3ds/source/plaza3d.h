/*
 * plaza3d.h - a small software 3D renderer for the Plaza (same scene as plaza3d.js on the website):
 * round island in the sea, plaza ring, fountain, pond, ring of buildings, trees, benches, lamps, clouds,
 * and walking characters with square avatar heads. Rendered into a streaming texture with a z-buffer.
 */
typedef struct { float x, y, z; } V3;
static V3 v3(float x, float y, float z) { V3 v = { x, y, z }; return v; }
static V3 vsub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 vadd(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 vmul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float vdot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 vcross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static V3 vnorm(V3 a) { float l = sqrtf(vdot(a, a)); return l > 1e-6f ? vmul(a, 1 / l) : a; }

typedef struct { V3 p[3]; float uv[3][2]; uint32_t col; int16_t tex; V3 n, cen; uint32_t sh[2]; } Tri;   /* tex = -1 none, else face texture id; n/cen/sh precomputed */
typedef struct { Tri *t; int n, cap; int pre; } Mesh;
static void mesh_add(Mesh *m, V3 a, V3 b, V3 c, uint32_t col) {
  if (m->n == m->cap) { m->cap = m->cap ? m->cap * 2 : 1024; m->t = realloc(m->t, sizeof(Tri) * (size_t)m->cap); }
  Tri *t = &m->t[m->n++]; t->p[0] = a; t->p[1] = b; t->p[2] = c; t->col = col; t->tex = -1;
}
static void mesh_quad(Mesh *m, V3 a, V3 b, V3 c, V3 d, uint32_t col) { mesh_add(m, a, b, c, col); mesh_add(m, a, c, d, col); }

/* transform helper: rotate around Y then translate */
typedef struct { float c, s; V3 o; float sc; } Xf;
static Xf xf(float x, float y, float z, float rotY, float sc) { Xf t; t.c = cosf(rotY); t.s = sinf(rotY); t.o = v3(x, y, z); t.sc = sc; return t; }
static V3 xp(const Xf *t, V3 p) { p = vmul(p, t->sc); return v3(t->o.x + p.x * t->c + p.z * t->s, t->o.y + p.y, t->o.z - p.x * t->s + p.z * t->c); }

static void prim_box(Mesh *m, const Xf *t, float w, float h, float d, uint32_t col) {
  float x = w / 2, z = d / 2, y = h / 2;
  V3 P[8] = { v3(-x, -y, -z), v3(x, -y, -z), v3(x, y, -z), v3(-x, y, -z), v3(-x, -y, z), v3(x, -y, z), v3(x, y, z), v3(-x, y, z) };
  for (int i = 0; i < 8; i++) P[i] = xp(t, P[i]);
  mesh_quad(m, P[4], P[5], P[6], P[7], col); mesh_quad(m, P[1], P[0], P[3], P[2], col);
  mesh_quad(m, P[0], P[4], P[7], P[3], col); mesh_quad(m, P[5], P[1], P[2], P[6], col);
  mesh_quad(m, P[3], P[7], P[6], P[2], col); mesh_quad(m, P[0], P[1], P[5], P[4], col);
}
/* cylinder / cone centred at the transform origin (y from -h/2 to h/2) */
static void prim_cyl(Mesh *m, const Xf *t, float rt, float rb, float h, int seg, uint32_t col, int caps) {
  for (int i = 0; i < seg; i++) {
    float a0 = i * 6.2831853f / seg, a1 = (i + 1) * 6.2831853f / seg;
    V3 b0 = xp(t, v3(sinf(a0) * rb, -h / 2, cosf(a0) * rb)), b1 = xp(t, v3(sinf(a1) * rb, -h / 2, cosf(a1) * rb));
    V3 t0 = xp(t, v3(sinf(a0) * rt, h / 2, cosf(a0) * rt)), t1 = xp(t, v3(sinf(a1) * rt, h / 2, cosf(a1) * rt));
    if (rt > 0.001f) mesh_quad(m, b0, b1, t1, t0, col); else mesh_add(m, b0, b1, t0, col);
    if (caps) {
      if (rt > 0.001f) mesh_add(m, xp(t, v3(0, h / 2, 0)), t0, t1, col);
      mesh_add(m, xp(t, v3(0, -h / 2, 0)), b1, b0, col);
    }
  }
}
static void prim_disc(Mesh *m, float cx, float y, float cz, float r, int seg, uint32_t col) {
  for (int i = 0; i < seg; i++) {
    float a0 = i * 6.2831853f / seg, a1 = (i + 1) * 6.2831853f / seg;
    mesh_add(m, v3(cx, y, cz), v3(cx + sinf(a0) * r, y, cz + cosf(a0) * r), v3(cx + sinf(a1) * r, y, cz + cosf(a1) * r), col);
  }
}
static void prim_ring(Mesh *m, float y, float r0, float r1, int seg, uint32_t col) {
  for (int i = 0; i < seg; i++) {
    float a0 = i * 6.2831853f / seg, a1 = (i + 1) * 6.2831853f / seg;
    mesh_quad(m, v3(sinf(a0) * r0, y, cosf(a0) * r0), v3(sinf(a0) * r1, y, cosf(a0) * r1), v3(sinf(a1) * r1, y, cosf(a1) * r1), v3(sinf(a1) * r0, y, cosf(a1) * r0), col);
  }
}
static void prim_sphere(Mesh *m, const Xf *t, float r, int seg, int rings, uint32_t col) {
  for (int j = 0; j < rings; j++) for (int i = 0; i < seg; i++) {
    float p0 = 3.14159265f * j / rings, p1 = 3.14159265f * (j + 1) / rings, a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
    V3 A = xp(t, v3(sinf(p0) * sinf(a0) * r, cosf(p0) * r, sinf(p0) * cosf(a0) * r)), B = xp(t, v3(sinf(p0) * sinf(a1) * r, cosf(p0) * r, sinf(p0) * cosf(a1) * r));
    V3 C = xp(t, v3(sinf(p1) * sinf(a1) * r, cosf(p1) * r, sinf(p1) * cosf(a1) * r)), D = xp(t, v3(sinf(p1) * sinf(a0) * r, cosf(p1) * r, sinf(p1) * cosf(a0) * r));
    mesh_quad(m, A, B, C, D, col);
  }
}

static uint32_t p3_rng_state = 7;
static float p3_rnd(void) { /* mulberry32 like the website (seed 7) */
  uint32_t t = (p3_rng_state += 0x6d2b79f5u);
  t = (t ^ (t >> 15)) * (t | 1u); t ^= t + (t ^ (t >> 7)) * (t | 61u);
  return (float)((t ^ (t >> 14)) & 0xffffffu) / 16777216.0f;
}

typedef struct { float x, z, r; } Obst;
static Mesh p3world;
static Obst p3obs[200]; static int p3nobs;
typedef struct { V3 pos; float speed, phase, wait, heading; float wx, wz; } Walker;
static V3 p3clouds[8]; static Mesh p3cloud;

static void p3_build_world(void) {
  if (p3world.n) return;
  Mesh *m = &p3world;
  p3_rng_state = 7;
  p3nobs = 0;
  p3obs[p3nobs++] = (Obst){ 0, 0, 4.2f }; p3obs[p3nobs++] = (Obst){ -28, 12, 9 };
  /* the sea is drawn as the background colour below the horizon (much faster than a huge disc) */
  { Xf t = xf(0, -4.5f, 0, 0, 1); prim_cyl(m, &t, 58, 50, 9, 32, 0x9b7a52, 0); }
  { Xf t = xf(0, -0.55f, 0, 0, 1); prim_cyl(m, &t, 58.6f, 58.6f, 1.2f, 32, 0x68b83a, 0); }
  prim_disc(m, 0, 0.02f, 0, 58.6f, 32, 0x79c944);
  prim_ring(m, 0.04f, 40, 43.5f, 32, 0xe3dfd2);
  prim_disc(m, 0, 0.12f, 0, 15.6f, 20, 0xcfcabb);
  prim_disc(m, 0, 0.13f, 0, 15, 20, 0xebe8de);
  for (int i = 0; i < 4; i++) {
    float a = i * 1.5707963f + 0.785398f, len = 28;
    Xf t = xf(sinf(a) * (len / 2 + 14), 0.05f, cosf(a) * (len / 2 + 14), a, 1);
    prim_box(m, &t, 5, 0.1f, len, 0xdedacd);
  }
  /* fountain */
  { Xf t = xf(0, 0.4f, 0, 0, 1); prim_cyl(m, &t, 3.6f, 3.8f, 0.8f, 16, 0xd7d3c6, 1); }
  { Xf t = xf(0, 0.41f, 0, 0, 1); prim_cyl(m, &t, 3.1f, 3.1f, 0.82f, 16, 0x58b9ea, 1); }
  { Xf t = xf(0, 1.5f, 0, 0, 1); prim_cyl(m, &t, 0.45f, 0.6f, 2.2f, 8, 0xd7d3c6, 1); }
  { Xf t = xf(0, 2.5f, 0, 0, 1); prim_cyl(m, &t, 1.4f, 0.9f, 0.4f, 12, 0xd7d3c6, 1); }
  { Xf t = xf(0, 2.52f, 0, 0, 1); prim_cyl(m, &t, 1.15f, 1.15f, 0.42f, 12, 0x58b9ea, 1); }
  /* pond */
  { Xf t = xf(-28, 0.05f, 12, 0, 1); prim_cyl(m, &t, 9.4f, 9.4f, 0.12f, 24, 0xcfc7a8, 1); }
  { Xf t = xf(-28, 0.08f, 12, 0, 1); prim_cyl(m, &t, 8.4f, 8.4f, 0.16f, 24, 0x4eb4e6, 1); }
  /* buildings */
  static const uint32_t pal[5] = { 0xf4f7fa, 0xdcecf6, 0xc5e1f2, 0xf0ece2, 0xe4eef5 };
  for (int i = 0; i < 26; i++) {
    float a = (i / 26.0f) * 6.2831853f + p3_rnd() * 0.05f, dist = 50 + p3_rnd() * 2.5f, w = 6.5f + p3_rnd() * 2.5f, d = 6 + p3_rnd() * 3, h = 5 + p3_rnd() * 12;
    float x = sinf(a) * dist, z = cosf(a) * dist; uint32_t col = pal[(int)(p3_rnd() * 5) % 5];
    Xf t = xf(x, h / 2, z, a, 1); prim_box(m, &t, w, h, d, col);
    Xf tb = xf(x, h * 0.5f, z, a, 1); prim_box(m, &tb, w + 0.1f, h * 0.62f, d + 0.1f, 0x6fb8e0);
    Xf tr = xf(x, h + 0.25f, z, a, 1); prim_box(m, &tr, w + 0.6f, 0.5f, d + 0.6f, 0xffffff);
  }
  /* trees */
  int nt = 0;
  for (int i = 0; i < 400 && nt < 36; i++) {   /* fewer trees than the Wii U: the 3DS draws everything on the CPU */
    float a = p3_rnd() * 6.2831853f, dist = 17 + p3_rnd() * 30, x = sinf(a) * dist, z = cosf(a) * dist;
    int onPath = 0;
    for (int k = 0; k < 4; k++) { float pa = k * 1.5707963f + 0.785398f, dx = sinf(pa), dz = cosf(pa); if (fabsf(x * dz - z * dx) < 5 && x * dx + z * dz > 0) onPath = 1; }
    if (onPath || hypotf(x + 28, z - 12) < 11 || (dist > 38.5f && dist < 45)) continue;
    float s = 0.8f + p3_rnd() * 0.7f, c = p3_rnd();
    static const uint32_t tint[4] = { 0x3f9f2e, 0x4cb336, 0x5bc23f, 0x38912c };
    uint32_t tc = tint[(int)(c * 4) % 4];
    uint32_t tc2 = lerpc(tc, 0xffffff, 0.08f) & 0xffffff;
    Xf t1 = xf(x, 1.1f * s, z, 0, s); prim_cyl(m, &t1, 0.28f, 0.4f, 2.2f, 4, 0x8a5a2b, 0);
    Xf t2 = xf(x, 3.6f * s, z, 0, s); prim_cyl(m, &t2, 0, 2.2f, 3.4f, 6, tc, 0);
    Xf t3 = xf(x, 5.4f * s, z, 0, s); prim_cyl(m, &t3, 0, 1.7f, 3.0f, 6, tc2, 0);
    if (p3nobs < 200) p3obs[p3nobs++] = (Obst){ x, z, 1.3f * s };
    nt++;
  }
  /* benches + lamps */
  for (int i = 0; i < 8; i++) {
    float a = (i / 8.0f) * 6.2831853f + 0.2f, x = sinf(a) * 12.5f, z = cosf(a) * 12.5f;
    Xf ts = xf(x, 0.55f, z, a, 1); prim_box(m, &ts, 2.4f, 0.2f, 0.8f, 0xb5793a);
    Xf tb = xf(x + sinf(a) * 0.4f, 0.95f, z + cosf(a) * 0.4f, a, 1); prim_box(m, &tb, 2.4f, 0.7f, 0.14f, 0xb5793a);
    if (p3nobs < 200) p3obs[p3nobs++] = (Obst){ x, z, 1.2f };
    float la = a + 0.3926991f, lx = sinf(la) * 14.2f, lz = cosf(la) * 14.2f;
    Xf tl = xf(lx, 2.1f, lz, 0, 1); prim_cyl(m, &tl, 0.1f, 0.14f, 4.2f, 6, 0x4a4f5c, 0);
    Xf tg = xf(lx, 4.4f, lz, 0, 1); prim_sphere(m, &tg, 0.36f, 4, 2, 0xfff4c2);
    if (p3nobs < 200) p3obs[p3nobs++] = (Obst){ lx, lz, 0.4f };
  }
  /* clouds: one shared mesh, positions animated */
  for (int k = 0; k < 4; k++) { Xf t = xf(k * 4 - 8, 0, 0, 0, 1); t.sc = 1; prim_sphere(&p3cloud, &t, 3 + (k % 2) * 1.5f, 5, 3, 0xffffff); }
  for (int i = 0; i < 7; i++) p3clouds[i] = v3((p3_rnd() - 0.5f) * 220, 32 + p3_rnd() * 12, (p3_rnd() - 0.5f) * 220);
}

static int p3_blocked(float x, float z, float fx, float fz) {
  for (int i = 0; i < p3nobs; i++) if (hypotf(x - p3obs[i].x, z - p3obs[i].z) < p3obs[i].r + 0.8f) return 1;
  float dx = x - fx, dz = z - fz, l2 = dx * dx + dz * dz; if (l2 < 1e-6f) l2 = 1;
  for (int i = 0; i < p3nobs; i++) {
    float t = ((p3obs[i].x - fx) * dx + (p3obs[i].z - fz) * dz) / l2; if (t < 0) t = 0; if (t > 1) t = 1;
    if (hypotf(fx + dx * t - p3obs[i].x, fz + dz * t - p3obs[i].z) < p3obs[i].r + 0.7f) return 1;
  }
  return 0;
}
static void p3_pick_wp(Walker *w) {
  for (int tries = 0; tries < 30; tries++) {
    float a = (rand() % 10000) / 10000.0f * 6.2831853f, d = 5 + (rand() % 10000) / 10000.0f * 38, x = sinf(a) * d, z = cosf(a) * d;
    if (!p3_blocked(x, z, w->pos.x, w->pos.z)) { w->wx = x; w->wz = z; return; }
  }
  w->wx = w->pos.x * 0.9f; w->wz = w->pos.z * 0.9f;
}

/* character pieces (like makeCharacter in plaza3d.js, simplified to boxes and cylinders) */
static const uint32_t SHIRT_COLS[12] = { 0xe5322d, 0xf09a1a, 0xf5d90a, 0x5ac800, 0x1b8fd3, 0x4b2d7f, 0xff77a8, 0x00b7a8, 0x8d5524, 0x222034, 0xffffff, 0x8a8a8a };
static void p3_character(Mesh *m, V3 pos, float heading, float phase, float amount, uint32_t shirt, uint32_t pants, int faceTex, float sc) {
  float s = sinf(phase) * 0.75f * amount;
  Xf base = xf(pos.x, pos.y, pos.z, heading, sc);
  /* legs swing around the hip (x axis): approximate by offsetting foot forward/back */
  for (int side = -1; side <= 1; side += 2) {
    float sw = side < 0 ? s : -s;
    for (int k = 0; k < 2; k++) {
      float y = 0.9f - 0.42f - k * 0.3f;
      Xf t = base; V3 c = xp(&base, v3(side * 0.2f, y, sinf(sw) * (0.42f + k * 0.3f)));
      t.o = c; prim_box(m, &t, 0.3f, 0.5f, 0.3f, pants);
    }
    Xf tf = base; tf.o = xp(&base, v3(side * 0.2f, 0.12f, 0.07f + sinf(sw) * 0.85f)); prim_box(m, &tf, 0.36f, 0.24f, 0.55f, 0x23252e);
  }
  { Xf t = base; t.o = xp(&base, v3(0, 0.98f, 0)); prim_box(m, &t, 0.8f, 0.5f, 0.55f, pants); }
  { Xf t = base; t.o = xp(&base, v3(0, 1.4f, 0)); prim_cyl(m, &t, 0.43f, 0.4f, 0.9f, 8, shirt, 1); }
  { Xf t = base; t.o = xp(&base, v3(0, 1.86f, 0)); prim_cyl(m, &t, 0.2f, 0.26f, 0.22f, 8, lerpc(shirt, 0x000000, 0.2f) & 0xffffff, 1); }
  for (int side = -1; side <= 1; side += 2) {
    float sw = side < 0 ? -s * 0.8f : s * 0.8f;
    Xf t = base; t.o = xp(&base, v3(side * 0.66f, 1.3f, sinf(sw) * 0.35f)); prim_box(m, &t, 0.22f, 0.8f, 0.22f, shirt);
  }
  /* head cube with the avatar face on the front */
  float hy = 2.42f + fabsf(sinf(phase)) * 0.07f * amount;
  Xf th = base; th.o = xp(&base, v3(0, hy, 0));
  int first = m->n;
  prim_box(m, &th, 1, 1, 1, 0xe9e9ef);
  /* prim_box order: front(+z) 2 tris, back, left, right, top, bottom */
  Tri *f0 = &m->t[first], *f1 = &m->t[first + 1];
  /* front quad P4 P5 P6 P7 -> uv */
  float uv0[3][2] = { { 0, 1 }, { 1, 1 }, { 1, 0 } }, uv1[3][2] = { { 0, 1 }, { 1, 0 }, { 0, 0 } };
  memcpy(f0->uv, uv0, sizeof uv0); memcpy(f1->uv, uv1, sizeof uv1);
  f0->tex = f1->tex = (int16_t)faceTex;
  m->t[first + 2].col = m->t[first + 3].col = 0xe9e9ef;
  m->t[first + 4].col = m->t[first + 5].col = 0xc9c9d2;
  m->t[first + 6].col = m->t[first + 7].col = 0xc9c9d2;
  m->t[first + 8].col = m->t[first + 9].col = 0xffffff;
}

/* ---------- rasterizer ---------- */
typedef struct { int w, h; uint32_t *px; float *zb; } P3Buf;
typedef struct { int w, h; uint32_t *px; } P3Tex;   /* face textures, packed ARGB8888 */
static P3Tex p3tex[64]; static int p3ntex;
static V3 p3eye, p3fw, p3rt, p3up;
static float p3f, p3aspect;
static const V3 P3SUN = { 0.4655f, 0.8095f, 0.3036f };   /* normalised (46,80,30) */

static uint32_t p3_shade(uint32_t col, V3 n, float fogt) {
  if (vdot(n, vsub(p3eye, v3(0, 0, 0))) < 0 && 0) n = vmul(n, -1);
  float ndl = vdot(n, P3SUN); if (ndl < 0) ndl = -ndl * 0.25f;
  float hemi = 0.5f + 0.5f * n.y; if (hemi < 0) hemi = 0;
  float r = (col >> 16) & 255, g = (col >> 8) & 255, b = col & 255;
  /* hemisphere sky white / ground green + warm sun */
  float hr = 0.62f * hemi + 0.42f * (1 - hemi) * (0x8f / 255.0f) / 0.56f, hg = 0.62f * hemi + 0.42f * (1 - hemi) * (0xbf / 255.0f) / 0.75f, hb = 0.62f * hemi + 0.42f * (1 - hemi) * (0x6a / 255.0f) / 0.42f;
  float L = 0.55f * ndl;
  r = r * (hr * 0.85f + L * 1.0f); g = g * (hg * 0.85f + L * 0.97f); b = b * (hb * 0.85f + L * 0.91f);
  if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
  if (fogt > 0) { r += (0xcf - r) * fogt; g += (0xee - g) * fogt; b += (0xff - b) * fogt; }
  return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;  /* packed ARGB8888 (endian safe) */
}

typedef struct { float x, y, z, w, u, v; } SV;   /* screen vertex: x,y pixels, w = 1/zc */
/* scanline rasterizer: walks only the pixels inside the triangle, depth (1/z) and uv/w from plane equations */
static void p3_raster(P3Buf *b, SV *a, SV *bb, SV *c, uint32_t col, int tex) {
  float det = (bb->x - a->x) * (c->y - a->y) - (c->x - a->x) * (bb->y - a->y);
  if (fabsf(det) < 1e-6f) return;
  float id = 1.0f / det;
  /* plane: v = A*x + B*y + C */
  #define PLANE(va, vb, vc, A, B, C) do { float d1 = (vb) - (va), d2 = (vc) - (va); A = (d1 * (c->y - a->y) - d2 * (bb->y - a->y)) * id; B = (d2 * (bb->x - a->x) - d1 * (c->x - a->x)) * id; C = (va) - A * a->x - B * a->y; } while (0)
  float wA, wB, wC; PLANE(a->w, bb->w, c->w, wA, wB, wC);
  P3Tex *T = tex >= 0 && tex < p3ntex ? &p3tex[tex] : NULL;
  float uA = 0, uB = 0, uC = 0, vA = 0, vB = 0, vC = 0;
  if (T) { PLANE(a->u * a->w, bb->u * bb->w, c->u * c->w, uA, uB, uC); PLANE(a->v * a->w, bb->v * bb->w, c->v * c->w, vA, vB, vC); }
  #undef PLANE
  SV *v[3] = { a, bb, c };
  float miny = fminf(a->y, fminf(bb->y, c->y)), maxy = fmaxf(a->y, fmaxf(bb->y, c->y));
  int y0 = (int)ceilf(miny - 0.5f), y1 = (int)floorf(maxy - 0.5f);
  if (y0 < 0) y0 = 0; if (y1 >= b->h) y1 = b->h - 1;
  for (int y = y0; y <= y1; y++) {
    float py = y + 0.5f, xl = 1e9f, xr = -1e9f;
    for (int e = 0; e < 3; e++) {
      SV *p = v[e], *q = v[(e + 1) % 3];
      if ((p->y <= py && q->y > py) || (q->y <= py && p->y > py)) {
        float x = p->x + (py - p->y) * (q->x - p->x) / (q->y - p->y);
        if (x < xl) xl = x; if (x > xr) xr = x;
      }
    }
    if (xl > xr) continue;
    int x0 = (int)ceilf(xl - 0.5f), x1 = (int)floorf(xr - 0.5f);
    if (x0 < 0) x0 = 0; if (x1 >= b->w) x1 = b->w - 1;
    if (x0 > x1) continue;
    float iw = wA * (x0 + 0.5f) + wB * py + wC;
    float *zb = b->zb + y * b->w; uint32_t *px = b->px + y * b->w;
    if (!T) {
      for (int x = x0; x <= x1; x++, iw += wA) if (iw > zb[x]) { zb[x] = iw; px[x] = col; }
    } else {
      float uw = uA * (x0 + 0.5f) + uB * py + uC, vw = vA * (x0 + 0.5f) + vB * py + vC;
      for (int x = x0; x <= x1; x++, iw += wA, uw += uA, vw += vA) {
        if (iw <= zb[x]) continue;
        zb[x] = iw;
        int tx = (int)(uw / iw * T->w), ty = (int)(vw / iw * T->h);
        if (tx < 0) tx = 0; if (ty < 0) ty = 0; if (tx >= T->w) tx = T->w - 1; if (ty >= T->h) ty = T->h - 1;
        px[x] = T->px[ty * T->w + tx];
      }
    }
  }
}

static void p3_set_camera(float az, float polar, float dist, float aspect) {
  V3 target = v3(0, 2, 0);
  p3eye = v3(target.x + dist * sinf(polar) * sinf(az), target.y + dist * cosf(polar), target.z + dist * sinf(polar) * cosf(az));
  p3fw = vnorm(vsub(target, p3eye));
  p3rt = vnorm(vcross(p3fw, v3(0, 1, 0)));
  p3up = vcross(p3rt, p3fw);
  p3f = 1.0f / tanf(46.0f * 3.14159265f / 360.0f);
  p3aspect = aspect;
}
/* world -> camera space (x right, y up, z forward distance) */
static V3 p3_cam(V3 p) { V3 d = vsub(p, p3eye); return v3(vdot(d, p3rt), vdot(d, p3up), vdot(d, p3fw)); }
static int p3_project(V3 c, int W, int H, float *sx, float *sy) {
  if (c.z < 0.5f) return 0;
  *sx = (c.x * p3f / p3aspect / c.z * 0.5f + 0.5f) * W;
  *sy = (0.5f - c.y * p3f / c.z * 0.5f) * H;
  return 1;
}

static void p3_precompute(Mesh *m) {
  for (int i = 0; i < m->n; i++) {
    Tri *t = &m->t[i];
    t->n = vnorm(vcross(vsub(t->p[1], t->p[0]), vsub(t->p[2], t->p[0])));
    t->cen = vmul(vadd(vadd(t->p[0], t->p[1]), t->p[2]), 1.0f / 3);
    t->sh[0] = p3_shade(t->col, t->n, 0); t->sh[1] = p3_shade(t->col, vmul(t->n, -1), 0);
  }
  m->pre = 1;
}
static uint32_t p3_fog(uint32_t c, float fogt) {
  if (fogt <= 0) return c;
  int r = (c >> 16) & 255, g = (c >> 8) & 255, bl = c & 255;
  r += (int)((0xcf - r) * fogt); g += (int)((0xee - g) * fogt); bl += (int)((0xff - bl) * fogt);
  return 0xff000000u | (uint32_t)(r << 16 | g << 8 | bl);
}
static void p3_draw_mesh(P3Buf *b, const Mesh *m, V3 offset) {
  const float NEAR = 0.5f;
  int pre = m->pre && offset.x == 0 && offset.y == 0 && offset.z == 0;
  for (int i = 0; i < m->n; i++) {
    const Tri *t = &m->t[i];
    V3 w[3] = { vadd(t->p[0], offset), vadd(t->p[1], offset), vadd(t->p[2], offset) };
    uint32_t col;
    if (pre) {
      V3 d = vsub(p3eye, t->cen);
      float d2 = vdot(d, d), fogt = d2 > 160 * 160 ? (sqrtf(d2) - 160) / 320 : 0; if (fogt > 1) fogt = 1;
      col = p3_fog(t->sh[vdot(t->n, d) < 0], fogt);
      /* quick reject: behind the camera */
      if (vdot(vsub(t->cen, p3eye), p3fw) < -12) continue;
    } else {
      V3 n = vnorm(vcross(vsub(w[1], w[0]), vsub(w[2], w[0])));
      V3 cen = vmul(vadd(vadd(w[0], w[1]), w[2]), 1.0f / 3);
      if (vdot(n, vsub(p3eye, cen)) < 0) n = vmul(n, -1);
      float dist = sqrtf(vdot(vsub(cen, p3eye), vsub(cen, p3eye)));
      float fogt = (dist - 160) / 320; if (fogt < 0) fogt = 0; if (fogt > 1) fogt = 1;
      col = p3_shade(t->col, n, fogt);
    }
    V3 c[3]; for (int k = 0; k < 3; k++) c[k] = p3_cam(w[k]);
    /* clip against the near plane (Sutherland-Hodgman, up to 4 vertices) */
    V3 poly[4]; float puv[4][2]; int np = 0;
    for (int k = 0; k < 3; k++) {
      int j = (k + 1) % 3;
      V3 A = c[k], B = c[j];
      int ain = A.z >= NEAR, bin = B.z >= NEAR;
      if (ain) { poly[np] = A; puv[np][0] = t->uv[k][0]; puv[np][1] = t->uv[k][1]; np++; }
      if (ain != bin && np < 4) {
        float s = (NEAR - A.z) / (B.z - A.z);
        poly[np] = vadd(A, vmul(vsub(B, A), s));
        puv[np][0] = t->uv[k][0] + (t->uv[j][0] - t->uv[k][0]) * s; puv[np][1] = t->uv[k][1] + (t->uv[j][1] - t->uv[k][1]) * s;
        np++;
      }
    }
    if (np < 3) continue;
    SV sv[4];
    for (int k = 0; k < np; k++) {
      p3_project(poly[k], b->w, b->h, &sv[k].x, &sv[k].y);
      sv[k].w = 1.0f / poly[k].z; sv[k].u = puv[k][0]; sv[k].v = puv[k][1];
    }
    p3_raster(b, &sv[0], &sv[1], &sv[2], col, t->tex);
    if (np == 4) p3_raster(b, &sv[0], &sv[2], &sv[3], col, t->tex);
  }
}
