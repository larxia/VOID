/* SPDX-License-Identifier: MPL-2.0
 * VOID: harmony (scales, chords, voicings) and the names of everything the pages show. */
#include "engine.h"

const uint8_t SCALES[NSCALES][8] = {
    {7, 0, 2, 4, 5, 7, 9, 11},           /* major */
    {7, 0, 2, 3, 5, 7, 8, 10},           /* minor */
    {7, 0, 2, 3, 5, 7, 9, 10},           /* dorian */
    {7, 0, 1, 3, 5, 7, 8, 10},           /* phrygian */
    {7, 0, 2, 4, 6, 7, 9, 11},           /* lydian */
    {7, 0, 2, 4, 5, 7, 9, 10},           /* mixolydian */
    {7, 0, 1, 3, 5, 6, 8, 10},           /* locrian */
    {7, 0, 2, 3, 5, 7, 8, 11},           /* harmonic minor */
    {7, 0, 2, 3, 5, 7, 9, 11},           /* melodic minor */
    {5, 0, 2, 4, 7, 9, 0, 0},            /* pentatonic major */
    {5, 0, 3, 5, 7, 10, 0, 0},           /* pentatonic minor */
    {5, 0, 2, 3, 7, 8, 0, 0},            /* hirajoshi */
    {5, 0, 1, 5, 7, 10, 0, 0},           /* in sen (Japan) */
    {5, 0, 1, 5, 6, 10, 0, 0},           /* iwato (Japan) */
    {5, 0, 2, 3, 7, 9, 0, 0},            /* kumoi (Japan) */
    {5, 0, 2, 5, 7, 9, 0, 0},            /* yo (Japan) */
    {5, 0, 1, 5, 7, 8, 0, 0},            /* miyako-bushi (Japan) */
    {5, 0, 4, 5, 7, 11, 0, 0},           /* ryukyu (Okinawa) */
    {5, 0, 1, 3, 7, 8, 0, 0},            /* pelog (Bali, Java) */
    {5, 0, 4, 6, 7, 11, 0, 0},           /* chinese */
    {7, 0, 1, 4, 5, 7, 8, 11},           /* bhairav (India) */
};
const char *const SCALE_NAME[NSCALES] = {"MAJOR", "MINOR", "DORIAN", "PHRYGIAN", "LYDIAN", "MIXOLYDIAN", "LOCRIAN",
                                         "HARM MINOR", "MEL MINOR", "PENTA MAJ", "PENTA MIN", "HIRAJOSHI",
                                         "IN SEN", "IWATO", "KUMOI", "YO", "MIYAKO-BUSHI", "RYUKYU", "PELOG",
                                         "CHINESE", "BHAIRAV"};

/* FOLLOW NOTE: built on the note played */
const int8_t CHORD_FIX[NFIXED][7] = {
    {1, 0}, {2, 0, 12}, {2, 0, 7},
    {3, 0, 4, 7}, {3, 0, 3, 7}, {3, 0, 2, 7}, {3, 0, 5, 7}, {3, 0, 3, 6}, {3, 0, 4, 8},
    {4, 0, 4, 7, 14}, {4, 0, 3, 7, 14}, {4, 0, 4, 7, 9}, {4, 0, 3, 7, 9}, {5, 0, 4, 7, 9, 14}, {5, 0, 3, 7, 9, 14},
    {4, 0, 4, 7, 11}, {4, 0, 3, 7, 10}, {4, 0, 4, 7, 10}, {4, 0, 3, 7, 11}, {4, 0, 3, 6, 10}, {4, 0, 5, 7, 10},
    {5, 0, 4, 7, 11, 14}, {5, 0, 3, 7, 10, 14}, {5, 0, 4, 7, 10, 14}, {5, 0, 5, 7, 10, 14}, {5, 0, 3, 7, 11, 14},
    {6, 0, 3, 7, 10, 14, 17}, {5, 0, 4, 7, 11, 18}, {6, 0, 4, 7, 11, 14, 21}, {6, 0, 3, 7, 10, 14, 21},
    {3, 0, 5, 10}, {4, 0, 5, 10, 15}, {4, 0, 2, 4, 7},
};
const char *const CHORD_SUFFIX[NFIXED] = {
    "", " OCT", "5", "", "m", "sus2", "sus4", "dim", "+", "add9", "m add9", "6", "m6", "6/9", "m6/9",
    "maj7", "m7", "7", "m maj7", "m7b5", "7sus4", "maj9", "m9", "9", "9sus4", "m maj9",
    "m11", "maj7#11", "maj13", "m13", " QRT", " QRT4", "add2"};
/* IN KEY: stacked from the scale (steps of it), the quality as the key makes it */
const int8_t CHORD_AUTO[NAUTO][7] = {
    {3, 0, 2, 4}, {4, 0, 2, 4, 6}, {5, 0, 2, 4, 6, 8}, {4, 0, 2, 4, 8}, {6, 0, 2, 4, 6, 8, 10}, {3, 0, 1, 4},
    {3, 0, 3, 4}, {4, 0, 2, 4, 5}, {3, 0, 4, 6}, {3, 0, 3, 6}, {3, 0, 1, 2}, {2, 0, 4}};
const char *const CHORD_NAME[NCHORDS] = {
    "SINGLE", "OCTAVE", "POWER 5TH", "MAJOR", "MINOR", "SUS2", "SUS4", "DIM", "AUG", "ADD9", "MINOR ADD9", "6",
    "MINOR 6", "6/9", "MINOR 6/9", "MAJ7", "MINOR 7", "DOM 7", "MIN MAJ7", "HALF-DIM", "7SUS4", "MAJ9", "MINOR 9",
    "DOM 9", "9SUS4", "MIN MAJ9", "MINOR 11", "MAJ7#11", "MAJ13", "MINOR 13", "QUARTAL", "QUARTAL 4", "ADD2",
    "KEY TRIAD", "KEY 7TH", "KEY 9TH", "KEY ADD9", "KEY 11TH", "KEY SUS2", "KEY SUS4", "KEY 6TH", "KEY SHELL",
    "KEY QUARTAL", "KEY CLUSTER", "KEY 5TH"};
const char *const VOICING_NAME[NVOICING] = {"CLOSE", "OPEN", "DROP 2", "WIDE", "DROP 3", "1ST INVERSION", "2ND INVERSION",
                                             "ROOTLESS", "LOW ROOT", "HIGH", "SPREAD", "OCTAVES", "SHIMMER", "FIFTH BASS"};

int32_t scale_len(uint32_t scale) { return SCALES[scale % NSCALES][0]; }

int32_t scale_deg(uint32_t scale, int32_t d)
{
    int32_t n = scale_len(scale), o = d >= 0 ? d / n : -((-d + n - 1) / n), k = d - o * n;
    return SCALES[scale % NSCALES][1 + k] + 12 * o;
}

static void sort8(int8_t *a, uint32_t n)
{
    uint32_t i, j;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && a[j - 1] > a[j]; j--) {
            int8_t t = a[j];
            a[j] = a[j - 1];
            a[j - 1] = t;
        }
}

uint32_t chord_build(int32_t root, int32_t d, const uint8_t *p, int8_t *out)
{
    int32_t semi[8], n, i, j, k = 0;
    uint32_t c = p[P_CHORD] % NCHORDS, sc = p[P_SCALE] % NSCALES, v = p[P_VOICING] % NVOICING;
    if (!p[P_HARM]) {                    /* SINGLE NOTES */
        out[0] = (int8_t)(root < 0 ? 0 : root > 127 ? 127 : root);
        return 1;
    }
    if (c < NFIXED) {
        n = CHORD_FIX[c][0];
        for (i = 0; i < n; i++)
            semi[i] = CHORD_FIX[c][1 + i];
    } else {
        const int8_t *a = CHORD_AUTO[c - NFIXED];
        int32_t len = scale_len(sc), base = scale_deg(sc, d);
        n = a[0];
        for (i = 0; i < n; i++) {        /* (a five-note scale: the steps scaled to it) */
            int32_t st = len == 7 ? a[1 + i] : (a[1 + i] * len + 3) / 7;
            semi[i] = scale_deg(sc, d + st) - base;
        }
    }
    if (v == 1u && n > 2)                /* OPEN: the second up an octave */
        semi[1] += 12;
    else if (v == 2u && n > 3)           /* DROP 2: the second from the top down an octave */
        semi[n - 2] -= 12;
    else if (v == 3u) {                  /* WIDE: every other up, the root doubled below */
        for (i = 1; i < n; i += 2)
            semi[i] += 12;
        if (n > 1 && n < 8)
            semi[n++] = -12;
    }
    else if (v == 4u && n > 3)           /* DROP 3: the third from the top down an octave */
        semi[n - 3] -= 12;
    else if (v == 5u && n > 1)           /* 1ST INVERSION: the root up an octave */
        semi[0] += 12;
    else if (v == 6u && n > 2) {         /* 2ND INVERSION: the two lowest up */
        semi[0] += 12;
        semi[1] += 12;
    } else if (v == 7u && n > 2) {       /* ROOTLESS: the root left to the bass */
        for (i = 0; i + 1 < n; i++)
            semi[i] = semi[i + 1];
        n--;
    } else if (v == 8u && n < 8) {       /* LOW ROOT: the root again, an octave under */
        semi[n++] = -12;
    } else if (v == 9u) {                /* HIGH: all an octave up */
        for (i = 0; i < n; i++)
            semi[i] += 12;
    } else if (v == 10u) {               /* SPREAD: each next note an octave further out (two octaves and more) */
        for (i = 1; i < n; i++)
            semi[i] += 12 * (i / 2 + (i & 1));
    } else if (v == 11u) {               /* OCTAVES: the chord doubled an octave up (as many as fit) */
        int32_t m = n;
        for (i = 0; i < m && n < 8; i++)
            semi[n++] = semi[i] + 12;
    } else if (v == 12u && n < 7) {      /* SHIMMER: the root two octaves up and a fifth above that */
        semi[n++] = 24;
        semi[n++] = 31;
    } else if (v == 13u && n < 8) {      /* FIFTH BASS: a fifth under the root, an octave down */
        semi[n++] = -17;
    }
    for (i = 0; i < n; i++) {            /* (no note twice) */
        int32_t x = root + semi[i];
        if (x < 12 || x > 115)
            continue;
        for (j = 0; j < k && out[j] != x; j++)
            ;
        if (j == k)
            out[k++] = (int8_t)x;
    }
    sort8(out, (uint32_t)k);
    return (uint32_t)k;
}

int32_t key_snap(int32_t n, const uint8_t *p, int32_t *deg)
{
    int32_t root = (int32_t)(p[P_ROOT] % 12u), sc = p[P_SCALE] % NSCALES, rel = n - (48 + root), d, best = 0, bd = 99;
    for (d = -21; d < 35; d++) {
        int32_t x = scale_deg((uint32_t)sc, d) - rel;
        x = x < 0 ? -x : x;
        if (x < bd) {
            bd = x;
            best = d;
        }
    }
    *deg = best;
    return 48 + root + scale_deg((uint32_t)sc, best);
}

/* ------------------------------------------------------------------- names --- */
const char *const DPRESET_NAME[NDPRESET] = {
    "CHOIR OF ICE", "DEEP RIVER", "FROZEN BELL", "BREATH", "AURORA", "LOW HUM", "GLASS ORGAN", "SNOWFIELD", "NIGHT AIR",
    "THAW", "POLAR", "EMBER",
    "SOFT SINE", "WARM PAD", "VELVET", "HOLLOW", "ORGAN DRONE", "TEMPLE BELLS", "GLASS HARMONICA", "FIFTHS", "DEEP SUB",
    "FM PAD", "DX GLASS", "LULLABY", "MIST", "DUSK STRINGS", "CLARINET HAZE", "GAMELAN"};
/* choir, partials, breath, timbre, tone, detune, attack, release, motion, cut, wave, series */
const uint8_t DPRESET_V[NDPRESET][12] = {
    {89, 51, 25, 44, 57, 51, 57, 70, 40, 110, 0, 0}, {102, 38, 13, 19, 32, 70, 70, 83, 50, 90, 0, 0},
    {19, 108, 13, 108, 76, 25, 32, 76, 30, 120, 0, 0}, {25, 25, 114, 51, 64, 38, 64, 76, 60, 110, 0, 0},
    {76, 76, 32, 76, 83, 64, 76, 89, 70, 115, 0, 0}, {70, 89, 6, 6, 23, 32, 51, 64, 20, 80, 0, 0},
    {13, 114, 6, 64, 70, 13, 25, 51, 25, 127, 0, 0}, {57, 44, 76, 89, 89, 57, 89, 102, 60, 120, 0, 0},
    {44, 32, 89, 32, 44, 44, 76, 89, 55, 100, 0, 0}, {108, 57, 19, 57, 70, 89, 44, 64, 45, 110, 0, 0},
    {51, 95, 38, 121, 102, 38, 64, 108, 65, 127, 0, 0}, {114, 25, 25, 38, 51, 76, 38, 57, 35, 100, 0, 0},
    {90, 40, 10, 20, 40, 30, 70, 80, 40, 100, 4, 0},     /* SOFT SINE */
    {100, 30, 15, 30, 35, 50, 65, 75, 45, 85, 1, 0},     /* WARM PAD */
    {100, 20, 20, 25, 30, 60, 60, 80, 50, 80, 2, 0},     /* VELVET */
    {80, 50, 10, 40, 35, 35, 55, 70, 35, 80, 3, 1},      /* HOLLOW */
    {50, 100, 5, 60, 50, 15, 40, 60, 20, 100, 4, 2},     /* ORGAN DRONE */
    {30, 110, 10, 50, 50, 20, 30, 100, 60, 110, 4, 4},   /* TEMPLE BELLS */
    {40, 100, 15, 40, 60, 25, 60, 90, 50, 110, 4, 5},    /* GLASS HARMONICA */
    {70, 80, 10, 40, 45, 30, 60, 80, 40, 95, 2, 3},      /* FIFTHS */
    {60, 90, 5, 10, 25, 20, 50, 70, 30, 70, 4, 6},       /* DEEP SUB */
    {100, 20, 10, 30, 45, 40, 60, 80, 40, 95, 5, 0},     /* FM PAD */
    {70, 60, 5, 50, 60, 20, 40, 90, 30, 115, 5, 5},      /* DX GLASS */
    {70, 50, 20, 25, 30, 25, 75, 100, 30, 75, 2, 2},     /* LULLABY */
    {50, 20, 80, 20, 30, 40, 90, 110, 70, 70, 4, 0},     /* MIST */
    {110, 15, 25, 30, 50, 70, 70, 80, 55, 90, 1, 0},     /* DUSK STRINGS */
    {60, 70, 30, 30, 40, 20, 50, 70, 30, 85, 3, 1},      /* CLARINET HAZE */
    {50, 100, 5, 60, 55, 15, 20, 90, 40, 110, 5, 4}};    /* GAMELAN */
const char *const DWAVE_NAME[NDWAVE] = {"SAW", "WARM", "TRIANGLE", "HOLLOW", "SINE", "FM"};
const char *const PSERIES_NAME[NPSERIES] = {"HARMONIC", "ODD", "OCTAVES", "FIFTHS", "BELL", "GLASS", "SUB"};
const float PSERIES_R[NPSERIES][6] = {
    {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},          /* the harmonics */
    {1.0f, 3.0f, 5.0f, 7.0f, 9.0f, 11.0f},         /* the odd ones: hollow, a clarinet's */
    {1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 0.5f},         /* octaves: an organ's, a sub-octave under */
    {1.0f, 1.5f, 2.0f, 3.0f, 4.5f, 6.0f},          /* stacked fifths */
    {1.0f, 2.76f, 5.40f, 8.93f, 13.34f, 18.64f},   /* a struck bell's (inharmonic) */
    {1.0f, 2.32f, 4.25f, 6.63f, 9.38f, 12.5f},     /* glass, a bowl's */
    {0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f}};         /* under the note: deep */
const char *const SPRESET_NAME[NSPRESET] = {"GLASS", "CHIME", "ICE", "MUSIC BOX", "BOWL", "STAR", "KALIMBA", "VIBES"};
const char *const MSOUND_NAME[NMSOUND] = {
    "E.PIANO", "RHODES BELL", "WURLI", "LOFI KEYS", "SOFT PIANO", "TOY PIANO", "ORGAN", "CLAV", "BELL", "VIBRAPHONE",
    "MARIMBA", "KALIMBA", "MUSIC BOX", "STEEL DRUM", "PLUCK", "HARP", "FLUTE", "OCARINA", "VOICE OOH", "SAW LEAD",
    "SUPERSAW", "ACID", "SINE LEAD", "WARM PAD",
    "CELESTA", "GLOCKENSPIEL", "HANDPAN", "KOTO", "GUZHENG", "SHAKUHACHI", "PAN FLUTE", "ERHU", "CHOIR AAH", "STRINGS",
    "BRASS", "SYNTH BELL", "CRYSTAL", "CHIPTUNE", "HOLLOW LEAD", "DREAM KEYS", "WAVETABLE PAD", "WT SWEEP", "CUSTOM"};
const char *const BMODEL_NAME[NBMODEL] = {"SINE", "TRIANGLE", "SQUARE", "SAW", "REESE", "SUB", "ORGAN", "FM",
                                          "808", "ACID", "PLUCK", "DUB", "WOBBLE", "MOOG", "UPRIGHT", "SYNTH"};
const char *const BMODE_NAME[NBMODE] = {"ROOT", "FIFTHS", "ARP UP", "ARP DOWN", "OCTAVES", "RANDOM"};
const char *const KIT_NAME[NKIT] = {"SOFT", "LOFI", "808", "GLITCH", "TRAP", "BRUSH", "TECHNO", "TABLA"};
const char *const UWAVE_NAME[NUWAVE] = {"SINE", "TRIANGLE", "SAW", "SQUARE", "FM", "SUPERSAW", "ORGAN", "CLAV", "WAVETABLE"};
const char *const UP2_NAME[NUP2] = {"OFF", "OCTAVE", "FIFTH UP", "2 OCTAVES", "BELL", "GLASS"};
const char *const LSHAPE_NAME[NLSHAPE] = {"SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE", "RANDOM", "SMOOTH RND"};
const char *const LTGT_NAME[NLTGT] = {"OFF", "FILTER", "RESONANCE", "DRONE TONE", "VIBRATO", "TREMOLO", "DRONE PAN",
                                      "CLOUD", "SHIMMER", "ECHO WARBLE", "BASS FILTER", "MELODY TONE", "PARTIALS",
                                      "MASTER FILT", "DRONE LEVEL"};
const char *const LDIV_NAME[NLDIV] = {"8 BARS", "4 BARS", "2 BARS", "1 BAR", "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16"};
const float LDIV_BEATS[NLDIV] = {32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.6666667f, 0.5f, 0.3333333f, 0.25f};

float lfo_hz(uint32_t rate, float bpm)
{
    if (rate < NLRATE_FREE)
        return 0.01f * fm_powf(2000.0f, (float)rate / (float)(NLRATE_FREE - 1u));
    rate -= NLRATE_FREE;
    return bpm / 60.0f / LDIV_BEATS[rate < NLDIV ? rate : NLDIV - 1u];
}
