/*
 * pklite.c - minimal PKLITE 1.1x ("large" + "extra" compression) unpacker.
 *
 * All three executables shipped with INVATAXI (INVATAXI.EXE, HJUVA_#1.EXE,
 * HJUVA_#2.EXE) were packed with PKLITE 1.12 using the -e (extra) option.
 * The game data we need (the 320x200 ColoRIX pictures) lives inside the
 * packed load modules, so we decompress them at runtime instead of shipping
 * extracted copyrighted pictures.
 *
 * The bitstream format follows the description in Jason Summers' Deark
 * (MIT licensed), modules/pklite.c.
 */
#include <stdlib.h>
#include <string.h>
#include "pklite.h"

typedef struct {
    const unsigned char *in;
    size_t inlen, ipos;
    unsigned bitbuf;
    int nbits;
    int err;
} bitreader;

static int rd_byte(bitreader *br)
{
    if (br->ipos >= br->inlen) { br->err = 1; return 0; }
    return br->in[br->ipos++];
}

static void refill(bitreader *br)
{
    int lo = rd_byte(br);
    int hi = rd_byte(br);
    br->bitbuf = (unsigned)lo | ((unsigned)hi << 8);
    br->nbits = 16;
}

static int getbit(bitreader *br)
{
    int v = br->bitbuf & 1;
    br->bitbuf >>= 1;
    br->nbits--;
    if (br->nbits == 0) refill(br);
    return v;
}

/* high nibble = code length, low 12 bits = code (MSB first) */
static const unsigned short ml_large[24] = {
    0x2003,0x3000,0x4002,0x4003,0x4004,0x500a,0x500b,0x500c,
    0x601a,0x601b,0x703a,0x703b,0x703c,0x807a,0x807b,0x807c,
    0x90fa,0x90fb,0x90fc,0x90fd,0x90fe,0x90ff,0x601c,0x2002
};
static const unsigned short offs_tab[32] = {
    0x1001,0x4000,0x4001,0x5004,0x5005,0x5006,0x5007,0x6010,
    0x6011,0x6012,0x6013,0x6014,0x6015,0x6016,0x702e,0x702f,
    0x7030,0x7031,0x7032,0x7033,0x7034,0x7035,0x7036,0x7037,
    0x7038,0x7039,0x703a,0x703b,0x703c,0x703d,0x703e,0x703f
};

static int read_code(bitreader *br, const unsigned short *tab, int n)
{
    unsigned code = 0;
    int len, i;
    for (len = 1; len <= 12; len++) {
        code = (code << 1) | (unsigned)getbit(br);
        for (i = 0; i < n; i++) {
            if ((tab[i] >> 12) == len && (tab[i] & 0x0fff) == code) return i;
        }
        if (br->err) return -1;
    }
    br->err = 1;
    return -1;
}

unsigned char *pklite_unpack(const unsigned char *exe, size_t exelen, size_t *outlen)
{
    bitreader br;
    unsigned char *out;
    size_t cap = 0x20000, olen = 0;
    size_t datapos = 752; /* compressed data start for v1.12 large/extra */

    if (exelen < 0x60 || exe[0] != 'M' || exe[1] != 'Z') return NULL;
    if (memcmp(exe + 0x1e, "PKLITE", 6) != 0 && memcmp(exe + 0x1e, "PKlite", 6) != 0)
        return NULL;
    /* version word at 0x1c: low byte minor, high nibble flags. 0x30 = large|extra */
    if ((exe[0x1d] & 0x30) != 0x30) return NULL;

    memset(&br, 0, sizeof br);
    br.in = exe; br.inlen = exelen; br.ipos = datapos;
    refill(&br);

    out = (unsigned char *)malloc(cap);
    if (!out) return NULL;

    for (;;) {
        int len_raw;
        unsigned matchlen, hi = 0, pos;
        int have_hi = 0;

        if (br.err) goto fail;
        if (getbit(&br) == 0) {
            int b = rd_byte(&br) ^ br.nbits;   /* "extra" compression */
            if (olen >= cap) { cap *= 2; out = (unsigned char *)realloc(out, cap); if (!out) return NULL; }
            out[olen++] = (unsigned char)b;
            continue;
        }
        len_raw = read_code(&br, ml_large, 24);
        if (len_raw < 0) goto fail;
        if (len_raw < 22) {
            matchlen = (unsigned)len_raw + 3;
        } else if (len_raw == 23) {
            matchlen = 2; have_hi = 1;
        } else { /* 22: long match */
            int b = rd_byte(&br);
            if (b == 0xfe) continue;          /* segment separator */
            if (b == 0xff) break;             /* end of data */
            if (b == 0xfd) goto fail;         /* uncompressed area: unsupported */
            matchlen = (unsigned)b + 25;
        }
        if (!have_hi) {
            int h = read_code(&br, offs_tab, 32);
            if (h < 0) goto fail;
            hi = (unsigned)h;
        }
        pos = (hi << 8) | (unsigned)rd_byte(&br);
        if (pos == 0 || pos > olen) goto fail;
        while (olen + matchlen >= cap) { cap *= 2; out = (unsigned char *)realloc(out, cap); if (!out) return NULL; }
        while (matchlen--) { out[olen] = out[olen - pos]; olen++; }
    }
    *outlen = olen;
    return out;
fail:
    free(out);
    return NULL;
}
