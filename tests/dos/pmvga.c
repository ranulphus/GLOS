/* PMVGA (GLOS M9 step C): a DJGPP program that writes "GLOS" into the text
 * screen from protected mode (its linear address B8000h, through _dos_ds),
 * then says "PMVGA done". A console job's program that does so is taken
 * full-screen (the console suite, tests/loopa/jobs.py). */
#include <go32.h>
#include <stdio.h>
#include <sys/farptr.h>

int main(void)
{
    _farpokeb(_dos_ds, 0xB8000, 'G');
    _farpokeb(_dos_ds, 0xB8002, 'L');
    _farpokeb(_dos_ds, 0xB8004, 'O');
    _farpokeb(_dos_ds, 0xB8006, 'S');
    printf("PMVGA done\n");
    return 0;
}
