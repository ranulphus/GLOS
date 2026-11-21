/* PCI devices' names from PCIIDS.DAT (pciids.h): the file unpacked through a 32 KB window (loader/inflate.c),
 * its lines read as they come, the names asked for copied out. The file is data from elsewhere: a line longer
 * than a name's room is cut, one of no known form is passed over, and a damaged stream ends the reading with
 * what was found so far. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../loader/inflate.h"
#include "pciids.h"

struct reader {
    const unsigned char *in;
    unsigned long in_n;
    int given;                          /* the input handed over */
    unsigned char *window;
    char line[200];
    unsigned len;
    int vendor;                         /* the vendor the lines are under (-1: none yet) */
    struct pci_name *want;
    int n, found;
};

static unsigned fill(const unsigned char **p, void *ctx)
{
    struct reader *r = ctx;
    if (r->given)
        return 0;
    r->given = 1;
    *p = r->in;
    return (unsigned)r->in_n;
}

static int hex4(const char *s)
{
    int v = 0, i;
    for (i = 0; i < 4; i++) {
        char c = s[i];
        if (c >= '0' && c <= '9')
            v = v << 4 | (c - '0');
        else if (c >= 'a' && c <= 'f')
            v = v << 4 | (c - 'a' + 10);
        else
            return -1;
    }
    return v;
}

static void take(char *to, unsigned room, const char *from)
{
    size_t n = strlen(from);
    if (n >= room)
        n = room - 1;
    memcpy(to, from, n);
    to[n] = 0;
}

static void one_line(struct reader *r)
{
    char *l = r->line;
    int i, id;
    r->line[r->len] = 0;
    if (l[0] == '\t' && r->vendor >= 0 && r->len > 6 && l[5] == ' ' && (id = hex4(l + 1)) >= 0) {
        for (i = 0; i < r->n; i++)
            if (r->want[i].vendor == r->vendor && r->want[i].device == id)
                take(r->want[i].device_name, sizeof r->want[i].device_name, l + 6);
    } else if (l[0] != '\t' && l[0] != '#' && r->len > 5 && l[4] == ' ' && (id = hex4(l)) >= 0) {
        r->vendor = id;
        for (i = 0; i < r->n; i++)
            if (r->want[i].vendor == id) {
                if (!r->want[i].vendor_name[0])
                    r->found++;
                take(r->want[i].vendor_name, sizeof r->want[i].vendor_name, l + 5);
            }
    }
}

static int flush(unsigned n, void *ctx)
{
    struct reader *r = ctx;
    unsigned i;
    for (i = 0; i < n; i++) {
        unsigned char c = r->window[i];
        if (c == '\n') {
            one_line(r);
            r->len = 0;
        } else if (r->len + 1 < sizeof r->line) {
            r->line[r->len++] = (char)c;
        }
    }
    return 0;
}

int pciids_lookup(const char *path, struct pci_name *want, int n)
{
    FILE *f = fopen(path, "rb");
    int i;
    if (!f) {
        for (i = 0; i < n; i++)
            want[i].vendor_name[0] = want[i].device_name[0] = 0;
        return -1;
    }
    return pciids_lookup_file(f, want, n);
}

int pciids_lookup_file(void *fp, struct pci_name *want, int n)
{
    struct reader r;
    unsigned char *file = 0, *state = 0;
    long size;
    FILE *f = fp;
    int i, ok = -1;
    for (i = 0; i < n; i++)
        want[i].vendor_name[0] = want[i].device_name[0] = 0;
    memset(&r, 0, sizeof r);
    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 12 && size < (8L << 20) && fseek(f, 0, SEEK_SET) == 0
        && (file = malloc((size_t)size)) != 0 && fread(file, 1, (size_t)size, f) == (size_t)size
        && !memcmp(file, "GLOSPCI1", 8) && (r.window = malloc(INFL_WINDOW)) != 0
        && (state = malloc(inflate_state_size())) != 0) {
        r.in = file + 12;
        r.in_n = (unsigned long)size - 12;
        r.vendor = -1;
        r.want = want;
        r.n = n;
        inflate_stream_r((struct infl *)state, fill, flush, &r, r.window);    /* (damaged: what came before it) */
        ok = r.found;
    }
    fclose(f);
    free(state);
    free(r.window);
    free(file);
    return ok;
}
