/* DOS's file sharing (share.h). */
#include "share.h"

/* RBIL's table 01404, row by row: the first open's sharing mode (compatibility, deny all, deny write, deny read,
   deny none) and access (R, W, RW, NA: read without the access date); in each row the second open's, in the
   same order: Y goes ahead, N error 05h, C INT 24h. tests/host/share_test.c holds RBIL's own text of it and
   checks every cell. */
static const char table[20][21] = {
    "YYYYNNNNYNNYNNNYYNNY",   /* compatibility R */
    "YYYCNNNNNNNNNNNYYNNY",   /* compatibility W */
    "YYYCNNNNNNNNNNNYYNNY",   /* compatibility RW */
    "YCCYNNNNYNNYNNNYYNNY",   /* compatibility NA */
    "CCCCNNNNNNNNNNNNNNNN",   /* deny all R */
    "CCCCNNNNNNNNNNNNNNNN",   /* deny all W */
    "CCCCNNNNNNNNNNNNNNNN",   /* deny all RW */
    "CCCCNNNNNNNNNNNNNNNN",   /* deny all NA */
    "YCCYNNNNYNNYNNNYYNNY",   /* deny write R */
    "CCCCNNNNNNNNYNNYYNNY",   /* deny write W */
    "CCCCNNNNNNNNNNNYYNNY",   /* deny write RW */
    "YCCYNNNNYNNYNNNYYNNY",   /* deny write NA */
    "CCCCNNNNNYNNNNNNNYNN",   /* deny read R */
    "CCCCNNNNNNNNNYNNNYNN",   /* deny read W */
    "CCCCNNNNNNNNNNNNNYNN",   /* deny read RW */
    "YYYYNNNNYYYYNNNYYYYY",   /* deny read NA */
    "YYYYNNNNYYYYNNNYYYYY",   /* deny none R */
    "CCCCNNNNNNNNYYYYYYYY",   /* deny none W */
    "CCCCNNNNNNNNNNNYYYYY",   /* deny none RW */
    "YYYYNNNNYYYYNNNYYYYY",   /* deny none NA */
};

/* The table's row or column of a mode, or -1. */
static int place(unsigned mode)
{
    unsigned access = mode & 7, share = (mode >> 4) & 7;
    int a = access == 0 ? 0 : access == 1 ? 1 : access == 2 ? 2 : access == 4 ? 3 : -1;

    if (a < 0 || share > 4)
        return -1;
    return (int)share * 4 + a;
}

int dos_share(unsigned first, unsigned second)
{
    int r = place(first), k = place(second);

    if (r < 0 || k < 0)
        return DOS_SHARE_NO;
    return table[r][k] == 'Y' ? DOS_SHARE_YES : table[r][k] == 'C' ? DOS_SHARE_INT24 : DOS_SHARE_NO;
}
