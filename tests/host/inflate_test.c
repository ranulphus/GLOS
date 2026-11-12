/* inflate_test: the loader's unpacker (loader/inflate.c) against streams zlib made (tests/host/inflate_cases.py
 * writes NAME.raw and NAME.z pairs): each unpacked whole, 2048 bytes of the stream at a time as the loader reads
 * its file, and equal to what was packed; then damaged and cut short, which must end with an error and never
 * with a wrong answer taken for right, a hang or a write outside the window. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../loader/inflate.h"

static unsigned char *zin, *out, win[INFL_WINDOW + 64];
static size_t zn, zat, outn, outmax, step;

static unsigned fill(const unsigned char **p, void *ctx)
{
    size_t n = zn - zat < step ? zn - zat : step;
    (void)ctx;
    *p = zin + zat;
    zat += n;
    return (unsigned)n;
}

static int flush(unsigned n, void *ctx)
{
    (void)ctx;
    if (outn + n > outmax)
        return 1;
    memcpy(out + outn, win, n);
    outn += n;
    return 0;
}

static unsigned char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    if (!f) {
        printf("inflate_test: no %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    *n = (size_t)ftell(f);
    rewind(f);
    b = malloc(*n + 1);
    if (fread(b, 1, *n, f) != *n)
        exit(2);
    fclose(f);
    return b;
}

static int run(void)
{
    int r;
    zat = outn = 0;
    memset(win + INFL_WINDOW, 0xA5, 64);
    r = inflate_stream(fill, flush, 0, win);
    for (size_t i = 0; i < 64; i++)
        if (win[INFL_WINDOW + i] != 0xA5) {
            printf("inflate_test: wrote past the window\n");
            exit(1);
        }
    return r;
}

int main(int argc, char **argv)
{
    int fails = 0, cases = 0, hurt = 0, caught = 0;
    unsigned seed = 12345;
    for (int a = 1; a < argc; a++) {
        char path[512];
        size_t rawn;
        unsigned char *raw;
        snprintf(path, sizeof path, "%s.raw", argv[a]);
        raw = slurp(path, &rawn);
        snprintf(path, sizeof path, "%s.z", argv[a]);
        zin = slurp(path, &zn);
        outmax = rawn + 70000;
        out = malloc(outmax);
        for (step = 2048; step; step = step == 2048 ? 1 : step == 1 ? 7 : 0) {     /* (as the loader; then a byte, 7) */
            int r = run();
            cases++;
            if (r != 0 || outn != rawn || memcmp(out, raw, rawn)) {
                printf("FAIL %s step=%zu r=%d out=%zu want=%zu\n", argv[a], step, r, outn, rawn);
                fails++;
            }
        }
        step = 2048;
        for (int k = 0; k < 200 && zn > 8; k++) {       /* a bit flipped, or the stream cut short */
            size_t keep = zn;
            unsigned char was = 0;
            size_t at = 0;
            int r;
            seed = seed * 1103515245u + 12345u;
            if (k & 1) {
                zn = (seed >> 8) % (zn - 1);
            } else {
                at = (seed >> 8) % zn;
                was = zin[at];
                zin[at] ^= (unsigned char)(1u << ((seed >> 4) & 7));
            }
            r = run();
            hurt++;
            if (r != 0 || outn != rawn || memcmp(out, raw, rawn))
                caught++;                       /* (an error, or another answer: the loader's checksum's to catch) */
            else if (k & 1) {
                printf("FAIL %s cut to %zu of %zu and still whole\n", argv[a], zn, keep);
                fails++;
            }
            zn = keep;
            if (!(k & 1))
                zin[at] = was;
        }
        free(out);
        free(raw);
        free(zin);
    }
    printf("inflate_test: %d failures (%d streams whole, %d of %d damaged ones told)\n", fails, cases, caught, hurt);
    return fails != 0 || !cases;
}
