/* DOS's file sharing (docs/v1/native-dos.md, N2: SHARE built in): may a second open of a file go ahead beside a
 * first? RBIL's table 01404, "DOS 7.x file sharing behavior" (GLOS's DOS says it is 7.10), as data. */
#ifndef K_NDOS_SHARE_H
#define K_NDOS_SHARE_H

/* What becomes of the second open: it goes ahead; it fails with error 05h (access denied); or it fails after
   INT 24h (critical error, then error 20h, sharing violation). */
enum { DOS_SHARE_YES = 0, DOS_SHARE_NO = 1, DOS_SHARE_INT24 = 2 };

/* first, second: the opens' modes as 3Dh's AL has them (bits 0-2 access: 0 read, 1 write, 2 both, 4 read
   without the access date; bits 4-6 sharing: 0 compatibility, 1 deny all, 2 deny write, 3 deny read, 4 deny
   none). A mode outside those: DOS_SHARE_NO. A new open must be let in beside every open already there. */
int dos_share(unsigned first, unsigned second);

#endif
