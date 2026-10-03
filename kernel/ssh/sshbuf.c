/* SSH wire encoding (sshbuf.h). */
#include <string.h>
#include "sshbuf.h"

void sb_init(struct sbuf *b, uint8_t *p, uint32_t cap)
{
    b->p = p;
    b->len = 0;
    b->cap = cap;
    b->err = 0;
}

void sb_bytes(struct sbuf *b, const void *p, uint32_t n)
{
    if (b->err || n > b->cap - b->len) {
        b->err = 1;
        return;
    }
    memcpy(b->p + b->len, p, n);
    b->len += n;
}

void sb_u8(struct sbuf *b, uint8_t v) { sb_bytes(b, &v, 1); }

void sb_u32(struct sbuf *b, uint32_t v)
{
    uint8_t x[4];
    put_be32(x, v);
    sb_bytes(b, x, 4);
}

void sb_string(struct sbuf *b, const void *p, uint32_t n)
{
    sb_u32(b, n);
    sb_bytes(b, p, n);
}

void sb_cstr(struct sbuf *b, const char *s) { sb_string(b, s, (uint32_t)strlen(s)); }

void sb_mpint(struct sbuf *b, const uint8_t *be, uint32_t n)
{
    while (n && !*be) {                         /* no leading zeros ... */
        be++;
        n--;
    }
    if (n && (*be & 0x80)) {                    /* ... but one if the top bit is set (unsigned) */
        sb_u32(b, n + 1);
        sb_u8(b, 0);
        sb_bytes(b, be, n);
    } else {
        sb_string(b, be, n);
    }
}

void sr_init(struct sread *r, const void *p, uint32_t len)
{
    r->p = p;
    r->len = len;
    r->pos = 0;
    r->err = 0;
}

const uint8_t *sr_bytes(struct sread *r, uint32_t n)
{
    const uint8_t *p;
    if (r->err || n > r->len - r->pos) {
        r->err = 1;
        return NULL;
    }
    p = r->p + r->pos;
    r->pos += n;
    return p;
}

uint8_t sr_u8(struct sread *r)
{
    const uint8_t *p = sr_bytes(r, 1);
    return p ? *p : 0;
}

uint32_t sr_u32(struct sread *r)
{
    const uint8_t *p = sr_bytes(r, 4);
    return p ? be32(p) : 0;
}

const uint8_t *sr_string(struct sread *r, uint32_t *n)
{
    uint32_t len = sr_u32(r);
    const uint8_t *p = sr_bytes(r, len);
    *n = p ? len : 0;
    return p;
}

int sr_streq(const uint8_t *p, uint32_t n, const char *s)
{
    return p && strlen(s) == n && memcmp(p, s, n) == 0;
}

int namelist_has(const uint8_t *list, uint32_t n, const char *name)
{
    uint32_t i = 0, start = 0, k = (uint32_t)strlen(name);
    for (i = 0; i <= n; i++)
        if (i == n || list[i] == ',') {
            if (i - start == k && memcmp(list + start, name, k) == 0)
                return 1;
            start = i + 1;
        }
    return 0;
}

const char *namelist_pick(const uint8_t *client, uint32_t n, const char *const *ours)
{
    uint32_t i, start = 0;
    int j;
    for (i = 0; i <= n; i++)
        if (i == n || client[i] == ',') {
            for (j = 0; ours[j]; j++)
                if (strlen(ours[j]) == i - start && memcmp(client + start, ours[j], i - start) == 0)
                    return ours[j];
            start = i + 1;
        }
    return NULL;
}
