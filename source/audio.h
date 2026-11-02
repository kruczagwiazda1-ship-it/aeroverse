/*
 * audio.h - original background music + soft UI sounds, synthesised live (no music files needed).
 * A calm "Frutiger Aero" loop: dreamy pad, bell arpeggios, soft bass and light percussion.
 */
#define AU_RATE 32000
static SDL_AudioDeviceID auDev;
static volatile int auMusic = 1;          /* music on/off (saved on the SD card) */
static volatile int auSfx;                /* pending sound effect: 1 pop, 2 yeah, 3 tab */
static float sinT[4096];
static inline float fsin(float ph) { return sinT[(int)(ph * 4096.0f) & 4095]; }   /* ph in cycles */
static float mtof(float n) { return 440.0f * powf(2.0f, (n - 69) / 12.0f); }

/* 16 chords, 1 bar each (8 eighth notes); notes as MIDI numbers, root first */
static const int CHORDS[16][5] = {
  { 48, 64, 67, 71, 74 }, { 45, 60, 64, 67, 71 }, { 41, 57, 60, 64, 67 }, { 43, 59, 62, 64, 69 },
  { 48, 64, 67, 71, 74 }, { 45, 60, 64, 67, 71 }, { 41, 57, 60, 64, 69 }, { 43, 59, 62, 65, 69 },
  { 50, 60, 65, 69, 72 }, { 43, 59, 62, 65, 69 }, { 52, 59, 62, 67, 71 }, { 45, 60, 64, 67, 72 },
  { 41, 57, 60, 64, 67 }, { 41, 56, 60, 63, 67 }, { 48, 64, 67, 71, 74 }, { 43, 59, 62, 65, 69 } };
typedef struct { float ph, ph2, f, amp, env, dec; int on; } AVoice;
static AVoice bells[6];
static float padPh[5][2], bassPh, kickPh, kickEnv, hatEnv, lpPad, sfxPh, sfxEnv, sfxF, sfxF2;
static uint32_t auNoise = 12345;
static long auSample;
static int auStep = -1;

static void au_trigger_bell(float note, float amp) {
  int k = 0; float mn = 2;
  for (int i = 0; i < 6; i++) if (bells[i].env < mn) { mn = bells[i].env; k = i; }
  bells[k].f = mtof(note); bells[k].ph = bells[k].ph2 = 0; bells[k].env = 1; bells[k].amp = amp; bells[k].dec = 0.99985f;
}
static void au_callback(void *u, Uint8 *stream, int len) {
  (void)u;
  int16_t *out = (int16_t *)stream; int n = len / 4;
  const float stepLen = AU_RATE * 60.0f / 92.0f / 2.0f;   /* eighth notes at 92 bpm */
  for (int i = 0; i < n; i++) {
    int step = (int)(auSample / stepLen);
    int chord = (step / 8) % 16, inBar = step % 8;
    if (step != auStep) {
      auStep = step;
      if (auMusic) {
        /* arpeggio: chord tones up and down, with a deterministic "random" variation per bar */
        static const int pat[4][8] = { { 1, 2, 3, 4, 3, 2, 1, 2 }, { 1, 3, 2, 4, 2, 3, 1, 4 }, { 4, 3, 2, 1, 2, 3, 4, 3 }, { 1, 2, 4, 3, 1, 4, 2, 3 } };
        int v = pat[(chord * 7 + step / 128) % 4][inBar];
        float note = CHORDS[chord][v] + ((inBar == 0 || inBar == 5) ? 12 : 0);
        if (!(inBar == 3 && (chord & 1))) au_trigger_bell(note, inBar == 0 ? 0.20f : 0.13f);
        if (inBar == 0 || inBar == 4) kickEnv = 1, kickPh = 0;
        if (inBar & 1) hatEnv = 1;
      }
    }
    float s = 0;
    if (auMusic) {
      /* pad: chord tones, two detuned sines each, gentle swell inside the bar */
      float t = fmodf((float)auSample, stepLen * 8) / (stepLen * 8);
      float swell = 0.55f + 0.45f * sinf(t * 3.14159f);
      float pad = 0;
      for (int k = 1; k < 5; k++) {
        float f = mtof(CHORDS[chord][k] - 12);
        padPh[k][0] += f / AU_RATE; padPh[k][1] += f * 1.004f / AU_RATE;
        if (padPh[k][0] > 1) padPh[k][0] -= 1; if (padPh[k][1] > 1) padPh[k][1] -= 1;
        pad += fsin(padPh[k][0]) + 0.6f * fsin(padPh[k][1]);
      }
      lpPad += (pad - lpPad) * 0.08f;
      s += lpPad * 0.035f * swell;
      /* bass on the root */
      bassPh += mtof(CHORDS[chord][0] - 12) / AU_RATE; if (bassPh > 1) bassPh -= 1;
      float bt = fmodf((float)auSample, stepLen * 4) / (stepLen * 4);
      s += fsin(bassPh) * 0.11f * (1 - bt * 0.6f);
      /* bells: soft FM electric piano */
      for (int k = 0; k < 6; k++) {
        AVoice *b = &bells[k];
        if (b->env < 0.0005f) continue;
        b->ph += b->f / AU_RATE; b->ph2 += b->f * 3.0f / AU_RATE;
        if (b->ph > 1) b->ph -= 1; if (b->ph2 > 1) b->ph2 -= 1;
        s += fsin(b->ph + 0.35f * b->env * fsin(b->ph2)) * b->amp * b->env;
        b->env *= b->dec;
      }
      /* soft kick + shaker */
      if (kickEnv > 0.001f) { kickPh += (45 + 90 * kickEnv) / AU_RATE; s += fsin(kickPh) * kickEnv * 0.16f; kickEnv *= 0.9992f; }
      if (hatEnv > 0.001f) { auNoise = auNoise * 1664525u + 1013904223u; float nz = ((auNoise >> 9) & 0xffff) / 32768.0f - 1; s += nz * hatEnv * 0.018f; hatEnv *= 0.9975f; }
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
      s += fsin(sfxPh) * sfxEnv * 0.16f;
      sfxEnv *= 0.9988f;
    }
    float o = tanhf(s * 1.3f);
    int16_t v = (int16_t)(o * 26000);
    out[i * 2] = v; out[i * 2 + 1] = v;
    auSample++;
  }
}
#define SETTINGS_FILE "fs:/vol/external01/wiiu/apps/aeroverse_settings.txt"
static void au_save(void) { FILE *f = fopen(SETTINGS_FILE, "w"); if (f) { fprintf(f, "music=%d\n", auMusic); fclose(f); } }
static void au_init(void) {
  for (int i = 0; i < 4096; i++) sinT[i] = sinf(i * 6.2831853f / 4096);
  FILE *f = fopen(SETTINGS_FILE, "r");
  if (f) { int m = 1; if (fscanf(f, "music=%d", &m) == 1) auMusic = m ? 1 : 0; fclose(f); }
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return;
  SDL_AudioSpec want, have; SDL_zero(want);
  want.freq = AU_RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = au_callback;
  auDev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (auDev) SDL_PauseAudioDevice(auDev, 0);
}
static void sfx(int k) { if (auDev) auSfx = k; }
