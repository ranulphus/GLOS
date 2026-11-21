/* FINDFIRST and FINDNEXT in GLOS's own DOS (docs/v1/native-dos.md, N2, area 3: 4Eh, 4Fh): their state in the
 * program's DTA as RBIL's table 01626 lays it out (MS-DOS 5's: the drive, the template, the attributes, the next
 * entry's place, the folder's cluster; FAT32's cluster's high word in the reserved bytes), the entry found after
 * it. Over kernel/fs/fsv.c; host-tested (tests/host/find_test.c). */
#ifndef K_NDOS_FIND_H
#define K_NDOS_FIND_H

#include "../fs/fsv.h"

#define DOS_DTA_FIND 43                 /* the bytes of the DTA a search uses */

/* 4Eh: the first entry of folder dir (a whole path, "\" or "\DIR") on volume v, drive (0 A:), matching spec's
   last part (names.c's template) with search attributes attr, into dta. 0; 03h (no such folder); 12h (none
   matches: to check against MS-DOS 7.1, which may say 02h); 02h (a spec that can't be a name). */
int dos_findfirst(struct fsv_vol *v, unsigned drive, const char *dir, const char *spec, unsigned attr, int cp,
                  unsigned char *dta);
/* 4Fh: the next, from the DTA alone. 0, or 12h. */
int dos_findnext(struct fsv_vol *v, unsigned char *dta);

#endif
