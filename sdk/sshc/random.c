/* randombytes() for TinySSH's crypto in an app (DJGPP): the kernel's pool, through GLOS's RANDOM. Without GLOS
   there are no keys to be made or sessions to be had: the program ends. */
#include <stdio.h>
#include <stdlib.h>

#include "randombytes.h"
#include <glos/glos.h>

void randombytes(void *p, long long n)
{
    unsigned char *at = p;
    while (n > 0) {                             /* (RANDOM gives 256 bytes a call at most) */
        unsigned k = n > 256 ? 256 : (unsigned)n;
        if (glos_random(at, k) != 0) {
            fprintf(stderr, "no random numbers: this needs GLOS\n");
            exit(3);
        }
        at += k, n -= k;
    }
}

const char *randombytes_source(void) { return "GLOS RANDOM"; }
