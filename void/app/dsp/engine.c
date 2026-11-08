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
static void svf_set(svf_t *f, float hz, float q)
{
    float g;
    hz = clampf(hz, 16.0f, 19000.0f);
    g = fm_tanf(3.14159265f * hz * INV_FS);
    f->k = 1.0f / (q < 0.3f ? 0.3f : q);
    f->a1 = 1.0f / (1.0f + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}
/* mode 0 low, 1 band, 2 high, 3 notch */
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

/* ------------------------------------------------------------------- modulation --- */
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

static void drone_on(int note, float vel)
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
static void drone_off(int note)
{
    uint32_t i;
    for (i = 0; i < NDV; i++)
        if (dv[i].note == note)
            dv[i].gate = 0;
}

/* a chunk of every drone voice into dl, dr */
static void drones_chunk(void)
{
    uint32_t i, j, k;
    float choir = u7(P_CHOIR), part = u7(P_PARTIALS), breath = u7(P_BREATH), timbre = u7(P_TIMBRE);
    float tone = clampf(u7(P_TONE) + 0.5f * mod[3], 0.0f, 1.0f), det = 3.0f + 30.0f * u7(P_DETUNE);
    float att = 0.03f * fm_powf(300.0f, u7(P_ATTACK)), rel = 0.1f * fm_powf(150.0f, u7(P_RELEASE));
    float ka = 1.0f - fm_expf(-(float)CH * INV_FS * 3.0f / att), kr = 1.0f - fm_expf(-(float)CH * INV_FS * 3.0f / rel);
    float vib = mod[4] * 60.0f, motion = u7(P_DMOTION), pmix = clampf(part * (1.0f + mod[12]), 0.0f, 1.5f);
    float pa[NPART], ps = 0.0f, stretch = timbre * timbre * 0.045f;
    for (k = 0; k < NPART; k++) {        /* TIMBRE: dark (the fundamental) .. bright and bell-like */
        pa[k] = fm_powf(1.0f / (float)(k + 1u), 2.4f - 2.1f * timbre);
        ps += pa[k];
    }
    for (k = 0; k < NPART; k++)
        pa[k] *= 0.55f / ps;
    for (i = 0; i < CH; i++)
        dl[i] = dr[i] = 0.0f;
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
        de = (e1 - e0) * (1.0f / (float)CH);
        f = mtof((float)v->note + vib * 0.01f);
        v->f = f;
        for (k = 0; k < NPART; k++) {    /* each partial's step this chunk, its wander */
            float fk = f * (float)(k + 1u) * (1.0f + stretch * (float)(k * k)), w;
            if (fk > 18000.0f) {
                cr[k] = 1.0f;
                ci[k] = 0.0f;
                continue;
            }
            w = 6.2831853f * fk * INV_FS;
            cr[k] = fm_cosf(w);
            ci[k] = fm_sinf(w);
            v->pdph[k] += v->pdrift[k] * (float)CH * INV_FS;
            v->pdph[k] -= v->pdph[k] >= 1.0f ? 1.0f : 0.0f;
        }
        dt[0] = f * fm_exp2f(det * (1.0f / 1200.0f)) * INV_FS;
        dt[1] = f * fm_exp2f(-det * (1.0f / 1200.0f)) * INV_FS;
        dt[2] = f * 0.5f * INV_FS;
        svf_set(&v->tone, f * (1.0f + 39.0f * tone * tone), 0.7f);
        svf_set(&v->br, f * 2.0f, 24.0f);
        g = 0.16f;
        {
            float amp[NPART];
            for (k = 0; k < NPART; k++)
                amp[k] = pa[k] * (1.0f - motion * 0.6f * (0.5f + 0.5f * fm_sin_turns(v->pdph[k]))) * pmix;
            for (i = 0; i < CH; i++) {
                float s = 0.0f, c = 0.0f, x;
                e0 += de;
                if (choir > 0.001f) {
                    for (k = 0; k < 3u; k++) {
                        v->sph[k] += dt[k];
                        v->sph[k] -= v->sph[k] >= 1.0f ? 1.0f : 0.0f;
                        c += saw(v->sph[k], dt[k]);
                    }
                    s += svf(&v->tone, c * 0.33f, 0) * choir;
                }
                if (pmix > 0.001f)
                    for (k = 0; k < NPART; k++) {
                        float re = v->pre[k] * cr[k] - v->pim[k] * ci[k];
                        v->pim[k] = v->pre[k] * ci[k] + v->pim[k] * cr[k];
                        v->pre[k] = re;
                        s += v->pim[k] * amp[k];
                    }
                if (breath > 0.001f)
                    s += svf(&v->br, rndf(), 1) * breath * 1.6f;
                x = s * e0 * e0 * g;
                dl[i] += x * v->pl;
                dr[i] += x * v->pr;
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
    uint8_t osc;                         /* 0 sine, 1 triangle, 2 saw, 3 square, 4 FM, 5 supersaw, 6 organ, 7 clav */
    float fmr, fmi, fmi_end, fmd;        /* FM: ratio, index from .. to, over seconds */
    float r2, l2, d2;                    /* a second partial: ratio, level, fall (bells, toys) */
    float cut, fenv, fdec, q;            /* filter: cutoff (times the note), its envelope (times), fall, Q; cut 0: none */
    float a, d, s, r;                    /* the level: attack, decay, sustain (0..1), release */
    float vib, trem, breath, drop, gain;
} msound_t;
static const msound_t MS[NMSOUND] = {
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
};
#define NMV 2u
typedef struct {
    int16_t note;
    uint8_t gate;
    float t, f, fglide, glide_k, env, fenv, i2;
    float ph[4], mph, p2, vph;
    svf_t flt;
    const msound_t *s;
    float vel;
} mvoice_t;
static mvoice_t mv[NMV];
static uint32_t mv_next;
static float mel_last_f;
static float ml[CH], mr[CH];

static void mel_on(int note, float vel)
{
    mvoice_t *v = &mv[mv_next++ % NMV];
    const msound_t *s = &MS[F.p[P_MSOUND] % NMSOUND];
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
static void mel_off_all(void)
{
    uint32_t i;
    for (i = 0; i < NMV; i++)
        mv[i].gate = 0;
}

static void mel_chunk(void)
{
    uint32_t i, j, k;
    float tone = clampf(u7(P_MTONE) + 0.5f * mod[11], 0.0f, 1.0f), tk = 0.3f + 1.7f * tone;
    for (i = 0; i < CH; i++)
        ml[i] = mr[i] = 0.0f;
    for (j = 0; j < NMV; j++) {
        mvoice_t *v = &mv[j];
        const msound_t *s = v->s;
        float e0 = v->env, e1, de, f, dt, ct = (float)CH * INV_FS, vib, fi = 0.0f, l2;
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

static void bass_on(int note, float len)
{
    float gl = u7(P_BGLIDE);
    bs.f = mtof((float)note);
    if (bs.note < 0 || gl < 0.01f)
        bs.fg = bs.f;
    bs.note = (int16_t)note;
    bs.t = 0.0f;
    bs.len = len;
}
static void bass_chunk(void)
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
    de = (e1 - bs.env) * (1.0f / (float)CH);
    dt = bs.fg * INV_FS;
    {
        float tone = clampf(u7(P_BTONE) + 0.5f * mod[10], 0.0f, 1.0f);
        svf_set(&bs.flt, 120.0f + 3600.0f * tone * tone * (1.0f + 1.5f * fm_expf(-bs.t * 12.0f)), 1.2f);
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
        default:
            bs.mph = wrap(bs.mph + dt * 2.0f);
            o = fm_sin_turns(wrap(bs.ph[0] + 0.35f * fm_expf(-bs.t * 6.0f) * fm_sin_turns(bs.mph) + 4.0f));
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
static void shard_on(int note, float vel)
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
static void shards_chunk(void)
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

static void cloud_chunk(float size, float decay_s, float damp, float shim)
{
    uint32_t i, k;
    float g[NFDN], lp = 0.08f + 0.85f * (1.0f - damp), fz = frz, in_g = 1.0f - fz;
    float tgt[NFDN];
    for (k = 0; k < NFDN; k++) {
        tgt[k] = (float)FDN_LEN[k] * (0.35f + 0.65f * size);
        g[k] = fz > 0.5f ? 1.0f : fm_powf(10.0f, -3.0f * tgt[k] / (decay_s * FS));
        g[k] += (1.0f - g[k]) * fz;
    }
    if (fz > 0.5f)
        lp = 1.0f;
    fdn_mod_ph = wrap(fdn_mod_ph + 0.4f * (float)CH * INV_FS);
    for (i = 0; i < CH; i++) {
        float x = (cin_l[i] + cin_r[i]) * 0.5f * in_g, y[NFDN], sum = 0.0f, a;
        /* the shimmer: an octave up off the cloud's last output, fed back in */
        if (shim > 0.001f) {
            float o = 0.0f, W = 2048.0f;
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
            y[k] = fdn_read(k, fdn_len[k] + (k < 2u ? 6.0f * fm_sin_turns(wrap(fdn_mod_ph + 0.25f * (float)k)) : 0.0f));
            sum += y[k];
        }
        sum *= 2.0f / (float)NFDN;
        for (k = 0; k < NFDN; k++) {     /* Householder: each minus the share of all, damped, fed back */
            a = (y[k] - sum) * g[k];
            fdn_lp[k] += (a - fdn_lp[k]) * lp;
            fdn_buf[k][fdn_w] = (int16_t)(clampf(fdn_lp[k] + (k & 1u ? x : -x) * 0.35f, -7.9f, 7.9f) * FDN_Q);
        }
        fdn_w = (fdn_w + 1u) % FDN_MAX;
        cwl[i] = (y[0] + y[2] + y[4] + y[6]) * 0.35f;
        cwr[i] = (y[1] + y[3] + y[5] + y[7]) * 0.35f;
        sh_out = (cwl[i] + cwr[i]) * 0.5f;
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
static void echo_chunk(float amt, float fb)
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
            echo_l[echo_w] = (int16_t)(nl * 16384.0f);
            echo_r[echo_w] = (int16_t)(nr * 16384.0f);
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
static void lfos_chunk(void)
{
    uint32_t l, k;
    float ct = (float)CH * INV_FS;
    for (k = 0; k < NLTGT; k++)
        mod[k] = 0.0f;
    for (l = 0; l < 3u; l++) {
        int base = P_L1SHAPE + 4 * (int)l, sh = F.p[base] % NLSHAPE, tg = F.p[base + 3] % NLTGT;
        float hz = lfo_hz(F.p[base + 1], bpm_now), p, v, depth = ((float)F.p[base + 2] - 64.0f) * (1.0f / 63.0f);
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

static float swing_off(int32_t s, float sps) { return (s & 1) ? u7(P_SWING) * 0.33f * sps : 0.0f; }

static void seq_step(int32_t abs_step)
{
    uint32_t s = (uint32_t)abs_step % NSTEP, t;
    float h = u7(P_DMHUMAN), spark = u7(P_SPARKLE);
    uint32_t bar = (uint32_t)abs_step / NSTEP;
    int fill = F.fill && s >= 8u;
    V.step = (int8_t)s;
    V.bar = bar;
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
        int32_t root = (int32_t)(F.p[P_ROOT] % 12u), note = 60 + root + scale_deg(F.p[P_SCALE], F.pat.mel[s]) +
                                                         12 * ((int32_t)F.p[P_MOCT] - 1) - (root > 5 ? 12 : 0);
        float vel = 1.0f - u7(P_MHUMAN) * 0.4f * rnd01();
        uint32_t gap = 1;
        while (gap < NSTEP && F.pat.mel[(s + gap) % NSTEP] == MEL_REST)
            gap++;
        mel_off_all();
        mel_on(note, vel);
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

static void seq_chunk(void)
{
    float bpm = F.p[P_CLOCK] && sq.ext_period > 1.0f ? FS * 60.0f / (sq.ext_period * 24.0f) : (float)(F.p[P_TEMPO] + 40u);
    float sps = FS * 60.0f / (bpm * 4.0f);   /* samples a sixteenth */
    bpm_now = bpm;
    V.bpm = bpm;
    if (!F.playing) {
        sq.running = 0;
        V.step = -1;
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
static float pump_env, swell_ph, sink_l1, sink_l2, sink_r1, sink_r2, mf_s[4], gain_now, dc_l, dc_r, meters[6];
static svf_t df_l, df_r;                 /* the drone filter */
static float dbuf_l[CH], dbuf_r[CH];
static float frz_t;

static void events(void)
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
        case EV_MEL: if (e.b > 0.0f) { mel_off_all(); mel_on(e.a, e.b); mel_gate_left = 0.35f; } break;
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
    drums_reset();
    V.step = -1;
}

void eng_render(int32_t *lr, uint32_t n, float gain)
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
            svf_set(&df_l, cut, q);
            df_r.a1 = df_l.a1, df_r.a2 = df_l.a2, df_r.a3 = df_l.a3, df_r.k = df_l.k;
        }
        swell_ph = wrap(swell_ph + 0.045f * (float)CH * INV_FS);
        sw = 1.0f - swell * 0.45f * (0.5f + 0.5f * fm_sin_turns(swell_ph));
        trem = 1.0f - 0.5f * (0.5f + 0.5f * mod[5]) * (mod[5] != 0.0f);
        pan = clampf(mod[6], -1.0f, 1.0f);
        pump_env *= fm_expf(-(float)CH * INV_FS * 8.0f);
        if (drums_kicked())
            pump_env = pump;
        pg = 1.0f - 0.8f * pump_env;
        for (i = 0; i < CH; i++) {
            float l = dl[i] * sw * trem, r = dr[i] * sw * trem;
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
            dl[i] = l * mx_d;
            dr[i] = r * mx_d;
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
            float gs, k1, k2, wl = 0.0f;
            float ga = fm_tanf(3.14159265f * clampf(sk, 40.0f, 18000.0f) * INV_FS), ka = 1.0f / (1.0f + ga);
            float gm = fm_tanf(3.14159265f * clampf(mcut, 40.0f, 18000.0f) * INV_FS), km = gm / (1.0f + gm);
            k1 = ga * ka;
            k2 = km;
            gs = 1.0f + 5.0f * drive;
            tape_ph = wrap(tape_ph + 0.33f * (float)CH * INV_FS);
            tape_ph2 = wrap(tape_ph2 + 5.1f * (float)CH * INV_FS);
            for (i = 0; i < m; i++) {
                float l, r, mid, side, pl, pr;
                l = dl[i] * pg + ml[i] * mx_m * pg + bsl[i] * mx_b + shl[i] * mx_s * pg + (cwl[i] * mx_c * 1.4f + ewl[i]) * pg + dbuf_l[i];
                r = dr[i] * pg + mr[i] * mx_m * pg + bsl[i] * mx_b + shr[i] * mx_s * pg + (cwr[i] * mx_c * 1.4f + ewr[i]) * pg + dbuf_r[i];
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
                    l = fm_tanhf(l * gs) / fm_tanhf(gs) * 0.9f;
                    r = fm_tanhf(r * gs) / fm_tanhf(gs) * 0.9f;
                }
                mid = (l + r) * 0.5f;
                side = (l - r) * 0.5f * width;
                l = mid + side;
                r = mid - side;
                dc_l += (l - dc_l) * 0.0005f;
                dc_r += (r - dc_r) * 0.0005f;
                l -= dc_l;
                r -= dc_r;
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
