/* SPDX-License-Identifier: MPL-2.0
 * VOID's sound, in the audio interrupt: the drones (choir, partials, breath), the drone filter, the melody synth, the
 * bass, the shards (FM bells), the drums (drums.c), the cloud (a feedback delay network, its shimmer and FREEZE), the
 * echo, PUMP, SINK, the tape, the master; the LFOs; the sequencers on one clock. Floats only; the work done a chunk
 * (CH frames) at a time where it can be (envelopes, filters' coefficients, the LFOs, the sequencers). */
#include "engine.h"
#include "drums.h"

#include "plat.h"
#define POOL PLAT_POOL                   /* big buffers in the FM-1's 336 KB pool RAM (not the 96 KB of .bss) */

floe_t F;
volatile floe_view_t V;

/* ------------------------------------------------------------------- small things --- */
static uint32_t rng = 0x12345678u;
static inline uint32_t rnd32(void)
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rng = x;
}
static inline float rndf(void) { return (float)(int32_t)rnd32() * (1.0f / 2147483648.0f); }   /* -1 .. 1 */
static inline float rnd01(void) { return (float)(rnd32() >> 8) * (1.0f / 16777216.0f); }
static inline float u7(int p) { return (float)F.p[p] * (1.0f / 127.0f); }
static inline float mtof(float m) { return 440.0f * fm_exp2f((m - 69.0f) * (1.0f / 12.0f)); }
static inline float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static inline float sat(float x) { x = clampf(x, -1.5f, 1.5f); return x - 0.148148148f * x * x * x; }

/* a TPT state-variable filter: its coefficients for a cutoff (Hz) and a Q, then a sample through it */
typedef struct { float a1, a2, a3, k, s1, s2; } svf_t;
RAM static void svf_set_at(svf_t *f, float hz, float q, float inv_fs)   /* (at a rate: the drones run at half) */
{
    float g;
    hz = clampf(hz, 16.0f, 0.43f / inv_fs);
    g = fm_tanf(3.14159265f * hz * inv_fs);
    f->k = 1.0f / (q < 0.3f ? 0.3f : q);
    f->a1 = 1.0f / (1.0f + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}
/* mode 0 low, 1 band, 2 high, 3 notch */
RAM static void svf_set(svf_t *f, float hz, float q) { svf_set_at(f, hz, q, INV_FS); }
static inline float svf(svf_t *f, float x, int mode)
{
    float v3 = x - f->s2, v1 = f->a1 * f->s1 + f->a2 * v3, v2 = f->s2 + f->a2 * f->s1 + f->a3 * v3;
    f->s1 = 2.0f * v1 - f->s1;
    f->s2 = 2.0f * v2 - f->s2;
    return mode == 0 ? v2 : mode == 1 ? v1 : mode == 2 ? x - f->k * v1 - v2 : x - f->k * v1;
}
static inline void svf_flush(svf_t *f) { f->s1 = fm_flush(f->s1); f->s2 = fm_flush(f->s2); }

/* a band-limited saw (polyBLEP) at phase t, step dt */
static inline float blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline float saw(float t, float dt) { return 2.0f * t - 1.0f - blep(t, dt); }
static inline float square(float t, float dt)
{
    float u = t + 0.5f;
    u -= u >= 1.0f ? 1.0f : 0.0f;
    return (t < 0.5f ? 1.0f : -1.0f) + blep(t, dt) - blep(u, dt);
}
static inline float wrap(float p) { return p - (float)(int32_t)p; }

/* ------------------------------------------------------------------- events --- */
#define NEV 64u
typedef struct { uint8_t type; int16_t a; float b; } ev_t;
static ev_t evq[NEV];
static volatile uint32_t ev_w, ev_r;
void eng_event(uint32_t type, int32_t a, float b)
{
    if (ev_w - ev_r >= NEV)
        return;
    evq[ev_w % NEV].type = (uint8_t)type;
    evq[ev_w % NEV].a = (int16_t)a;
    evq[ev_w % NEV].b = b;
    __asm__ volatile("" ::: "memory");
    ev_w++;
}

/* the two of tables.c's helpers the audio path needs, here in RAM (the tables they read may stay in the flash) */
static inline int32_t eng_scale_deg(uint32_t scale, int32_t d)
{
    int32_t n = SCALES[scale % NSCALES][0], o = d >= 0 ? d / n : -((-d + n - 1) / n), k = d - o * n;
    return SCALES[scale % NSCALES][1 + k] + 12 * o;
}
static inline float eng_lfo_hz(uint32_t rate, float bpm)
{
    if (rate < NLRATE_FREE)
        return 0.01f * fm_powf(2000.0f, (float)rate / (float)(NLRATE_FREE - 1u));
    rate -= NLRATE_FREE;
    return bpm / 60.0f / LDIV_BEATS[rate < NLDIV ? rate : NLDIV - 1u];
}

/* ------------------------------------------------------------------- modulation --- */
static float sc_hit, sc_pg_last = 1.0f;
static uint8_t sc_step;                  /* (the sequencer: a step the sidechain's rhythm fires on) */
static float mod[NLTGT];                 /* each LFO TARGET's sum now, -1 .. 1 scaled by depth */
static float lfo_ph[3], lfo_v[3], lfo_r[3][2];
static float bpm_now = 90.0f;

/* ------------------------------------------------------------------- the drones --- */
#define NDV 8u
#define NPART 6u
typedef struct {
    int16_t note;                        /* -1 free */
    uint8_t gate;
    uint32_t age;
    float env, f;
    float sph[3];                        /* the choir's three saws */
    float pre[NPART], pim[NPART];        /* the partials: rotating phasors */
    float pdrift[NPART], pdph[NPART];    /* each partial's slow wander */
    svf_t tone, br;                      /* the choir's low-pass, the breath's band-pass */
    float pl, pr;                        /* its place */
} dvoice_t;
static dvoice_t dv[NDV];
volatile uint32_t eng_maxdv = NDV;       /* the drone voices allowed (the main loop's CPU guard lowers it) */
static uint32_t ages;
static float dl[CH], dr[CH];             /* the drones' chunk */
#define DH (CH / 2u)                     /* the drones run at half the rate (22.05 kHz: soft pads, filtered), then up */
static float hl[DH], hr[DH], hl_last, hr_last;

RAM static void drone_on(int note, float vel)
{
    uint32_t i, k;
    dvoice_t *v = 0;
    uint32_t nv = eng_maxdv < 2u ? 2u : eng_maxdv > NDV ? NDV : eng_maxdv;
    (void)vel;
    for (i = 0; i < NDV; i++)            /* the same note again: it comes back */
        if (dv[i].note == note) {
            dv[i].gate = 1;
            return;
        }
    for (i = 0; i < nv && !v; i++)
        if (dv[i].note < 0)
            v = &dv[i];
    if (!v)                              /* the quietest released, else the oldest */
        for (i = 0; i < nv; i++)
            if (!dv[i].gate && (!v || dv[i].env < v->env))
                v = &dv[i];
    if (!v)
        for (i = 0; i < nv; i++)
            if (!v || dv[i].age < v->age)
                v = &dv[i];
    v->note = (int16_t)note;
    v->gate = 1;
    v->env = 0.0f;
    v->age = ++ages;
    for (k = 0; k < 3u; k++)
        v->sph[k] = rnd01();
    for (k = 0; k < NPART; k++) {
        float a = rnd01() * 6.2831853f;
        v->pre[k] = fm_cosf(a);
        v->pim[k] = fm_sinf(a);
        v->pdph[k] = rnd01();
        v->pdrift[k] = 0.05f + 0.15f * rnd01();
    }
    v->tone.s1 = v->tone.s2 = v->br.s1 = v->br.s2 = 0.0f;
    {
        float p = 0.5f * rndf() * (0.3f + 0.7f * u7(P_SPREAD_OCT));
        v->pl = fm_cosf((p + 1.0f) * 0.7853982f);
        v->pr = fm_sinf((p + 1.0f) * 0.7853982f);
    }
}
RAM static void drone_off(int note)
{
    uint32_t i;
    for (i = 0; i < NDV; i++)
        if (dv[i].note == note)
            dv[i].gate = 0;
}

/* a chunk of every drone voice into dl, dr */
RAM static void drones_chunk(void)
{
    uint32_t i, j, k;
    float choir = u7(P_CHOIR), part = u7(P_PARTIALS), breath = u7(P_BREATH), timbre = u7(P_TIMBRE);
    float tone = clampf(u7(P_TONE) + 0.5f * mod[3], 0.0f, 1.0f), det = 3.0f + 30.0f * u7(P_DETUNE);
    float att = 0.03f * fm_powf(300.0f, u7(P_ATTACK)), rel = 0.1f * fm_powf(150.0f, u7(P_RELEASE));
    float ka = 1.0f - fm_expf(-(float)CH * INV_FS * 3.0f / att), kr = 1.0f - fm_expf(-(float)CH * INV_FS * 3.0f / rel);
    float vib = mod[4] * 60.0f, motion = u7(P_DMOTION), pmix = clampf(part * (1.0f + mod[12]), 0.0f, 1.5f);
    float pa[NPART], ps = 0.0f, stretch = timbre * timbre * 0.045f;
    uint32_t wave = F.p[P_DWAVE] % NDWAVE, series = F.p[P_PSERIES] % NPSERIES;
    float fmi = 0.04f + 0.3f * timbre;  /* (FM: the modulator's depth, in turns, by TIMBRE) */
    for (k = 0; k < NPART; k++) {        /* TIMBRE: dark (the fundamental) .. bright and bell-like */
        pa[k] = fm_powf(1.0f / (float)(k + 1u), 2.4f - 2.1f * timbre);
        ps += pa[k];
    }
    for (k = 0; k < NPART; k++)
        pa[k] *= 0.55f / ps;
    const float IFS = 2.0f * INV_FS;    /* (one step of the half rate) */
    for (i = 0; i < DH; i++)
        hl[i] = hr[i] = 0.0f;
    V.sounding = 0;
    for (j = 0; j < NDV; j++) {
        dvoice_t *v = &dv[j];
        float e0 = v->env, e1, de, f, cr[NPART], ci[NPART], dt[3], g;
        if (v->note < 0) {
            V.vnote[j] = -1;
            V.vlvl[j] = 0.0f;
            continue;
        }
        V.sounding++;
        if (j >= eng_maxdv)              /* (over the CPU guard's count: let go) */
            v->gate = 0;
        e1 = e0 + ((v->gate ? 1.0f : 0.0f) - e0) * (v->gate ? ka : kr);
        if (!v->gate && e1 < 0.0008f) {
            v->note = -1;
            continue;
        }
        v->env = e1;
        V.vnote[j] = (int8_t)v->note;
        V.vlvl[j] = e1;
        de = (e1 - e0) * (1.0f / (float)DH);
        f = mtof((float)v->note + vib * 0.01f);
        v->f = f;
        for (k = 0; k < NPART; k++) {    /* each partial's step this chunk, its wander */
            float fk = f * PSERIES_R[series][k] * (1.0f + stretch * (float)(k * k)), w;
            if (fk > 10000.0f) {
                cr[k] = 1.0f;
                ci[k] = 0.0f;
                continue;
            }
            w = 6.2831853f * fk * IFS;
            cr[k] = fm_cosf(w);
            ci[k] = fm_sinf(w);
            v->pdph[k] += v->pdrift[k] * (float)CH * INV_FS;
            v->pdph[k] -= v->pdph[k] >= 1.0f ? 1.0f : 0.0f;
        }
        dt[0] = f * fm_exp2f(det * (1.0f / 1200.0f)) * IFS;
        dt[1] = f * fm_exp2f(-det * (1.0f / 1200.0f)) * IFS;
        dt[2] = f * 0.5f * IFS;
        svf_set_at(&v->tone, f * (1.0f + 39.0f * tone * tone), 0.7f, IFS);
        svf_set_at(&v->br, f * 2.0f, 24.0f, IFS);
        g = 0.16f;
        {
            float amp[NPART], re[NPART], im[NPART];
            uint32_t np = 0;
            for (k = 0; k < NPART; k++)
                amp[k] = pa[k] * (1.0f - motion * 0.6f * (0.5f + 0.5f * fm_sin_turns(v->pdph[k]))) * pmix;
            for (k = 0; k < NPART; k++) {    /* (the audible ones, in locals: no memory traffic a sample) */
                re[k] = v->pre[k];
                im[k] = v->pim[k];
                if (amp[k] > 0.004f && ci[k] != 0.0f)
                    np = k + 1u;
            }
            for (i = 0; i < DH; i++) {
                float s = 0.0f, c = 0.0f, x;
                e0 += de;
                if (choir > 0.001f) {
                    for (k = 0; k < 3u; k++) {
                        float t = v->sph[k] += dt[k];
                        if (t >= 1.0f)
                            t = v->sph[k] -= 1.0f;
                        switch (wave) {      /* the voices' wave: buzzy .. round */
                        case 0: c += saw(t, dt[k]); break;
                        case 1: c += 0.55f * saw(t, dt[k]) + 0.9f * (1.0f - 4.0f * fm_fabsf(t - 0.5f)); break;
                        case 2: c += 1.3f * (1.0f - 4.0f * fm_fabsf(t - 0.5f)); break;
                        case 3: c += 0.8f * square(t, dt[k]); break;
                        case 4: c += 1.4f * fm_sin_turns(t); break;
                        default: c += 1.2f * fm_sin_turns(t + fmi * fm_sin_turns(2.0f * t)); break;
                        }
                    }
                    s += svf(&v->tone, c * 0.33f, 0) * choir;
                }
                for (k = 0; k < np; k++) {
                    float r2 = re[k] * cr[k] - im[k] * ci[k];
                    im[k] = re[k] * ci[k] + im[k] * cr[k];
                    re[k] = r2;
                    s += im[k] * amp[k];
                }
                if (breath > 0.001f)
                    s += svf(&v->br, rndf(), 1) * breath * 1.6f;
                x = s * e0 * e0 * g;
                hl[i] += x * v->pl;
                hr[i] += x * v->pr;
            }
            for (k = 0; k < np; k++) {
                v->pre[k] = re[k];
                v->pim[k] = im[k];
            }
        }
        for (k = 0; k < NPART; k++) {    /* the phasors back to unit length */
            float m = v->pre[k] * v->pre[k] + v->pim[k] * v->pim[k], s = 1.5f - 0.5f * m;
            v->pre[k] *= s;
            v->pim[k] *= s;
        }
        svf_flush(&v->tone);
        svf_flush(&v->br);
    }
}

/* ------------------------------------------------------------------- melody --- */
typedef struct {
    uint8_t osc;                         /* 0 sine, 1 triangle, 2 saw, 3 square, 4 FM, 5 supersaw, 6 organ, 7 clav,
                                          * 8 wavetable (fmr: its place 0..4, fmi: how far it sweeps as fenv falls) */
    float fmr, fmi, fmi_end, fmd;        /* FM: ratio, index from .. to, over seconds */
    float r2, l2, d2;                    /* a second partial: ratio, level, fall (bells, toys) */
    float cut, fenv, fdec, q;            /* filter: cutoff (times the note), its envelope (times), fall, Q; cut 0: none */
    float a, d, s, r;                    /* the level: attack, decay, sustain (0..1), release */
    float vib, trem, breath, drop, gain;
} msound_t;
static const msound_t MS[NMSOUND - 1u] = {   /* (and CUSTOM, made from its settings: ms_custom) */
    /* E.PIANO */ {4, 1.0f, 1.6f, 0.08f, 0.9f, 14.0f, 0.05f, 0.08f, 0, 0, 0, 0.7f, 0.004f, 1.2f, 0.25f, 1.0f, 0, 0, 0, 0, 0.9f},
    /* RHODES BELL */ {4, 1.0f, 2.2f, 0.12f, 0.7f, 2.01f, 0.25f, 0.6f, 0, 0, 0, 0.7f, 0.003f, 1.6f, 0.3f, 1.2f, 0, 0, 0, 0, 0.9f},
    /* WURLI */ {4, 1.0f, 3.2f, 0.5f, 0.45f, 0, 0, 0, 6.0f, 0, 0, 0.7f, 0.003f, 0.9f, 0.4f, 0.6f, 0, 0.2f, 0, 0, 0.8f},
    /* LOFI KEYS */ {4, 1.0f, 1.3f, 0.06f, 0.8f, 0, 0, 0, 3.0f, 0, 0, 0.7f, 0.006f, 1.3f, 0.3f, 1.0f, 0.12f, 0, 0.02f, 0, 0.9f},
    /* SOFT PIANO */ {1, 0, 0, 0, 0, 2.0f, 0.25f, 0.8f, 4.0f, 3.0f, 0.6f, 0.7f, 0.005f, 2.6f, 0.0f, 0.8f, 0, 0, 0, 0, 1.0f},
    /* TOY PIANO */ {0, 0, 0, 0, 0, 3.1f, 0.4f, 0.25f, 0, 0, 0, 0.7f, 0.002f, 1.1f, 0.0f, 0.5f, 0, 0, 0, 0, 1.0f},
    /* ORGAN */ {6, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.7f, 0.006f, 0.1f, 1.0f, 0.08f, 0.1f, 0, 0, 0, 0.6f},
    /* CLAV */ {7, 0, 0, 0, 0, 0, 0, 0, 3.0f, 0, 0, 1.6f, 0.002f, 0.4f, 0.3f, 0.2f, 0, 0, 0, 0, 0.8f},
    /* BELL */ {4, 3.5f, 1.4f, 0.03f, 2.2f, 0, 0, 0, 0, 0, 0, 0.7f, 0.003f, 2.6f, 0.0f, 2.0f, 0, 0, 0, 0, 0.8f},
    /* VIBRAPHONE */ {0, 0, 0, 0, 0, 4.0f, 0.2f, 0.15f, 0, 0, 0, 0.7f, 0.002f, 2.6f, 0.0f, 1.5f, 0, 0.25f, 0, 0, 1.0f},
    /* MARIMBA */ {0, 0, 0, 0, 0, 4.0f, 0.3f, 0.06f, 0, 0, 0, 0.7f, 0.002f, 0.55f, 0.0f, 0.4f, 0, 0, 0, 0, 1.1f},
    /* KALIMBA */ {4, 3.0f, 0.9f, 0.02f, 0.35f, 0, 0, 0, 0, 0, 0, 0.7f, 0.002f, 1.1f, 0.0f, 0.8f, 0, 0, 0, 0, 1.0f},
    /* MUSIC BOX */ {0, 0, 0, 0, 0, 5.4f, 0.3f, 0.2f, 0, 0, 0, 0.7f, 0.001f, 1.2f, 0.0f, 0.9f, 0, 0, 0, 12.0f, 0.9f},
    /* STEEL DRUM */ {4, 2.0f, 1.1f, 0.05f, 0.4f, 2.0f, 0.25f, 0.5f, 0, 0, 0, 0.7f, 0.002f, 1.0f, 0.0f, 0.8f, 0, 0, 0, 0.5f, 0.9f},
    /* PLUCK */ {2, 0, 0, 0, 0, 0, 0, 0, 1.0f, 8.0f, 0.25f, 2.0f, 0.002f, 0.6f, 0.0f, 0.4f, 0, 0, 0, 0, 0.7f},
    /* HARP */ {1, 0, 0, 0, 0, 2.0f, 0.2f, 0.9f, 2.0f, 6.0f, 0.6f, 0.7f, 0.003f, 1.7f, 0.0f, 1.2f, 0, 0, 0, 0, 1.0f},
    /* FLUTE */ {0, 0, 0, 0, 0, 2.0f, 0.12f, 9.0f, 0, 0, 0, 0.7f, 0.08f, 0.2f, 0.9f, 0.25f, 0.15f, 0, 0.06f, 0, 0.9f},
    /* OCARINA */ {0, 0, 0, 0, 0, 3.0f, 0.06f, 9.0f, 0, 0, 0, 0.7f, 0.06f, 0.2f, 0.9f, 0.2f, 0.12f, 0, 0.04f, 0, 1.0f},
    /* VOICE OOH */ {5, 0, 0, 0, 0, 0, 0, 0, 2.5f, 0, 0, 3.0f, 0.15f, 0.3f, 0.9f, 0.4f, 0.14f, 0, 0, 0, 0.6f},
    /* SAW LEAD */ {2, 0, 0, 0, 0, 0, 0, 0, 4.0f, 3.0f, 0.3f, 1.8f, 0.01f, 0.3f, 0.8f, 0.18f, 0.08f, 0, 0, 0, 0.6f},
    /* SUPERSAW */ {5, 0, 0, 0, 0, 0, 0, 0, 6.0f, 2.0f, 0.4f, 0.8f, 0.01f, 0.3f, 0.9f, 0.3f, 0, 0, 0, 0, 0.55f},
    /* ACID */ {2, 0, 0, 0, 0, 0, 0, 0, 1.5f, 12.0f, 0.18f, 8.0f, 0.003f, 0.25f, 0.4f, 0.08f, 0, 0, 0, 0, 0.6f},
    /* SINE LEAD */ {0, 0, 0, 0, 0, 2.0f, 0.06f, 9.0f, 0, 0, 0, 0.7f, 0.02f, 0.2f, 0.9f, 0.2f, 0.18f, 0, 0, 0, 1.0f},
    /* WARM PAD */ {5, 0, 0, 0, 0, 0.5f, 0.3f, 9.0f, 1.5f, 1.0f, 1.5f, 0.7f, 0.5f, 0.6f, 0.9f, 1.5f, 0.05f, 0, 0, 0, 0.6f},
    /* CELESTA */ {0, 0, 0, 0, 0, 4.0f, 0.35f, 0.3f, 0, 0, 0, 0.7f, 0.002f, 1.4f, 0.0f, 1.0f, 0, 0, 0, 0, 1.0f},
    /* GLOCKENSPIEL */ {0, 0, 0, 0, 0, 2.76f, 0.5f, 0.4f, 0, 0, 0, 0.7f, 0.001f, 1.8f, 0.0f, 1.4f, 0, 0, 0, 0, 0.9f},
    /* HANDPAN */ {0, 0, 0, 0, 0, 2.0f, 0.4f, 0.5f, 0, 0, 0, 0.7f, 0.003f, 1.6f, 0.0f, 1.3f, 0, 0, 0, 0.4f, 1.0f},
    /* KOTO */ {2, 0, 0, 0, 0, 0, 0, 0, 3.0f, 6.0f, 0.15f, 0.9f, 0.002f, 0.9f, 0.0f, 0.5f, 0.12f, 0, 0, 0.6f, 0.8f},
    /* GUZHENG */ {1, 0, 0, 0, 0, 0, 0, 0, 4.0f, 4.0f, 0.3f, 0.8f, 0.002f, 1.3f, 0.0f, 0.8f, 0.25f, 0, 0, 0.3f, 1.0f},
    /* SHAKUHACHI */ {0, 0, 0, 0, 0, 2.0f, 0.15f, 9.0f, 0, 0, 0, 0.7f, 0.09f, 0.3f, 0.85f, 0.3f, 0.22f, 0, 0.12f, 0, 0.9f},
    /* PAN FLUTE */ {1, 0, 0, 0, 0, 0, 0, 0, 3.0f, 0, 1.0f, 0.7f, 0.05f, 0.2f, 0.9f, 0.25f, 0.1f, 0, 0.08f, 0, 0.9f},
    /* ERHU */ {2, 0, 0, 0, 0, 0, 0, 0, 2.5f, 0, 1.0f, 1.2f, 0.12f, 0.3f, 0.9f, 0.3f, 0.35f, 0, 0, 0, 0.55f},
    /* CHOIR AAH */ {5, 0, 0, 0, 0, 0, 0, 0, 2.0f, 0.5f, 1.0f, 2.5f, 0.25f, 0.4f, 0.9f, 0.6f, 0.1f, 0, 0.03f, 0, 0.5f},
    /* STRINGS */ {5, 0, 0, 0, 0, 0, 0, 0, 3.5f, 0, 1.0f, 0.8f, 0.3f, 0.5f, 0.9f, 0.7f, 0.08f, 0, 0, 0, 0.5f},
    /* BRASS */ {2, 0, 0, 0, 0, 0, 0, 0, 1.5f, 5.0f, 0.25f, 1.0f, 0.04f, 0.3f, 0.8f, 0.2f, 0.05f, 0, 0, 0, 0.55f},
    /* SYNTH BELL */ {4, 3.5f, 2.0f, 0.2f, 0.6f, 0, 0, 0, 0, 0, 0, 0.7f, 0.002f, 2.0f, 0.0f, 1.5f, 0, 0, 0, 0, 0.8f},
    /* CRYSTAL */ {4, 7.0f, 1.2f, 0.05f, 0.3f, 2.0f, 0.3f, 0.6f, 0, 0, 0, 0.7f, 0.002f, 2.2f, 0.0f, 1.8f, 0, 0, 0, 0, 0.8f},
    /* CHIPTUNE */ {3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.7f, 0.001f, 0.2f, 0.7f, 0.05f, 0.12f, 0, 0, 0, 0.5f},
    /* HOLLOW LEAD */ {3, 0, 0, 0, 0, 0, 0, 0, 3.0f, 1.0f, 0.3f, 1.0f, 0.01f, 0.3f, 0.8f, 0.2f, 0.15f, 0, 0, 0, 0.6f},
    /* DREAM KEYS */ {4, 1.0f, 1.0f, 0.3f, 0.8f, 0, 0, 0, 0, 0, 0, 0.7f, 0.02f, 1.8f, 0.3f, 1.2f, 0, 0.2f, 0, 0, 0.8f},
    /* WAVETABLE PAD */ {8, 0.5f, 1.5f, 0, 0, 0, 0, 0, 6.0f, 0, 2.0f, 0.7f, 0.3f, 1.0f, 0.8f, 1.0f, 0.05f, 0, 0, 0, 0.6f},
    /* WT SWEEP */ {8, 3.8f, -3.5f, 0, 0, 0, 0, 0, 0, 0, 0.6f, 0.7f, 0.005f, 1.2f, 0.5f, 0.6f, 0, 0, 0, 0, 0.55f},
};
/* CUSTOM: the SOUND DESIGN pages' settings as a sound (made again each chunk: a turn is heard at once) */
static msound_t ms_custom;
static const float U_RATIO[11] = {0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 3.5f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
static const float U_P2[NUP2] = {0.0f, 2.0f, 3.0f, 4.0f, 2.76f, 5.4f};
static const float U_GAIN[NUWAVE] = {1.0f, 1.0f, 0.6f, 0.7f, 0.9f, 0.55f, 0.6f, 0.8f, 0.7f};
RAM static void custom_build(void)
{
    msound_t *c = &ms_custom;
    float pos = u7(P_UPOS), amt = u7(P_UMOD), x;
    c->osc = (uint8_t)(F.p[P_UWAVE] % NUWAVE);
    if (c->osc == 8u) {                  /* WAVETABLE: where in the table, and how far it sweeps (either way) */
        c->fmr = pos * 4.0f;
        c->fmi = (amt - 0.5f) * 8.0f;
    } else {                             /* FM: the ratio, the amount (it falls to a third) */
        c->fmr = U_RATIO[(uint32_t)(pos * 10.0f + 0.5f)];
        c->fmi = amt * 4.0f;
    }
    c->fmi_end = c->fmi * 0.3f;
    c->fmd = 0.6f;
    c->r2 = U_P2[F.p[P_UP2R] % NUP2];
    c->l2 = c->r2 > 0.0f ? u7(P_UP2L) * 0.8f : 0.0f;
    c->d2 = 1.5f;
    x = u7(P_UCUT);
    c->cut = x > 0.99f ? 0.0f : 0.5f * fm_powf(40.0f, x);
    c->fenv = u7(P_UFENV) * 8.0f;
    x = u7(P_URES);
    c->q = 0.5f + 8.0f * x * x;
    x = u7(P_UATK);
    c->a = 0.001f + 2.0f * x * x;
    x = u7(P_UDEC);
    c->d = 0.05f + 3.0f * x * x;
    c->fdec = c->d;
    c->s = u7(P_USUS);
    x = u7(P_UREL);
    c->r = 0.02f + 3.0f * x * x;
    c->vib = u7(P_UVIB) * 0.4f;
    c->trem = 0.0f;
    c->breath = u7(P_UBREATH) * 0.25f;
    c->drop = u7(P_UDROP) * 12.0f;
    c->gain = U_GAIN[c->osc];
}
/* (the main loop's) a factory sound's settings into CUSTOM's, as near as they go */
static uint8_t u127(float x) { x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; return (uint8_t)(x * 127.0f + 0.5f); }
static float usqrt(float x) { return x <= 0.0f ? 0.0f : fm_sqrtf(x); }
void eng_custom_from(uint32_t sound, uint8_t *p)
{
    const msound_t *s = &MS[sound % (NMSOUND - 1u)];
    uint32_t i, best = 0;
    p[P_UWAVE] = s->osc;
    if (s->osc == 8u) {
        p[P_UPOS] = u127(s->fmr / 4.0f);
        p[P_UMOD] = u127(s->fmi / 8.0f + 0.5f);
    } else {
        for (i = 1; i < 11u; i++)
            if (fm_fabsf(U_RATIO[i] - s->fmr) < fm_fabsf(U_RATIO[best] - s->fmr))
                best = i;
        p[P_UPOS] = u127((float)best / 10.0f);
        p[P_UMOD] = u127(s->fmi / 4.0f);
    }
    best = 0;
    if (s->l2 > 0.0f)
        for (i = 1, best = 1; i < NUP2; i++)
            if (fm_fabsf(U_P2[i] - s->r2) < fm_fabsf(U_P2[best] - s->r2))
                best = i;
    p[P_UP2R] = (uint8_t)best;
    p[P_UP2L] = u127(s->l2 / 0.8f);
    p[P_UCUT] = s->cut <= 0.0f ? 127u : u127(fm_log2f(s->cut / 0.5f) / fm_log2f(40.0f));
    if (p[P_UCUT] == 127u && s->cut > 0.0f)
        p[P_UCUT] = 126u;
    p[P_UFENV] = u127(s->fenv / 8.0f);
    p[P_URES] = u127(usqrt((s->q - 0.5f) / 8.0f));
    p[P_UATK] = u127(usqrt((s->a - 0.001f) / 2.0f));
    p[P_UDEC] = u127(usqrt((s->d - 0.05f) / 3.0f));
    p[P_USUS] = u127(s->s);
    p[P_UREL] = u127(usqrt((s->r - 0.02f) / 3.0f));
    p[P_UVIB] = u127(s->vib / 0.4f);
    p[P_UBREATH] = u127(s->breath / 0.25f);
    p[P_UDROP] = u127(s->drop / 12.0f);
}
#define NMV 4u                          /* (the sequence's notes and the ones played live) */
typedef struct {
    int16_t note;
    uint8_t gate;
    float t, f, fglide, glide_k, env, fenv, i2;
    float ph[4], mph, p2, vph;
    svf_t flt;
    const msound_t *s;
    float vel;
    uint8_t live;                        /* played on the keys: held until its key lets go */
} mvoice_t;
static mvoice_t mv[NMV];
static uint32_t mv_next;
static float mel_last_f;
static float ml[CH], mr[CH];

static inline float fm_cos_turns_b(float t) { return fm_sin_turns(t + 0.25f); }

/* the wavetable: five shapes in a row (sine, triangle, saw, square, a thin pulse), between two of them */
static inline float wt_shape(uint32_t k, float ph, float dt)
{
    switch (k) {
    case 0: return fm_sin_turns(ph);
    case 1: return 1.0f - 4.0f * fm_fabsf(ph - 0.5f);
    case 2: return saw(ph, dt);
    case 3: return square(ph, dt) * 0.8f;
    default: {                           /* a 25 % pulse, its offset taken out */
        float u = ph + 0.75f;
        u -= u >= 1.0f ? 1.0f : 0.0f;
        return 0.7f * ((ph < 0.25f ? 1.0f : -1.0f) + blep(ph, dt) - blep(u, dt) + 0.5f);
    }
    }
}
static inline float wt_at(float pos, float ph, float dt)
{
    uint32_t k = (uint32_t)pos;
    float fr = pos - (float)k;
    return wt_shape(k, ph, dt) + (wt_shape(k + 1u, ph, dt) - wt_shape(k, ph, dt)) * fr;
}

RAM static void mel_on(int note, float vel, int live)
{
    mvoice_t *v = 0;
    uint32_t i;
    for (i = 0; i < NMV && !v; i++)      /* a free voice, else the next in turn */
        if (!mv[(mv_next + i) % NMV].gate && mv[(mv_next + i) % NMV].env < 0.01f)
            v = &mv[(mv_next + i) % NMV];
    if (!v)
        v = &mv[mv_next % NMV];
    mv_next++;
    v->live = (uint8_t)live;
    uint32_t sn = F.p[P_MSOUND] % NMSOUND;
    const msound_t *s = sn == MS_CUSTOM ? &ms_custom : &MS[sn];
    float f = mtof((float)note), gl = u7(P_MGLIDE);
    v->s = s;
    v->note = (int16_t)note;
    v->gate = 1;
    v->t = 0.0f;
    v->vel = vel;
    v->f = f;
    v->fglide = gl > 0.01f && mel_last_f > 0.0f ? mel_last_f : f;
    v->glide_k = 1.0f - fm_expf(-1.0f / (0.004f + gl * gl * 0.4f) * INV_FS * (float)CH);
    mel_last_f = f;
    v->env = 0.0f;
    v->fenv = 1.0f;
    v->i2 = 1.0f;
    v->ph[0] = v->ph[1] = v->ph[2] = v->ph[3] = 0.0f;
    v->mph = v->p2 = v->vph = 0.0f;
    v->flt.s1 = v->flt.s2 = 0.0f;
}
RAM static void mel_off_all(void)
{
    uint32_t i;
    for (i = 0; i < NMV; i++)
        if (!mv[i].live)                 /* (the notes held on the keys ring on) */
            mv[i].gate = 0;
}

RAM static void mel_chunk(void)
{
    uint32_t i, j, k;
    float tone = clampf(u7(P_MTONE) + 0.5f * mod[11], 0.0f, 1.0f), tk = 0.3f + 1.7f * tone;
    for (i = 0; i < CH; i++)
        ml[i] = mr[i] = 0.0f;
    custom_build();
    for (j = 0; j < NMV; j++) {
        mvoice_t *v = &mv[j];
        const msound_t *s = v->s;
        float e0 = v->env, e1, de, f, dt, ct = (float)CH * INV_FS, vib, fi = 0.0f, l2, wpos = 0.0f;
        if (v->note < 0 || !s)
            continue;
        if (v->gate) {                   /* attack, decay to sustain */
            if (v->t < s->a)
                e1 = v->t / s->a;
            else
                e1 = s->s + (1.0f - s->s) * fm_expf(-(v->t - s->a) * 4.6f / s->d);
        } else {
            e1 = e0 * fm_expf(-ct * 4.6f / s->r);
        }
        if (!v->gate && e1 < 0.0005f) {
            v->note = -1;
            continue;
        }
        v->t += ct;
        v->env = e1;
        de = (e1 - e0) * (1.0f / (float)CH);
        v->fglide += (v->f - v->fglide) * v->glide_k;
        vib = s->vib * (v->t > 0.25f ? 1.0f : v->t * 4.0f);
        v->vph = wrap(v->vph + 5.5f * ct);
        f = v->fglide * (1.0f + vib * 0.03f * fm_sin_turns(v->vph));
        if (s->drop > 0.0f)
            f *= 1.0f + 0.03f * s->drop * fm_expf(-v->t * 30.0f);
        dt = f * INV_FS;
        v->fenv = fm_expf(-v->t / (s->fdec + 0.001f));
        if (s->cut > 0.0f)
            svf_set(&v->flt, f * s->cut * tk * (1.0f + s->fenv * v->fenv), s->q);
        if (s->osc == 4) {
            float k1 = fm_expf(-v->t / s->fmd);
            fi = (s->fmi_end + (s->fmi - s->fmi_end) * k1) * tk;
        }
        l2 = s->l2 * fm_expf(-v->t / (s->d2 + 0.001f)) * tk;
        if (s->osc == 8u)                /* the wavetable's place: from fmr, swept by fmi as the envelope falls */
            wpos = clampf(s->fmr + s->fmi * (1.0f - v->fenv), 0.0f, 3.999f);
        {
            float tr = 1.0f - s->trem * (0.5f + 0.5f * fm_sin_turns(wrap(v->t * 5.5f)));
            float g = s->gain * 0.32f * v->vel * tr;
            for (i = 0; i < CH; i++) {
                float o = 0.0f;
                e0 += de;
                v->ph[0] += dt;
                v->ph[0] -= v->ph[0] >= 1.0f ? 1.0f : 0.0f;
                switch (s->osc) {
                case 0: o = fm_sin_turns(v->ph[0]); break;
                case 1: o = 4.0f * fm_fabsf(v->ph[0] - 0.5f) - 1.0f; break;
                case 2: o = saw(v->ph[0], dt); break;
                case 3: o = square(v->ph[0], dt) * 0.7f; break;
                case 4:
                    v->mph += dt * s->fmr;
                    v->mph -= (float)(int32_t)v->mph;
                    o = fm_sin_turns(wrap(v->ph[0] + fi * 0.16f * fm_sin_turns(v->mph) + 4.0f));
                    break;
                case 5:
                    o = saw(v->ph[0], dt);
                    for (k = 1; k < 3u; k++) {
                        float d = dt * (k == 1u ? 1.006f : 0.994f);
                        v->ph[k] += d;
                        v->ph[k] -= v->ph[k] >= 1.0f ? 1.0f : 0.0f;
                        o += saw(v->ph[k], d);
                    }
                    o *= 0.45f;
                    break;
                case 6: {
                    float p = v->ph[0];
                    o = 0.5f * fm_sin_turns(p) + 0.3f * fm_sin_turns(wrap(2.0f * p)) + 0.2f * fm_sin_turns(wrap(3.0f * p)) +
                        0.12f * fm_sin_turns(wrap(4.0f * p));
                    break;
                }
                case 7: o = square(v->ph[0], dt) * 0.5f + saw(v->ph[0], dt) * 0.3f; break;
                default: o = wt_at(wpos, v->ph[0], dt); break;
                }
                if (l2 > 0.0005f) {
                    v->p2 = wrap(v->p2 + dt * s->r2);
                    o += fm_sin_turns(v->p2) * l2;
                }
                if (s->breath > 0.0f)
                    o += rndf() * s->breath;
                if (s->cut > 0.0f)
                    o = svf(&v->flt, o, s->osc == 7 ? 1 : 0);
                o *= e0 * g;
                ml[i] += o;
                mr[i] += o;
            }
        }
        svf_flush(&v->flt);
    }
}

/* ------------------------------------------------------------------- bass --- */
static struct {
    int16_t note;
    float f, fg, t, env, len, ph[3], mph;
    svf_t flt;
} bs = {-1, 0, 0, 0, 0, 0, {0}, 0, {0}};
static float bsl[CH];

RAM static void bass_on(int note, float len)
{
    float gl = u7(P_BGLIDE);
    bs.f = mtof((float)note);
    if (bs.note < 0 || gl < 0.01f)
        bs.fg = bs.f;
    bs.note = (int16_t)note;
    bs.t = 0.0f;
    bs.len = len;
}
RAM static void bass_chunk(void)
{
    uint32_t i;
    float ct = (float)CH * INV_FS, gl = u7(P_BGLIDE), e1, de, dt, g, kgl;
    int model = F.p[P_BMODEL] % NBMODEL;
    for (i = 0; i < CH; i++)
        bsl[i] = 0.0f;
    if (bs.note < 0)
        return;
    kgl = 1.0f - fm_expf(-ct / (0.003f + gl * gl * 0.3f));
    bs.fg += (bs.f - bs.fg) * kgl;
    bs.t += ct;
    e1 = bs.t < 0.006f ? bs.t / 0.006f : bs.t < bs.len ? 1.0f : fm_expf(-(bs.t - bs.len) * 18.0f);
    if (bs.t > bs.len + 0.5f) {
        bs.note = -1;
        return;
    }
    if (model == 14)                     /* UPRIGHT: a plucked string, it fades even held */
        e1 *= fm_expf(-bs.t * 2.5f);
    de = (e1 - bs.env) * (1.0f / (float)CH);
    dt = bs.fg * INV_FS * (model == 8 ? 1.0f + 0.6f * fm_expf(-bs.t * 22.0f) : 1.0f);   /* 808: the pitch falls in */
    {
        float tone = clampf(u7(P_BTONE) + 0.5f * mod[10], 0.0f, 1.0f), cut, q = 1.2f;
        switch (model) {                 /* each model's filter */
        case 9:                          /* ACID: resonant, its envelope snapping shut */
            cut = 140.0f + 4200.0f * tone * tone * fm_expf(-bs.t * 9.0f) + 200.0f * tone;
            q = 6.0f;
            break;
        case 10:                         /* PLUCK: bright at the front, closing fast */
            cut = 180.0f + 3200.0f * (0.3f + tone) * fm_expf(-bs.t * 20.0f);
            break;
        case 12:                         /* WOBBLE: the filter swung in eighths */
            cut = 150.0f + 2600.0f * tone * (0.5f - 0.5f * fm_cos_turns_b(bs.t * bpm_now / 30.0f));
            q = 3.0f;
            break;
        case 13:                         /* MOOG: a fat low-pass, a little resonance, a soft fall */
            cut = 200.0f + 2400.0f * tone * tone * (0.4f + 0.6f * fm_expf(-bs.t * 5.0f));
            q = 2.0f;
            break;
        default:
            cut = 120.0f + 3600.0f * tone * tone * (1.0f + 1.5f * fm_expf(-bs.t * 12.0f));
            break;
        }
        svf_set(&bs.flt, cut, q);
    }
    g = 0.5f;
    for (i = 0; i < CH; i++) {
        float o;
        bs.env += de;
        bs.ph[0] = wrap(bs.ph[0] + dt);
        switch (model) {
        case 0: o = fm_sin_turns(bs.ph[0]); break;
        case 1: o = 4.0f * fm_fabsf(bs.ph[0] - 0.5f) - 1.0f; break;
        case 2: o = square(bs.ph[0], dt) * 0.6f; break;
        case 3: o = saw(bs.ph[0], dt) * 0.7f; break;
        case 4:
            bs.ph[1] = wrap(bs.ph[1] + dt * 1.008f);
            bs.ph[2] = wrap(bs.ph[2] + dt * 0.5f);
            o = (saw(bs.ph[0], dt) + saw(bs.ph[1], dt * 1.008f)) * 0.4f + 0.5f * fm_sin_turns(bs.ph[2]);
            break;
        case 5:
            bs.ph[2] = wrap(bs.ph[2] + dt * 0.5f);
            o = fm_sin_turns(bs.ph[2]) + 0.3f * (4.0f * fm_fabsf(bs.ph[0] - 0.5f) - 1.0f);
            break;
        case 6: o = 0.6f * fm_sin_turns(bs.ph[0]) + 0.3f * fm_sin_turns(wrap(2.0f * bs.ph[0])) + 0.15f * fm_sin_turns(wrap(3.0f * bs.ph[0])); break;
        case 7:
            bs.mph = wrap(bs.mph + dt * 2.0f);
            o = fm_sin_turns(wrap(bs.ph[0] + 0.35f * fm_expf(-bs.t * 6.0f) * fm_sin_turns(bs.mph) + 4.0f));
            break;
        case 8: o = fm_tanhf(1.8f * fm_sin_turns(bs.ph[0])) * 0.9f; break;          /* 808: a sine, driven */
        case 9: o = saw(bs.ph[0], dt) * 0.7f; break;                                  /* ACID */
        case 10: o = 0.5f * saw(bs.ph[0], dt) + 0.5f * (4.0f * fm_fabsf(bs.ph[0] - 0.5f) - 1.0f); break;   /* PLUCK */
        case 11: o = fm_sin_turns(bs.ph[0]) + 0.25f * fm_sin_turns(wrap(2.0f * bs.ph[0])); break;          /* DUB */
        case 12: o = 0.5f * saw(bs.ph[0], dt) + 0.4f * square(bs.ph[0], dt); break;                          /* WOBBLE */
        case 13:                                                                       /* MOOG: two, a hair apart */
            bs.ph[1] = wrap(bs.ph[1] + dt * 1.004f);
            o = fm_tanhf(1.4f * (0.5f * saw(bs.ph[0], dt) + 0.4f * square(bs.ph[1], dt * 1.004f)));
            break;
        case 14: o = 0.8f * fm_sin_turns(bs.ph[0]) + 0.25f * (4.0f * fm_fabsf(bs.ph[0] - 0.5f) - 1.0f); break;   /* UPRIGHT */
        default:                                                                       /* SYNTH: a square, a sub under */
            bs.ph[2] = wrap(bs.ph[2] + dt * 0.5f);
            o = 0.45f * square(bs.ph[0], dt) + 0.6f * fm_sin_turns(bs.ph[2]);
            break;
        }
        o = svf(&bs.flt, o, 0);
        bsl[i] = o * bs.env * g;
    }
    svf_flush(&bs.flt);
}

/* ------------------------------------------------------------------- shards (FM bells) --- */
static const float SP[NSPRESET][4] = {   /* ratio, index, ring (s), octave */
    {3.5f, 1.4f, 2.5f, 1}, {2.0f, 1.0f, 3.0f, 1}, {4.1f, 1.8f, 2.0f, 2}, {5.0f, 0.7f, 1.6f, 2},
    {2.76f, 1.2f, 4.5f, 0}, {7.0f, 2.2f, 2.4f, 2}, {3.0f, 0.8f, 1.4f, 1}, {4.0f, 0.5f, 3.0f, 1}};
#define NSV 4u
typedef struct {
    float f, t, ring, idx, ratio, ph, mph, pan, amp;
    uint8_t on;
} svoice_t;
static svoice_t sv[NSV];
static uint32_t sv_next;
static float shl[CH], shr[CH];
RAM static void shard_on(int note, float vel)
{
    const float *p = SP[F.p[P_SPRESET] % NSPRESET];
    float spark = u7(P_SPARKLE), ring = 0.3f + 1.5f * u7(P_SRING);
    svoice_t *v = &sv[sv_next++ % NSV];
    v->f = mtof((float)note);
    v->t = 0.0f;
    v->ring = p[2] * ring;
    v->idx = p[1] * (0.6f + 0.9f * spark);
    v->ratio = p[0];
    v->ph = v->mph = 0.0f;
    v->pan = rndf() * 0.8f;
    v->amp = vel * (0.08f + 0.06f * rnd01());
    v->on = 1;
}
RAM static void shards_chunk(void)
{
    uint32_t i, j;
    float ct = (float)CH * INV_FS;
    for (i = 0; i < CH; i++)
        shl[i] = shr[i] = 0.0f;
    for (j = 0; j < NSV; j++) {
        float e, ix, dt, l, r;
        if (!sv[j].on)
            continue;
        sv[j].t += ct;
        e = fm_expf(-sv[j].t * 6.9f / sv[j].ring) * (sv[j].t < 0.004f ? sv[j].t / 0.004f : 1.0f);
        if (e < 0.0005f) {
            sv[j].on = 0;
            continue;
        }
        ix = sv[j].idx * fm_expf(-sv[j].t * 2.5f) * 0.16f;
        dt = sv[j].f * INV_FS;
        l = 0.7f - 0.5f * sv[j].pan;
        r = 0.7f + 0.5f * sv[j].pan;
        for (i = 0; i < CH; i++) {
            float o;
            sv[j].ph = wrap(sv[j].ph + dt);
            sv[j].mph = wrap(sv[j].mph + dt * sv[j].ratio);
            o = fm_sin_turns(wrap(sv[j].ph + ix * fm_sin_turns(sv[j].mph) + 4.0f)) * e * sv[j].amp;
            shl[i] += o * l;
            shr[i] += o * r;
        }
    }
}

/* ------------------------------------------------------------------- the cloud --- */
#define NFDN 8u
static const uint16_t FDN_LEN[NFDN] = {1559, 1907, 2269, 2593, 2963, 3329, 3701, 4093};
#define FDN_MAX 4100u
static int16_t fdn_buf[NFDN][FDN_MAX] POOL;   /* (16 bits: 66 KB for the eight lines) */
#define FDN_Q 4096.0f
#define FDN_IQ (1.0f / 4096.0f)
static uint32_t fdn_w;
static float fdn_len[NFDN], fdn_lp[NFDN];
#define AP_N 2u
static float ap_buf[AP_N][1024] POOL;
static const uint16_t AP_LEN[AP_N] = {353, 829};
static uint32_t ap_w;
#define SH_N 4096u
static float sh_buf[SH_N] POOL;          /* the shimmer's octave-up shifter */
static uint32_t sh_w;
static float sh_ph, sh_out, fdn_mod_ph;
static float frz = 0.0f;                 /* FREEZE, gliding 0 .. 1 */
static float cin_l[CH], cin_r[CH], cwl[CH], cwr[CH];

static inline float fdn_read(uint32_t k, float len)
{
    float pos = (float)fdn_w - len, fr;
    int32_t a;
    if (pos < 0.0f)
        pos += (float)FDN_MAX;
    a = (int32_t)pos;
    fr = pos - (float)a;
    return ((float)fdn_buf[k][a] + (float)(fdn_buf[k][(uint32_t)(a + 1) % FDN_MAX] - fdn_buf[k][a]) * fr) * FDN_IQ;
}

static float cw_last_l, cw_last_r, fdn_mo[2];
RAM static void cloud_chunk(float size, float decay_s, float damp, float shim)
{
    uint32_t i, k;
    float g[NFDN], lp = 0.08f + 0.85f * (1.0f - damp), fz = frz, in_g = 1.0f - fz;
    float tgt[NFDN];
    for (k = 0; k < NFDN; k++) {
        tgt[k] = (float)FDN_LEN[k] * 0.5f * (0.35f + 0.65f * size);   /* (at half the rate: half the samples) */
        g[k] = fz > 0.5f ? 1.0f : fm_powf(10.0f, -3.0f * tgt[k] / (decay_s * FS * 0.5f));
        g[k] += (1.0f - g[k]) * fz;
    }
    if (fz > 0.5f)
        lp = 1.0f;
    fdn_mod_ph = wrap(fdn_mod_ph + 0.4f * (float)CH * INV_FS);
    {
        float m0 = 3.0f * fm_sin_turns(fdn_mod_ph), m1 = 3.0f * fm_sin_turns(wrap(fdn_mod_ph + 0.25f));
        fdn_mo[0] = m0;                  /* (the two lines' wander, once a chunk: it moves at 0.4 Hz) */
        fdn_mo[1] = m1;
    }
    for (i = 0; i < CH; i += 2u) {      /* the cloud runs at half the rate: a sample from each two */
        float x = (cin_l[i] + cin_r[i] + cin_l[i + 1u] + cin_r[i + 1u]) * 0.25f * in_g, y[NFDN], sum = 0.0f, a, ol, or_;
        /* the shimmer: an octave up off the cloud's last output, fed back in */
        if (shim > 0.001f) {
            float o = 0.0f, W = 1024.0f;
            uint32_t h;
            sh_buf[sh_w] = sh_out;
            sh_ph += 1.0f / W;
            sh_ph -= sh_ph >= 1.0f ? 1.0f : 0.0f;
            for (h = 0; h < 2u; h++) {
                float p = sh_ph + 0.5f * (float)h, d, rp, fr;
                int32_t ri;
                p -= p >= 1.0f ? 1.0f : 0.0f;
                d = W * (1.0f - p) + 2.0f;
                rp = (float)sh_w - d;
                if (rp < 0.0f)
                    rp += (float)SH_N;
                ri = (int32_t)rp;
                fr = rp - (float)ri;
                o += (sh_buf[ri] + (sh_buf[(uint32_t)(ri + 1) % SH_N] - sh_buf[ri]) * fr) * fm_sin_turns(p * 0.5f);
            }
            sh_w = (sh_w + 1u) % SH_N;
            x += o * shim * 0.6f * in_g;
        }
        for (k = 0; k < AP_N; k++) {     /* diffusion */
            uint32_t r = (ap_w + 1024u - AP_LEN[k]) & 1023u;
            float d = ap_buf[k][r], v = x + 0.6f * d;
            ap_buf[k][ap_w] = v;
            x = d - 0.6f * v;
        }
        ap_w = (ap_w + 1u) & 1023u;
        for (k = 0; k < NFDN; k++) {
            fdn_len[k] += (tgt[k] - fdn_len[k]) * 0.0005f;
            y[k] = fdn_read(k, fdn_len[k] + (k < 2u ? fdn_mo[k] : 0.0f));
            sum += y[k];
        }
        sum *= 2.0f / (float)NFDN;
        for (k = 0; k < NFDN; k++) {     /* Householder: each minus the share of all, damped, fed back */
            a = (y[k] - sum) * g[k];
            fdn_lp[k] += (a - fdn_lp[k]) * lp;
            fdn_buf[k][fdn_w] = (int16_t)(clampf(fdn_lp[k] + (k & 1u ? x : -x) * 0.35f, -7.9f, 7.9f) * FDN_Q);
        }
        fdn_w = (fdn_w + 1u) % FDN_MAX;
        ol = (y[0] + y[2] + y[4] + y[6]) * 0.35f;
        or_ = (y[1] + y[3] + y[5] + y[7]) * 0.35f;
        cwl[i] = 0.5f * (cw_last_l + ol);
        cwr[i] = 0.5f * (cw_last_r + or_);
        cwl[i + 1u] = cw_last_l = ol;
        cwr[i + 1u] = cw_last_r = or_;
        sh_out = (ol + or_) * 0.5f;
    }
    for (k = 0; k < NFDN; k++)
        fdn_lp[k] = fm_flush(fdn_lp[k]);
}

/* ------------------------------------------------------------------- the echo --- */
#define ECHO_N 26500u                    /* 0.6 s */
static int16_t echo_l[ECHO_N] POOL, echo_r[ECHO_N] POOL;
static uint32_t echo_w;
static float echo_len, echo_lp_l, echo_lp_r;
static float ein_l[CH], ein_r[CH], ewl[CH], ewr[CH];
static const float ECHO_BEATS[8] = {0.25f, 0.5f, 0.3333333f, 0.75f, 1.0f, 1.5f, 0.6666667f, 2.0f};
RAM static void echo_chunk(float amt, float fb)
{
    uint32_t i;
    float beats = ECHO_BEATS[F.p[P_ETIME] % 8u], target = beats * 60.0f / bpm_now * FS, warble = mod[9];
    if (target > (float)(ECHO_N - 64u))
        target = (float)(ECHO_N - 64u);
    for (i = 0; i < CH; i++) {
        float pos, fr, a, b;
        int32_t ri;
        echo_len += (target - echo_len) * 0.0002f;
        pos = (float)echo_w - echo_len * (1.0f + 0.004f * warble);
        if (pos < 0.0f)
            pos += (float)ECHO_N;
        ri = (int32_t)pos;
        fr = pos - (float)ri;
        a = ((float)echo_l[ri] + ((float)echo_l[(uint32_t)(ri + 1) % ECHO_N] - (float)echo_l[ri]) * fr) * (1.0f / 16384.0f);
        b = ((float)echo_r[ri] + ((float)echo_r[(uint32_t)(ri + 1) % ECHO_N] - (float)echo_r[ri]) * fr) * (1.0f / 16384.0f);
        echo_lp_l += (a - echo_lp_l) * 0.35f;
        echo_lp_r += (b - echo_lp_r) * 0.35f;
        ewl[i] = echo_lp_l * amt;
        ewr[i] = echo_lp_r * amt;
        {                                /* ping-pong: each side into the other */
            float nl = clampf(ein_l[i] + echo_lp_r * fb, -1.9f, 1.9f), nr = clampf(ein_r[i] + echo_lp_l * fb, -1.9f, 1.9f);
            echo_l[echo_w] = (int16_t)(clampf(nl, -1.99f, 1.99f) * 16384.0f);   /* (in range on every CPU) */
            echo_r[echo_w] = (int16_t)(clampf(nr, -1.99f, 1.99f) * 16384.0f);
        }
        echo_w = (echo_w + 1u) % ECHO_N;
    }
    echo_lp_l = fm_flush(echo_lp_l);
    echo_lp_r = fm_flush(echo_lp_r);
}

/* ------------------------------------------------------------------- the tape (wow and flutter) --- */
#define TAPE_N 1024u
static float tape_l[TAPE_N] POOL, tape_r[TAPE_N] POOL;
static uint32_t tape_w;
static float tape_ph, tape_ph2;

/* ------------------------------------------------------------------- the LFOs --- */
RAM static void lfos_chunk(void)
{
    uint32_t l, k;
    float ct = (float)CH * INV_FS;
    for (k = 0; k < NLTGT; k++)
        mod[k] = 0.0f;
    for (l = 0; l < 3u; l++) {
        int base = P_L1SHAPE + 4 * (int)l, sh = F.p[base] % NLSHAPE, tg = F.p[base + 3] % NLTGT;
        float hz = eng_lfo_hz(F.p[base + 1], bpm_now), p, v, depth = ((float)F.p[base + 2] - 64.0f) * (1.0f / 63.0f);
        lfo_ph[l] += hz * ct;
        if (lfo_ph[l] >= 1.0f) {
            lfo_ph[l] -= (float)(int32_t)lfo_ph[l];
            lfo_r[l][0] = lfo_r[l][1];
            lfo_r[l][1] = rndf();
        }
        p = lfo_ph[l];
        switch (sh) {
        case 0: v = fm_sin_turns(p); break;
        case 1: v = p < 0.25f ? 4.0f * p : p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f; break;
        case 2: v = 2.0f * p - 1.0f; break;
        case 3: v = 1.0f - 2.0f * p; break;
        case 4: v = p < 0.5f ? 1.0f : -1.0f; break;
        case 5: v = lfo_r[l][1]; break;
        default: {
            float c = 0.5f - 0.5f * fm_cosf(3.14159265f * p);
            v = lfo_r[l][0] + (lfo_r[l][1] - lfo_r[l][0]) * c;
            break;
        }
        }
        lfo_v[l] += (v - lfo_v[l]) * (sh == 4 || sh == 5 ? 0.5f : 1.0f);   /* (steps a touch rounded: no clicks) */
        V.lfo[l] = lfo_v[l];
        if (tg)
            mod[tg] += lfo_v[l] * depth;
    }
    {                                    /* key pressure: one more source, on its own target */
        uint32_t pt = F.p[P_PTGT] % NLTGT;
        if (pt)
            mod[pt] += F.press * ((float)F.p[P_PDEPTH] - 64.0f) * (1.0f / 63.0f);
    }
}

/* ------------------------------------------------------------------- the sequencers --- */
static struct {
    float pos;                           /* sixteenths since the next step's place on the grid (floats only: the FM-1
                                          * has no double maths) */
    int32_t step;                        /* the last struck, absolute */
    uint8_t running;
    uint32_t bar;
    float bass_last;
    uint32_t ext_ticks, ext_last_ms;
    float ext_period;                    /* MIDI clock: samples between ticks, smoothed */
    uint32_t sample;                     /* samples since the start */
    uint32_t last_tick_sample;
    uint8_t ext_run;
} sq;

RAM static float swing_off(int32_t s, float sps) { return (s & 1) ? u7(P_SWING) * 0.33f * sps : 0.0f; }

RAM static void seq_step(int32_t abs_step)
{
    uint32_t s = (uint32_t)abs_step % NSTEP, t;
    float h = u7(P_DMHUMAN), spark = u7(P_SPARKLE);
    uint32_t bar = (uint32_t)abs_step / NSTEP;
    int fill = F.fill && s >= 8u;
    V.step = (int8_t)s;
    V.bar = bar;
    switch (F.p[P_SCTRIG] % NSCTRIG) {   /* the sidechain's rhythm: beats it fires on, drums or not */
    case 1: sc_step |= (s % 4u) == 0u; break;                  /* every beat */
    case 2: sc_step |= (s % 2u) == 0u; break;                  /* the eighths */
    case 3: sc_step |= (s % 4u) == 2u; break;                  /* the off-beats */
    case 6: sc_step |= s == 0u || s == 6u || s == 12u; break;  /* 3-3-2 (a dotted feel) */
    default: break;
    }
    /* drums */
    if (F.p[P_DMPLAYS]) {
        for (t = 0; t < NDRUM; t++) {
            uint32_t v = F.pat.drum[t][s];
            if (fill && t != DM_KICK)
                v = 0;
            if (!v && t == DM_HAT && h > 0.4f && rnd01() < (h - 0.4f) * 0.3f)
                v = 1;
            if (v == 2u && rnd01() < h * 0.2f)
                v = 1;
            if (v)
                drums_hit(t, (v == 2u ? 1.0f : 0.55f) * (1.0f - h * 0.25f * rnd01()));
        }
        if (fill) {
            uint32_t k = s - 8u;
            drums_hit(DM_SNARE, k < 4u ? (k & 1u ? 0.5f : 0.8f) : 1.0f);
            if (k >= 4u && (k & 1u))
                drums_hit(DM_CLAP, 0.8f);
            if (s == 15u) {
                drums_hit(DM_OHAT, 0.9f);
                F.fill = 0;
            }
        }
    }
    /* the bass */
    if (F.p[P_BPLAYS] && F.pat.bass[s] != BASS_OFF) {
        int32_t root = F.harm_n ? F.harm[0] : 48 + (int32_t)(F.p[P_ROOT] % 12u), n = 0, note, pc = root % 12, m;
        uint32_t nt = 0, i;
        int32_t tones[8];
        for (i = 0; i < F.harm_n && i < 8u; i++) {   /* the chord's tones above the root, once each */
            int32_t x = ((F.harm[i] - root) % 12 + 12) % 12, j;
            for (j = 0; j < (int32_t)nt && tones[j] != x; j++)
                ;
            if (j == (int32_t)nt)
                tones[nt++] = x;
        }
        if (!nt)
            tones[nt++] = 0;
        for (i = 1; i < nt; i++)         /* (low to high) */
            for (m = (int32_t)i; m > 0 && tones[m - 1] > tones[m]; m--) {
                int32_t x = tones[m];
                tones[m] = tones[m - 1];
                tones[m - 1] = x;
            }
        if (F.pat.bass[s] == BASS_ROOT) {
            switch (F.p[P_BMODE] % NBMODE) {
            case 1: n = (s % 8u) >= 4u ? 7 : 0; break;
            case 2: n = tones[(s / 2u) % nt]; break;
            case 3: n = tones[nt - 1u - (s / 2u) % nt]; break;
            case 4: n = (s % 4u) == 2u ? 12 : 0; break;
            case 5: n = tones[rnd32() % nt]; break;
            default: n = 0; break;
            }
            note = 36 + pc + n;
        } else {
            note = 36 + F.pat.bass[s];
        }
        note += 12 * ((int32_t)F.p[P_BOCT] - 1);
        while (note > 55)
            note -= 12;
        bass_on(note, (0.08f + 0.9f * u7(P_BLEN)) * 60.0f / bpm_now);
    }
    /* the melody */
    if (F.p[P_MPLAYS] && F.pat.mel[s] != MEL_REST && F.pat.mel[s] >= 0) {
        int32_t root = (int32_t)(F.p[P_ROOT] % 12u), note = 60 + root + eng_scale_deg(F.p[P_SCALE], F.pat.mel[s]) +
                                                         12 * ((int32_t)F.p[P_MOCT] - 1) - (root > 5 ? 12 : 0);
        float vel = 1.0f - u7(P_MHUMAN) * 0.4f * rnd01();
        uint32_t gap = 1;
        while (gap < NSTEP && F.pat.mel[(s + gap) % NSTEP] == MEL_REST)
            gap++;
        mel_off_all();
        mel_on(note, vel, 0);
        (void)gap;
    } else if (F.p[P_MPLAYS] && (s % 2u) == 0u) {
        /* a rest: the note held through, let go by LENGTH below */
    }
    /* the shards: on the eighths, as dense as DENSITY and SPARKLE say */
    if ((s & 1u) == 0u) {
        float dens = u7(P_SDENS) * 0.55f + spark * 0.18f;
        if (dens > 0.0f && rnd01() < dens && F.harm_n) {
            int32_t pc = F.harm[rnd32() % F.harm_n] % 12, oct = (int32_t)(F.p[P_SOCT] % 3u);
            int32_t note = 72 + pc + 12 * oct - (pc > 7 ? 12 : 0) + (rnd01() < 0.3f ? 12 : 0);
            shard_on(note, 1.0f);
        }
    }
}

/* the melody's notes let go after LENGTH (the next note or LENGTH's share of the gap, whichever first) */
static float mel_gate_left;

RAM static void seq_chunk(void)
{
    float bpm = F.p[P_CLOCK] && sq.ext_period > 1.0f ? FS * 60.0f / (sq.ext_period * 24.0f) : (float)(F.p[P_TEMPO] + 40u);
    float sps = FS * 60.0f / (bpm * 4.0f);   /* samples a sixteenth */
    bpm_now = bpm;
    V.bpm = bpm;
    if (!F.playing) {
        sq.running = 0;
        V.step = -1;
        if (V.sounding && F.harm_n) {    /* SPARKLE with no beat: bells now and then over the chord held */
            float spark = u7(P_SPARKLE), rate = spark * spark * 3.0f + u7(P_SDENS) * 0.4f;
            if (rnd01() < rate * (float)CH * INV_FS) {
                int32_t pc = F.harm[rnd32() % F.harm_n] % 12, oct = (int32_t)(F.p[P_SOCT] % 3u);
                shard_on(72 + pc + 12 * oct - (pc > 7 ? 12 : 0) + (rnd01() < 0.4f ? 12 : 0), 0.8f + 0.2f * rnd01());
            }
        }
        return;
    }
    if (!sq.running) {                   /* PLAY: from the top */
        sq.running = 1;
        sq.pos = 0.0f;
        sq.step = -1;
        mel_last_f = 0.0f;
    }
    {
        int32_t next = sq.step + 1;
        if (sq.pos >= swing_off(next, sps) / sps) {
            sq.step = next;
            sq.pos -= 1.0f;              /* (now measured from the step after) */
            seq_step(next);
            if (F.pat.mel[next % (int32_t)NSTEP] != MEL_REST) {
                uint32_t gap = 1;
                while (gap < NSTEP && F.pat.mel[(uint32_t)(next + (int32_t)gap) % NSTEP] == MEL_REST)
                    gap++;
                mel_gate_left = (float)gap * sps * (0.15f + 0.85f * u7(P_MLEN)) * INV_FS;
            }
        }
    }
    if (mel_gate_left > 0.0f) {
        mel_gate_left -= (float)CH * INV_FS;
        if (mel_gate_left <= 0.0f)
            mel_off_all();
    }
    sq.pos += (float)CH / sps;
    V.sfrac = clampf(sq.pos + 1.0f, 0.0f, 1.0f);
}

void eng_midi_clock(uint32_t status)
{
    if (status == 0xF8u) {
        uint32_t d = sq.sample - sq.last_tick_sample;
        sq.last_tick_sample = sq.sample;
        if (d > 200u && d < 20000u)
            sq.ext_period = sq.ext_period > 1.0f ? sq.ext_period + (float)((float)d - sq.ext_period) * 0.1f : (float)d;
    }
}

/* ------------------------------------------------------------------- render --- */
static float lim_env, pump_env, swell_ph, sink_l1, sink_l2, sink_r1, sink_r2, mf_s[4], gain_now, dc_l, dc_r, meters[6];
static svf_t df_l, df_r;                 /* the drone filter */
static float dbuf_l[CH], dbuf_r[CH];
static float frz_t;

RAM static void events(void)
{
    while (ev_r != ev_w) {
        ev_t e = evq[ev_r % NEV];
        __asm__ volatile("" ::: "memory");
        ev_r++;
        switch (e.type) {
        case EV_DRONE_ON: drone_on(e.a, e.b); break;
        case EV_DRONE_OFF: drone_off(e.a); break;
        case EV_ALL_OFF: {
            uint32_t i;
            for (i = 0; i < NDV; i++)
                dv[i].gate = 0;
            break;
        }
        case EV_MEL: if (e.b > 0.0f) { mel_off_all(); mel_on(e.a, e.b, 0); mel_gate_left = 0.35f; } break;
        case EV_MEL_ON: mel_on(e.a, e.b, 1); break;
        case EV_MEL_OFF: {
            uint32_t i;
            for (i = 0; i < NMV; i++)
                if (mv[i].live && mv[i].note == e.a)
                    mv[i].gate = 0, mv[i].live = 0;
            break;
        }
        case EV_BASS: bass_on(e.a, e.b); break;
        case EV_DRUM: drums_hit((uint32_t)e.a, e.b); break;
        case EV_SHARD: shard_on(e.a, e.b); break;
        case EV_PANIC: {
            uint32_t i;
            for (i = 0; i < NDV; i++)
                dv[i].note = -1;
            for (i = 0; i < NMV; i++)
                mv[i].note = -1;
            for (i = 0; i < NSV; i++)
                sv[i].on = 0;
            bs.note = -1;
            drums_reset();
            break;
        }
        default: break;
        }
    }
}

void eng_init(void)
{
    uint32_t i;
    for (i = 0; i < NDV; i++)
        dv[i].note = -1;
    for (i = 0; i < NMV; i++)
        mv[i].note = -1;
    bs.note = -1;
    for (i = 0; i < NFDN; i++)
        fdn_len[i] = (float)FDN_LEN[i];
    echo_len = 0.5f * FS;
    PLAT_FAR(drums_reset)();             /* (RAM code: from the flash, through a pointer) */
    V.step = -1;
}

RAM void eng_render(int32_t *lr, uint32_t n, float gain)
{
    uint32_t off;
    events();
    for (off = 0; off < n; off += CH) {
        uint32_t i, m = n - off < CH ? n - off : CH;
        float mx_d = u7(P_MXDRONE), mx_m = u7(P_MXMEL), mx_b = u7(P_MXBASS), mx_s = u7(P_MXSHARD), mx_c = u7(P_MXCLOUD);
        float blur = u7(P_BLUR), sink = u7(P_SINK), swell = u7(P_SWELL), pump = u7(P_PUMP);
        float camt = clampf((u7(P_CAMT) * (0.6f + 0.8f * blur)) * (1.0f + mod[7]), 0.0f, 1.6f);
        float size = clampf(u7(P_CSIZE) + 0.3f * blur, 0.0f, 1.0f), decay = 1.5f + 18.0f * size * size;
        float shim = clampf(u7(P_SHIM) + 0.6f * mod[8], 0.0f, 1.0f), eamt = u7(P_EAMT);
        float efb = clampf(0.15f + 0.7f * u7(P_EFB) + 0.15f * blur, 0.0f, 0.93f);
        float dsend = camt * 0.9f, msend = camt * 0.6f, ssend = camt * 1.2f, drsend = u7(P_DMSPACE) * 0.6f;
        float sw, pg, trem, pan, fdrive = u7(P_FDRIVE);
        int ftype = F.p[P_FTYPE] % 4u;
        sq.sample += m;
        lfos_chunk();
        seq_chunk();
        drones_chunk();
        mel_chunk();
        bass_chunk();
        shards_chunk();
        for (i = 0; i < CH; i++)
            dbuf_l[i] = dbuf_r[i] = 0.0f;
        drums_render(dbuf_l, dbuf_r, CH, u7(P_DMLEVEL));
        /* FREEZE glides in and out */
        frz_t = F.p[P_FREEZE] ? 1.0f : 0.0f;
        frz += (frz_t - frz) * 0.02f;
        /* the drone filter, SWELL, tremolo, pan */
        {
            float cut = 30.0f * fm_powf(630.0f, u7(P_FCUT)) * fm_exp2f(mod[1] * 4.0f);
            float q = 0.5f + 18.0f * u7(P_FRES) * u7(P_FRES) * (1.0f + mod[2]);
            svf_set_at(&df_l, cut, q, 2.0f * INV_FS);   /* (the drones' filter runs at their half rate) */
            df_r.a1 = df_l.a1, df_r.a2 = df_l.a2, df_r.a3 = df_l.a3, df_r.k = df_l.k;
        }
        swell_ph = wrap(swell_ph + 0.045f * (float)CH * INV_FS);
        sw = 1.0f - swell * 0.45f * (0.5f + 0.5f * fm_sin_turns(swell_ph));
        trem = 1.0f - 0.5f * (0.5f + 0.5f * mod[5]) * (mod[5] != 0.0f);
        trem *= clampf(1.0f + mod[14], 0.0f, 2.0f);   /* DRONE LEVEL (pressure: louder as you press) */
        pan = clampf(mod[6], -1.0f, 1.0f);
        {                                /* the sidechain: fired, ducked (ATTACK in), let back (RELEASE) */
            uint32_t trig = F.p[P_SCTRIG] % NSCTRIG;
            int kick = drums_kicked(), snare = drums_snared(), fire;
            float rel = 0.03f * fm_powf(50.0f, u7(P_SCREL)), atk = u7(P_SCATK);
            fire = trig == 0u ? kick : trig == 4u ? snare : trig == 5u ? (kick || snare) : sc_step;
            sc_step = 0;
            if (fire && pump > 0.0f)
                sc_hit = pump;
            if (sc_hit > 0.0f) {
                if (atk < 0.01f)
                    pump_env = sc_hit;
                else
                    pump_env += (sc_hit - pump_env) * (1.0f - fm_expf(-(float)CH * INV_FS / (0.002f + 0.06f * atk * atk)));
                if (pump_env >= sc_hit * 0.97f)
                    sc_hit = 0.0f;       /* (in: now the release) */
            } else {
                pump_env *= fm_expf(-(float)CH * INV_FS / rel);
            }
        }
        pg = 1.0f - 0.8f * pump_env;
        for (i = 0; i < DH; i++) {       /* the drones at their half rate: drive, filter, pan; then up to the full */
            float l = hl[i] * sw * trem, r = hr[i] * sw * trem;
            if (fdrive > 0.01f) {
                l = fm_tanhf(l * (1.0f + 5.0f * fdrive)) / (1.0f + 2.0f * fdrive);
                r = fm_tanhf(r * (1.0f + 5.0f * fdrive)) / (1.0f + 2.0f * fdrive);
            }
            l = svf(&df_l, l, ftype);
            r = svf(&df_r, r, ftype);
            if (pan != 0.0f) {
                l *= 1.0f - 0.8f * (pan > 0.0f ? pan : 0.0f);
                r *= 1.0f + 0.8f * (pan < 0.0f ? pan : 0.0f);
            }
            l *= mx_d;
            r *= mx_d;
            dl[2u * i] = 0.5f * (hl_last + l);
            dr[2u * i] = 0.5f * (hr_last + r);
            dl[2u * i + 1u] = hl_last = l;
            dr[2u * i + 1u] = hr_last = r;
        }
        for (i = 0; i < CH; i++) {
            /* the sends: the cloud, the echo */
            cin_l[i] = dl[i] * dsend + ml[i] * mx_m * msend + shl[i] * mx_s * ssend + dbuf_l[i] * drsend + bsl[i] * mx_b * 0.05f;
            cin_r[i] = dr[i] * dsend + mr[i] * mx_m * msend + shr[i] * mx_s * ssend + dbuf_r[i] * drsend + bsl[i] * mx_b * 0.05f;
            ein_l[i] = ml[i] * mx_m * 0.5f + shl[i] * mx_s * 0.8f + dl[i] * 0.12f + dbuf_l[i] * drsend * 0.3f;
            ein_r[i] = mr[i] * mx_m * 0.5f + shr[i] * mx_s * 0.8f + dr[i] * 0.12f + dbuf_r[i] * drsend * 0.3f;
        }
        svf_flush(&df_l);
        svf_flush(&df_r);
        cloud_chunk(size, decay, u7(P_DAMP), shim);
        echo_chunk(eamt, efb);
        {                                /* the mix, PUMP, SINK, the master filter, the tape, width, out */
            float gl = gain, sk = 12000.0f * fm_exp2f(-6.6f * fm_powf(sink, 0.75f));
            float mcut = 200.0f * fm_powf(95.0f, clampf(u7(P_MFILT) + 0.3f * mod[13], 0.0f, 1.0f));
            float drive = u7(P_TDRIVE), wob = u7(P_TWOBBLE), width = 0.5f + u7(P_WIDTH);
            float gs, gn, k1, k2, wl = 0.0f;
            float ga = fm_tanf(3.14159265f * clampf(sk, 40.0f, 18000.0f) * INV_FS), ka = 1.0f / (1.0f + ga);
            float gm = fm_tanf(3.14159265f * clampf(mcut, 40.0f, 18000.0f) * INV_FS), km = gm / (1.0f + gm);
            k1 = ga * ka;
            k2 = km;
            gs = 1.0f + 5.0f * drive;
            gn = 0.9f / fm_tanhf(gs);
            tape_ph = wrap(tape_ph + 0.33f * (float)CH * INV_FS);
            tape_ph2 = wrap(tape_ph2 + 5.1f * (float)CH * INV_FS);
            static const uint8_t DUCKS[NSCWHAT] = {   /* what the sidechain ducks: drones, melody, bass, shards, cloud+echo */
                0x1Bu, 0x1Fu, 0x01u, 0x11u, 0x06u, 0x10u};
            uint32_t dk = DUCKS[F.p[P_SCWHAT] % NSCWHAT];
            float pg0 = sc_pg_last, dpg = (pg - sc_pg_last) / (float)m;   /* (the duck glides across the chunk) */
            sc_pg_last = pg;
            for (i = 0; i < m; i++) {
                float l, r, mid, side, pl, pr, g = pg0 + dpg * (float)(i + 1u);
                float gd = dk & 1u ? g : 1.0f, gm = dk & 2u ? g : 1.0f, gb = dk & 4u ? g : 1.0f;
                float gsh = dk & 8u ? g : 1.0f, gc = dk & 16u ? g : 1.0f;
                l = dl[i] * gd + ml[i] * mx_m * gm + bsl[i] * mx_b * gb + shl[i] * mx_s * gsh + (cwl[i] * mx_c * 1.4f + ewl[i]) * gc + dbuf_l[i];
                r = dr[i] * gd + mr[i] * mx_m * gm + bsl[i] * mx_b * gb + shr[i] * mx_s * gsh + (cwr[i] * mx_c * 1.4f + ewr[i]) * gc + dbuf_r[i];
                if (sink > 0.002f) {     /* SINK: everything under (one-pole x2, gliding in) */
                    sink_l1 += (l - sink_l1) * k1;
                    sink_l2 += (sink_l1 - sink_l2) * k1;
                    sink_r1 += (r - sink_r1) * k1;
                    sink_r2 += (sink_r1 - sink_r2) * k1;
                    l += (sink_l2 * (1.0f + 0.4f * sink) - l) * fm_minf(1.0f, sink * 20.0f);
                    r += (sink_r2 * (1.0f + 0.4f * sink) - r) * fm_minf(1.0f, sink * 20.0f);
                }
                if (mcut < 18000.0f) {   /* MASTER FILTER (two poles) */
                    mf_s[0] += (l - mf_s[0]) * k2;
                    mf_s[1] += (mf_s[0] - mf_s[1]) * k2;
                    mf_s[2] += (r - mf_s[2]) * k2;
                    mf_s[3] += (mf_s[2] - mf_s[3]) * k2;
                    l = mf_s[1];
                    r = mf_s[3];
                }
                if (wob > 0.01f) {       /* the tape's wow and flutter: a short line, its length wandering */
                    float d = 30.0f + wob * (60.0f * (1.0f + fm_sin_turns(tape_ph)) + 6.0f * fm_sin_turns(tape_ph2)), rp, fr;
                    int32_t ri;
                    tape_l[tape_w] = l;
                    tape_r[tape_w] = r;
                    rp = (float)tape_w - d;
                    if (rp < 0.0f)
                        rp += (float)TAPE_N;
                    ri = (int32_t)rp;
                    fr = rp - (float)ri;
                    l = tape_l[ri] + (tape_l[(uint32_t)(ri + 1) % TAPE_N] - tape_l[ri]) * fr;
                    r = tape_r[ri] + (tape_r[(uint32_t)(ri + 1) % TAPE_N] - tape_r[ri]) * fr;
                    tape_w = (tape_w + 1u) % TAPE_N;
                }
                if (drive > 0.01f) {
                    l = fm_tanhf(l * gs) * gn;
                    r = fm_tanhf(r * gs) * gn;
                }
                mid = (l + r) * 0.5f;
                side = (l - r) * 0.5f * width;
                l = mid + side;
                r = mid - side;
                dc_l += (l - dc_l) * 0.0005f;
                dc_r += (r - dc_r) * 0.0005f;
                l -= dc_l;
                r -= dc_r;
                {                        /* the limiter: everything at once turned down smoothly, never crunched */
                    float pk = fm_maxf(fm_fabsf(l), fm_fabsf(r)), g;
                    lim_env = pk > lim_env ? lim_env + (pk - lim_env) * 0.3f : lim_env * 0.99998f;   /* (slow back: it does not undo PUMP) */
                    g = lim_env > 0.5f ? 0.5f / lim_env : 1.0f;
                    l *= g;
                    r *= g;
                }
                gain_now += (gl - gain_now) * 0.004f;
                pl = sat(l * 1.1f);
                pr = sat(r * 1.1f);
                wl = fm_maxf(wl, fm_fabsf(pl));
                lr[2u * (off + i)] = (int32_t)(pl * gain_now * 8300000.0f);
                lr[2u * (off + i) + 1u] = (int32_t)(pr * gain_now * 8300000.0f);
            }
            meters[0] = fm_maxf(meters[0] * 0.97f, fm_fabsf(dl[0]) + fm_fabsf(dl[m / 2u]));
            meters[1] = fm_maxf(meters[1] * 0.97f, fm_fabsf(ml[0] * mx_m) + fm_fabsf(ml[m / 2u] * mx_m));
            meters[2] = fm_maxf(meters[2] * 0.97f, fm_fabsf(bsl[0] * mx_b) + fm_fabsf(bsl[m / 2u] * mx_b));
            meters[3] = fm_maxf(meters[3] * 0.97f, fm_fabsf(dbuf_l[0]) + fm_fabsf(dbuf_l[m / 2u]));
            meters[4] = fm_maxf(meters[4] * 0.97f, fm_fabsf(shl[0] * mx_s) + fm_fabsf(shl[m / 2u] * mx_s));
            meters[5] = fm_maxf(meters[5] * 0.97f, wl > 0.0f ? fm_fabsf(cwl[0]) * mx_c : 0.0f);
            for (i = 0; i < 6u; i++)
                V.level[i] = meters[i];
        }
        dc_l = fm_flush(dc_l);
        dc_r = fm_flush(dc_r);
        sink_l1 = fm_flush(sink_l1);
        sink_l2 = fm_flush(sink_l2);
        sink_r1 = fm_flush(sink_r1);
        sink_r2 = fm_flush(sink_r2);
    }
}
