/* SPDX-License-Identifier: GPL-2.0-or-later */
/* digineighbor: a NEIGHBOR machine for the Digitakt mk1 (OS 1.53) SRC page.
 *
 * A track whose machine is NEIGHBOR (4) plays another track's audio instead
 * of a sample: its own chain (overdrive, level/envelope, bit reduction,
 * filter, volume, pan, sends) processes the source track's signal, as the
 * Octatrack's neighbor machine does. The SRC page is SLICE's, trimmed
 * (glue.s): TUNE, BR, SLOT (SLICE's parameter: the source track, 1-8, 0
 * none), GAIN (LEN's) and LEV.
 *
 * The render (read from an emulator trace of the stock OS) keeps one mono
 * block per track, 32 x 32-bit samples, at
 * 0x80001a18 + 128 t, and runs each stage over all eight tracks in place:
 * playback, overdrive, level/envelope, filter, then the mixer (volume, pan,
 * sends). So:
 *   nb_tap (before the mixer): amplify each NEIGHBOR track's finished
 *     block by its GAIN, then copy each source track's finished block
 *     (post-filter, pre-volume);
 *   nb_inject (after playback, next block): put it into each NEIGHBOR
 *     track's block, pitch-shifted by TUNE and the trig's note and scaled by
 *     LEV and the trig's velocity; that track's chain then runs on it.
 * One block (32 samples, 0.67 ms) of latency; a chain of neighbours adds one
 * block per hop, and the pitch shifter 3 to 27 ms more. Both
 * run in the render (interrupt level 5): short, no firmware calls.
 */
#ifdef OS154                       /* the Digitakt mk1 1.54 (mod.json's port) */
#include "os154.h"
#else                              /* the Digitakt mk1 1.53 */
#include "os153.h"
#endif

typedef unsigned char u8;
typedef short s16;
typedef int s32;
typedef unsigned int u32;

#define NB_MACHINE  4
#define TRACKS      8
#define BLOCK       32
#define TBUF(t)     ((s32 *)(OS_TBUF + 128 * (t)))            /* a track's block */
#define MACH(t)     (*(volatile const u8 *)(OS_MACH + (t)))    /* its machine */
/* The render's voice parameters, 106 bytes a track at 0x80002794, 8.8. */
#define VP(t, o)    (*(volatile const s16 *)(OS_VP + 106 * (t) + (o)))
#define P_TUNE      0       /* semitones, 0x4000 = 0 */
#define P_SLOT      8       /* SLICE: the source track */
#define P_GAIN      10      /* LEN, 0-63: GAIN, 0.5 dB a step */
#define P_LEV       14      /* 0-127 */
#define NOTE(t)     (*(volatile const s32 *)(OS_NOTE + 4 * (t)))   /* the trig's note, 16.16, 60 = none */
#define VEL(t)      (*(volatile const s16 *)(OS_VEL + 2 * (t)))   /* its velocity, 8.8 */
/* The firmware's pitch table (0x40075184 reads it): 2^((i - 10752)/2048) in
 * Q29, i = 0-14848, indexed by the pitch / 384. */
#define PITCH_TAB   ((const u32 *)OS_PITCH_TAB)
#define UNITY       0x20000000u

static s32 nb_saved[TRACKS][BLOCK];     /* each source's last finished block */
u32 nb_lay[11];                         /* NEIGHBOR's SRC page layout (glue.s) */
u32 nb_lay_ok;
u32 nb_page_m;                          /* the machine whose layout the UI asked for last (glue.s) */
char nb_txt[12];                        /* GAIN's pop-up text (glue.s) */
static s32 nb_gq[TRACKS];               /* each track's last GAIN, Q16, less 1 (so 0 at rest) */

/* glue.s: 32 longs from src to dst. The copies are not C loops: for one, gcc
 * (-O2, ColdFire) emitted move.l (a0)+,(0,a0,d0.l), which relies on the
 * destination using a0 before its increment; the emulator used it after,
 * and wrote every sample one word late. Which one the MCF54418 does is not
 * known, so new C here is checked for the form:
 *   objdump -d X.o | grep -E '%a([0-7])@[+],[^ ]*%a\1@' */
extern void nb_copy32(s32 *dst, const s32 *src);
volatile u32 nb_blocks;                 /* blocks a NEIGHBOR track was fed (diagnostics) */
volatile u32 nb_shifted;                /* of them, pitch-shifted */

/* The pitch shifter: a delay line read by two taps half a window apart,
 * whose delays sweep at (1 - ratio) samples a sample and wrap round the
 * window; each is faded in and out over the window (smoothstep of a
 * triangle, 0 where it wraps), and the two fades sum to 1. 16-bit samples
 * (the source's top half).
 *
 * A plain two-tap shifter splices with a phase jump at every wrap, which
 * moves a tone off pitch by up to half the splice rate (+26 cents at +12
 * here) and loses level. So when a tap wraps, silent, its delay is also
 * moved by up to +-LAG samples to where its waveform best lines up with the
 * other tap's (a correlation, as WSOLA splices): it then fades in in phase. */
#define WIN         1024                /* samples: 21 ms */
#define SPAN        (WIN << 16)         /* the taps' phase, Q16 samples */
#define HALF        (SPAN >> 1)
#define LAG         128                 /* the splice search, +-samples */
#define CORN        16                  /* its correlation: points */
#define CORS        4                   /*   their spacing */
#define DMIN        (LAG + 1)           /* the shortest nominal delay */
#define RING        1352                /* >= DMIN + WIN + LAG + 1 + CORN CORS */
static s16 nb_ring[TRACKS][RING];
static struct {
    s32 w;                              /* the ring's next write */
    u32 p;                              /* the first tap's phase, 0..SPAN */
    s32 o1, o2;                         /* the taps' splice offsets, samples */
    s32 g;                              /* the last block's gain, Q14 */
    u8 on;                              /* NEIGHBOR last block */
} nb_st[TRACKS];

/* The source track of NEIGHBOR track b, or -1: SLOT 1-8, not b itself. */
static s32 nb_src(s32 b)
{
    u32 v = (u8)(VP(b, P_SLOT) >> 8);
    if (v < 1 || v > TRACKS || (s32)v - 1 == b)
        return -1;
    return (s32)v - 1;
}

/* The pitch ratio, Q29, as the firmware takes a sample's (0x40075772):
 * TUNE plus the trig's note, 3 semitones up, clamped to 0-87, into the
 * table. TUNE 0 and note 60 give exactly UNITY. */
static u32 nb_ratio(s32 b)
{
    s32 p = ((s32)VP(b, P_TUNE) - 0x4000) * 256 + NOTE(b) + (3 << 16);
    if (p < 0)
        p = 0;
    else if (p > (87 << 16))
        p = 87 << 16;
    return PITCH_TAB[p / 384];
}

/* The gain, Q14: a sample's level law, (LEV/127)^2 (VEL/127) (0x40074c60),
 * scaled so that LEV 100 and velocity 100, the defaults, give exactly 1.
 * Up to 2.05 at 127/127. */
static s32 nb_gain(s32 b)
{
    s32 l = VP(b, P_LEV), v = VEL(b);
    if (l < 0) l = 0; else if (l > (127 << 8)) l = 127 << 8;
    if (v < 0) v = 0; else if (v > (127 << 8)) v = 127 << 8;
    l = l * l / 40000;                  /* (l / 25600)^2, Q14 */
    v = v * 16 / 25;                    /* v / 25600, Q14 */
    return (l * v) >> 14;
}

/* GAIN, Q16: 0.5 dB a unit of LEN (8.8), 0 to +31.5 dB (x 37.6). 2^(dB /
 * 6.0206) from the pitch table (2048 steps an octave) and a shift. */
static s32 nb_gain_out(s32 b)
{
    s32 v = VP(b, P_GAIN), k;
    if (v <= 0)
        return 0x10000;
    if (v > (63 << 8))
        v = 63 << 8;
    k = (v * 43541) >> 16;              /* 2048ths of an octave: v / 512 / 6.0206 x 2048 */
    return (s32)(PITCH_TAB[10752 + (k & 2047)] >> 13) << (k >> 11);
}

/* d[i] *= g, g ramping over the block from g0 to g1 (Q16, up to 2^23),
 * saturated. |d| times g in 32 bits: the high half times g, the low half
 * times g's two parts; past thr (2^47 / the larger g) it saturates. */
static void nb_amp(s32 *d, s32 g0, s32 g1)
{
    s32 i, gm = g0 > g1 ? g0 : g1, dg = (g1 - g0) / BLOCK;
    u32 thr = gm <= 0x10000 ? 0xffffffffu : (0xffffffffu / (u32)gm) << 15;
    for (i = 0; i < BLOCK; i++) {
        s32 v = d[i], g = i == BLOCK - 1 ? g1 : g0 + dg * (i + 1);
        u32 a = v < 0 ? 0u - (u32)v : (u32)v, lo = a & 0xffff, y;
        if (a > thr)
            y = 0x80000000u;
        else
            y = (a >> 16) * (u32)g + ((lo * (u32)(g >> 8)) >> 8) + ((lo * (u32)(g & 255)) >> 16);
        if (v < 0)
            d[i] = y >= 0x80000000u ? (s32)0x80000000 : -(s32)y;
        else
            d[i] = y > 0x7fffffffu ? 0x7fffffff : (s32)y;
    }
}

/* GAIN's text: "+12.5", and "dB" after it when db (glue.s: the knob's value
 * and the pop-up). v is LEN's value, 8.8. */
char *nb_fmt_gain(char *b, s32 v, s32 db)
{
    s32 t = (v < 0 ? 0 : v) * 5 >> 8, n = t / 10;   /* tenths of a dB */
    char *p = b;
    *p++ = '+';
    if (n >= 10)
        *p++ = (char)('0' + n / 10);
    *p++ = (char)('0' + n % 10);
    *p++ = '.';
    *p++ = (char)('0' + t % 10);
    if (db) {
        *p++ = 'd';
        *p++ = 'B';
    }
    *p = 0;
    return b;
}

/* d[i] = x[i] g, g ramping over the block from g0 to g1 (Q14), saturated. */
static void nb_scale(s32 *d, const s32 *x, s32 g0, s32 g1)
{
    s32 i, ga = g0 << 5, dg = g1 - g0;
    for (i = 0; i < BLOCK; i++) {
        s32 v = x[i], g, hi;
        u32 lo;
        ga += dg;
        g = ga >> 5;
        hi = (v >> 16) * g;             /* |.| < 2^15 * 2^15.1 */
        lo = ((u32)v & 0xffff) * (u32)g >> 14;
        if (hi > 0x1ffe0000)
            v = 0x7fffffff;
        else if (hi < -0x1ffe0000)
            v = (s32)0x80000000;
        else
            v = hi * 4 + (s32)lo;
        d[i] = v;
    }
}

/* One tap: the ring at delay DMIN + o + q (q in Q16 samples) before w,
 * interpolated, times its fade, Q15 x Q15. */
static inline __attribute__((always_inline)) s32 nb_tap1(const s16 *r, s32 w, u32 q, s32 o)
{
    s32 j = w - DMIN - o - (s32)(q >> 16), k, s0, s1, f, u, g;
    u32 m = q < HALF ? q : SPAN - q;    /* 0..HALF */
    if (j < 0)
        j += RING;
    k = j ? j - 1 : RING - 1;
    s0 = r[j];
    s1 = r[k];
    f = (s32)(q & 0xffff) >> 2;         /* Q14 */
    s0 += ((s1 - s0) * f) >> 14;
    u = (s32)(m >> 10);                 /* the triangle, Q15, 0..32768 */
    g = (((u * u) >> 15) * (49152 - u)) >> 14;     /* u^2 (3 - 2u), Q15 */
    return s0 * g;
}

/* The correlation of ref (CORN points) with the ring's CORN points, CORS
 * apart, going back from index b (> -RING). */
static s32 nb_corr(const s16 *r, const s32 *ref, s32 b)
{
    s32 n, c = 0;
    if (b < 0)
        b += RING;
    if (b >= (CORN - 1) * CORS) {       /* no wrap: the usual case */
        const s16 *q = r + b;
        for (n = 0; n < CORN; n++, q -= CORS)
            c += (ref[n] * *q) >> 4;    /* 16 x 2^30 >> 4: no overflow */
    } else {
        for (n = 0; n < CORN; n++) {
            c += (ref[n] * r[b]) >> 4;
            b -= CORS;
            if (b < 0)
                b += RING;
        }
    }
    return c;
}

/* The splice offset, -LAG..LAG, for a tap starting at nominal delay d (whole
 * samples, from w) to line up with the other tap at delay dref: the best
 * correlation over every 8th offset, then every 2nd round it, then every
 * one. About 40 correlations. */
static s32 nb_align(const s16 *r, s32 w, s32 dref, s32 d)
{
    s32 ref[CORN];
    s32 n, o, c, step, lo, hi, a = w - dref, b = w - d, bo = 0, bc = (s32)0x80000000;
    if (a < 0)
        a += RING;
    for (n = 0; n < CORN; n++) {
        ref[n] = r[a];
        a -= CORS;
        if (a < 0)
            a += RING;
    }
    for (o = -LAG; o <= LAG; o += 8) {
        c = nb_corr(r, ref, b - o);
        if (c > bc) { bc = c; bo = o; }
    }
    for (step = 2; step > 0; step--) {
        lo = bo - 3 * step;             /* +-6 by 2, then +-1 by 1 */
        hi = bo + 3 * step;
        if (step == 1) { lo = bo - 1; hi = bo + 1; }
        if (lo < -LAG) lo = -LAG;
        if (hi > LAG) hi = LAG;
        for (o = lo, n = bo; o <= hi; o += step) {
            if (o == n)
                continue;
            c = nb_corr(r, ref, b - o);
            if (c > bc) { bc = c; bo = o; }
        }
    }
    return bo;
}

/* d = x pitch-shifted by ratio (Q29), through track b's ring. */
static void nb_shift(s32 *d, const s32 *x, s32 b, u32 ratio)
{
    s16 *r = nb_ring[b];
    s32 w = nb_st[b].w, o1 = nb_st[b].o1, o2 = nb_st[b].o2, i;
    u32 p = nb_st[b].p;
    u32 inc = 0x10000u - (ratio >> 13);         /* 1 - ratio, Q16, mod SPAN */
    for (i = 0; i < BLOCK; i++) {
        u32 q2 = (p + HALF) & (SPAN - 1), np, nq2;
        r[w] = (s16)(x[i] >> 16);
        d[i] = (nb_tap1(r, w, p, o1) + nb_tap1(r, w, q2, o2)) << 1;
        np = (p + inc) & (SPAN - 1);
        nq2 = (np + HALF) & (SPAN - 1);
        /* a phase that jumps by more than half the window has wrapped */
        if ((s32)(np - p) > HALF || (s32)(np - p) < -HALF)
            o1 = nb_align(r, w, DMIN + o2 + (s32)(nq2 >> 16), DMIN + (s32)(np >> 16));
        if ((s32)(nq2 - q2) > HALF || (s32)(nq2 - q2) < -HALF)
            o2 = nb_align(r, w, DMIN + o1 + (s32)(np >> 16), DMIN + (s32)(nq2 >> 16));
        p = np;
        if (++w == RING)
            w = 0;
    }
    nb_st[b].w = w;
    nb_st[b].p = p;
    nb_st[b].o1 = o1;
    nb_st[b].o2 = o2;
}

/* Only into the ring (the shifter keeps its history while unshifted). */
static void nb_record(const s32 *x, s32 b)
{
    s16 *r = nb_ring[b];
    s32 w = nb_st[b].w, i;
    for (i = 0; i < BLOCK; i++) {
        r[w] = (s16)(x[i] >> 16);
        if (++w == RING)
            w = 0;
    }
    nb_st[b].w = w;
}

/* Before the mixer: each NEIGHBOR track's GAIN, on its finished block (after
 * its own chain, before its volume); then the sources' blocks are saved. */
void nb_tap(void)
{
    u32 need = 0;
    s32 b, t, g;
    for (b = 0; b < TRACKS; b++) {
        if (MACH(b) != NB_MACHINE) {
            nb_gq[b] = 0;
            continue;
        }
        g = nb_gain_out(b);
        if (g != 0x10000 || nb_gq[b] != 0)
            nb_amp(TBUF(b), nb_gq[b] + 0x10000, g);
        nb_gq[b] = g - 0x10000;
        if ((t = nb_src(b)) >= 0)
            need |= 1u << t;
    }
    for (t = 0; t < TRACKS; t++) {
        const s32 *s;
        if (!(need & (1u << t)))
            continue;
        s = TBUF(t);
        nb_copy32(nb_saved[t], s);
    }
}

static const s32 nb_zero[BLOCK];

void nb_inject(void)
{
    s32 b, t, i, g;
    u32 ratio;
    for (b = 0; b < TRACKS; b++) {
        s32 *d;
        const s32 *x;
        if (MACH(b) != NB_MACHINE) {
            nb_st[b].on = 0;
            continue;
        }
        d = TBUF(b);
        g = nb_gain(b);
        if (!nb_st[b].on) {             /* newly NEIGHBOR: a clean shifter */
            s16 *r = nb_ring[b];
            for (i = 0; i < RING; i++)
                r[i] = 0;
            nb_st[b].w = 0;
            nb_st[b].p = HALF;
            nb_st[b].o1 = nb_st[b].o2 = 0;
            nb_st[b].g = g;
            nb_st[b].on = 1;
        }
        t = nb_src(b);
        x = t < 0 ? nb_zero : nb_saved[t];
        ratio = nb_ratio(b);
        if (ratio == UNITY) {
            nb_record(x, b);
            nb_copy32(d, x);
        } else {
            nb_shift(d, x, b, ratio);
            nb_shifted++;
        }
        if (g != 0x4000 || nb_st[b].g != 0x4000)
            nb_scale(d, d, nb_st[b].g, g);
        nb_st[b].g = g;
        if (t >= 0)
            nb_blocks++;
    }
}
