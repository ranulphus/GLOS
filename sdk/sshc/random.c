/* randombytes() for TinySSH's crypto in an app (DJGPP): the kernel's pool, through GLOS_FN_RANDOM. Without GLOS
   there are no keys to be made or sessions to be had: the program ends. */
#include <stdio.h>
#include <stdlib.h>

#include "randombytes.h"
#include <glos/api.h>

void randombytes(void *p, long long n)
{
    if (n < 0 || glos_random(p, (unsigned)n) != 0) {
        fprintf(stderr, "no random numbers: this needs GLOS\n");
        exit(3);
    }
}

const char *randombytes_source(void) { return "GLOS_FN_RANDOM"; }
