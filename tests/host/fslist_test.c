/* Host test for FS's entries and their packing into batches (kernel/app/fslist.c; API 2.0 step 7). */
#include <stdio.h>
#include <string.h>
#include "../../kernel/app/fslist.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void dta(u8 *d, const char *name, u8 attr, u32 size, u16 date, u16 time)
{
    memset(d, 0xEE, FS_DTA);
    d[0x15] = attr;
    d[0x16] = (u8)time, d[0x17] = (u8)(time >> 8);
    d[0x18] = (u8)date, d[0x19] = (u8)(date >> 8);
    memcpy(d + 0x1A, &size, 4);
    memset(d + 0x1E, 0, 13);
    strcpy((char *)d + 0x1E, name);
}

int main(void)
{
    u8 d[FS_DTA], l[FS_LFN], buf[256];
    struct fs_found e, f;
    struct fs_record *r;
    u32 used = 0, n, i, before;

    /* FindFirst's data. */
    dta(d, "README.TXT", 0x20, 123456, 0x5B6A, 0x7A21);
    fs_from_dta(d, &e);
    CHECK(e.attr == 0x20 && e.size_lo == 123456 && e.size_hi == 0 && e.mtime == 0x5B6A7A21u && !strcmp(e.name, "README.TXT"));
    CHECK(!e.short_name[0] && !fs_dots(&e));
    dta(d, "12345678.123", 0x10, 0, 1, 2);              /* the longest 8.3 name fills its field: no NUL after it */
    d[0x1E + 12] = 'X';
    fs_from_dta(d, &f);
    CHECK(!strcmp(f.name, "12345678.123") && f.attr == 0x10);
    dta(d, ".", 0x10, 0, 0, 0);
    fs_from_dta(d, &f);
    CHECK(fs_dots(&f));
    dta(d, "..", 0x10, 0, 0, 0);
    fs_from_dta(d, &f);
    CHECK(fs_dots(&f));
    dta(d, "...", 0x10, 0, 0, 0);
    fs_from_dta(d, &f);
    CHECK(!fs_dots(&f));
    dta(d, ".EXT", 0, 0, 0, 0);
    fs_from_dta(d, &f);
    CHECK(!fs_dots(&f));

    /* The long-name calls' data: both names, a size over 4 GB, the last write's date and time. */
    memset(l, 0, sizeof l);
    l[0] = 0x21;
    l[0x14] = 0x21, l[0x15] = 0x7A, l[0x16] = 0x6A, l[0x17] = 0x5B;
    l[0x1C] = 2;                                        /* (the high dword) */
    l[0x20] = 0x10, l[0x21] = 0x27;
    strcpy((char *)l + 0x2C, "A long name with spaces.text");
    strcpy((char *)l + 0x130, "ALONGN~1.TEX");
    fs_from_lfn(l, &f);
    CHECK(f.attr == 0x21 && f.mtime == 0x5B6A7A21u && f.size_hi == 2 && f.size_lo == 10000);
    CHECK(!strcmp(f.name, "A long name with spaces.text") && !strcmp(f.short_name, "ALONGN~1.TEX"));

    /* Packed: the record's size first, a multiple of 4, the names NUL-ended; one after another. */
    CHECK(fs_record_size(&e, 437) == 36 + 12);          /* (the fixed part, the name and its NUL, rounded) */
    CHECK(fs_pack(buf, sizeof buf, &used, &e, 437) == 1 && used == 48);
    r = (struct fs_record *)buf;
    CHECK(r->size == 48 && r->attr == 0x20 && r->bytes_lo == 123456 && r->mtime == 0x5B6A7A21u && !strcmp(r->name, "README.TXT")
          && !r->short_name[0]);
    before = used;
    CHECK(fs_pack(buf, sizeof buf, &used, &f, 437) == 1 && used == before + fs_record_size(&f, 437) && !(used & 3));
    r = (struct fs_record *)(buf + before);
    CHECK(r->size == used - before && r->bytes_hi == 2 && !strcmp(r->name, "A long name with spaces.text")
          && !strcmp(r->short_name, "ALONGN~1.TEX"));

    /* No room: nothing written, nothing used. */
    memset(buf, 0x55, sizeof buf);
    n = used = sizeof buf - 40;
    CHECK(fs_pack(buf, sizeof buf, &used, &e, 437) == 0 && used == n);
    for (i = 0; i < sizeof buf; i++)
        CHECK(buf[i] == 0x55);
    used = sizeof buf - 48;                             /* exactly enough */
    CHECK(fs_pack(buf, sizeof buf, &used, &e, 437) == 1 && used == sizeof buf);
    used = sizeof buf + 4;                              /* (past the end already: refused, not wrapped) */
    CHECK(fs_pack(buf, sizeof buf, &used, &e, 437) == 0);

    /* A batch: as many as fit, each whole; the next batch goes on with the one that didn't. */
    {
        u32 batches = 0, packed = 0, k = 0;
        while (k < 100) {
            used = 0;
            while (k < 100) {
                char name[16];
                snprintf(name, sizeof name, "FILE%04u.DAT", k);
                dta(d, name, 0x20, k, 0, 0);
                fs_from_dta(d, &f);
                if (!fs_pack(buf, sizeof buf, &used, &f, 437))
                    break;
                k++, packed++;
            }
            CHECK(used && used <= sizeof buf && used % 52 == 0);
            batches++;
        }
        CHECK(packed == 100 && batches == 25);          /* (four 52-byte records a 256-byte batch) */
    }

    /* Names through the code page: bytes above 7Fh as UTF-8. */
    dta(d, "CAF\x82.TXT", 0x20, 1, 0, 0);               /* e acute in 437 and 850 */
    fs_from_dta(d, &f);
    used = 0;
    CHECK(fs_pack(buf, sizeof buf, &used, &f, 850) == 1);
    CHECK(!strcmp(((struct fs_record *)buf)->name, "CAF\xC3\xA9.TXT") && fs_record_size(&f, 850) == 36 + 12);
    dta(d, "\x8F\x90\x88.TXT", 0x20, 1, 0, 0);          /* Cyrillic capitals in 866 */
    fs_from_dta(d, &f);
    used = 0;
    CHECK(fs_pack(buf, sizeof buf, &used, &f, 866) == 1);
    CHECK(!strcmp(((struct fs_record *)buf)->name, "\xD0\x9F\xD0\xA0\xD0\x98.TXT"));

    printf("fslist_test: %d failures\n", fails);
    return fails != 0;
}
