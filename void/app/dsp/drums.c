/* SPDX-License-Identifier: MPL-2.0
 * VOID's drums (drums.h): each voice a tone (a sine or a square, its pitch falling), a noise (or six squares: the 808's
 * metal) through a state-variable filter, each with its own fall; then the kit's colour on the bus (LOFI's grit and
 * dullness, 808's warmth, GLITCH's steps), the level, the cloud's share. In the audio interrupt: floats only */
#include "drums.h"
#include "engine.h"
#define BR_FS FS
#define BR_INV_FS INV_FS
static inline float br_randf(uint32_t *s){uint32_t x=*s;x^=x<<13;x^=x>>17;x^=x<<5;*s=x;return (float)(int32_t)x*(1.0f/2147483648.0f);}
static int kicked, snared;
#define DM_NV 6u

typedef struct {
    uint8_t on, sq, metal, clap, flt;    /* sounding; the tone a square; the noise the 808's metal; CLAP's bursts; the
                                          * filter: 0 low, 1 band, 2 high */
    float vel;                           /* struck at, times the voice's level */
    float amp, ad;                       /* the tone: its level, its fall a sample */
    float namp, nad;                     /* the noise: the same */
    float ph, flo, fhi, pe, ped;         /* the tone's phase, its pitch from fhi down to flo (pe 1 .. 0, falling by ped) */
    float a1, a2, a3, k, s1, s2;         /* the filter (TPT state variable) */
    float pl, pr;                        /* its place */
    uint32_t t;                          /* samples since it was struck */
    float mph[6], minc[6];               /* the metal's six squares */
} dvoice_t;

#define DM_BUF 512u
static dvoice_t dv[DM_NV];
static uint32_t drng = 0x2545F491u;
static float bus_l[DM_BUF], bus_r[DM_BUF];
static float lp_l, lp_r, peak;

/* -60 dB over sec seconds, a sample's factor */
RAM static float fall(float sec)
{
    if (sec < 0.002f)
        sec = 0.002f;
    return fm_expf(-6.9f * BR_INV_FS / sec);
}

RAM static void filter(dvoice_t *d, int type, float hz, float q)
{
    float g, lim = 0.45f * BR_FS;
    hz = hz < 20.0f ? 20.0f : hz > lim ? lim : hz;
    g = fm_tanf(3.14159265f * hz * BR_INV_FS);
    d->flt = (uint8_t)type;
    d->k = 1.0f / (q < 0.3f ? 0.3f : q);
    d->a1 = 1.0f / (1.0f + g * (g + d->k));
    d->a2 = g * d->a1;
    d->a3 = g * d->a2;
    d->s1 = d->s2 = 0.0f;
}

static inline float svf(dvoice_t *d, float x)
{
    float v3 = x - d->s2, v1 = d->a1 * d->s1 + d->a2 * v3, v2 = d->s2 + d->a2 * d->s1 + d->a3 * v3;
    d->s1 = 2.0f * v1 - d->s1;
    d->s2 = 2.0f * v2 - d->s2;
    return d->flt == 0 ? v2 : d->flt == 1 ? v1 : x - d->k * v1 - v2;
}

RAM static float rnd_between(float a, float b) { return a + (b - a) * (0.5f + 0.5f * br_randf(&drng)); }

RAM void drums_reset(void)
{
    uint32_t i;
    for (i = 0; i < DM_NV; i++)
        dv[i].on = 0;
    lp_l = lp_r = peak = 0.0f;
}

/* the kits after the first four: each voice's settings from a table */
typedef struct {
    float k_hi, k_lo, k_sweep, k_dec, k_click;                          /* kick: pitch from .. to, its sweep, fall, click */
    float s_hi, s_lo, s_tamp, s_tdec, s_namp, s_ndec, s_ft, s_hz, s_q;  /* snare: tone, noise, the noise's filter */
    float c_ndec, c_hz, c_q;                                            /* clap */
    float h_cdec, h_odec, h_amp, h_metal, h_ft, h_hz, h_q;              /* hats: closed, open, metal (808-like) or noise */
    float p_f, p_sq, p_amp, p_dec, p_ft, p_hz, p_q;                     /* perc */
} kitdef_t;
static const kitdef_t KITS[NKIT - 4u] = {
    /* TRAP: a long 808 boom, a crisp snare, fast metal hats, a rim */
    {260.0f, 40.0f, 0.10f, 1.6f, 0.15f, 260.0f, 200.0f, 0.25f, 0.08f, 0.45f, 0.13f, 2, 1500.0f, 0.7f, 0.18f, 1300.0f, 2.2f,
     0.035f, 0.30f, 0.22f, 1, 1, 10000.0f, 1.0f, 1900.0f, 1, 0.2f, 0.04f, 1, 2400.0f, 3.0f},
    /* BRUSH: soft, dark, a brushed snare, a gentle tom */
    {110.0f, 48.0f, 0.08f, 0.35f, 0.03f, 220.0f, 190.0f, 0.08f, 0.06f, 0.35f, 0.35f, 0, 3500.0f, 0.5f, 0.30f, 900.0f, 1.0f,
     0.08f, 0.45f, 0.12f, 0, 0, 6000.0f, 0.6f, 160.0f, 0, 0.35f, 0.25f, 0, 900.0f, 0.7f},
    /* TECHNO: a punchy kick, a big clap, bright metal hats, a ride */
    {200.0f, 48.0f, 0.06f, 0.45f, 0.25f, 300.0f, 210.0f, 0.15f, 0.08f, 0.50f, 0.15f, 1, 2200.0f, 1.2f, 0.25f, 1500.0f, 2.5f,
     0.04f, 0.50f, 0.25f, 1, 2, 8000.0f, 0.7f, 3200.0f, 1, 0.15f, 0.18f, 1, 6000.0f, 2.0f},
    /* TABLA: the bayan's low bend, the dayan's ring, a shaker, a wood click */
    {75.0f, 105.0f, 0.25f, 0.6f, 0.02f, 520.0f, 500.0f, 0.45f, 0.35f, 0.08f, 0.02f, 1, 1000.0f, 1.0f, 0.09f, 6000.0f, 1.5f,
     0.05f, 0.20f, 0.12f, 0, 1, 7000.0f, 2.0f, 1100.0f, 0, 0.35f, 0.06f, 1, 1500.0f, 2.0f}};
RAM static void kit_hit(dvoice_t *d, uint32_t v, const kitdef_t *K, float tn, float dk)
{
    uint32_t j;
    switch (v) {
    case DM_KICK:
        d->fhi = K->k_hi * tn;
        d->flo = K->k_lo * tn;
        d->pe = 1.0f;
        d->ped = fm_expf(-BR_INV_FS / (K->k_sweep * 0.3f));
        d->amp = 0.95f;
        d->ad = fall(K->k_dec * dk);
        d->namp = K->k_click;
        d->nad = fall(0.012f);
        filter(d, 2, 3000.0f, 0.7f);
        break;
    case DM_SNARE:
        d->fhi = K->s_hi * tn;
        d->flo = K->s_lo * tn;
        d->pe = 1.0f;
        d->ped = fm_expf(-BR_INV_FS / 0.03f);
        d->amp = K->s_tamp;
        d->ad = fall(K->s_tdec * dk);
        d->namp = K->s_namp;
        d->nad = fall(K->s_ndec * dk);
        filter(d, (int)K->s_ft, K->s_hz * tn, K->s_q);
        break;
    case DM_CLAP:
        d->clap = 1;
        d->namp = 0.55f;
        d->nad = fall(K->c_ndec * dk);
        filter(d, 1, K->c_hz * tn, K->c_q);
        d->pl = 1.0f;
        d->pr = 0.85f;
        break;
    case DM_HAT:
    case DM_OHAT:
        if (v == DM_HAT)
            dv[DM_OHAT].nad = fall(0.02f);
        d->namp = K->h_amp * (v == DM_HAT ? 1.0f : 0.75f);
        d->nad = fall((v == DM_HAT ? K->h_cdec : K->h_odec) * dk);
        if (K->h_metal > 0.5f) {
            static const float RM[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
            d->metal = 1;
            for (j = 0; j < 6u; j++) {
                d->mph[j] = 0.0f;
                d->minc[j] = RM[j] * 1.6f * tn * BR_INV_FS;
            }
        }
        filter(d, (int)K->h_ft, K->h_hz * tn, K->h_q);
        d->pl = 0.8f;
        d->pr = 1.0f;
        break;
    default:                             /* DM_PERC */
        d->fhi = d->flo = K->p_f * tn;
        d->sq = K->p_sq > 0.5f;
        d->amp = K->p_amp;
        d->ad = fall(K->p_dec * dk);
        d->namp = 0.05f;
        d->nad = fall(0.006f);
        filter(d, (int)K->p_ft, K->p_hz * tn, K->p_q);
        d->pl = 1.0f;
        d->pr = 0.7f;
        break;
    }
}

RAM void drums_hit(uint32_t v, float vel)
{
    dvoice_t *d;
    int kit;
    float tn, dk;
    uint32_t j;
    if (v >= DM_NV)
        return;
    d = &dv[v];
    kit = (int)(F.p[P_DMKIT] % NKIT);
    tn = fm_exp2f(((float)F.pat.dtune[v] - 12.0f) * (1.0f / 12.0f));
    dk = fm_exp2f(((float)F.pat.ddecay[v] - 64.0f) * (1.0f / 32.0f));
    if (v == DM_KICK)
        kicked = 1;
    if (v == DM_SNARE || v == DM_CLAP)
        snared = 1;
    d->on = 1;
    d->sq = d->metal = d->clap = 0;
    {
        float vv = (float)F.pat.dvol[v] * (1.0f / 127.0f);
        d->vel = vel * vv * vv;
    }
    d->t = 0;
    d->ph = 0.0f;
    d->amp = d->namp = 0.0f;
    d->ad = d->nad = 0.9f;
    d->pe = 0.0f;
    d->ped = 0.99f;
    d->pl = d->pr = 1.0f;
    if (kit >= 4) {                      /* the newer kits: from their table */
        kit_hit(d, v, &KITS[kit - 4], tn, dk);
        return;
    }
    switch (v) {
    case DM_KICK: {
        float hi = kit == 3 ? 320.0f : kit == 2 ? 120.0f : 150.0f, lo = kit == 2 ? 46.0f : kit == 3 ? 34.0f : 44.0f;
        float sweep = kit == 3 ? 0.05f : kit == 2 ? 0.12f : 0.16f, dec = (kit == 2 ? 1.3f : kit == 3 ? 0.22f : 0.55f) * dk;
        d->fhi = hi * tn;
        d->flo = lo * tn;
        d->pe = 1.0f;
        d->ped = fm_expf(-BR_INV_FS / (sweep * 0.3f));
        d->amp = 0.95f;
        d->ad = fall(dec);
        d->namp = kit ? 0.22f : 0.1f;   /* a click at its front */
        d->nad = fall(0.012f);
        filter(d, 2, 3000.0f, 0.7f);
        break;
    }
    case DM_SNARE:
        d->fhi = (kit == 2 ? 240.0f : 230.0f) * tn;
        d->flo = (kit == 2 ? 180.0f : 200.0f) * tn;
        d->pe = 1.0f;
        d->ped = fm_expf(-BR_INV_FS / 0.03f);
        d->amp = kit == 2 ? 0.35f : 0.18f;
        d->ad = fall(0.12f * dk);
        d->namp = kit == 2 ? 0.4f : 0.5f;
        d->nad = fall((kit == 3 ? 0.07f : kit == 2 ? 0.22f : 0.18f) * dk);
        if (kit == 2)
            filter(d, 2, 1800.0f * tn, 0.7f);
        else
            filter(d, 1, (kit == 3 ? rnd_between(900.0f, 5000.0f) : 1700.0f) * tn, 1.4f);
        break;
    case DM_CLAP:
        d->clap = 1;
        d->namp = 0.55f;
        d->nad = fall(0.22f * dk);
        filter(d, 1, 1200.0f * tn, 2.0f);
        d->pl = 1.0f;
        d->pr = 0.85f;
        break;
    case DM_HAT:
    case DM_OHAT: {
        float dec = v == DM_HAT ? (kit == 3 ? 0.02f : kit == 2 ? 0.06f : 0.05f) : (kit == 2 ? 0.4f : 0.35f);
        if (v == DM_HAT)                 /* a closed hat chokes the open one */
            dv[DM_OHAT].nad = fall(0.02f);
        d->namp = (v == DM_HAT ? 0.2f : 0.15f) * (kit == 3 ? 1.6f : 1.0f);
        d->nad = fall(dec * dk);
        if (kit == 2) {                  /* the 808's metal: six squares, inharmonic */
            static const float R[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
            d->metal = 1;
            for (j = 0; j < 6u; j++) {
                d->mph[j] = 0.0f;
                d->minc[j] = R[j] * 1.6f * tn * BR_INV_FS;
            }
            filter(d, 1, 10000.0f, 1.0f);
        } else if (kit == 3) {
            filter(d, 1, rnd_between(4000.0f, 11000.0f) * tn, 8.0f);
        } else {
            filter(d, 2, (kit == 1 ? 5200.0f : v == DM_HAT ? 7500.0f : 6500.0f) * tn, 0.7f);
        }
        d->pl = 0.8f;
        d->pr = 1.0f;
        break;
    }
    case DM_PERC:                        /* a woodblock; the 808's rim; GLITCH's, its pitch anywhere */
        d->fhi = d->flo = (kit == 3 ? rnd_between(400.0f, 2000.0f) : kit == 2 ? 1700.0f : 800.0f) * tn;
        d->sq = kit == 2;
        d->amp = kit == 2 ? 0.2f : 0.3f;
        d->ad = fall((kit == 2 ? 0.04f : 0.09f) * dk);
        d->namp = 0.05f;
        d->nad = fall(0.006f);
        filter(d, kit == 2 ? 1 : 2, kit == 2 ? 2200.0f * tn : 2000.0f, kit == 2 ? 3.0f : 0.7f);
        d->pl = 1.0f;
        d->pr = 0.7f;
        break;
    }
}

/* CLAP's front: three short bursts some 11 ms apart, then its tail (namp's fall) */
static inline float clap_env(dvoice_t *d)
{
    float ms = (float)d->t * BR_INV_FS * 1000.0f;
    if (ms < 34.0f) {
        float in = ms - 11.3f * fm_floorf(ms * (1.0f / 11.3f));
        return 0.6f * fm_expf(-in * (1.0f / 2.5f));
    }
    return d->namp;
}

RAM void drums_render(float *l, float *r, uint32_t n, float level)
{
    uint32_t v, i, j, any = 0;
    float lv = level * level * 1.2f;
    if (n > DM_BUF)
        n = DM_BUF;
    for (v = 0; v < DM_NV; v++)
        any |= dv[v].on;
    if (!any)
        return;
    for (i = 0; i < n; i++)
        bus_l[i] = bus_r[i] = 0.0f;
    for (v = 0; v < DM_NV; v++) {
        dvoice_t *d = &dv[v];
        if (!d->on)
            continue;
        for (i = 0; i < n; i++) {
            float s = 0.0f;
            if (d->amp > 1e-5f) {
                float f = d->flo + (d->fhi - d->flo) * d->pe, o;
                d->pe *= d->ped;
                d->ph += f * BR_INV_FS;
                d->ph -= (float)(int32_t)d->ph;
                o = d->sq ? (d->ph < 0.5f ? 0.5f : -0.5f) : fm_sin_turns(d->ph);
                s += o * d->amp;
                d->amp *= d->ad;
            }
            if (d->namp > 1e-5f) {
                float x, e;
                if (d->metal) {
                    x = 0.0f;
                    for (j = 0; j < 6u; j++) {
                        d->mph[j] += d->minc[j];
                        d->mph[j] -= d->mph[j] >= 1.0f ? 1.0f : 0.0f;
                        x += d->mph[j] < 0.5f ? 0.15f : -0.15f;
                    }
                } else {
                    x = br_randf(&drng);
                }
                e = d->clap ? clap_env(d) : d->namp;
                s += svf(d, x) * e;
                d->namp *= d->nad;
            }
            s *= d->vel;
            bus_l[i] += s * d->pl;
            bus_r[i] += s * d->pr;
            d->t++;
        }
        if (d->amp <= 1e-5f && d->namp <= 1e-5f)
            d->on = 0;
        d->s1 = fm_flush(d->s1);
        d->s2 = fm_flush(d->s2);
    }
    {                                    /* the kit's colour on the bus */
        int kit = F.p[P_DMKIT] % 4u;
        float kl = kit == 1 ? 0.45f : 0.75f;   /* LOFI's low-pass some 4 kHz, GLITCH's some 9 kHz */
        for (i = 0; i < n; i++) {
            float a = bus_l[i], b = bus_r[i];
            if (kit == 1) {
                a = (float)(int32_t)(fm_tanhf(a * 1.6f) * 12.0f + (a >= 0.0f ? 0.5f : -0.5f)) * (1.0f / 12.0f);
                b = (float)(int32_t)(fm_tanhf(b * 1.6f) * 12.0f + (b >= 0.0f ? 0.5f : -0.5f)) * (1.0f / 12.0f);
            } else if (kit == 3) {
                a = (float)(int32_t)(a * 6.0f + (a >= 0.0f ? 0.5f : -0.5f)) * (1.0f / 6.0f);
                b = (float)(int32_t)(b * 6.0f + (b >= 0.0f ? 0.5f : -0.5f)) * (1.0f / 6.0f);
            } else if (kit == 2) {
                a = fm_tanhf(a * 1.3f);
                b = fm_tanhf(b * 1.3f);
            }
            if (kit == 1 || kit == 3) {
                lp_l += kl * (a - lp_l);
                lp_r += kl * (b - lp_r);
                a = lp_l;
                b = lp_r;
            }
            a *= lv;
            b *= lv;
            l[i] += a;
            r[i] += b;
            a = fm_fabsf(a);
            peak = a > peak ? a : peak;
        }
        lp_l = fm_flush(lp_l);
        lp_r = fm_flush(lp_r);
    }
}

RAM int drums_snared(void)               /* a snare or a clap since the last call (the sidechain) */
{
    int k = snared;
    snared = 0;
    return k;
}
RAM int drums_kicked(void)
{
    int k = kicked;
    kicked = 0;
    return k;
}
