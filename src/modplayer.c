/*
 * modplayer.c - 4-channel ProTracker MOD replayer + one effect channel,
 * modelled on PaxiMod v9.06 as linked into INVATAXI.EXE.
 *
 * Behaviour carried over from the original assembler:
 *  - pitch: step (8.8 fixed point, source bytes per output sample) is
 *    K / period with K = (1193182 / mixrate) * 768, i.e. the PIT clock based
 *    constant computed in PaxiMod's init (code at 0FFBh).
 *  - tick rate: mixrate / (bpm*6/15 + 1) samples per tick; default divisor 51.
 *  - effective channel volume = (note volume 0..64 * channel volume 0..255) >> 8,
 *    recomputed only when an instrument is set or a volume effect runs
 *    (exactly like the original, so music "ducking" takes effect on the next
 *    note/volume command).
 *  - mixing: each music voice contributes (sample * effvol * 2) >> 8. When the
 *    fifth (effect) voice is playing, the music sum and the effect sample are
 *    both halved before adding.
 *  - sample loops only if the repeat length is >= 4 words; voice end is
 *    length*2-3 like the original.
 *  - song end: loop back to order 0 (game passes loop mode 4).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "modplayer.h"
#include "platform.h"
#include "files.h"
#include "periods.h"

typedef struct {
    const int8_t *data;
    uint32_t datalen;      /* readable bytes at data */
    uint32_t pos, end;
    uint32_t loopstart, loopend;
    int      looping;
    uint8_t  frac;
    uint16_t step;
    int      effvol;
    int      active;
} voice_t;

typedef struct {
    int sample;            /* 1..31 */
    int period;            /* current period */
    int vol;               /* 0..64 */
    int finetune;
    int eff, param;
    int porta_target, porta_speed;
    int vib_speed, vib_depth, vib_pos;
    int trem_speed, trem_depth, trem_pos;
    int offset_mem;
    int loop_row, loop_count;
    int delay_note_period, delay_note_sample;
} chan_t;

typedef struct {
    char     name[23];
    uint32_t start;        /* offset into mod data */
    uint32_t len;          /* bytes */
    int      finetune;
    int      vol;
    uint32_t rep, replen;  /* bytes */
} sample_t;

static unsigned char *mod;
static size_t   modlen;
static sample_t smp[32];
static int      songlen, npat;
static const unsigned char *orders, *patterns;

static int      mixrate = 22000;     /* nominal (game's choice) */
static uint32_t K;
static int      audio_ok;
static int      playing;

static voice_t  vc[4], fxv;
static chan_t   ch[4];
static int      chanvol[4] = { 255, 255, 255, 255 };

static int      order, row, speed = 6, tick, tick_samples, tick_left;
static int      break_row, jump_flag, pattern_delay;

static int8_t  *fx_file;             /* loaded .SKP sample */

static int period_step(int period)
{
    uint32_t s;
    if (period <= 0) return 0;
    s = K / (uint32_t)period;
    return (int)(s & 0xffff);
}

static void set_effvol(int c)
{
    int v = ch[c].vol;
    if (v > 64) v = 64;
    if (v < 0) v = 0;
    vc[c].effvol = (v * chanvol[c]) >> 8;
}

static int find_note(int period)
{
    int i;
    for (i = 0; i < 36; i++) if (period >= pt_periods[0][i]) return i;
    return 35;
}

static void trigger(int c, int period, int offset)
{
    chan_t *h = &ch[c];
    voice_t *v = &vc[c];
    sample_t *s = &smp[h->sample];
    if (!h->sample || s->len < 4) { v->active = 0; return; }
    v->data = (const int8_t *)(mod + s->start);
    v->datalen = (uint32_t)((s->start + s->len <= modlen) ? s->len : (modlen > s->start ? modlen - s->start : 0));
    v->pos = (uint32_t)offset;
    v->frac = 0;
    v->end = s->len - 3;
    v->looping = s->replen >= 8;
    v->loopstart = s->rep;
    v->loopend = s->rep + s->replen - 3;
    v->step = (uint16_t)period_step(period);
    v->active = 1;
}

static void set_sample(int c, int sn)
{
    if (sn < 1 || sn > 31) return;
    ch[c].sample = sn;
    ch[c].vol = smp[sn].vol;
    ch[c].finetune = smp[sn].finetune;
    set_effvol(c);
}

static const unsigned char sine_tab[32] = {
    0,24,49,74,97,120,141,161,180,197,212,224,235,244,250,253,
    255,253,250,244,235,224,212,197,180,161,141,120,97,74,49,24
};

static void do_vibrato(int c)
{
    chan_t *h = &ch[c];
    int d = (sine_tab[h->vib_pos & 31] * h->vib_depth) >> 7;
    if (h->vib_pos & 32) d = -d;
    vc[c].step = (uint16_t)period_step(h->period + d);
    h->vib_pos = (h->vib_pos + h->vib_speed) & 63;
}

static void do_volslide(int c)
{
    chan_t *h = &ch[c];
    int up = h->param >> 4, dn = h->param & 15;
    if (up) h->vol += up; else h->vol -= dn;
    if (h->vol < 0) h->vol = 0;
    if (h->vol > 64) h->vol = 64;
    set_effvol(c);
}

static void do_toneporta(int c)
{
    chan_t *h = &ch[c];
    if (!h->porta_target) return;
    if (h->period < h->porta_target) {
        h->period += h->porta_speed;
        if (h->period > h->porta_target) h->period = h->porta_target;
    } else if (h->period > h->porta_target) {
        h->period -= h->porta_speed;
        if (h->period < h->porta_target) h->period = h->porta_target;
    }
    vc[c].step = (uint16_t)period_step(h->period);
}

static void process_row(void)
{
    int c;
    const unsigned char *rp;
    int pat;

    if (order >= songlen) order = 0;
    pat = orders[order];
    if (pat >= npat) pat = 0;
    rp = patterns + (size_t)pat * 1024 + (size_t)row * 16;

    for (c = 0; c < 4; c++) {
        const unsigned char *n = rp + c * 4;
        chan_t *h = &ch[c];
        int sn = (n[0] & 0xf0) | (n[2] >> 4);
        int per = ((n[0] & 0x0f) << 8) | n[1];
        int eff = n[2] & 0x0f, prm = n[3];

        h->eff = eff; h->param = prm;
        if (sn) set_sample(c, sn);

        if (per) {
            /* apply finetune via the period table */
            if (h->finetune) per = pt_periods[h->finetune & 15][find_note(per)];
            if (eff == 3 || eff == 5) {
                h->porta_target = per;
            } else if (eff == 0x0e && (prm >> 4) == 0x0d && (prm & 15)) {
                h->delay_note_period = per; h->delay_note_sample = h->sample;
            } else {
                int off = 0;
                h->period = per;
                h->vib_pos = 0; h->trem_pos = 0;
                if (eff == 9) {
                    if (prm) h->offset_mem = prm;
                    off = h->offset_mem << 8;
                }
                trigger(c, per, off);
            }
        }

        switch (eff) {
        case 0x3:
            if (prm) h->porta_speed = prm;
            break;
        case 0x4:
            if (prm & 0xf0) h->vib_speed = prm >> 4;
            if (prm & 0x0f) h->vib_depth = prm & 15;
            break;
        case 0x7:
            if (prm & 0xf0) h->trem_speed = prm >> 4;
            if (prm & 0x0f) h->trem_depth = prm & 15;
            break;
        case 0xB:
            order = prm - 1;          /* incremented again when the row ends */
            break_row = 0;
            jump_flag = 1;
            break;
        case 0xC:
            h->vol = prm > 64 ? 64 : prm;
            set_effvol(c);
            break;
        case 0xD: {
            int r = (prm >> 4) * 10 + (prm & 15);
            break_row = (r > 63) ? 0 : r;
            jump_flag = 1;
            break;
        }
        case 0xE: {
            int x = prm >> 4, y = prm & 15;
            switch (x) {
            case 0x1: h->period -= y; if (h->period < 113) h->period = 113; vc[c].step = (uint16_t)period_step(h->period); break;
            case 0x2: h->period += y; if (h->period > 856) h->period = 856; vc[c].step = (uint16_t)period_step(h->period); break;
            case 0x5: h->finetune = y; break;
            case 0x6:
                if (y == 0) h->loop_row = row;
                else {
                    if (h->loop_count == 0) h->loop_count = y;
                    else h->loop_count--;
                    if (h->loop_count) { break_row = h->loop_row; jump_flag = 2; }
                }
                break;
            case 0xA: h->vol += y; if (h->vol > 64) h->vol = 64; set_effvol(c); break;
            case 0xB: h->vol -= y; if (h->vol < 0) h->vol = 0; set_effvol(c); break;
            case 0xC: if (y == 0) { h->vol = 0; set_effvol(c); } break;
            case 0xE: pattern_delay = y; break;
            default: break;
            }
            break;
        }
        case 0xF:
            if (prm == 0) break;
            if (prm < 0x20) { speed = prm; tick = 0; }
            else tick_samples = mixrate / ((prm * 6) / 15 + 1);
            break;
        default: break;
        }
    }
}

static void advance_row(void)
{
    if (pattern_delay) { pattern_delay--; return; }
    if (jump_flag == 2) {                 /* E6x pattern loop: same pattern */
        row = break_row; jump_flag = 0; return;
    }
    row++;
    if (jump_flag || row >= 64) {
        row = jump_flag ? break_row : 0;
        jump_flag = 0;
        break_row = 0;
        order = (order + 1) & 0x7f;
        if (order >= songlen) order = 0;  /* loop mode 4 */
    }
}

static void process_tick_effects(void)
{
    int c;
    for (c = 0; c < 4; c++) {
        chan_t *h = &ch[c];
        int prm = h->param;
        switch (h->eff) {
        case 0x0:
            if (prm) {
                int note = find_note(h->period), p = h->period;
                int t = tick % 3;
                if (t == 1) p = pt_periods[h->finetune & 15][note + (prm >> 4) > 35 ? 35 : note + (prm >> 4)];
                else if (t == 2) p = pt_periods[h->finetune & 15][note + (prm & 15) > 35 ? 35 : note + (prm & 15)];
                vc[c].step = (uint16_t)period_step(p);
            }
            break;
        case 0x1:
            h->period -= prm; if (h->period < 113) h->period = 113;
            vc[c].step = (uint16_t)period_step(h->period);
            break;
        case 0x2:
            h->period += prm; if (h->period > 856) h->period = 856;
            vc[c].step = (uint16_t)period_step(h->period);
            break;
        case 0x3: do_toneporta(c); break;
        case 0x4: do_vibrato(c); break;
        case 0x5: do_toneporta(c); do_volslide(c); break;
        case 0x6: do_vibrato(c); do_volslide(c); break;
        case 0x7: {
            int d = (sine_tab[h->trem_pos & 31] * h->trem_depth) >> 6;
            int v = h->vol + ((h->trem_pos & 32) ? -d : d);
            if (v < 0) v = 0;
            if (v > 64) v = 64;
            vc[c].effvol = (v * chanvol[c]) >> 8;
            h->trem_pos = (h->trem_pos + h->trem_speed) & 63;
            break;
        }
        case 0xA: do_volslide(c); break;
        case 0xE: {
            int x = prm >> 4, y = prm & 15;
            if (x == 0x9 && y && tick % y == 0) trigger(c, h->period, 0);
            if (x == 0xC && tick == y) { h->vol = 0; set_effvol(c); }
            if (x == 0xD && tick == y && h->delay_note_period) {
                h->period = h->delay_note_period;
                trigger(c, h->period, 0);
                h->delay_note_period = 0;
            }
            break;
        }
        default: break;
        }
    }
}

static void player_tick(void)
{
    if (tick == 0) process_row();
    else process_tick_effects();
    if (++tick >= speed) {
        tick = 0;
        advance_row();
    }
}

static int voice_sample(voice_t *v)
{
    uint32_t acc;
    if (!v->active) return 0;
    if (v->end < v->pos) {
        if (v->looping) { v->pos = v->loopstart; v->end = v->loopend; }
        else { v->active = 0; return 0; }
    }
    acc = (uint32_t)v->frac + (v->step & 0xff);
    v->frac = (uint8_t)acc;
    v->pos += (uint32_t)(v->step >> 8) + (acc >> 8);
    return (v->pos < v->datalen) ? v->data[v->pos] : 0;
}

static int floor_half(int x) { return (x >= 0) ? x / 2 : -((-x + 1) / 2); }

static void audio_cb(int16_t *out, int frames, void *user)
{
    int i, c;
    (void)user;
    for (i = 0; i < frames; i++) {
        int L = 0, R = 0;
        if (playing && mod) {
            if (tick_left <= 0) { player_tick(); tick_left += tick_samples > 0 ? tick_samples : 1; }
            tick_left--;
            for (c = 0; c < 4; c++) {
                int s = voice_sample(&vc[c]);
                int contrib = (s * vc[c].effvol * 2) >> 8;
                if (c == 0 || c == 3) L += contrib; else R += contrib;
            }
        }
        if (fxv.active) {
            int s = voice_sample(&fxv);
            if (fxv.active || s) {
                L = floor_half(L) + floor_half(s);
                R = floor_half(R) + floor_half(s);
            }
        }
        {
            int m = (L + R) * 128;   /* mono (Sound Blaster 1.x/2.0 is mono) */
            if (m > 32767) m = 32767;
            if (m < -32768) m = -32768;
            out[i] = (int16_t)m;
        }
    }
}

int pm_open_audio(int rate)
{
    int div, actual;
    mixrate = rate;
    div = 1193182 / rate;
    K = (uint32_t)div * 768u;
    actual = 1193182 / div;
    tick_samples = mixrate / 51;
    tick_left = 0;
    audio_ok = plat_audio_open(actual, audio_cb, NULL) != 0;
    return audio_ok;
}

int pm_load(const char *path)
{
    size_t len;
    unsigned char *d = game_load(path, &len);
    int i, maxp = 0;
    uint32_t off;
    if (!d) return 1;
    if (len < 1084 || !(memcmp(d + 1080, "M.K.", 4) == 0 || memcmp(d + 1080, "M!K!", 4) == 0 ||
                        memcmp(d + 1080, "FLT4", 4) == 0 || memcmp(d + 1080, "4CHN", 4) == 0)) {
        free(d);
        return 2;
    }
    plat_audio_lock();
    free(mod);
    mod = d; modlen = len;
    songlen = d[950];
    if (songlen < 1 || songlen > 128) songlen = 1;
    orders = d + 952;
    for (i = 0; i < 128; i++) if (orders[i] > maxp) maxp = orders[i];
    npat = maxp + 1;
    patterns = d + 1084;
    off = 1084 + (uint32_t)npat * 1024;
    memset(smp, 0, sizeof smp);
    for (i = 1; i <= 31; i++) {
        const unsigned char *h = d + 20 + (i - 1) * 30;
        sample_t *s = &smp[i];
        memcpy(s->name, h, 22); s->name[22] = 0;
        s->len = (uint32_t)((h[22] << 8) | h[23]) * 2;
        s->finetune = h[24] & 15;
        s->vol = h[25] > 64 ? 64 : h[25];
        s->rep = (uint32_t)((h[26] << 8) | h[27]) * 2;
        s->replen = (uint32_t)((h[28] << 8) | h[29]) * 2;
        s->start = off;
        off += s->len;
        if (s->start + s->len > modlen) s->len = (s->start < modlen) ? (uint32_t)(modlen - s->start) : 0;
    }
    memset(vc, 0, sizeof vc);
    memset(ch, 0, sizeof ch);
    order = 0; row = 0; tick = 0; speed = 6;
    tick_samples = mixrate / 51; tick_left = 0;
    break_row = 0; jump_flag = 0; pattern_delay = 0;
    playing = 1;
    plat_audio_unlock();
    return 0;
}

void pm_set_volumes(int a, int b, int c, int d)
{
    plat_audio_lock();
    chanvol[0] = a & 255; chanvol[1] = b & 255; chanvol[2] = c & 255; chanvol[3] = d & 255;
    plat_audio_unlock();
}

void pm_jump(int ord, int r)
{
    plat_audio_lock();
    order = ord & 0x7f;
    row = (r & 0x3f);
    plat_audio_unlock();
}

void pm_play_instrument(int period, int ins)
{
    sample_t *s;
    if (!mod || ins < 1 || ins > 31) return;
    s = &smp[ins];
    plat_audio_lock();
    memset(&fxv, 0, sizeof fxv);
    fxv.data = (const int8_t *)(mod + s->start);
    fxv.datalen = s->len;
    fxv.end = (uint16_t)(s->len - 3);
    fxv.step = (uint16_t)period_step(period);
    fxv.active = s->len > 0;
    plat_audio_unlock();
}

int pm_play_file(const char *path, int period)
{
    FILE *f = game_fopen(path, "rb");
    int8_t *buf;
    size_t n;
    if (!f) return 1;
    buf = (int8_t *)malloc(65535);
    if (!buf) { fclose(f); return 2; }
    n = fread(buf, 1, 65535, f);
    fclose(f);
    plat_audio_lock();
    free(fx_file);
    fx_file = buf;
    memset(&fxv, 0, sizeof fxv);
    fxv.data = buf;
    fxv.datalen = (uint32_t)n;
    fxv.end = (uint32_t)n;
    fxv.step = (uint16_t)period_step(period);
    fxv.active = 1;
    plat_audio_unlock();
    return 0;
}

void pm_stop(void)
{
    plat_audio_lock();
    playing = 0;
    fxv.active = 0;
    memset(vc, 0, sizeof vc);
    plat_audio_unlock();
}
