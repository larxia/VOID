/* SPDX-License-Identifier: MPL-2.0
 * VOID for the M-VAVE FM-1: drones, chords, melody, bass, drums, ice.
 * The engine's shared half: the settings (P_*), the patterns, and the calls between the UI (main loop) and the sound
 * (audio interrupt). The DSP units (dsp/ *.c) are compiled apart at -O2; the UI (app.c) sees this header only. */
#pragma once
#include <stdint.h>
#include "fastmath.h"

#define FS 44100.0f
#define INV_FS (1.0f / 44100.0f)
#define CH 32u                           /* the engine's chunk: control rate, sequencer resolution (0.7 ms) */

/* ------------------------------------------------------------------- the settings --- */
enum {
    /* harmony and the keys */
    P_ROOT, P_SCALE, P_CHORD, P_VOICING, P_HARM, P_HOLD, P_LATCH, P_SPREAD_OCT,
    /* the drone */
    P_DPRESET, P_CHOIR, P_PARTIALS, P_BREATH, P_TIMBRE, P_TONE, P_DETUNE, P_ATTACK, P_RELEASE, P_DMOTION,
    /* the drone filter */
    P_FTYPE, P_FCUT, P_FRES, P_FDRIVE,
    /* cloud, echo, freeze */
    P_CSIZE, P_CAMT, P_SHIM, P_DAMP, P_ETIME, P_EFB, P_EAMT, P_FREEZE,
    /* generative moves */
    P_SWELL, P_SPARKLE, P_BLUR, P_SINK,
    /* tape and master */
    P_TDRIVE, P_TWOBBLE, P_MFILT, P_WIDTH,
    /* three LFOs: shape, rate, depth, target */
    P_L1SHAPE, P_L1RATE, P_L1DEPTH, P_L1TGT,
    P_L2SHAPE, P_L2RATE, P_L2DEPTH, P_L2TGT,
    P_L3SHAPE, P_L3RATE, P_L3DEPTH, P_L3TGT,
    /* shards (generative FM bells) */
    P_SPRESET, P_SDENS, P_SRING, P_SOCT,
    /* melody */
    P_MPLAYS, P_MSOUND, P_MOCT, P_MLEN, P_MTONE, P_MGLIDE, P_MCUR, P_MNOTE, P_MGEN, P_MCLEAR, P_MSHIFT, P_MHUMAN,
    /* bass */
    P_BPLAYS, P_BMODEL, P_BMODE, P_BGLIDE, P_BLEN, P_BTONE, P_BOCT, P_BCUR, P_BNOTE,
    /* drums */
    P_DMPLAYS, P_DMKIT, P_DMLEVEL, P_DMTRACK, P_DMTUNE, P_DMDECAY, P_DMVOL, P_DMHUMAN, P_DMSPACE,
    /* chord sequence */
    P_CSPLAYS, P_CSBARS, P_CSCUR, P_CSDEG,
    /* global */
    P_TEMPO, P_SWING, P_PUMP, P_CLOCK, P_MIDICH,
    /* mixer */
    P_MXDRONE, P_MXMEL, P_MXBASS, P_MXDRUM, P_MXSHARD, P_MXCLOUD,
    /* scenes */
    P_MORPH,
    P_DRAND, P_MRAND, P_DMEVOLVE,          /* the UI's: a random drone, a random melody sound, drums that evolve */
    NP
};
#define NSTORED P_MCUR                   /* (not a range: every setting is kept; the cursors and actions are reset) */

/* ------------------------------------------------------------------- the patterns --- */
#define NSTEP 16u
#define NDRUM 6u
#define NCSEQ 8u
#define MEL_REST (-1)
#define BASS_OFF (-2)
#define BASS_ROOT (-1)
typedef struct {
    uint8_t drum[NDRUM][NSTEP];          /* 0 off, 1 soft, 2 full */
    uint8_t dtune[NDRUM], ddecay[NDRUM], dvol[NDRUM];
    int8_t mel[NSTEP];                   /* MEL_REST, or a degree of the scale from the root (0 .. 20) */
    int8_t bass[NSTEP];                  /* BASS_OFF, BASS_ROOT (follows the chord, P_BMODE), or a note 0 .. 11 */
    int8_t cseq[NCSEQ];                  /* -1 none, a degree 0 .. 13 */
} pattern_t;

/* ------------------------------------------------------------------- the shared state --- */
/* the UI writes; the engine reads (bytes and whole words: no tearing that matters) */
typedef struct {
    uint8_t p[NP];
    pattern_t pat;
    int8_t harm[8];                      /* the notes the bass and the shards follow (MIDI), low to high; harm_n of them */
    uint8_t harm_n;
    uint8_t playing;                     /* the transport: the sequencers and the shards run */
    uint8_t fill;                        /* a drum fill asked (the engine clears it once played) */
} floe_t;
extern floe_t F;

/* what the engine shows back */
typedef struct {
    uint32_t bar;                        /* bars since PLAY */
    int8_t step;                         /* the sixteenth playing, -1 stopped */
    float lfo[3];                        /* each LFO now, -1 .. 1 */
    float level[6];                      /* meters: drone, melody, bass, drums, shards, cloud */
    float bpm;                           /* the tempo it runs at (a MIDI clock's, else TEMPO's) */
    uint32_t sounding;                   /* drone voices sounding */
    int8_t vnote[8];                     /* each drone voice's note (-1 free) and level, for the screen */
    float vlvl[8];
    uint32_t xruns;
} floe_view_t;
extern volatile floe_view_t V;

/* events from the UI to the engine (a ring) */
enum { EV_DRONE_ON = 1, EV_DRONE_OFF, EV_ALL_OFF, EV_MEL, EV_BASS, EV_DRUM, EV_SHARD, EV_PANIC, EV_START, EV_STOP };
void eng_event(uint32_t type, int32_t a, float b);

void eng_init(void);
extern volatile uint32_t eng_maxdv;      /* drone voices allowed, 2 .. 8 (a CPU guard) */
void eng_render(int32_t *lr, uint32_t n, float gain);   /* the audio interrupt: n stereo frames, 24-bit */
void eng_midi_clock(uint32_t status);                     /* (the audio interrupt's MIDI: F8, FA, FB, FC) */

/* ------------------------------------------------------------------- harmony --- */
#define NSCALES 12u
#define NFIXED 33u                       /* the chords built on the note played */
#define NAUTO 12u                        /* the chords stacked from the scale */
#define NCHORDS (NFIXED + NAUTO)
#define NVOICING 4u
extern const uint8_t SCALES[NSCALES][8];       /* [0]: how many notes; then the semitones */
extern const char *const SCALE_NAME[NSCALES];
extern const int8_t CHORD_FIX[NFIXED][7];      /* [0]: how many; then semitones */
extern const char *const CHORD_NAME[NCHORDS];
extern const char *const CHORD_SUFFIX[NFIXED];
extern const int8_t CHORD_AUTO[NAUTO][7];      /* [0]: how many; then steps of the scale */
extern const char *const VOICING_NAME[NVOICING];
int32_t scale_deg(uint32_t scale, int32_t d);   /* degree d of the scale, in semitones from the root */
int32_t scale_len(uint32_t scale);
/* a chord on root (MIDI) at degree d (in-key chords), as the settings make it: out its notes low to high, the count */
uint32_t chord_build(int32_t root, int32_t d, const uint8_t *p, int8_t *out);
/* the degree nearest note n in the key (root, scale), and that degree's note */
int32_t key_snap(int32_t n, const uint8_t *p, int32_t *deg);

/* ------------------------------------------------------------------- the tables the UI names --- */
#define NDPRESET 12u
#define NSPRESET 8u
#define NMSOUND 24u
#define NBMODEL 8u
#define NBMODE 6u
#define NKIT 4u
#define NLSHAPE 7u
#define NLTGT 14u
#define NLRATE_FREE 64u                  /* LFO RATE: 0 .. 63 free (0.01 .. 20 Hz), then the synced divisions */
#define NLDIV 10u
extern const char *const DPRESET_NAME[NDPRESET];
extern const uint8_t DPRESET_V[NDPRESET][10];  /* choir, partials, breath, timbre, tone, detune, attack, release, motion, cut */
extern const char *const SPRESET_NAME[NSPRESET];
extern const char *const MSOUND_NAME[NMSOUND];
extern const char *const BMODEL_NAME[NBMODEL];
extern const char *const BMODE_NAME[NBMODE];
extern const char *const KIT_NAME[NKIT];
extern const char *const LSHAPE_NAME[NLSHAPE];
extern const char *const LTGT_NAME[NLTGT];
extern const char *const LDIV_NAME[NLDIV];
extern const float LDIV_BEATS[NLDIV];
float lfo_hz(uint32_t rate, float bpm);
