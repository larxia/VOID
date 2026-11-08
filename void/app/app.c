/* SPDX-License-Identifier: MPL-2.0
 * VOID for the M-VAVE FM-1: the UI (the main loop) around the engine (dsp/: the audio interrupt).
 *
 * The keys play the drones as chords (HARMONY: on the note played, or stacked in the key; CHORDS or SINGLE notes; ONE
 * at a time or LAYERed; LATCH), or, on a sequencer's page, write its steps. The buttons are pages, four settings each
 * on K1 .. K4. PLAY runs the sequencers, REC freezes the cloud (on a step page: a rest), SAVE keeps scenes.
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in scope. */
#include "engine.h"
#include "logo.h"

/* ------------------------------------------------------------------- the settings' faces --- */
enum { FM_PCT, FM_NUM, FM_NAMES, FM_NOTE, FM_ONOFF, FM_BPM, FM_BIPOLAR, FM_SEMI, FM_LRATE, FM_OCT, FM_STEP, FM_MNOTE,
       FM_BNOTE, FM_CDEG, FM_TURN2, FM_MORPH, FM_CH, FM_SHIFT, FM_ECHO, FM_PLUS1, FM_DICE, FM_SCTIME, FM_SCATK, FM_UPOS, FM_UMOD, FM_UCUT,
       FM_UTIME };
typedef struct {
    const char *name;
    uint8_t max, def, fmt;
    const char *const *names;
} pd_t;
static const char *const NOTE_N[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
static const char *const HARM_N[2] = {"SINGLE", "CHORDS"};
static const char *const HOLD_N[2] = {"LAYER", "ONE"};
static const char *const FTYPE_N[4] = {"LOWPASS", "BANDPASS", "HIGHPASS", "NOTCH"};
static const char *const CLOCK_N[2] = {"INTERNAL", "MIDI"};
static const char *const TRACK_N[6] = {"KICK", "SNARE", "CLAP", "HAT", "OPEN HAT", "PERC"};
static const char *const BARS_N[4] = {"1 BAR", "2 BARS", "4 BARS", "8 BARS"};
static const char *const ECHO_N[8] = {"1/16", "1/8", "1/8T", "1/8.", "1/4", "1/4.", "1/4T", "1/2"};
static const char *const MOCT_N[4] = {"-1", "0", "+1", "+2"};
static const char *const BOCT_N[3] = {"-1", "0", "+1"};
static const char *const SOCT_N[3] = {"LOW", "MID", "HIGH"};
static const char *const KEYPLAY_N[3] = {"CHORDS", "MELODY", "BOTH"};
static const char *const SCTRIG_N[NSCTRIG] = {"KICK", "EVERY BEAT", "EIGHTHS", "OFF-BEATS", "SNARE", "KICK+SNARE", "3-3-2"};
static const char *const SCWHAT_N[NSCWHAT] = {"PADS + FX", "ALL BUT DRUMS", "DRONES", "DRONES + CLOUD", "MELODY + BASS",
                                              "CLOUD + ECHO"};

static const pd_t PD[NP] = {
    [P_ROOT] = {"ROOT", 11, 9, FM_NAMES, NOTE_N},
    [P_SCALE] = {"SCALE", NSCALES - 1, 1, FM_NAMES, SCALE_NAME},
    [P_CHORD] = {"CHORD", NCHORDS - 1, 22, FM_NAMES, CHORD_NAME},
    [P_VOICING] = {"VOICING", NVOICING - 1, 1, FM_NAMES, VOICING_NAME},
    [P_HARM] = {"PLAY", 1, 1, FM_NAMES, HARM_N},
    [P_HOLD] = {"HOLD", 1, 1, FM_NAMES, HOLD_N},
    [P_LATCH] = {"LATCH", 1, 1, FM_ONOFF, 0},
    [P_SPREAD_OCT] = {"SPREAD", 127, 80, FM_PCT, 0},
    [P_DPRESET] = {"PRESET", NDPRESET - 1, 0, FM_NAMES, DPRESET_NAME},
    [P_CHOIR] = {"CHOIR", 127, 89, FM_PCT, 0},
    [P_PARTIALS] = {"PARTIALS", 127, 51, FM_PCT, 0},
    [P_BREATH] = {"BREATH", 127, 25, FM_PCT, 0},
    [P_TIMBRE] = {"TIMBRE", 127, 44, FM_PCT, 0},
    [P_TONE] = {"TONE", 127, 57, FM_PCT, 0},
    [P_DETUNE] = {"DETUNE", 127, 51, FM_PCT, 0},
    [P_ATTACK] = {"ATTACK", 127, 57, FM_PCT, 0},
    [P_RELEASE] = {"RELEASE", 127, 70, FM_PCT, 0},
    [P_DMOTION] = {"MOTION", 127, 40, FM_PCT, 0},
    [P_FTYPE] = {"TYPE", 3, 0, FM_NAMES, FTYPE_N},
    [P_FCUT] = {"CUTOFF", 127, 110, FM_PCT, 0},
    [P_FRES] = {"RESONANCE", 127, 20, FM_PCT, 0},
    [P_FDRIVE] = {"DRIVE", 127, 0, FM_PCT, 0},
    [P_CSIZE] = {"SIZE", 127, 90, FM_PCT, 0},
    [P_CAMT] = {"AMOUNT", 127, 70, FM_PCT, 0},
    [P_SHIM] = {"SHIMMER", 127, 45, FM_PCT, 0},
    [P_DAMP] = {"DAMPING", 127, 57, FM_PCT, 0},
    [P_ETIME] = {"ECHO TIME", 7, 3, FM_NAMES, ECHO_N},
    [P_EFB] = {"FEEDBACK", 127, 50, FM_PCT, 0},
    [P_EAMT] = {"ECHO", 127, 40, FM_PCT, 0},
    [P_FREEZE] = {"FREEZE", 1, 0, FM_ONOFF, 0},
    [P_SWELL] = {"SWELL", 127, 38, FM_PCT, 0},
    [P_SPARKLE] = {"SPARKLE", 127, 44, FM_PCT, 0},
    [P_BLUR] = {"BLUR", 127, 38, FM_PCT, 0},
    [P_SINK] = {"SINK", 127, 0, FM_PCT, 0},
    [P_TDRIVE] = {"TAPE DRIVE", 127, 30, FM_PCT, 0},
    [P_TWOBBLE] = {"WOBBLE", 127, 25, FM_PCT, 0},
    [P_MFILT] = {"MASTER FILT", 127, 127, FM_PCT, 0},
    [P_WIDTH] = {"WIDTH", 127, 64, FM_PCT, 0},
    [P_L1SHAPE] = {"SHAPE", NLSHAPE - 1, 0, FM_NAMES, LSHAPE_NAME},
    [P_L1RATE] = {"RATE", NLRATE_FREE + NLDIV - 1, 22, FM_LRATE, 0},
    [P_L1DEPTH] = {"DEPTH", 127, 86, FM_BIPOLAR, 0},
    [P_L1TGT] = {"TARGET", NLTGT - 1, 1, FM_NAMES, LTGT_NAME},
    [P_L2SHAPE] = {"SHAPE", NLSHAPE - 1, 1, FM_NAMES, LSHAPE_NAME},
    [P_L2RATE] = {"RATE", NLRATE_FREE + NLDIV - 1, 50, FM_LRATE, 0},
    [P_L2DEPTH] = {"DEPTH", 127, 74, FM_BIPOLAR, 0},
    [P_L2TGT] = {"TARGET", NLTGT - 1, 3, FM_NAMES, LTGT_NAME},
    [P_L3SHAPE] = {"SHAPE", NLSHAPE - 1, 6, FM_NAMES, LSHAPE_NAME},
    [P_L3RATE] = {"RATE", NLRATE_FREE + NLDIV - 1, 16, FM_LRATE, 0},
    [P_L3DEPTH] = {"DEPTH", 127, 96, FM_BIPOLAR, 0},
    [P_L3TGT] = {"TARGET", NLTGT - 1, 6, FM_NAMES, LTGT_NAME},
    [P_SPRESET] = {"SHARDS", NSPRESET - 1, 0, FM_NAMES, SPRESET_NAME},
    [P_SDENS] = {"DENSITY", 127, 38, FM_PCT, 0},
    [P_SRING] = {"RING", 127, 76, FM_PCT, 0},
    [P_SOCT] = {"OCTAVE", 2, 1, FM_NAMES, SOCT_N},
    [P_MPLAYS] = {"PLAYS", 1, 1, FM_ONOFF, 0},
    [P_MSOUND] = {"SOUND", NMSOUND - 1, 0, FM_NAMES, MSOUND_NAME},
    [P_MOCT] = {"OCTAVE", 3, 1, FM_NAMES, MOCT_N},
    [P_MLEN] = {"LENGTH", 127, 57, FM_PCT, 0},
    [P_MTONE] = {"TONE", 127, 70, FM_PCT, 0},
    [P_MGLIDE] = {"GLIDE", 127, 0, FM_PCT, 0},
    [P_MCUR] = {"STEP", 15, 0, FM_STEP, 0},
    [P_MNOTE] = {"NOTE", 21, 0, FM_MNOTE, 0},
    [P_MGEN] = {"GENERATE", 2, 0, FM_TURN2, 0},
    [P_MCLEAR] = {"CLEAR", 2, 0, FM_TURN2, 0},
    [P_MSHIFT] = {"SHIFT", 2, 1, FM_SHIFT, 0},
    [P_MHUMAN] = {"HUMANIZE", 127, 25, FM_PCT, 0},
    [P_BPLAYS] = {"PLAYS", 1, 1, FM_ONOFF, 0},
    [P_BMODEL] = {"MODEL", NBMODEL - 1, 4, FM_NAMES, BMODEL_NAME},
    [P_BMODE] = {"R PLAYS", NBMODE - 1, 0, FM_NAMES, BMODE_NAME},
    [P_BGLIDE] = {"GLIDE", 127, 32, FM_PCT, 0},
    [P_BLEN] = {"LENGTH", 127, 70, FM_PCT, 0},
    [P_BTONE] = {"TONE", 127, 44, FM_PCT, 0},
    [P_BOCT] = {"OCTAVE", 2, 1, FM_NAMES, BOCT_N},
    [P_BCUR] = {"STEP", 15, 0, FM_STEP, 0},
    [P_BNOTE] = {"NOTE", 13, 0, FM_BNOTE, 0},
    [P_DMPLAYS] = {"PLAYS", 1, 1, FM_ONOFF, 0},
    [P_DMKIT] = {"KIT", NKIT - 1, 0, FM_NAMES, KIT_NAME},
    [P_DMLEVEL] = {"LEVEL", 127, 96, FM_PCT, 0},
    [P_DMTRACK] = {"TRACK", 5, 0, FM_NAMES, TRACK_N},
    [P_DMTUNE] = {"TUNE", 24, 12, FM_SEMI, 0},
    [P_DMDECAY] = {"DECAY", 127, 64, FM_PCT, 0},
    [P_DMVOL] = {"VOLUME", 127, 100, FM_PCT, 0},
    [P_DMHUMAN] = {"HUMANIZE", 127, 32, FM_PCT, 0},
    [P_DMSPACE] = {"SPACE", 127, 38, FM_PCT, 0},
    [P_CSPLAYS] = {"PLAYS", 1, 0, FM_ONOFF, 0},
    [P_CSBARS] = {"EACH", 3, 1, FM_NAMES, BARS_N},
    [P_CSCUR] = {"STEP", 7, 0, FM_STEP, 0},
    [P_CSDEG] = {"CHORD", 14, 0, FM_CDEG, 0},
    [P_TEMPO] = {"TEMPO", 160, 34, FM_BPM, 0},
    [P_SWING] = {"SWING", 127, 19, FM_PCT, 0},
    [P_PUMP] = {"PUMP", 127, 38, FM_PCT, 0},
    [P_CLOCK] = {"CLOCK", 1, 0, FM_NAMES, CLOCK_N},
    [P_MIDICH] = {"MIDI IN", 16, 0, FM_CH, 0},
    [P_MXDRONE] = {"DRONE", 127, 110, FM_PCT, 0},
    [P_MXMEL] = {"MELODY", 127, 90, FM_PCT, 0},
    [P_MXBASS] = {"BASS", 127, 96, FM_PCT, 0},
    [P_MXDRUM] = {"DRUMS", 127, 100, FM_PCT, 0},
    [P_MXSHARD] = {"SHARDS", 127, 80, FM_PCT, 0},
    [P_MXCLOUD] = {"CLOUD", 127, 100, FM_PCT, 0},
    [P_MORPH] = {"MORPH", 8, 3, FM_MORPH, 0},
    [P_DRAND] = {"RANDOM DRONE", 1, 0, FM_DICE, 0},
    [P_MRAND] = {"RANDOM SOUND", 1, 0, FM_DICE, 0},
    [P_DMEVOLVE] = {"EVOLVE", 1, 0, FM_ONOFF, 0},
    [P_PTGT] = {"PRESSURE", NLTGT - 1, 14, FM_NAMES, LTGT_NAME},
    [P_PDEPTH] = {"PRESS DEPTH", 127, 110, FM_BIPOLAR, 0},
    [P_KEYPLAY] = {"KEYS PLAY", 2, 0, FM_NAMES, KEYPLAY_N},
    [P_DWAVE] = {"WAVE", NDWAVE - 1, 0, FM_NAMES, DWAVE_NAME},
    [P_PSERIES] = {"PARTIALS TUNE", NPSERIES - 1, 0, FM_NAMES, PSERIES_NAME},
    [P_SCTRIG] = {"TRIGGER", NSCTRIG - 1, 0, FM_NAMES, SCTRIG_N},
    [P_SCREL] = {"RELEASE", 127, 46, FM_SCTIME, 0},
    [P_SCATK] = {"ATTACK", 127, 0, FM_SCATK, 0},
    [P_SCWHAT] = {"DUCKS", NSCWHAT - 1, 0, FM_NAMES, SCWHAT_N},
    [P_UFROM] = {"START FROM", NMSOUND - 2, 0, FM_NAMES, MSOUND_NAME},
    [P_UWAVE] = {"WAVE", NUWAVE - 1, 4, FM_NAMES, UWAVE_NAME},
    [P_UPOS] = {"TABLE / RATIO", 127, 13, FM_UPOS, 0},
    [P_UMOD] = {"AMOUNT", 127, 51, FM_UMOD, 0},
    [P_UP2R] = {"PARTIAL 2", NUP2 - 1, 0, FM_NAMES, UP2_NAME},
    [P_UP2L] = {"PARTIAL LEVEL", 127, 0, FM_PCT, 0},
    [P_UCUT] = {"CUTOFF", 127, 127, FM_UCUT, 0},
    [P_UFENV] = {"FILTER ENV", 127, 0, FM_PCT, 0},
    [P_URES] = {"RESONANCE", 127, 0, FM_PCT, 0},
    [P_UATK] = {"ATTACK", 127, 6, FM_UTIME, 0},
    [P_UDEC] = {"DECAY", 127, 76, FM_UTIME, 0},
    [P_USUS] = {"SUSTAIN", 127, 32, FM_PCT, 0},
    [P_UREL] = {"RELEASE", 127, 72, FM_UTIME, 0},
    [P_UVIB] = {"VIBRATO", 127, 0, FM_PCT, 0},
    [P_UBREATH] = {"BREATH", 127, 0, FM_PCT, 0},
    [P_UDROP] = {"PITCH DROP", 127, 0, FM_PCT, 0},
};
static const uint16_t MORPH_MS[9] = {0, 500, 1000, 2000, 4000, 8000, 15000, 30000, 60000};

/* ------------------------------------------------------------------- the pages --- */
enum { V_RIVER, V_DRUMS, V_MEL, V_BASS, V_CSEQ, V_FILTER, V_LFO, V_MIXER, V_SCENES, V_DUCK };
enum { K_DRONE, K_DRUMS, K_MEL, K_BASS, K_CSEQ, K_SCENES, K_LIVE };   /* (K_LIVE: the melody played freely) */   /* what the keys do on the page */
typedef struct {
    const char *name;
    uint8_t button, view, keys;
    uint8_t p[4];
} page_t;
#define NONE NP
static const page_t PAGES[] = {
    {"PLAY", B_HOME, V_RIVER, K_DRONE, {P_KEYPLAY, P_CHORD, P_HOLD, P_LATCH}},
    {"SOUND", B_HOME, V_RIVER, K_DRONE, {P_ATTACK, P_RELEASE, P_DETUNE, P_FCUT}},
    {"SOUND II", B_HOME, V_RIVER, K_DRONE, {P_DPRESET, P_VOICING, P_HARM, P_DRAND}},
    {"DRONE TYPE", B_HOME, V_RIVER, K_DRONE, {P_DPRESET, P_DWAVE, P_PSERIES, P_TONE}},
    {"SPACE", B_HOME, V_RIVER, K_DRONE, {P_CAMT, P_SHIM, P_EAMT, P_FREEZE}},
    {"KEY & TOUCH", B_HOME, V_RIVER, K_DRONE, {P_ROOT, P_SCALE, P_PTGT, P_PDEPTH}},
    {"MIXER", B_HOME, V_MIXER, K_DRONE, {P_MXDRONE, P_MXMEL, P_MXBASS, P_MXDRUM}},
    {"MIXER II", B_HOME, V_MIXER, K_DRONE, {P_MXSHARD, P_MXCLOUD, P_PUMP, P_WIDTH}},
    {"HARMONY", B_SEL, V_RIVER, K_DRONE, {P_ROOT, P_SCALE, P_CHORD, P_VOICING}},
    {"KEYS", B_SEL, V_RIVER, K_DRONE, {P_HARM, P_HOLD, P_LATCH, P_SPREAD_OCT}},
    {"TOUCH", B_SEL, V_RIVER, K_DRONE, {P_PTGT, P_PDEPTH, P_SCALE, P_ROOT}},
    {"DRONE", B_EDIT, V_RIVER, K_DRONE, {P_DPRESET, P_CHOIR, P_PARTIALS, P_BREATH}},
    {"DRONE II", B_EDIT, V_RIVER, K_DRONE, {P_TIMBRE, P_TONE, P_DETUNE, P_DMOTION}},
    {"DRONE III", B_EDIT, V_RIVER, K_DRONE, {P_ATTACK, P_RELEASE, P_SPREAD_OCT, P_DRAND}},
    {"DRONE TYPE", B_EDIT, V_RIVER, K_DRONE, {P_DWAVE, P_PSERIES, P_TIMBRE, P_DPRESET}},
    {"SHARDS", B_EDIT, V_RIVER, K_DRONE, {P_SPRESET, P_SDENS, P_SRING, P_SOCT}},
    {"SHAPE", B_ENV, V_RIVER, K_DRONE, {P_ATTACK, P_RELEASE, P_SWELL, P_SPREAD_OCT}},
    {"FILTER", B_ENV, V_FILTER, K_DRONE, {P_FTYPE, P_FCUT, P_FRES, P_FDRIVE}},
    {"LFO 1", B_LFO, V_LFO, K_DRONE, {P_L1SHAPE, P_L1RATE, P_L1DEPTH, P_L1TGT}},
    {"LFO 2", B_LFO, V_LFO, K_DRONE, {P_L2SHAPE, P_L2RATE, P_L2DEPTH, P_L2TGT}},
    {"LFO 3", B_LFO, V_LFO, K_DRONE, {P_L3SHAPE, P_L3RATE, P_L3DEPTH, P_L3TGT}},
    {"MOVES", B_FX, V_RIVER, K_DRONE, {P_SWELL, P_SPARKLE, P_BLUR, P_SINK}},
    {"DRONE FX", B_FX, V_RIVER, K_DRONE, {P_ATTACK, P_RELEASE, P_DETUNE, P_DMOTION}},
    {"FILTER", B_FX, V_FILTER, K_DRONE, {P_FTYPE, P_FCUT, P_FRES, P_FDRIVE}},
    {"CLOUD", B_FX, V_RIVER, K_DRONE, {P_CSIZE, P_CAMT, P_SHIM, P_DAMP}},
    {"ECHO", B_FX, V_RIVER, K_DRONE, {P_ETIME, P_EFB, P_EAMT, P_FREEZE}},
    {"TAPE", B_FX, V_RIVER, K_DRONE, {P_TDRIVE, P_TWOBBLE, P_MFILT, P_WIDTH}},
    {"SIDECHAIN", B_FX, V_DUCK, K_DRONE, {P_PUMP, P_SCTRIG, P_SCREL, P_SCWHAT}},
    {"SIDECHAIN II", B_FX, V_DUCK, K_DRONE, {P_SCATK, P_PUMP, P_SCREL, P_SCTRIG}},
    {"MELODY", B_ARP, V_MEL, K_MEL, {P_MPLAYS, P_MSOUND, P_MOCT, P_MLEN}},
    {"MELODY LIVE", B_ARP, V_MEL, K_LIVE, {P_KEYPLAY, P_MSOUND, P_MOCT, P_MTONE}},
    {"MELODY SOUND", B_ARP, V_MEL, K_MEL, {P_MSOUND, P_MRAND, P_MTONE, P_MGLIDE}},
    {"SOUND DESIGN", B_ARP, V_MEL, K_LIVE, {P_UFROM, P_UWAVE, P_UPOS, P_UMOD}},
    {"SOUND DESIGN 2", B_ARP, V_MEL, K_LIVE, {P_UP2R, P_UP2L, P_UCUT, P_UFENV}},
    {"SOUND DESIGN 3", B_ARP, V_MEL, K_LIVE, {P_URES, P_UATK, P_UDEC, P_USUS}},
    {"SOUND DESIGN 4", B_ARP, V_MEL, K_LIVE, {P_UREL, P_UVIB, P_UBREATH, P_UDROP}},
    {"MELODY STEP", B_ARP, V_MEL, K_MEL, {P_MCUR, P_MNOTE, P_MTONE, P_MGLIDE}},
    {"MELODY TOOLS", B_ARP, V_MEL, K_MEL, {P_MGEN, P_MCLEAR, P_MSHIFT, P_MHUMAN}},
    {"BASS", B_ARP, V_BASS, K_BASS, {P_BPLAYS, P_BMODE, P_BCUR, P_BNOTE}},
    {"BASS SOUND", B_ARP, V_BASS, K_BASS, {P_BMODEL, P_BTONE, P_BGLIDE, P_BLEN}},
    {"BASS OCTAVE", B_ARP, V_BASS, K_BASS, {P_BOCT, P_BCUR, P_BNOTE, NONE}},
    {"DRUMS", B_SEQ, V_DRUMS, K_DRUMS, {P_DMPLAYS, P_DMKIT, P_DMLEVEL, P_DMTRACK}},
    {"DRUM TRACK", B_SEQ, V_DRUMS, K_DRUMS, {P_DMTRACK, P_DMTUNE, P_DMDECAY, P_DMVOL}},
    {"DRUM FEEL", B_SEQ, V_DRUMS, K_DRUMS, {P_SWING, P_DMHUMAN, P_DMSPACE, P_PUMP}},
    {"DRUM TOOLS", B_SEQ, V_DRUMS, K_DRUMS, {P_DMEVOLVE, P_DMKIT, P_DMLEVEL, P_DMSPACE}},
    {"SIDECHAIN", B_SEQ, V_DUCK, K_DRONE, {P_PUMP, P_SCTRIG, P_SCREL, P_SCATK}},
    {"CHORD SEQ", B_SEQ, V_CSEQ, K_CSEQ, {P_CSPLAYS, P_CSBARS, P_CSCUR, P_CSDEG}},
    {"GLOBAL", B_GLO, V_RIVER, K_DRONE, {P_TEMPO, P_SWING, P_CLOCK, P_MIDICH}},
    {"SCENES", B_SAVE, V_SCENES, K_SCENES, {P_MORPH, NONE, NONE, NONE}},
};
#define NPAGES ((uint32_t)(sizeof PAGES / sizeof PAGES[0]))
#define PG_SCENES (NPAGES - 1u)

/* ------------------------------------------------------------------- state --- */
#define STATE_MAGIC 0x37454C46u          /* "FLE7" */
#define NSCN 8u
typedef struct {
    uint8_t p[NP];
    pattern_t pat;
    int8_t held[8];                      /* the drones latched (a scene's chord), 0: none */
    char name[12];
} scene_t;
typedef struct {
    uint32_t magic;
    scene_t now;
    int8_t octave;
    uint8_t page, scene, rsv;
} state_t;
typedef struct {
    uint32_t magic;
    uint32_t used;                       /* bit s: slot s stored */
    scene_t s[NSCN];
} scenes_t;
static state_t st;
static scenes_t scn;
_Static_assert(sizeof(scenes_t) <= PLAT_STORE_MAX, "the scenes fit their object");

/* the drones the keys hold: groups (a key's chord), each note counted */
#define NGROUP 12u
#define G_SEQ 300                        /* the chord sequence's group */
#define G_SCENE 301                      /* a scene's latched chord */
typedef struct {
    int16_t id;                          /* a key (0 .. 26), a MIDI note (+100), G_SEQ, G_SCENE; -1 free */
    uint8_t n;
    int8_t note[8];
} group_t;
static group_t grp[NGROUP];
static uint8_t cnt[128];

static struct {
    char msg[2][28];
    uint32_t msg_until;
    uint32_t keys, btn, btn_at[NB];
    uint8_t btn_used[NB];
    uint8_t redraw, vis_due;
    uint32_t vis_at, touched_at;
    int8_t touched;                      /* the knob turned last (0 .. 3), -1 */
    uint8_t turn2[NP];                   /* (an action's first turn: the second does it) */
    uint32_t turn2_at[NP];
    uint32_t cs_bar;                     /* the chord sequence: the bar it changed on */
    int8_t cs_idx;
    uint8_t ext_play;                    /* a MIDI Start/Stop for the main loop */
    uint32_t morph_at, morph_ms;
    float morph_from[NP], morph_to[NP];
    uint8_t morphing;
    uint8_t clear_arm;
    uint8_t at;                          /* MIDI aftertouch in */
    uint8_t rec;                         /* recording: the melody played goes into the melody's steps */
    uint8_t rec_chords, rec_cs_was;      /* (chords recorded this take; the sequence was playing before) */
    int8_t livenote[NKEYS];              /* each key's melody note sounding (played live), -1 */
    int8_t midilive[128];                /* (a MIDI note's, +1: 0 none) */
    uint32_t clear_at;
} ui;

/* MIDI notes in, from the audio interrupt to the main loop */
static volatile uint32_t mq[32], mq_w, mq_r;

/* ------------------------------------------------------------------- text --- */
static void tcat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static uint32_t strlen_(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}
static void itxt(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void note_txt(int32_t n, char *b)
{
    char o[8];
    b[0] = 0;
    tcat(b, NOTE_N[((n % 12) + 12) % 12]);
    itxt(n / 12 - 1, o);
    tcat(b, o);
}
/* the chord a key would play, named: its root's note and the chord's suffix (in key: its quality) */
static void chord_txt(int32_t root, int32_t deg, char *b)
{
    uint32_t c = F.p[P_CHORD] % NCHORDS;
    b[0] = 0;
    tcat(b, NOTE_N[((root % 12) + 12) % 12]);
    if (!F.p[P_HARM])
        return;
    if (c < NFIXED) {
        tcat(b, CHORD_SUFFIX[c]);
    } else {                             /* in key: minor, major, diminished from the third and fifth; a 7th, a 9th */
        int8_t n[8];
        uint32_t k = chord_build(60, deg, F.p, n), i, has[12] = {0};
        for (i = 0; i < k; i++)
            has[((n[i] - 60) % 12 + 12) % 12] = 1;
        if (has[3] && has[6] && !has[7])
            tcat(b, has[10] ? "m7b5" : "dim");
        else if (has[3])
            tcat(b, has[10] ? (has[2] ? "m9" : "m7") : "m");
        else if (has[4])
            tcat(b, has[11] ? (has[2] ? "maj9" : "maj7") : has[10] ? "7" : has[2] ? "add9" : "");
        else if (has[2])
            tcat(b, "sus2");
        else if (has[5])
            tcat(b, "sus4");
    }
}

static void value_txt(uint32_t p, char *b)
{
    const pd_t *d = &PD[p];
    uint32_t x = F.p[p];
    b[0] = 0;
    switch (d->fmt) {
    case FM_PCT: itxt((int32_t)((x * 100u + 63u) / 127u), b); break;
    case FM_NUM: itxt((int32_t)x, b); break;
    case FM_NAMES: tcat(b, d->names[x <= d->max ? x : 0]); break;
    case FM_ONOFF: tcat(b, x ? "ON" : "OFF"); break;
    case FM_BPM: itxt((int32_t)x + 40, b); break;
    case FM_BIPOLAR: itxt((int32_t)x - 64, b); break;
    case FM_SEMI: if (x > 12u) tcat(b, "+"); itxt((int32_t)x - 12, b + (x > 12u)); tcat(b, " ST"); break;
    case FM_LRATE:
        if (x < NLRATE_FREE) {
            float hz = lfo_hz(x, 90.0f);
            if (hz < 1.0f) {
                itxt((int32_t)(1.0f / hz + 0.5f), b);
                tcat(b, "S");
            } else {
                itxt((int32_t)(hz * 10.0f + 0.5f) / 10, b);
                tcat(b, ".");
                {
                    char *e = b;
                    while (*e)
                        e++;
                    itxt((int32_t)(hz * 10.0f + 0.5f) % 10, e);
                }
                tcat(b, "HZ");
            }
        } else {
            tcat(b, "SYNC ");
            tcat(b, LDIV_NAME[x - NLRATE_FREE]);
        }
        break;
    case FM_STEP: itxt((int32_t)x + 1, b); break;
    case FM_MNOTE: {
        int8_t d2 = F.pat.mel[F.p[P_MCUR] % NSTEP];
        if (d2 < 0) {
            tcat(b, "REST");
        } else {
            int32_t root = F.p[P_ROOT] % 12u;
            note_txt(60 + root + scale_deg(F.p[P_SCALE], d2) + 12 * ((int32_t)F.p[P_MOCT] - 1) - (root > 5 ? 12 : 0), b);
        }
        break;
    }
    case FM_BNOTE: {
        int8_t v = F.pat.bass[F.p[P_BCUR] % NSTEP];
        tcat(b, v == BASS_OFF ? "OFF" : v == BASS_ROOT ? "R" : NOTE_N[v % 12]);
        break;
    }
    case FM_CDEG: {
        int8_t g = F.pat.cseq[F.p[P_CSCUR] % NCSEQ];
        if (g < 0) {
            tcat(b, "-");
        } else {
            int32_t r = 48 + (int32_t)(F.p[P_ROOT] % 12u) + scale_deg(F.p[P_SCALE], g);
            chord_txt(r, g, b);
        }
        break;
    }
    case FM_TURN2: tcat(b, x == 1u ? "AGAIN" : x == 2u ? "DONE" : "TURN"); break;
    case FM_MORPH: {
        uint32_t ms = MORPH_MS[x % 9u];
        if (!ms) {
            tcat(b, "CUT");
        } else if (ms < 1000u) {
            tcat(b, "0.5S");
        } else {
            itxt((int32_t)(ms / 1000u), b);
            tcat(b, "S");
        }
        break;
    }
    case FM_CH: if (!x) tcat(b, "ALL"); else itxt((int32_t)x, b); break;
    case FM_SHIFT: tcat(b, "< >"); break;
    case FM_DICE: tcat(b, "TURN"); break;
    case FM_UPOS:                        /* where in the wavetable, or the FM ratio, or nothing */
        if (F.p[P_UWAVE] == 8u) {
            static const char *const WT[5] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "PULSE"};
            uint32_t q = x * 4u / 127u, fr = (x * 400u / 127u) % 100u;
            tcat(b, WT[q > 4u ? 4u : q]);
            if (fr >= 25u && q < 4u) {
                tcat(b, ">");
                tcat(b, WT[q + 1u]);
            }
        } else if (F.p[P_UWAVE] == 4u) {
            static const char *const RAT[11] = {"X0.5", "X1", "X1.5", "X2", "X3", "X3.5", "X4", "X5", "X6", "X7", "X8"};
            tcat(b, RAT[(x * 10u + 63u) / 127u]);
        } else {
            tcat(b, "-");
        }
        break;
    case FM_UMOD:                        /* the wavetable's sweep (either way), or the FM's depth */
        if (F.p[P_UWAVE] == 8u) {
            int32_t sw = (int32_t)x - 64;
            if (sw > 0)
                tcat(b, "+");
            itxt(sw, b + (sw > 0));
        } else if (F.p[P_UWAVE] == 4u) {
            itxt((int32_t)((x * 100u + 63u) / 127u), b);
        } else {
            tcat(b, "-");
        }
        break;
    case FM_UCUT:
        if (x >= 127u)
            tcat(b, "OPEN");
        else
            itxt((int32_t)((x * 100u + 63u) / 127u), b);
        break;
    case FM_UTIME: {                     /* the level's times, in milliseconds or seconds */
        float u = (float)x / 127.0f, sec = p == P_UATK ? 0.001f + 2.0f * u * u : p == P_UDEC ? 0.05f + 3.0f * u * u
                                                                                               : 0.02f + 3.0f * u * u;
        if (sec < 1.0f) {
            itxt((int32_t)(sec * 1000.0f + 0.5f), b);
            tcat(b, " MS");
        } else {
            itxt((int32_t)sec, b);
            tcat(b, ".");
            itxt((int32_t)(sec * 10.0f) % 10, b + strlen_(b));
            tcat(b, " S");
        }
        break;
    }
    case FM_SCTIME: case FM_SCATK: {     /* the sidechain's times, in milliseconds */
        float u = (float)x / 127.0f, sec = d->fmt == FM_SCTIME ? 0.03f * fm_powf(50.0f, u) : 0.002f + 0.06f * u * u;
        if (d->fmt == FM_SCATK && x == 0u) {
            tcat(b, "INSTANT");
            break;
        }
        itxt((int32_t)(sec * 1000.0f + 0.5f), b);
        tcat(b, " MS");
        break;
    }
    default: itxt((int32_t)x, b); break;
    }
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[64];
    for (i = 0; i < 27u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 27u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1400u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        tcat(t, ui.msg[0]);
        tcat(t, " ");
        tcat(t, ui.msg[1]);
        ann_say(t, 0, ANN_NOW);
    }
}

/* ------------------------------------------------------------------- the drones: groups, notes --- */
static void harm_update(void)            /* what the bass and the shards follow: every note held, low to high */
{
    uint32_t n, k = 0;
    for (n = 0; n < 128u && k < 8u; n++)
        if (cnt[n])
            F.harm[k++] = (int8_t)n;
    if (!k) {                            /* nothing held: the key's chord */
        int8_t c[8];
        uint32_t i, m = chord_build(48 + (int32_t)(F.p[P_ROOT] % 12u), 0, F.p, c);
        for (i = 0; i < m && i < 8u; i++)
            F.harm[i] = c[i];
        k = m;
    }
    F.harm_n = (uint8_t)k;
}
static int32_t grp_find(int32_t id)
{
    uint32_t i;
    for (i = 0; i < NGROUP; i++)
        if (grp[i].id == id)
            return (int32_t)i;
    return -1;
}
static void grp_off(int32_t id)
{
    int32_t g = grp_find(id);
    uint32_t i;
    if (g < 0)
        return;
    for (i = 0; i < grp[g].n; i++) {
        uint32_t x = (uint8_t)grp[g].note[i];
        if (cnt[x] && !--cnt[x])
            eng_event(EV_DRONE_OFF, (int32_t)x, 0.0f);
    }
    grp[g].id = -1;
    harm_update();
}
static void grp_on(int32_t id, const int8_t *notes, uint32_t n, float vel)
{
    int32_t g = grp_find(id);
    uint32_t i;
    if (g >= 0)
        grp_off(id);
    for (i = 0; i < NGROUP && grp[i].id >= 0; i++)
        ;
    if (i == NGROUP) {                   /* full: the first one goes */
        grp_off(grp[0].id);
        i = 0;
    }
    grp[i].id = (int16_t)id;
    grp[i].n = (uint8_t)(n > 8u ? 8u : n);
    for (g = 0; g < (int32_t)grp[i].n; g++) {
        uint32_t x = (uint8_t)notes[g];
        grp[i].note[g] = notes[g];
        if (!cnt[x]++)
            eng_event(EV_DRONE_ON, (int32_t)x, vel);
    }
    harm_update();
}
static void groups_all_off(int keep_seq)
{
    uint32_t i;
    for (i = 0; i < NGROUP; i++)
        if (grp[i].id >= 0 && !(keep_seq && grp[i].id == G_SEQ))
            grp_off(grp[i].id);
}
/* a key (or a MIDI note) played: its chord, as HARMONY makes it */
static void view_sync(void);
static void rec_chord(int32_t note)      /* recording: the chord's degree into the chord sequence, this bar's slot */
{
    uint32_t bars = 1u << (F.p[P_CSBARS] % 4u), slot;
    int32_t d, n = scale_len(F.p[P_SCALE]);
    if (!ui.rec || !F.playing || V.step < 0)
        return;
    key_snap(note, F.p, &d);
    d = ((d % n) + n) % n;               /* (the chord on that note of the key, in the lower octave) */
    slot = ((V.bar + (V.step >= 12 ? 1u : 0u)) / bars) % NCSEQ;   /* (late in a bar: the next bar's) */
    F.pat.cseq[slot] = (int8_t)d;
    ui.rec_chords = 1;
    view_sync();
}
static void play_note(int32_t id, int32_t note, float vel)
{
    if (id < 200)
        rec_chord(note);
    int8_t c[8];
    int32_t root = note, deg = 0;
    uint32_t n;
    if (F.p[P_CHORD] >= NFIXED && F.p[P_HARM])   /* in key: the nearest degree */
        root = key_snap(note, F.p, &deg);
    n = chord_build(root, deg, F.p, c);
    if (F.p[P_LATCH] && grp_find(id) >= 0) {     /* latched: again lets it go */
        grp_off(id);
        return;
    }
    if (F.p[P_HOLD]) {                   /* ONE at a time: the others go (the notes they share ring on) */
        uint32_t i;
        grp_on(id, c, n, vel);
        for (i = 0; i < NGROUP; i++)
            if (grp[i].id >= 0 && grp[i].id != id && grp[i].id != G_SEQ)
                grp_off(grp[i].id);
        return;
    }
    grp_on(id, c, n, vel);
}
static void release_note(int32_t id)
{
    if (!F.p[P_LATCH])
        grp_off(id);
}

/* the chord sequence: each EACH bars, the next step that has a chord (the main loop, on the engine's bars) */
static void cseq_run(void)
{
    uint32_t bars = 1u << (F.p[P_CSBARS] % 4u), i;
    if (!F.playing || !F.p[P_CSPLAYS]) {
        if (grp_find(G_SEQ) >= 0)
            grp_off(G_SEQ);
        ui.cs_idx = -1;
        ui.cs_bar = 0xFFFFFFFFu;
        return;
    }
    if (V.step < 0 || V.bar == ui.cs_bar || V.bar % bars)
        return;
    ui.cs_bar = V.bar;
    for (i = 1; i <= NCSEQ; i++) {
        int32_t k = (ui.cs_idx + (int32_t)i) % (int32_t)NCSEQ;
        if (k < 0)
            k += NCSEQ;
        if (F.pat.cseq[k] >= 0) {
            int8_t c[8];
            int32_t d = F.pat.cseq[k], r = 48 + (int32_t)(F.p[P_ROOT] % 12u) + scale_deg(F.p[P_SCALE], d);
            uint32_t n = chord_build(r, d, F.p, c);
            ui.cs_idx = (int8_t)k;
            grp_on(G_SEQ, c, n, 0.9f);
            return;
        }
    }
}

/* EVOLVE: every two bars while it plays, a hat, perc or clap step comes or goes (as VOID in the browser) */
static void evolve_run(void)
{
    static uint32_t last = 0xFFFFFFFFu;
    static const uint8_t T[5] = {3, 3, 5, 5, 2};
    uint32_t r, t, s;
    if (!F.playing || !F.p[P_DMEVOLVE] || V.step < 0 || V.bar == last || V.bar % 2u)
        return;
    last = V.bar;
    r = (plat_ms() + V.bar) * 2654435761u;
    t = T[(r >> 8) % 5u];
    s = (r >> 16) % NSTEP;
    F.pat.drum[t][s] = F.pat.drum[t][s] ? 0u : (uint8_t)(1u + ((r >> 24) & 1u));
}

/* ------------------------------------------------------------------- settings turned --- */
static uint32_t pg;                      /* the page */
static void view_sync(void)              /* the steps' settings show the step under the cursor */
{
    int8_t m = F.pat.mel[F.p[P_MCUR] % NSTEP], b = F.pat.bass[F.p[P_BCUR] % NSTEP], c = F.pat.cseq[F.p[P_CSCUR] % NCSEQ];
    uint32_t t = F.p[P_DMTRACK] % NDRUM;
    F.p[P_MNOTE] = (uint8_t)(m < 0 ? 0 : m + 1);
    F.p[P_BNOTE] = (uint8_t)(b == BASS_OFF ? 0 : b == BASS_ROOT ? 1 : b + 2);
    F.p[P_CSDEG] = (uint8_t)(c < 0 ? 0 : c + 1);
    F.p[P_DMTUNE] = F.pat.dtune[t];
    F.p[P_DMDECAY] = F.pat.ddecay[t];
    F.p[P_DMVOL] = F.pat.dvol[t];
}
static void mel_generate(void)
{
    static const char *const RH[8] = {"x.x.x..x.x..x.x.", "x..x..x.x..x..x.", "x...x.x.x...x.x.", "xx.x..x.xx.x..x.",
                                      "x.......x...x...", "x.x...x.x.x...x.", "x..x.x..x..x.x..", "x...x...x..xx..."};
    uint32_t s, r = plat_ms() * 2654435761u;
    int32_t n = scale_len(F.p[P_SCALE]), top = n * 2 > 14 ? 14 : n * 2, d = (int32_t)(r % 3u) * 2;
    const char *rh = RH[(r >> 8) % 8u];
    for (s = 0; s < NSTEP; s++) {
        r = r * 1103515245u + 12345u;
        if (rh[s] != 'x') {
            F.pat.mel[s] = MEL_REST;
            continue;
        }
        if (s >= 8u && F.pat.mel[s - 8u] >= 0 && (r >> 16) % 10u < 6u) {
            F.pat.mel[s] = F.pat.mel[s - 8u];
        } else {
            static const int8_t STEPS[9] = {-2, -1, -1, 0, 1, 1, 2, -3, 3};
            d += STEPS[(r >> 16) % 9u];
            d = d < 0 ? 0 : d > top ? top : d;
            F.pat.mel[s] = (int8_t)d;
        }
    }
}
static void drum_generate(void)
{
    static const char *const KS[] = {"2000000010200000", "2000000000000000", "2000001020000000", "2000000000200000",
                                     "2000000020000000", "2000001000100000"};
    static const char *const SS[] = {"0000000020000000", "0000200000002000", "0000000000000000", "0000000020000001"};
    uint32_t r = plat_ms() * 2654435761u, s;
    const char *k = KS[r % 6u], *sn = SS[(r >> 4) % 4u];
    int busy = (r >> 9) & 1u;
    for (s = 0; s < NSTEP; s++) {
        uint32_t x;
        r = r * 1103515245u + 12345u;
        x = (r >> 16) % 100u;
        F.pat.drum[0][s] = (uint8_t)(k[s] - '0');
        F.pat.drum[1][s] = (uint8_t)(sn[s] - '0');
        F.pat.drum[2][s] = sn[s] != '0' && x < 35u ? 1u : 0u;
        F.pat.drum[3][s] = x < ((s & 1u) ? (busy ? 40u : 10u) : (busy ? 85u : 55u)) ? 1u + (x < 30u) : 0u;
        F.pat.drum[4][s] = (s == 6u || s == 14u) && x > 65u ? 1u : 0u;
        F.pat.drum[5][s] = x > 90u ? 1u + (x > 97u) : 0u;
    }
}

static void param_changed(uint32_t p, int32_t was)
{
    uint32_t x = F.p[p];
    switch (p) {
    case P_DPRESET: {                    /* a drone preset: its nine settings */
        const uint8_t *v = DPRESET_V[x % NDPRESET];
        F.p[P_CHOIR] = v[0], F.p[P_PARTIALS] = v[1], F.p[P_BREATH] = v[2], F.p[P_TIMBRE] = v[3], F.p[P_TONE] = v[4];
        F.p[P_DETUNE] = v[5], F.p[P_ATTACK] = v[6], F.p[P_RELEASE] = v[7], F.p[P_DMOTION] = v[8], F.p[P_FCUT] = v[9];
        F.p[P_DWAVE] = v[10], F.p[P_PSERIES] = v[11];
        break;
    }
    case P_ROOT: case P_SCALE: case P_CHORD: case P_VOICING: case P_HARM: {   /* the held chords made again */
        uint32_t i;
        for (i = 0; i < NGROUP; i++) {
            int32_t id = grp[i].id;
            if (id >= 0 && id < 127) {
                int32_t note = id >= 100 ? id - 100 : 53 + id + 12 * st.octave, root = note, deg = 0;
                int8_t c[8];
                uint32_t n;
                if (F.p[P_CHORD] >= NFIXED && F.p[P_HARM])
                    root = key_snap(note, F.p, &deg);
                n = chord_build(root, deg, F.p, c);
                grp_on(id, c, n, 0.9f);
            }
        }
        harm_update();
        break;
    }
    case P_HOLD:
        if (x) {                         /* ONE: only the last stays */
            uint32_t i, last = NGROUP;
            for (i = 0; i < NGROUP; i++)
                if (grp[i].id >= 0 && grp[i].id < 200)
                    last = i;
            for (i = 0; i < NGROUP; i++)
                if (grp[i].id >= 0 && grp[i].id < 200 && i != last)
                    grp_off(grp[i].id);
        }
        break;
    case P_LATCH:
        if (!x)
            groups_all_off(1);
        break;
    case P_MCUR: case P_BCUR: case P_CSCUR: case P_DMTRACK:
        view_sync();
        break;
    case P_MNOTE: F.pat.mel[F.p[P_MCUR] % NSTEP] = (int8_t)((int32_t)x - 1); break;
    case P_BNOTE: F.pat.bass[F.p[P_BCUR] % NSTEP] = (int8_t)((int32_t)x - 2); break;
    case P_CSDEG: F.pat.cseq[F.p[P_CSCUR] % NCSEQ] = (int8_t)((int32_t)x - 1); break;
    case P_DMTUNE: F.pat.dtune[F.p[P_DMTRACK] % NDRUM] = (uint8_t)x; break;
    case P_DMDECAY: F.pat.ddecay[F.p[P_DMTRACK] % NDRUM] = (uint8_t)x; break;
    case P_DMVOL: F.pat.dvol[F.p[P_DMTRACK] % NDRUM] = (uint8_t)x; break;
    case P_MGEN:
        if (x == 2u) {
            mel_generate();
            app_say("MELODY", "NEW");
            view_sync();
        }
        break;
    case P_MCLEAR:
        if (x == 2u) {
            uint32_t s;
            for (s = 0; s < NSTEP; s++)
                F.pat.mel[s] = MEL_REST;
            app_say("MELODY", "CLEARED");
            view_sync();
        }
        break;
    case P_MSHIFT: {                     /* turned: the steps shift a step that way, the knob back to the middle */
        int8_t t[NSTEP];
        uint32_t s;
        memcpy(t, F.pat.mel, NSTEP);
        for (s = 0; s < NSTEP; s++)
            F.pat.mel[s] = x > (uint32_t)was ? t[(s + NSTEP - 1u) % NSTEP] : t[(s + 1u) % NSTEP];
        F.p[P_MSHIFT] = 1;
        view_sync();
        break;
    }
    case P_FREEZE:
        app_say("FREEZE", x ? "ON" : "OFF");
        break;
    case P_UFROM:                        /* CUSTOM starts from a factory sound: its settings copied in */
        eng_custom_from(x, F.p);
        F.p[P_MSOUND] = MS_CUSTOM;
        app_say("CUSTOM FROM", MSOUND_NAME[x % (NMSOUND - 1u)]);
        break;
    case P_UWAVE: case P_UPOS: case P_UMOD: case P_UP2R: case P_UP2L: case P_UCUT: case P_UFENV: case P_URES:
    case P_UATK: case P_UDEC: case P_USUS: case P_UREL: case P_UVIB: case P_UBREATH: case P_UDROP:
        if (F.p[P_MSOUND] != MS_CUSTOM) {   /* (designing: the melody plays the custom sound) */
            F.p[P_MSOUND] = MS_CUSTOM;
            app_say("MELODY SOUND", "CUSTOM");
        }
        break;
    case P_DRAND: {                      /* a new drone: every drone setting rolled, attack kept gentle */
        static const uint8_t R[] = {P_CHOIR, P_PARTIALS, P_BREATH, P_TIMBRE, P_TONE, P_DETUNE, P_RELEASE, P_DMOTION};
        uint32_t i, r = plat_ms() * 2654435761u + ui.keys;
        for (i = 0; i < sizeof R; i++) {
            r = r * 1103515245u + 12345u;
            F.p[R[i]] = (uint8_t)(6u + (r >> 16) % 115u);
        }
        r = r * 1103515245u + 12345u;
        F.p[P_ATTACK] = (uint8_t)(25u + (r >> 16) % 77u);
        r = r * 1103515245u + 12345u;
        F.p[P_DWAVE] = (uint8_t)((r >> 16) % NDWAVE);
        r = r * 1103515245u + 12345u;
        F.p[P_PSERIES] = (uint8_t)((r >> 16) % NPSERIES);
        F.p[P_TONE] = (uint8_t)(F.p[P_TONE] * 3u / 4u);   /* (rolled: kept on the soft side) */
        F.p[P_DRAND] = 0;
        app_say("DRONE", "RANDOM");
        break;
    }
    case P_MRAND: {                      /* a different melody sound, heard */
        uint32_t r = (plat_ms() * 2654435761u) >> 8, n = (F.p[P_MSOUND] % MS_CUSTOM + 1u + r % (MS_CUSTOM - 1u)) % MS_CUSTOM;
        F.p[P_MSOUND] = (uint8_t)n;
        F.p[P_MRAND] = 0;
        if (!F.playing)
            eng_event(EV_MEL, 69, 0.8f);
        app_say("MELODY", MSOUND_NAME[n]);
        break;
    }
    default:
        break;
    }
}

static void param_turn(uint32_t p, int32_t d)
{
    const pd_t *pd = &PD[p];
    int32_t was = F.p[p], x;
    if (p >= NP)
        return;
    if (pd->fmt == FM_TURN2) {           /* an action: turned once, AGAIN; again within two seconds, done */
        uint32_t now = plat_ms();
        if (d <= 0)
            return;
        if (F.p[p] == 1u && now - ui.turn2_at[p] < 2000u) {
            F.p[p] = 2;
            param_changed(p, was);
        } else {
            F.p[p] = 1;
        }
        ui.turn2_at[p] = now;
        return;
    }
    if (pd->fmt == FM_DICE) {            /* a die: each turn rolls again */
        param_changed(p, was);
        return;
    }
    if (pd->fmt == FM_SHIFT) {
        F.p[p] = (uint8_t)(d > 0 ? 2 : 0);
        param_changed(p, 1);
        return;
    }
    x = was + d;
    x = x < 0 ? 0 : x > pd->max ? pd->max : x;
    if (x == was)
        return;
    F.p[p] = (uint8_t)x;
    param_changed(p, was);
}

/* ------------------------------------------------------------------- scenes, the state --- */
static void scene_take(scene_t *s)
{
    uint32_t i, n = 0;
    memcpy(s->p, F.p, NP);
    memcpy(&s->pat, &F.pat, sizeof s->pat);
    memset(s->held, 0, sizeof s->held);
    for (i = 0; i < 128u && n < 8u; i++)
        if (cnt[i])
            s->held[n++] = (int8_t)i;
}
static void sane(void)                   /* every setting in its range; the actions, the cursors at rest */
{
    uint32_t i, t;
    for (i = 0; i < NP; i++)
        if (F.p[i] > PD[i].max)
            F.p[i] = PD[i].def;
    F.p[P_MGEN] = F.p[P_MCLEAR] = 0;
    F.p[P_MSHIFT] = 1;
    F.p[P_FREEZE] = 0;
    for (t = 0; t < NDRUM; t++)
        for (i = 0; i < NSTEP; i++)
            if (F.pat.drum[t][i] > 2u)
                F.pat.drum[t][i] = 0;
    for (i = 0; i < NSTEP; i++) {
        if (F.pat.mel[i] < MEL_REST || F.pat.mel[i] > 21)
            F.pat.mel[i] = MEL_REST;
        if (F.pat.bass[i] < BASS_OFF || F.pat.bass[i] > 11)
            F.pat.bass[i] = BASS_OFF;
    }
    for (i = 0; i < NCSEQ; i++)
        if (F.pat.cseq[i] < -1 || F.pat.cseq[i] > 13)
            F.pat.cseq[i] = -1;
    view_sync();
}
static void scene_give(const scene_t *s, int morph)
{
    uint32_t i, n = 0;
    int8_t held[8];
    uint32_t ms = MORPH_MS[F.p[P_MORPH] % 9u];
    if (morph && ms) {                   /* MORPH: the settings that glide, from here to there */
        for (i = 0; i < NP; i++) {
            ui.morph_from[i] = (float)F.p[i];
            ui.morph_to[i] = (float)s->p[i];
        }
        ui.morph_at = plat_ms();
        ui.morph_ms = ms;
        ui.morphing = 1;
    }
    for (i = 0; i < NP; i++)             /* the names (a preset, a model, a type) switch at once; the rest after */
        if (!(morph && ms) || PD[i].fmt != FM_PCT || i == P_MORPH)
            F.p[i] = s->p[i];
    F.p[P_MORPH] = st.now.p[P_MORPH];
    memcpy(&F.pat, &s->pat, sizeof F.pat);
    sane();
    groups_all_off(0);
    for (i = 0; i < 8u; i++)
        if (s->held[i] > 0)
            held[n++] = s->held[i];
    if (n)
        grp_on(G_SCENE, held, n, 0.9f);
    harm_update();
}
static void morph_run(void)
{
    uint32_t i, t;
    float k;
    if (!ui.morphing)
        return;
    t = plat_ms() - ui.morph_at;
    k = t >= ui.morph_ms ? 1.0f : (float)t / (float)ui.morph_ms;
    k = k * k * (3.0f - 2.0f * k);
    for (i = 0; i < NP; i++)
        if (PD[i].fmt == FM_PCT && i != P_MORPH)
            F.p[i] = (uint8_t)(ui.morph_from[i] + (ui.morph_to[i] - ui.morph_from[i]) * k + 0.5f);
    if (t >= ui.morph_ms)
        ui.morphing = 0;
    ui.redraw = 1;
}

static void defaults(void)
{
    uint32_t i, t;
    static const char *const K = "2000000010200000", *S = "0000200000002000", *H = "1010101110101011";
    static const int8_t MEL[NSTEP] = {4, -1, -1, 2, -1, -1, 4, -1, 7, -1, -1, 6, -1, 4, -1, -1};
    memset(&F, 0, sizeof F);
    for (i = 0; i < NP; i++)
        F.p[i] = PD[i].def;
    for (i = 0; i < NSTEP; i++) {
        F.pat.drum[0][i] = (uint8_t)(K[i] - '0');
        F.pat.drum[1][i] = (uint8_t)(S[i] - '0');
        F.pat.drum[3][i] = (uint8_t)(H[i] - '0');
        F.pat.mel[i] = MEL[i];
        F.pat.bass[i] = BASS_OFF;
    }
    F.pat.bass[0] = F.pat.bass[10] = BASS_ROOT;
    for (t = 0; t < NDRUM; t++) {
        F.pat.dtune[t] = 12;
        F.pat.ddecay[t] = 64;
        F.pat.dvol[t] = 100;
    }
    for (i = 0; i < NCSEQ; i++)
        F.pat.cseq[i] = -1;
    F.pat.cseq[0] = 0, F.pat.cseq[1] = 5, F.pat.cseq[2] = 3, F.pat.cseq[3] = 6;
    eng_custom_from(0, F.p);             /* (CUSTOM starts as the E.PIANO) */
    st.octave = 0;
}

static int state_save(void)
{
    st.magic = STATE_MAGIC;
    scene_take(&st.now);
    st.page = (uint8_t)pg;
    return plat_store_save(OBJ_STATE, &st, sizeof st);
}

/* ------------------------------------------------------------------- the audio interrupt --- */
static float gain_q;
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {  /* MIDI: the clock to the engine, the notes to the main loop */
        uint32_t s = (pkt >> 8) & 0xFFu;
        if (s >= 0xF8u) {
            if (F.p[P_CLOCK]) {
                eng_midi_clock(s);
                if (s == 0xFAu || s == 0xFBu)
                    ui.ext_play = 1;
                else if (s == 0xFCu)
                    ui.ext_play = 2;
            }
            continue;
        }
        if (mq_w - mq_r < 32u)
            mq[mq_w++ % 32u] = pkt;
    }
    gain_q = (float)master_q12 / 4096.0f;
    PLAT_FAR(eng_render)(lr, n, gain_q);  /* (the sound runs from RAM: reached through a pointer) */
}

void app_panic(void) { eng_event(EV_PANIC, 0, 0.0f); }

/* ------------------------------------------------------------------- the keys --- */
static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
static int32_t white_of(uint32_t k)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (WK[i] == k)
            return (int32_t)i;
    return -1;
}
static int32_t black_of(uint32_t k)
{
    uint32_t i;
    for (i = 0; i < 11u; i++)
        if (BK[i] == k)
            return (int32_t)i;
    return -1;
}
static int32_t key_note(uint32_t k) { return 53 + (int32_t)k + 12 * st.octave; }

/* a step page: the cursor on to the next step */
static void cur_next(uint32_t p, uint32_t len)
{
    F.p[p] = (uint8_t)((F.p[p] + 1u) % len);
    view_sync();
}

static void drum_key(uint32_t k)
{
    int32_t w = white_of(k), b = black_of(k);
    uint32_t t = F.p[P_DMTRACK] % NDRUM, now = plat_ms();
    char v[12];
    if (w >= 0) {                        /* a step: off, FULL, SOFT, off */
        uint32_t x = F.pat.drum[t][w];
        F.pat.drum[t][w] = (uint8_t)(x == 0u ? 2u : x == 2u ? 1u : 0u);
        if (F.pat.drum[t][w] && !F.playing)
            eng_event(EV_DRUM, (int32_t)t, F.pat.drum[t][w] == 2u ? 1.0f : 0.55f);
        return;
    }
    if (b >= 0 && b < (int32_t)NDRUM) {  /* a track: picked, heard */
        F.p[P_DMTRACK] = (uint8_t)b;
        view_sync();
        eng_event(EV_DRUM, b, 0.9f);
        app_say("TRACK", TRACK_N[b]);
        return;
    }
    v[0] = 0;
    switch (b) {
    case 6:
        F.fill = 1;
        app_say("FILL", F.playing ? "NEXT HALF BAR" : "PRESS PLAY");
        break;
    case 7:
        F.p[P_SWING] = (uint8_t)(F.p[P_SWING] >= 96u ? 0u : F.p[P_SWING] + 32u);
        itxt((int32_t)F.p[P_SWING] * 100 / 127, v);
        app_say("SWING", v);
        break;
    case 8:
        F.p[P_DMKIT] = (uint8_t)((F.p[P_DMKIT] + 1u) % NKIT);
        eng_event(EV_DRUM, 0, 1.0f);
        app_say("KIT", KIT_NAME[F.p[P_DMKIT]]);
        break;
    case 9:                              /* NEW GROOVE: tapped twice */
    case 10:                             /* CLEAR the track: tapped twice (OCT- held: every track) */
        if (ui.clear_arm == (uint8_t)b && now - ui.clear_at < 900u) {
            ui.clear_arm = 0;
            if (b == 9) {
                drum_generate();
                app_say("NEW GROOVE", "");
            } else {
                uint32_t i;
                for (i = 0; i < NDRUM; i++)
                    if (i == t || (ui.btn >> B_OCTDN & 1u))
                        memset(F.pat.drum[i], 0, NSTEP);
                app_say("CLEARED", (ui.btn >> B_OCTDN & 1u) ? "EVERY TRACK" : TRACK_N[t]);
            }
        } else {
            ui.clear_arm = (uint8_t)b;
            ui.clear_at = now;
            app_say(b == 9 ? "NEW GROOVE?" : "CLEAR?", "TAP AGAIN");
        }
        break;
    default:
        break;
    }
}

/* the melody step's degree nearest a note (the steps keep degrees of the key, from the melody's own octave) */
static int32_t mel_base(void)
{
    int32_t root = F.p[P_ROOT] % 12u;
    return 60 + root + 12 * ((int32_t)F.p[P_MOCT] - 1) - (root > 5 ? 12 : 0);
}
static int32_t mel_degree(int32_t note)
{
    int32_t base = mel_base(), d, snapped = key_snap(note, F.p, &d), dd = 0, best = 99, i;
    for (i = 0; i <= 21; i++) {
        int32_t x = base + scale_deg(F.p[P_SCALE], i) - snapped;
        x = x < 0 ? -x : x;
        if (x < best) {
            best = x;
            dd = i;
        }
    }
    return dd;
}
/* the melody played freely: the note sounds while held; recording, it goes into the nearest step too */
static int32_t live_on(int32_t note, float v)
{
    int32_t n = note + 12 * ((int32_t)F.p[P_MOCT] - 1);
    n = n < 24 ? 24 : n > 108 ? 108 : n;
    eng_event(EV_MEL_ON, n, v);
    if (ui.rec && F.playing && V.step >= 0) {
        uint32_t s = ((uint32_t)V.step + (V.sfrac > 0.5f ? 1u : 0u)) % NSTEP;
        F.pat.mel[s] = (int8_t)mel_degree(n);
        if (!F.p[P_MPLAYS])
            F.p[P_MPLAYS] = 1;
        view_sync();
    }
    return n;
}

static void key_down(uint32_t k, uint32_t vel)
{
    const page_t *pp = &PAGES[pg];
    float v = 0.35f + 0.65f * (float)vel / 127.0f;
    int32_t note = key_note(k);
    if (pp->keys == K_SCENES) {          /* the scenes on the white keys: a tap takes one, SAVE held stores */
        int32_t w = white_of(k);
        char t[12] = "SCENE ";
        if (w < 0 || w >= (int32_t)NSCN)
            return;
        itxt(w + 1, t + 6);
        if (ui.btn >> B_SAVE & 1u) {
            ui.btn_used[B_SAVE] = 1;
            scene_take(&scn.s[w]);
            memcpy(scn.s[w].name, t, sizeof scn.s[w].name);
            scn.used |= 1u << w;
            scn.magic = STATE_MAGIC;
            st.scene = (uint8_t)w;
            app_say("STORED", plat_store_save(OBJ_SCENES, &scn, sizeof scn) ? "FAILED" : t);
            state_save();
        } else if (scn.used >> w & 1u) {
            st.scene = (uint8_t)w;
            scene_give(&scn.s[w], 1);
            app_say(t, MORPH_MS[F.p[P_MORPH] % 9u] ? "MORPHING" : "");
        } else {
            app_say(t, "EMPTY: SAVE + KEY STORES");
        }
        return;
    }
    switch (pp->keys) {
    case K_DRUMS:
        drum_key(k);
        return;
    case K_MEL: {                        /* the note into the step under the cursor, then on */
        int32_t root = F.p[P_ROOT] % 12u, base = 60 + root + 12 * ((int32_t)F.p[P_MOCT] - 1) - (root > 5 ? 12 : 0), d;
        int32_t snapped = key_snap(note, F.p, &d), dd = 0, best = 99, i;
        for (i = 0; i <= 21; i++) {      /* (the degree from the melody's own base) */
            int32_t x = base + scale_deg(F.p[P_SCALE], i) - snapped;
            x = x < 0 ? -x : x;
            if (x < best) {
                best = x;
                dd = i;
            }
        }
        F.pat.mel[F.p[P_MCUR] % NSTEP] = (int8_t)dd;
        eng_event(EV_MEL, base + scale_deg(F.p[P_SCALE], dd), v);
        cur_next(P_MCUR, NSTEP);
        return;
    }
    case K_BASS:                         /* its note, fixed */
        F.pat.bass[F.p[P_BCUR] % NSTEP] = (int8_t)(note % 12);
        eng_event(EV_BASS, 36 + note % 12, 0.3f);
        cur_next(P_BCUR, NSTEP);
        return;
    case K_CSEQ: {                       /* the chord on that degree */
        int32_t d, i;
        key_snap(note, F.p, &d);
        i = scale_len(F.p[P_SCALE]);
        d = ((d % (2 * i)) + 2 * i) % (2 * i);
        F.pat.cseq[F.p[P_CSCUR] % NCSEQ] = (int8_t)(d > 13 ? 13 : d);
        {
            int8_t c[8];
            int32_t r = 48 + (int32_t)(F.p[P_ROOT] % 12u) + scale_deg(F.p[P_SCALE], d);
            uint32_t n = chord_build(r, d, F.p, c);
            if (!F.playing || !F.p[P_CSPLAYS])
                grp_on(G_SEQ, c, n, 0.9f);   /* (heard while it is stopped; the sequence takes over on PLAY) */
        }
        cur_next(P_CSCUR, NCSEQ);
        return;
    }
    case K_LIVE:
        ui.livenote[k] = (int8_t)live_on(note, v);
        return;
    default:
        if (F.p[P_KEYPLAY] != 0u)        /* MELODY or BOTH: the melody's sound on the keys */
            ui.livenote[k] = (int8_t)live_on(note, v);
        if (F.p[P_KEYPLAY] != 1u)        /* CHORDS or BOTH */
            play_note((int32_t)k, note, v);
        return;
    }
}
static void key_up(uint32_t k)
{
    if (ui.livenote[k] >= 0) {
        eng_event(EV_MEL_OFF, ui.livenote[k], 0.0f);
        ui.livenote[k] = -1;
    }
    if (PAGES[pg].keys == K_DRONE)
        release_note((int32_t)k);
}

/* REC: FREEZE, or on a step page a rest (the bass's R) and on */
static void rec_tap(void)
{
    switch (PAGES[pg].keys) {
    case K_MEL:
        F.pat.mel[F.p[P_MCUR] % NSTEP] = MEL_REST;
        cur_next(P_MCUR, NSTEP);
        app_say("REST", "");
        return;
    case K_BASS:
        F.pat.bass[F.p[P_BCUR] % NSTEP] = (ui.btn >> B_OCTDN & 1u) ? BASS_OFF : BASS_ROOT;
        app_say((ui.btn >> B_OCTDN & 1u) ? "OFF" : "R", "");
        cur_next(P_BCUR, NSTEP);
        return;
    case K_CSEQ:
        F.pat.cseq[F.p[P_CSCUR] % NCSEQ] = -1;
        cur_next(P_CSCUR, NCSEQ);
        return;
    default:
        break;
    }
    ui.rec ^= 1u;                        /* REC: what the keys play goes into the steps (melody notes, chords) */
    if (ui.rec) {
        ui.rec_chords = 0;
        if (!F.playing)
            F.playing = 1;
        if (PAGES[pg].keys == K_LIVE || F.p[P_KEYPLAY] != 0u)
            F.p[P_MPLAYS] = 1;
        else if (F.p[P_CSPLAYS]) {       /* (recording chords: the sequence quiet meanwhile, so it doesn't fight you) */
            F.p[P_CSPLAYS] = 0;
            ui.rec_cs_was = 1;
        }
        app_say("RECORDING", PAGES[pg].keys == K_LIVE || F.p[P_KEYPLAY] == 1u ? "PLAY THE MELODY" : "PLAY CHORDS");
    } else {
        if (ui.rec_chords || ui.rec_cs_was)
            F.p[P_CSPLAYS] = 1;          /* the chords played come back, each EACH bars */
        ui.rec_cs_was = 0;
        app_say("RECORD", ui.rec_chords ? "CHORDS KEPT" : "OFF");
    }
}

static void page_button(uint32_t b)
{
    uint32_t i, from = PAGES[pg].button == b ? pg + 1u : 0u;
    for (i = 0; i < NPAGES; i++) {
        uint32_t q = (from + i) % NPAGES;
        if (PAGES[q].button == b) {
            pg = q;
            ui.touched = -1;
            ui.redraw = 1;
            return;
        }
    }
}
static void page_step(int32_t d)         /* SELECT: through every page, in order (HOME .. SCENES), round */
{
    pg = (pg + NPAGES + (uint32_t)(d > 0 ? 1 : -1)) % NPAGES;
    ui.touched = -1;
    ui.redraw = 1;
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, now = plat_ms();
    int32_t e;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit))
            key_down(k, touch_vel(k));
        else if (!(keys & bit) && (ui.keys & bit))
            key_up(k);
    }
    for (k = 0; k < NB; k++) {           /* buttons: on release (a hold does something else) */
        uint32_t bit = 1u << k;
        if ((btn & bit) && !(ui.btn & bit)) {
            ui.btn_at[k] = now;
            ui.btn_used[k] = 0;
            if (k == B_PLAY) {
                F.playing ^= 1u;
                ui.btn_used[k] = 1;
                if (!F.playing) {
                    F.fill = 0;
                    ui.rec = 0;
                    if (grp_find(G_SEQ) >= 0)
                        grp_off(G_SEQ);
                }
                app_say(F.playing ? "PLAY" : "STOP", "");
            } else if (k == B_OCTDN || k == B_OCTUP) {
                if ((btn >> B_OCTDN & 1u) && (btn >> B_OCTUP & 1u)) {   /* both: every drone let go */
                    groups_all_off(0);
                    app_say("RELEASE ALL", "");
                    ui.btn_used[B_OCTDN] = ui.btn_used[B_OCTUP] = 1;
                }
            }
        } else if (!(btn & bit) && (ui.btn & bit) && !ui.btn_used[k]) {
            if (k == B_OCTDN || k == B_OCTUP) {
                int32_t o = st.octave + (k == B_OCTUP ? 1 : -1);
                char t[6];
                if (o >= -3 && o <= 3) {
                    st.octave = (int8_t)o;
                    t[0] = o < 0 ? '-' : '+';
                    t[1] = (char)('0' + (o < 0 ? -o : o));
                    t[2] = 0;
                    app_say("OCTAVE", t);
                }
            } else if (k == B_REC) {      /* REC tapped: record (held: freeze, below) */
                rec_tap();
            } else if (k == B_SAVE && now - ui.btn_at[k] > 900u) {
                app_say("STATE", state_save() ? "NOT SAVED" : "SAVED");
            } else {
                page_button(k);
            }
        }
        if ((btn & bit) && k == B_REC && !ui.btn_used[k] && now - ui.btn_at[k] > 600u) {
            ui.btn_used[k] = 1;              /* REC held: the cloud frozen (or let go) */
            F.p[P_FREEZE] ^= 1u;
            app_say("FREEZE", F.p[P_FREEZE] ? "ON" : "OFF");
        }
        if ((btn & bit) && k == B_SAVE && !ui.btn_used[k] && now - ui.btn_at[k] > 900u && PAGES[pg].keys != K_SCENES) {
            ui.btn_used[k] = 1;              /* SAVE held: the state kept now */
            app_say("STATE", state_save() ? "NOT SAVED" : "SAVED");
        }
    }
    for (k = 0; k < NE; k++) {
        if ((e = plat_enc((int)k)) == 0)
            continue;
        if (k == EN_SELECT) {
            page_step(e);
        } else if (k == EN_ALGO) {
            param_turn(P_CHORD, e);
            app_say("CHORD", CHORD_NAME[F.p[P_CHORD]]);
        } else if (k == EN_PRESET) {
            param_turn(P_DPRESET, e);
            app_say("DRONE", DPRESET_NAME[F.p[P_DPRESET]]);
        } else {
            uint32_t i = k - EN_K1, p = PAGES[pg].p[i];
            if (p < NP) {
                int32_t step = e;
                if (PD[p].max >= 100u && (e > 1 || e < -1))
                    step = e * 3;        /* (quick turns go further) */
                param_turn(p, step);
                ui.touched = (int8_t)i;
                ui.touched_at = now;
            }
        }
        ui.redraw = 1;
    }
    while (mq_r != mq_w) {               /* MIDI notes in: the drones, as the keys play them */
        uint32_t pkt = mq[mq_r++ % 32u], s = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24, ch = s & 15u;
        if (F.p[P_MIDICH] && ch + 1u != F.p[P_MIDICH])
            continue;
        if ((s & 0xF0u) == 0x90u && d2) {
            float v = 0.35f + 0.65f * (float)d2 / 127.0f;
            if (F.p[P_KEYPLAY] != 0u)
                ui.midilive[d1] = (int8_t)(live_on((int32_t)d1, v) + 1);
            if (F.p[P_KEYPLAY] != 1u)
                play_note(100 + (int32_t)d1, (int32_t)d1, v);
        } else if ((s & 0xF0u) == 0x80u || (s & 0xF0u) == 0x90u) {
            if (ui.midilive[d1]) {
                eng_event(EV_MEL_OFF, ui.midilive[d1] - 1, 0.0f);
                ui.midilive[d1] = 0;
            }
            release_note(100 + (int32_t)d1);
        }
        else if ((s & 0xF0u) == 0xD0u)   /* channel aftertouch: pressure */
            ui.at = (uint8_t)d1;
        else if ((s & 0xF0u) == 0xA0u)   /* poly aftertouch */
            ui.at = (uint8_t)d2;
        else if ((s & 0xF0u) == 0xB0u && d1 == 64u)
            F.p[P_FREEZE] = d2 >= 64u;
        else if ((s & 0xF0u) == 0xB0u && d1 == 1u)
            F.p[P_FCUT] = (uint8_t)d2;
        ui.redraw = 1;
    }
    if (ui.ext_play) {
        F.playing = ui.ext_play == 1u;
        ui.ext_play = 0;
        ui.redraw = 1;
    }
    {                                    /* pressure: the hardest-pressed key held on a playing page, smoothed */
        uint32_t pmax = 0;
        if (PAGES[pg].keys == K_DRONE)
            for (k = 0; k < NKEYS; k++)
                if (keys >> k & 1u) {
                    uint32_t pr = touch_pressure(k);
                    pmax = pr > pmax ? pr : pmax;
                }
        if (pmax || ui.at == 0u)
            F.press += ((float)pmax / 127.0f - F.press) * 0.25f;
        else
            F.press += ((float)ui.at / 127.0f - F.press) * 0.25f;
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
}

static uint32_t blink_ms(void) { return plat_ms(); }

/* the lights: the page's button; the keys as the page has them */
static void lights(void)
{
    uint32_t b = 1u << PAGES[pg].button, kb = 0, kd = 0, i;
    const page_t *pp = &PAGES[pg];
    if (F.playing)
        b |= 1u << B_PLAY;
    if (F.p[P_FREEZE] || (ui.rec && (blink_ms() / 300u) % 2u))   /* (recording: REC blinks) */
        b |= 1u << B_REC;
    switch (pp->keys) {
    case K_DRUMS: {
        uint32_t t = F.p[P_DMTRACK] % NDRUM;
        for (i = 0; i < NSTEP; i++) {
            uint32_t x = F.pat.drum[t][i];
            if (F.playing && V.step == (int8_t)i)
                (x ? 0 : (kd |= 1u << WK[i]));
            else if (x == 2u)
                kb |= 1u << WK[i];
            else if (x == 1u)
                kd |= 1u << WK[i];
        }
        for (i = 0; i < 11u; i++)
            if (i == t)
                kb |= 1u << BK[i];
            else
                kd |= 1u << BK[i];
        break;
    }
    case K_MEL: case K_BASS: case K_CSEQ: {
        uint32_t cur = pp->keys == K_MEL ? F.p[P_MCUR] : pp->keys == K_BASS ? F.p[P_BCUR] : F.p[P_CSCUR];
        uint32_t len = pp->keys == K_CSEQ ? NCSEQ : NSTEP;
        for (i = 0; i < len; i++) {
            int on = pp->keys == K_MEL ? F.pat.mel[i] >= 0 : pp->keys == K_BASS ? F.pat.bass[i] != BASS_OFF : F.pat.cseq[i] >= 0;
            if (i == cur)
                kb |= 1u << WK[i];
            else if (on)
                kd |= 1u << WK[i];
        }
        break;
    }
    case K_SCENES:
        for (i = 0; i < NSCN; i++)
            if (i == st.scene && (scn.used >> i & 1u))
                kb |= 1u << WK[i];
            else if (scn.used >> i & 1u)
                kd |= 1u << WK[i];
        break;
    default: {                           /* the drones: the roots held bright, the key's notes dim */
        int32_t root = F.p[P_ROOT] % 12u;
        uint32_t sc = F.p[P_SCALE] % NSCALES, n = (uint32_t)scale_len(sc), j;
        for (i = 0; i < NKEYS; i++) {
            int32_t note = key_note(i), rel = ((note - root) % 12 + 12) % 12;
            if (grp_find((int32_t)i) >= 0) {
                kb |= 1u << i;
                continue;
            }
            for (j = 0; j < n; j++)
                if (SCALES[sc][1 + j] == rel)
                    kd |= 1u << i;
        }
        break;
    }
    }
    plat_lights(b | ui.btn, kb | ui.keys, 0, kd & ~(kb | ui.keys), 1);
}

/* ------------------------------------------------------------------- the screen --- */
/* the theme: black, light pink, and cyan for what's live (the cursor, the beat, the knob just turned) */
#define C_BG RGB(4, 3, 5)                /* black */
#define C_ICE RGB(255, 184, 216)         /* light pink: names, values, notes */
#define C_AMBER RGB(80, 228, 255)        /* cyan: the accent */
#define C_ACC_MID RGB(44, 156, 190)
#define C_ACC_DIM RGB(26, 94, 118)
#define C_ACC_BG RGB(8, 40, 52)
#define C_MIST RGB(176, 150, 164)        /* labels */
#define C_SLATE RGB(88, 68, 80)
#define C_RULE RGB(40, 26, 34)
#define C_DEEP RGB(30, 14, 24)           /* a dark plum under the cursor */
#define C_SOFT RGB(176, 92, 134)         /* deeper pink: bars, soft steps */

static inline void px(int32_t x, int32_t y, uint16_t c) { cv_pset(x, y, c); }
static void box(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { cv_rect(x, y, w, h, c); }
static int32_t tw(const felucca_font_t *f, const char *s) { return text_w(f, s); }

static void draw_header(void)
{
    char t[40];
    uint32_t now = plat_ms();
    cv_begin(240, 30, C_BG);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 5, &FONT_B, ui.msg[0], C_ICE);
        cv_text(x + 8, 9, &FONT_S, ui.msg[1], C_AMBER);
    } else {
        int32_t x;
        cv_text(8, 5, &FONT_B, PAGES[pg].name, C_ICE);
        t[0] = 0;
        itxt((int32_t)(V.bpm + 0.5f), t);
        tcat(t, F.playing ? "  >" : "  .");
        x = 232 - tw(&FONT_S, t);
        cv_text(x, 9, &FONT_S, t, F.playing ? C_AMBER : C_SLATE);
        if (ui.rec)
            cv_text(x - 34, 9, &FONT_S, "REC", C_AMBER);
        else if (F.p[P_FREEZE])
            cv_text(x - 34, 9, &FONT_S, "FRZ", C_ICE);
        if (PAGES[pg].button == B_GLO) { /* GLOBAL: how hard the sound works the FM-1 (and its late blocks) */
            uint32_t c = plat_cpu_pct();
            t[0] = 0;
            tcat(t, "CPU ");
            itxt((int32_t)c, t + 4);
            tcat(t, "%");
            cv_text(x - 46 - tw(&FONT_S, t), 9, &FONT_S, t, c > 85u ? C_AMBER : C_MIST);
        }
    }
    box(0, 29, 240, 1, C_RULE);
    cv_blit(0, 0);
}

/* the logo (logo.h: each pixel 0..15 of its pink) onto the canvas at x0, y0; rows from row0, n of them */
static void draw_logo_img(const uint8_t *img, uint32_t w, uint32_t row0, uint32_t n, int32_t x0, int32_t y0)
{
    static uint16_t pal[16];
    static int made;
    uint32_t x, y;
    if (!made) {                         /* black .. the logo's pink, sixteen steps */
        uint32_t k;
        for (k = 0; k < 16u; k++)
            pal[k] = RGB(LOGO_PINK_R * k / 15u, LOGO_PINK_G * k / 15u, LOGO_PINK_B * k / 15u);
        made = 1;
    }
    for (y = 0; y < n; y++) {
        const uint8_t *row = img + (row0 + y) * (w / 2u);
        for (x = 0; x < w; x++) {
            uint32_t v = (row[x >> 1] >> (x & 1u ? 0 : 4)) & 15u;
            if (v)
                px(x0 + (int32_t)x, y0 + (int32_t)y, pal[v]);
        }
    }
}


void app_splash(void)                    /* the first screen at boot: the logo, in three strips */
{
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        cv_begin(240, 80, RGB(0, 0, 0));
        draw_logo_img(logo_splash, LOGO_SPLASH_W, k * 80u, 80u, 0, 0);
        cv_blit(0, (int32_t)(k * 80u));
    }
}

void app_overload(void)                  /* the render ran late: a drone voice fewer (the main loop gives it back) */
{
    if (eng_maxdv > 3u)
        eng_maxdv--;
}

/* the river: each drone voice a current, its height its pitch, its brightness its level; the chord's name */
static void view_river(void)
{
    uint32_t j, now = plat_ms();
    int32_t x, y;
    char t[24];
    float ph = (float)(now % 100000u) * 0.001f;
    int32_t lab[8], nl = 0, q;
    for (y = 0; y < 100; y += 2)         /* the water */
        box(0, y, 240, 1, y < 50 ? C_BG : C_DEEP);
    for (j = 0; j < 8u; j++) {
        int32_t n = V.vnote[j], y0;
        float a = V.vlvl[j];
        uint16_t c;
        if (n < 0 || a < 0.02f)
            continue;
        y0 = 92 - (n - 38) * 84 / 48;
        y0 = y0 < 6 ? 6 : y0 > 94 ? 94 : y0;
        c = a > 0.6f ? C_AMBER : a > 0.3f ? C_ACC_MID : C_ACC_DIM;
        for (x = 0; x < 240; x += 2) {
            float w = fm_sinf((float)x * 0.035f + ph * (0.7f + 0.13f * (float)j) + (float)n) * 3.0f * a;
            px(x, y0 + (int32_t)w, c);
            px(x + 1, y0 + (int32_t)w, c);
        }
        for (q = 0; q < nl && (lab[q] - y0 > 11 || y0 - lab[q] > 11); q++)
            ;
        if (q == nl && nl < 8) {         /* (a name where there is room for it) */
            lab[nl++] = y0;
            note_txt(n, t);
            cv_text(4, y0 - 13 < 0 ? 0 : y0 - 13, &FONT_T, t, C_MIST);
        }
    }
    if (F.harm_n && V.sounding) {        /* the chord, large */
        int32_t r = F.harm[0], d = 0;
        if (F.p[P_CHORD] >= NFIXED)
            key_snap(r, F.p, &d);
        chord_txt(r, d, t);
        cv_text(236 - tw(&FONT_M, t), 4, &FONT_M, t, C_ICE);
    } else {                             /* nothing sounding: the name */
        draw_logo_img(logo_word, LOGO_WORD_W, 0, LOGO_WORD_H, 120 - (int32_t)LOGO_WORD_W / 2, 2);
        cv_text(120 - tw(&FONT_XS, "PLAY A KEY") / 2, 82, &FONT_XS, "PLAY A KEY", C_SLATE);
    }
    if (F.playing && V.step >= 0)        /* the beat along the bottom */
        for (j = 0; j < NSTEP; j++)
            box(8 + (int32_t)j * 14, 97, 12, 2, (int32_t)j == V.step ? C_AMBER : j % 4u ? C_RULE : C_SLATE);
}

static void view_drums(void)
{
    uint32_t t, s, sel = F.p[P_DMTRACK] % NDRUM;
    for (t = 0; t < NDRUM; t++) {
        int32_t y = 5 + (int32_t)t * 15;
        cv_text(4, y + 1, &FONT_XS, TRACK_N[t], t == sel ? C_AMBER : C_SLATE);
        for (s = 0; s < NSTEP; s++) {
            int32_t x = 58 + (int32_t)s * 11;
            uint32_t v = F.pat.drum[t][s];
            int play = F.playing && V.step == (int8_t)s;
            uint16_t c = v == 2u ? (t == sel ? C_AMBER : C_ICE) : v == 1u ? (t == sel ? C_ACC_DIM : C_SOFT)
                                                                          : (s % 4u ? C_RULE : C_SLATE);
            if (play)
                c = v ? RGB(255, 255, 255) : C_MIST;
            box(x, y, 9, 11, c);
        }
    }
}

static void view_mel(void)
{
    uint32_t s, cur = F.p[P_MCUR] % NSTEP;
    int32_t y;
    for (s = 0; s < NSTEP; s++) {
        int32_t x = 8 + (int32_t)s * 14, d = F.pat.mel[s];
        int play = F.playing && V.step == (int8_t)s;
        box(x, 4, 12, 88, s == cur ? C_DEEP : C_BG);
        box(x, 94, 12, 2, s == cur ? C_AMBER : s % 4u ? C_RULE : C_SLATE);
        if (d >= 0) {
            y = 86 - d * 80 / 21;
            box(x, y, 12, 5, play ? RGB(255, 255, 255) : s == cur ? C_AMBER : C_ICE);
        }
    }
}

static void view_bass(void)
{
    uint32_t s, cur = F.p[P_BCUR] % NSTEP;
    for (s = 0; s < NSTEP; s++) {
        int32_t x = 8 + (int32_t)s * 14, v = F.pat.bass[s];
        int play = F.playing && V.step == (int8_t)s;
        const char *t = v == BASS_OFF ? "." : v == BASS_ROOT ? "R" : NOTE_N[v % 12];
        box(x, 20, 12, 50, s == cur ? C_DEEP : C_BG);
        box(x, 72, 12, 2, s == cur ? C_AMBER : s % 4u ? C_RULE : C_SLATE);
        cv_text(x + (12 - tw(&FONT_XS, t)) / 2, 38, &FONT_XS, t,
                play ? RGB(255, 255, 255) : v == BASS_OFF ? C_SLATE : v == BASS_ROOT ? C_ICE : C_AMBER);
    }
    cv_text(8, 82, &FONT_XS, "R FOLLOWS THE CHORD. REC: R, OCT- + REC: OFF", C_SLATE);
}

static void view_cseq(void)
{
    uint32_t s, cur = F.p[P_CSCUR] % NCSEQ;
    char t[16];
    for (s = 0; s < NCSEQ; s++) {
        int32_t x = 6 + (int32_t)(s % 4u) * 58, y = 8 + (int32_t)(s / 4u) * 44, g = F.pat.cseq[s];
        int now = F.playing && F.p[P_CSPLAYS] && ui.cs_idx == (int8_t)s;
        box(x, y, 54, 38, now ? C_ACC_BG : s == cur ? C_DEEP : C_BG);
        box(x, y + 37, 54, 1, s == cur ? C_AMBER : C_RULE);
        if (g < 0) {
            cv_text(x + 22, y + 10, &FONT_S, "-", C_SLATE);
        } else {
            chord_txt(48 + (int32_t)(F.p[P_ROOT] % 12u) + scale_deg(F.p[P_SCALE], g), g, t);
            cv_text(x + (54 - tw(&FONT_S, t)) / 2, y + 10, &FONT_S, t, now ? C_AMBER : C_ICE);
        }
    }
}

static void view_filter(void)
{
    int32_t x, py = -1;
    float fc = 30.0f * fm_powf(630.0f, (float)F.p[P_FCUT] / 127.0f), q = 0.5f + 18.0f * fm_powf((float)F.p[P_FRES] / 127.0f, 2.0f);
    int type = F.p[P_FTYPE] % 4u;
    for (x = 0; x < 240; x++) {
        float f = 20.0f * fm_powf(1000.0f, (float)x / 239.0f), w = f / fc, w2 = w * w, re = 1.0f - w2, im = w / q;
        float den = re * re + im * im, mag;
        int32_t y;
        mag = type == 0 ? 1.0f / den : type == 1 ? (im * im) / den : type == 2 ? (w2 * w2) / den : (re * re) / den;
        y = 60 - (int32_t)(10.0f * fm_log2f(mag + 1e-6f) * 0.6f);
        y = y < 2 ? 2 : y > 96 ? 96 : y;
        if (py >= 0) {
            int32_t a = py < y ? py : y, b = py < y ? y : py, k;
            for (k = a; k <= b; k++)
                px(x, k, C_ICE);
        }
        box(x, y + 1, 1, 98 - y, C_DEEP);
        py = y;
    }
}

static void view_lfo(void)
{
    uint32_t l = (uint32_t)(PAGES[pg].p[0] - P_L1SHAPE) / 4u % 3u, sh = F.p[P_L1SHAPE + 4u * l] % NLSHAPE;
    int32_t x, depth = (int32_t)F.p[P_L1DEPTH + 4u * l] - 64, py = 50;
    for (x = 0; x < 240; x++) {
        float p = (float)x / 240.0f * 2.0f, v;
        int32_t y;
        p -= (float)(int32_t)p;
        switch (sh) {
        case 0: v = fm_sinf(p * 6.2831853f); break;
        case 1: v = p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f; break;
        case 2: v = 2.0f * p - 1.0f; break;
        case 3: v = 1.0f - 2.0f * p; break;
        case 4: v = p < 0.5f ? 1.0f : -1.0f; break;
        default: v = fm_sinf(p * 6.2831853f * 3.0f) * 0.6f + fm_sinf(p * 6.2831853f * 1.3f) * 0.4f; break;
        }
        y = 50 - (int32_t)(v * (float)depth * 0.68f);
        {
            int32_t a = py < y ? py : y, b = py < y ? y : py, k;
            for (k = a; k <= b; k++)
                px(x, k, C_SOFT);
        }
        py = y;
    }
    box(0, 50, 240, 1, C_RULE);
    {                                    /* where it is now */
        int32_t y = 50 - (int32_t)(V.lfo[l] * (float)depth * 0.68f);
        box(116, y - 3, 7, 7, C_AMBER);
    }
}

static void view_mixer(void)
{
    static const char *const N[6] = {"DRONE", "MEL", "BASS", "DRUMS", "SHARD", "CLOUD"};
    static const uint8_t P[6] = {P_MXDRONE, P_MXMEL, P_MXBASS, P_MXDRUM, P_MXSHARD, P_MXCLOUD};
    uint32_t i;
    for (i = 0; i < 6u; i++) {
        int32_t x = 8 + (int32_t)i * 38, h = (int32_t)F.p[P[i]] * 70 / 127, m = (int32_t)(V.level[i] * 120.0f);
        m = m > 70 ? 70 : m;
        box(x, 10, 12, 70, C_RULE);
        box(x, 80 - h, 12, h, C_SOFT);
        box(x + 15, 80 - m, 4, m, C_AMBER);
        cv_text(x + 8 - tw(&FONT_T, N[i]) / 2, 86, &FONT_T, N[i], C_MIST);
    }
}

static void view_scenes(void)
{
    uint32_t s;
    char t[8];
    for (s = 0; s < NSCN; s++) {
        int32_t x = 6 + (int32_t)(s % 4u) * 58, y = 6 + (int32_t)(s / 4u) * 40;
        int used = scn.used >> s & 1u;
        box(x, y, 54, 34, s == st.scene && used ? C_ACC_BG : used ? C_DEEP : C_BG);
        box(x, y + 33, 54, 1, used ? C_ICE : C_RULE);
        itxt((int32_t)s + 1, t);
        cv_text(x + 22, y + 8, &FONT_S, t, used ? (s == st.scene ? C_AMBER : C_ICE) : C_SLATE);
    }
    cv_text(6, 88, &FONT_XS, "KEY: RECALL   SAVE HELD + KEY: STORE", C_MIST);
}

/* the sidechain: its gain over a bar (each trigger, ATTACK down, RELEASE back up), the beat moving along */
static void view_duck(void)
{
    static const char *const TR[NSCTRIG] = {"2000000010200000", "1000100010001000", "1010101010101010",
                                            "0010001000100010", "0000100000001000", "2000100010201000", "1000001000001000"};
    uint32_t trig = F.p[P_SCTRIG] % NSCTRIG, s;
    float depth = 0.8f * (float)F.p[P_PUMP] / 127.0f, rel = 0.03f * fm_powf(50.0f, (float)F.p[P_SCREL] / 127.0f);
    float atk = (float)F.p[P_SCATK] / 127.0f, at_s = F.p[P_SCATK] ? 0.002f + 0.06f * atk * atk : 0.0f;
    float step_s = 60.0f / (V.bpm > 1.0f ? V.bpm : 74.0f) / 4.0f, env = 0.0f, t_since = 99.0f;
    const char *pat = TR[trig];
    int32_t x, py = -1;
    char kick[17];
    for (s = 0; s < NSTEP; s++)          /* (KICK: the drum pattern's own kicks) */
        kick[s] = trig == 0u || trig == 5u ? (F.pat.drum[0][s] ? '1' : '0') : pat[s];
    if (trig == 5u)
        for (s = 0; s < NSTEP; s++)
            kick[s] = (F.pat.drum[0][s] || F.pat.drum[1][s] || F.pat.drum[2][s]) ? '1' : '0';
    if (trig == 4u)
        for (s = 0; s < NSTEP; s++)
            kick[s] = (F.pat.drum[1][s] || F.pat.drum[2][s]) ? '1' : '0';
    box(0, 12, 240, 1, C_RULE);
    for (x = 0; x < 240; x++) {
        float t = (float)x / 240.0f * 16.0f * step_s, g;
        int32_t st = x * 16 / 240, y;
        if (kick[st] != '0' && x == st * 240 / 16)
            t_since = 0.0f;
        else
            t_since += 16.0f * step_s / 240.0f;
        (void)t;
        if (at_s > 0.0f && t_since < at_s)
            env = depth * t_since / at_s;
        else if (t_since < 98.0f)
            env = depth * fm_expf(-(t_since - at_s) / rel);
        g = 1.0f - env;
        y = 14 + (int32_t)((1.0f - g) * 70.0f);
        if (py >= 0) {
            int32_t a = py < y ? py : y, b2 = py < y ? y : py, k;
            for (k = a; k <= b2; k++)
                px(x, k, C_ICE);
        }
        box(x, 12, 1, y - 12, C_DEEP);
        py = y;
    }
    for (s = 0; s < NSTEP; s++)          /* the triggers, the beat */
        box((int32_t)s * 15 + 2, 90, 11, 4,
            F.playing && V.step == (int8_t)s ? C_AMBER : kick[s] != '0' ? C_SOFT : C_RULE);
}

static void draw_view(void)
{
    cv_begin(240, 100, C_BG);
    switch (PAGES[pg].view) {
    case V_DRUMS: view_drums(); break;
    case V_MEL: view_mel(); break;
    case V_BASS: view_bass(); break;
    case V_CSEQ: view_cseq(); break;
    case V_FILTER: view_filter(); break;
    case V_LFO: view_lfo(); break;
    case V_MIXER: view_mixer(); break;
    case V_SCENES: view_scenes(); break;
    case V_DUCK: view_duck(); break;
    default: view_river(); break;
    }
    cv_blit(0, 30);
}

static void draw_cells(void)
{
    uint32_t i, now = plat_ms();
    char t[40];
    cv_begin(240, 110, C_BG);
    box(0, 0, 240, 1, C_RULE);
    for (i = 0; i < 4u; i++) {
        int32_t x = 8 + (int32_t)(i % 2u) * 118, y = 6 + (int32_t)(i / 2u) * 52;
        uint32_t p = PAGES[pg].p[i];
        int lit = ui.touched == (int8_t)i && now - ui.touched_at < 800u;
        char k[4] = {'K', (char)('1' + i), 0, 0};
        if (p >= NP)
            continue;
        cv_text(x, y, &FONT_XS, k, lit ? C_AMBER : C_SLATE);
        cv_text(x + 18, y, &FONT_XS, PD[p].name, lit ? C_AMBER : C_MIST);
        value_txt(p, t);
        {
            const felucca_font_t *f = tw(&FONT_M, t) <= 108 ? &FONT_M : tw(&FONT_B, t) <= 108 ? &FONT_B : &FONT_S;
            cv_text(x, y + 14, f, t, lit ? RGB(255, 255, 255) : C_ICE);
        }
        if (PD[p].fmt == FM_PCT || PD[p].fmt == FM_BIPOLAR || PD[p].fmt == FM_BPM) {
            int32_t w = (int32_t)F.p[p] * 104 / (PD[p].max ? PD[p].max : 1);
            box(x, y + 42, 104, 2, C_RULE);
            box(x, y + 42, w, 2, lit ? C_AMBER : C_SOFT);
        }
    }
    cv_blit(0, 130);
}

static void draw(void)
{
    draw_header();
    draw_view();
    draw_cells();
}

/* ------------------------------------------------------------------- the contract --- */
void app_init(void)
{
    uint32_t i;
    for (i = 0; i < NGROUP; i++)
        grp[i].id = -1;
    defaults();
    if (plat_store_load(OBJ_STATE, &st, sizeof st) == (int)sizeof st && st.magic == STATE_MAGIC) {
        memcpy(F.p, st.now.p, NP);
        memcpy(&F.pat, &st.now.pat, sizeof F.pat);
        if (st.octave < -3 || st.octave > 3)
            st.octave = 0;
        pg = st.page < NPAGES ? st.page : 0u;
    } else {
        defaults();
        st.scene = 0;
    }
    if (plat_store_load(OBJ_SCENES, &scn, sizeof scn) != (int)sizeof scn || scn.magic != STATE_MAGIC)
        memset(&scn, 0, sizeof scn);
    sane();
    eng_init();
    harm_update();
    ui.touched = -1;
    ui.cs_idx = -1;
    memset(ui.livenote, -1, sizeof ui.livenote);
    ui.cs_bar = 0xFFFFFFFFu;
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BG);
    ui.redraw = 1;
}

void app_input_only(void) { input(); }

void app_frame(void)
{
    static uint32_t last_vis, last_step = 99, last_msg, cpu_at;
    uint32_t now = plat_ms();
    input();
    if (now - cpu_at >= 400u) {          /* the CPU guard: fewer drone voices while the audio runs late */
        uint32_t c = plat_cpu_pct();
        cpu_at = now;
        if (c > 85u && eng_maxdv > 3u)
            eng_maxdv--;
        else if (c < 65u && eng_maxdv < 8u)
            eng_maxdv++;
    }
    cseq_run();
    evolve_run();
    morph_run();
    lights();
    if (ann_on())
        ann_tick(now);
    if (ui.redraw || (ui.msg_until && now >= ui.msg_until && last_msg != ui.msg_until)) {
        if (ui.msg_until && now >= ui.msg_until)
            last_msg = ui.msg_until;
        ui.redraw = 0;
        draw();
        last_vis = now;
    } else if (now - last_vis >= 50u || (uint32_t)(V.step + 1) != last_step) {   /* the view moves: 20 times a second */
        last_step = (uint32_t)(V.step + 1);
        last_vis = now;
        draw_view();
        if (PAGES[pg].view == V_RIVER && now % 1000u < 60u)
            draw_header();             /* (the tempo, the CPU on GLOBAL: once a second) */
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "page"))
        *got = (int)pg;
    else if (!strcmp(what, "playing"))
        *got = F.playing;
    else if (!strcmp(what, "step"))
        *got = V.step;
    else if (!strcmp(what, "voices"))
        *got = (int)V.sounding;
    else if (!strcmp(what, "harm_n"))
        *got = F.harm_n;
    else if (!strcmp(what, "harm0"))
        *got = F.harm[0];
    else if (!strcmp(what, "groups")) {
        uint32_t i;
        *got = 0;
        for (i = 0; i < NGROUP; i++)
            *got += grp[i].id >= 0;
    } else if (!strncmp(what, "p", 1) && what[1] >= '0' && what[1] <= '9')
        *got = F.p[atoi(what + 1) % NP];
    else if (!strncmp(what, "mel", 3))
        *got = F.pat.mel[atoi(what + 3) % NSTEP];
    else if (!strncmp(what, "bass", 4))
        *got = F.pat.bass[atoi(what + 4) % NSTEP];
    else if (!strncmp(what, "cseq", 4))
        *got = F.pat.cseq[atoi(what + 4) % NCSEQ];
    else if (!strncmp(what, "drum", 4))  /* drumTS: track T, step S (two digits) */
        *got = F.pat.drum[(what[4] - '0') % NDRUM][atoi(what + 5) % NSTEP];
    else if (!strcmp(what, "used"))
        *got = (int)scn.used;
    else if (!strcmp(what, "press"))
        *got = (int)(F.press * 100.0f + 0.5f);
    else if (!strcmp(what, "octave"))
        *got = st.octave;
    else if (!strcmp(what, "bpm"))
        *got = (int)(V.bpm + 0.5f);
    else
        return 0;
    return 1;
}
#endif
