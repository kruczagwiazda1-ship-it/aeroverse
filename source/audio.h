/*
 * audio.h - original background music + soft UI sounds, synthesised live (no music files needed).
 * "Plaza Bossa": a bright, bouncy bossa-nova loop (124 bpm, F major) in the spirit of console menu music:
 * vibraphone lead, electric-piano comping, round plucked bass, rim clicks and shaker, stereo echo.
 * The second pass of the loop swaps the vibraphone for a soft flute with a vibraphone counter-line.
 */
#define AU_RATE 32000
static SDL_AudioDeviceID auDev;
static volatile int auMusic = 1;          /* music on/off (saved on the SD card) */
static volatile int auSfx;                /* pending sound effect: 1 pop, 2 yeah, 3 tab */
static float sinT[4096];
static inline float fsin(float ph) { return sinT[(int)(ph * 4096.0f) & 4095]; }   /* ph in cycles */
static float mtof(float n) { return 440.0f * powf(2.0f, (n - 69) / 12.0f); }

/* chords per half bar: bass root, then 4 voicing notes */
enum { FMA7, DM7, GM7, C7, AM7, D7, BBMA7, BBM6, C7SUS, NCH };
static const int CHV[NCH][5] = {
  { 41, 57, 60, 64, 67 }, { 38, 53, 57, 60, 64 }, { 43, 53, 57, 58, 62 }, { 36, 52, 57, 58, 62 }, { 45, 55, 60, 64, 67 },
  { 38, 54, 57, 60, 64 }, { 46, 57, 60, 62, 65 }, { 46, 55, 61, 65, 67 }, { 36, 53, 58, 62, 64 } };
static const unsigned char PROG[32] = {   /* 16 bars x 2 halves */
  FMA7, FMA7, DM7, DM7, GM7, GM7, C7, C7, FMA7, FMA7, AM7, D7, GM7, GM7, C7, C7,
  BBMA7, BBMA7, BBM6, BBM6, AM7, AM7, D7, D7, GM7, GM7, C7, C7, FMA7, FMA7, C7SUS, C7 };
/* melody: 16 bars x 8 eighth notes; MIDI note, 0 rest, 1 hold */
static const unsigned char MEL[16][8] = {
  { 81, 1, 84, 0, 88, 1, 86, 84 }, { 81, 1, 1, 0, 77, 81, 84, 1 }, { 86, 1, 84, 82, 81, 1, 79, 1 }, { 76, 79, 82, 1, 81, 79, 1, 0 },
  { 81, 0, 84, 0, 89, 1, 88, 84 }, { 88, 1, 84, 81, 78, 81, 84, 86 }, { 82, 1, 1, 81, 79, 1, 86, 1 }, { 84, 1, 82, 79, 76, 1, 0, 0 },
  { 86, 1, 89, 1, 88, 1, 86, 1 }, { 85, 1, 82, 1, 79, 1, 77, 1 }, { 88, 91, 88, 84, 1, 81, 1, 0 }, { 78, 81, 84, 86, 1, 90, 1, 0 },
  { 91, 1, 89, 86, 1, 82, 1, 0 }, { 88, 1, 86, 84, 82, 1, 79, 1 }, { 81, 1, 1, 1, 1, 0, 77, 79 }, { 81, 84, 0, 82, 1, 79, 76, 0 } };
/* counter line for the second pass (one note per half bar, vibraphone, an octave lower) */
static const unsigned char CNT[32] = {
  64, 67, 65, 69, 70, 69, 67, 64, 64, 72, 72, 66, 70, 74, 72, 70,
  69, 72, 73, 70, 67, 72, 69, 72, 70, 74, 76, 72, 69, 72, 70, 67 };

enum { VT_VIBE, VT_EP, VT_BASS, VT_FLUTE };
typedef struct { float f, ph, ph2, env, amp, pan, att, dec, rel; int type, gate, age; } AVoice;
#define NVOICE 20
static AVoice vo[NVOICE];
static float kickPh, kickEnv, rimEnv, rimPh, hatEnv, hatAcc, hatLp, hatHp, sfxPh, sfxEnv, sfxF, sfxF2;
static uint32_t auNoise = 12345;
static long auSample;
static int auTick = -1;
#define ECHO_LEN 11616               /* 3 sixteenths at 124 bpm, ping-pong */
static float echoL[ECHO_LEN], echoR[ECHO_LEN];
static int echoPos;
static float lpL, lpR;

static inline float au_noise(void) { auNoise = auNoise * 1664525u + 1013904223u; return ((auNoise >> 9) & 0xffff) / 32768.0f - 1; }
static void au_note(int type, float note, float amp, int gateSamples, float pan) {
  int k = 0; float mn = 9;
  for (int i = 0; i < NVOICE; i++) if (vo[i].env < mn) { mn = vo[i].env; k = i; }
  AVoice *v = &vo[k]; memset(v, 0, sizeof *v);
  v->type = type; v->f = mtof(note); v->amp = amp; v->gate = gateSamples; v->pan = pan; v->env = 0.0001f;
  switch (type) {
    case VT_VIBE:  v->att = 0.5f;   v->dec = 0.99997f; v->rel = 0.9996f; break;
    case VT_EP:    v->att = 0.3f;   v->dec = 0.99993f; v->rel = 0.9992f; break;
    case VT_BASS:  v->att = 0.2f;   v->dec = 0.99990f; v->rel = 0.9985f; break;
    default:       v->att = 0.004f; v->dec = 0.99999f; v->rel = 0.9994f; break;
  }
}
static int au_hold_len(int bar, int e) { int n = 1; while (e + n < 8 && MEL[bar][e + n] == 1) n++; return n; }

static void au_tick(int t) {               /* t = sixteenth-note index */
  const int s16 = (int)(AU_RATE * 60.0f / 124.0f / 4.0f);
  int bar = (t / 16) % 16, pass = (t / 256) % 2, in16 = t % 16, half = (t / 8) % 32;
  const int *ch = CHV[PROG[half]];
  int even = !(in16 & 1), e = in16 / 2;
  /* melody */
  if (even) {
    int n = MEL[bar][e];
    if (n > 1) {
      int len = au_hold_len(bar, e) * 2 * s16;
      if (pass == 0) au_note(VT_VIBE, n, 0.30f, len, 0.15f);
      else { au_note(VT_FLUTE, n, 0.22f, len - s16 / 3, 0.1f); }
    }
  }
  if (pass == 1 && in16 % 8 == 2) au_note(VT_VIBE, CNT[half], 0.13f, 4 * s16, -0.45f);
  /* electric piano comping, bossa rhythm over two bars */
  static const unsigned short COMP[2] = { (1 << 0) | (1 << 6) | (1 << 12), (1 << 4) | (1 << 10) };
  if (COMP[bar & 1] & (1 << in16))
    for (int k = 1; k < 5; k++) au_note(VT_EP, ch[k], 0.085f, 3 * s16, k & 1 ? -0.35f : 0.35f);
  /* bass: root - root - fifth - fifth */
  if (in16 == 0 || in16 == 8) au_note(VT_BASS, ch[0], 0.42f, 5 * s16, 0);
  if (in16 == 6 || in16 == 14) au_note(VT_BASS, ch[0] + 7, 0.30f, 2 * s16, 0);
  if (in16 == 12 && (bar & 1)) au_note(VT_BASS, ch[0] + 12, 0.22f, s16, 0);
  /* drums */
  if (in16 == 0 || in16 == 8) kickEnv = 1, kickPh = 0;
  if (in16 == 6 || in16 == 14) kickEnv = 0.45f, kickPh = 0;
  static const unsigned short CLAVE[2] = { (1 << 0) | (1 << 6) | (1 << 12), (1 << 4) | (1 << 8) };
  if (CLAVE[bar & 1] & (1 << in16)) rimEnv = 1, rimPh = 0;
  hatEnv = 1; hatAcc = (in16 % 4 == 2) ? 1.0f : (even ? 0.55f : 0.35f);
}

static void au_callback(void *u, Uint8 *stream, int len) {
  (void)u;
  int16_t *out = (int16_t *)stream; int n = len / 4;
  const float s16 = AU_RATE * 60.0f / 124.0f / 4.0f;
  for (int i = 0; i < n; i++) {
    /* light swing: odd sixteenths a little late */
    long pair = (long)(auSample / (s16 * 2));
    float inPair = auSample - pair * s16 * 2;
    int t = (int)(pair * 2 + (inPair >= s16 * 1.16f ? 1 : 0));
    if (t != auTick) { auTick = t; if (auMusic) au_tick(t); }
    float L = 0, Rr = 0;
    if (auMusic) {
      for (int k = 0; k < NVOICE; k++) {
        AVoice *v = &vo[k];
        if (v->env < 0.00005f && v->age > 100) continue;
        v->age++;
        /* envelope: fast attack, decay while held, release after the gate */
        if (v->age < 4000 && v->att < 0.4f && v->env < 1 && v->gate > 0) { v->env += v->att * (1.02f - v->env); }
        else if (v->att >= 0.2f && v->age < 40) v->env = 1;
        if (v->gate > 0) { v->gate--; v->env *= v->dec; } else v->env *= v->rel;
        float s, e = v->env;
        switch (v->type) {
          case VT_VIBE: {
            v->ph += v->f / AU_RATE; v->ph2 += v->f * 4.0f / AU_RATE;
            if (v->ph > 1) v->ph -= 1; if (v->ph2 > 1) v->ph2 -= 1;
            float trem = 1 - 0.22f * (0.5f + 0.5f * fsin(auSample * (5.5f / AU_RATE)));
            float hit = v->age < 3000 ? 1 - v->age / 3000.0f : 0;
            s = (fsin(v->ph) + 0.28f * hit * fsin(v->ph2)) * trem;
            break; }
          case VT_EP: {
            v->ph += v->f / AU_RATE; v->ph2 += v->f / AU_RATE;
            if (v->ph > 1) v->ph -= 1; if (v->ph2 > 1) v->ph2 -= 1;
            float idx = 0.18f + 0.6f * e * e;
            s = fsin(v->ph + idx * fsin(v->ph2 * 1.0f + 0.25f));
            break; }
          case VT_BASS: {
            v->ph += v->f / AU_RATE; if (v->ph > 1) v->ph -= 1;
            float pl = v->age < 1600 ? 1 - v->age / 1600.0f : 0;
            s = fsin(v->ph) + (0.25f + 0.35f * pl) * fsin(v->ph * 2) + 0.1f * pl * fsin(v->ph * 3);
            s = tanhf(s * 1.2f);
            break; }
          default: {   /* flute: sine + soft octave, delayed vibrato, a bit of breath */
            float vib = v->age > 4000 ? 0.004f * fsin(auSample * (5.2f / AU_RATE)) : 0;
            v->ph += v->f * (1 + vib) / AU_RATE; if (v->ph > 1) v->ph -= 1;
            s = fsin(v->ph) + 0.18f * fsin(v->ph * 2) + 0.05f * fsin(v->ph * 3) + 0.04f * au_noise();
            break; }
        }
        s *= e * v->amp;
        L += s * (0.5f - v->pan * 0.5f) * 1.4f; Rr += s * (0.5f + v->pan * 0.5f) * 1.4f;
      }
      /* drums */
      float d = 0;
      if (kickEnv > 0.001f) { kickPh += (48 + 70 * kickEnv * kickEnv) / AU_RATE; d += fsin(kickPh) * kickEnv * 0.30f; kickEnv *= 0.9990f; }
      if (rimEnv > 0.001f) { rimPh += 1650.0f / AU_RATE; d += (fsin(rimPh) * 0.6f + au_noise() * 0.5f) * rimEnv * 0.10f; rimEnv *= 0.9925f; }
      if (hatEnv > 0.001f) {
        float nz = au_noise(); hatLp += (nz - hatLp) * 0.55f; float hp = nz - hatLp;
        d += hp * hatEnv * hatAcc * 0.05f; hatEnv *= 0.9965f;
      }
      L += d * 0.95f; Rr += d;
      /* ping-pong echo on the mix (quiet) */
      float eL = echoL[echoPos], eR = echoR[echoPos];
      echoL[echoPos] = Rr * 0.22f + eR * 0.35f;
      echoR[echoPos] = L * 0.22f + eL * 0.35f;
      if (++echoPos >= ECHO_LEN) echoPos = 0;
      lpL += (eL - lpL) * 0.35f; lpR += (eR - lpR) * 0.35f;
      L += lpL; Rr += lpR;
    }
    /* UI sounds */
    if (auSfx) {
      int k = auSfx; auSfx = 0;
      sfxEnv = 1; sfxPh = 0;
      if (k == 1) { sfxF = 1046; sfxF2 = 1568; } else if (k == 2) { sfxF = 1318; sfxF2 = 1976; } else { sfxF = 784; sfxF2 = 1175; }
    }
    if (sfxEnv > 0.001f) {
      float f = sfxF + (sfxF2 - sfxF) * (1 - sfxEnv);
      sfxPh += f / AU_RATE; if (sfxPh > 1) sfxPh -= 1;
      float s = fsin(sfxPh) * sfxEnv * 0.16f;
      L += s; Rr += s;
      sfxEnv *= 0.9988f;
    }
    out[i * 2] = (int16_t)(tanhf(L * 1.1f) * 25000);
    out[i * 2 + 1] = (int16_t)(tanhf(Rr * 1.1f) * 25000);
    auSample++;
  }
}
#define SETTINGS_FILE "fs:/vol/external01/wiiu/apps/aeroverse_settings.txt"
static int auRules;   /* rules accepted once on this console */
static void au_save(void) { FILE *f = fopen(SETTINGS_FILE, "w"); if (f) { fprintf(f, "music=%d\nrules=%d\n", auMusic, auRules); fclose(f); } }
static void au_init(void) {
  for (int i = 0; i < 4096; i++) sinT[i] = sinf(i * 6.2831853f / 4096);
  FILE *f = fopen(SETTINGS_FILE, "r");
  if (f) { char ln[64]; int v; while (fgets(ln, sizeof ln, f)) { if (sscanf(ln, "music=%d", &v) == 1) auMusic = v ? 1 : 0; else if (sscanf(ln, "rules=%d", &v) == 1) auRules = v ? 1 : 0; } fclose(f); }
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
  SDL_AudioSpec want, have; SDL_zero(want);
  want.freq = AU_RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = au_callback;
  auDev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (auDev) SDL_PauseAudioDevice(auDev, 0);
}
static void sfx(int k) { if (auDev) auSfx = k; }
