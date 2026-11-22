/* pciids_test FILE [USBFILE]: apps/shared/pciids.c against the packed list the Control Panel ships (PCIIDS.DAT): the
   names of the devices the benches and Loop A's machines have; a vendor it knows with a device it doesn't; one
   it doesn't know; a file that is not one; damaged copies of it never read past their ends (with ASan). And
   USBIDS.DAT's names for a pad and a receiver, and a vendor's with a device it hasn't. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/pciids.h"

static int checks, fails;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char **argv)
{
    static struct pci_name w[8];
    static const unsigned short ids[8][2] = { { 0x8086, 0x7190 }, { 0x102b, 0x0525 }, { 0x10ec, 0x8029 }, { 0x8086, 0x1229 },
                                              { 0x1002, 0x5046 }, { 0x121a, 0x0001 }, { 0x8086, 0xfff0 }, { 0x0002, 0x0001 } };
    char tmp[256];
    FILE *f;
    long size;
    unsigned char *data;
    int i, k, r;
    if (argc < 2)
        return 2;
    for (i = 0; i < 8; i++)
        w[i].vendor = ids[i][0], w[i].device = ids[i][1];
    r = pciids_lookup(argv[1], w, 8);
    CHECK(r == 7, "vendors known: %d", r);
    CHECK(!strcmp(w[0].vendor_name, "Intel Corporation") && !strcmp(w[0].device_name, "440BX/ZX/DX - 82443BX/ZX/DX Host bridge"),
          "8086:7190 %s / %s", w[0].vendor_name, w[0].device_name);
    CHECK(!strcmp(w[1].vendor_name, "Matrox Electronics Systems Ltd.") && !strcmp(w[1].device_name, "MGA G400/G450"),
          "102b:0525 %s / %s", w[1].vendor_name, w[1].device_name);
    CHECK(strstr(w[2].vendor_name, "Realtek") && strstr(w[2].device_name, "8029"), "10ec:8029 %s / %s", w[2].vendor_name,
          w[2].device_name);
    CHECK(strstr(w[3].device_name, "8255") != 0, "8086:1229 %s", w[3].device_name);
    CHECK(strstr(w[4].device_name, "Rage 128") != 0, "1002:5046 %s", w[4].device_name);
    CHECK(strstr(w[5].vendor_name, "3Dfx") && strstr(w[5].device_name, "Voodoo"), "121a:0001 %s / %s", w[5].vendor_name,
          w[5].device_name);
    CHECK(!strcmp(w[6].vendor_name, "Intel Corporation") && !w[6].device_name[0], "a device it hasn't: %s", w[6].device_name);
    CHECK(!w[7].vendor_name[0] && !w[7].device_name[0], "a vendor it hasn't");
    CHECK(pciids_lookup("/nonexistent/PCIIDS.DAT", w, 8) == -1 && !w[0].vendor_name[0], "no file");
    if (argc > 2) {                             /* (USBIDS.DAT: the list of USB IDs, in the same form) */
        static struct pci_name u[3];
        u[0].vendor = 0x045e, u[0].device = 0x028e, u[1].vendor = 0x046d, u[1].device = 0xc52b;
        u[2].vendor = 0x0002, u[2].device = 0xffff;
        r = pciids_lookup(argv[2], u, 3);
        CHECK(r == 3 && !strcmp(u[0].vendor_name, "Microsoft Corp.") && !strcmp(u[0].device_name, "Xbox360 Controller"),
              "usb 045e:028e %d %s / %s", r, u[0].vendor_name, u[0].device_name);
        CHECK(!strcmp(u[1].vendor_name, "Logitech, Inc.") && !strcmp(u[1].device_name, "Unifying Receiver"),
              "usb 046d:c52b %s / %s", u[1].vendor_name, u[1].device_name);
        CHECK(!strcmp(u[2].vendor_name, "Ingram") && !u[2].device_name[0], "usb 0002:ffff %s", u[2].vendor_name);
    }
    /* a file that is not one, and damaged copies */
    f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)size);
    CHECK(fread(data, 1, (size_t)size, f) == (size_t)size, "read");
    fclose(f);
    snprintf(tmp, sizeof tmp, "%s.test", argv[1]);
    for (k = 0; k < 40; k++) {
        unsigned char *copy = malloc((size_t)size);
        long n = k < 10 ? size * k / 10 : size;
        memcpy(copy, data, (size_t)size);
        if (k == 0)
            memcpy(copy, "NOTPCIID", 8), n = size;
        for (i = 0; k >= 10 && i < 30; i++)
            copy[12 + rand() % (size - 12)] = (unsigned char)rand();
        f = fopen(tmp, "wb");
        fwrite(copy, 1, (size_t)n, f);
        fclose(f);
        r = pciids_lookup(tmp, w, 8);
        CHECK(k ? r >= -1 && r <= 7 : r == -1, "damaged %d: %d", k, r);
        for (i = 0; i < 8; i++)
            CHECK(memchr(w[i].vendor_name, 0, sizeof w[i].vendor_name) && memchr(w[i].device_name, 0, sizeof w[i].device_name),
                  "names ended");
        free(copy);
    }
    remove(tmp);
    free(data);
    printf("pciids_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
