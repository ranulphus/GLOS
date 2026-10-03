/* The kernel's random numbers (core/random.c). randombytes() itself is
 * declared by third_party/tinyssh/randombytes.h. */
#ifndef K_RANDOM_H
#define K_RANDOM_H
#include "types.h"

struct bootinfo;

void random_init(const struct bootinfo *bi);
void random_event(u32 v);               /* timing, from any context: cheap */
void random_add(const void *data, u32 n);
u32 random_events(void);                /* how many timing events so far */

#endif
