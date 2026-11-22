/* DOS's names in GLOS's own DOS (docs/v1/native-dos.md, N2, area 3: directories and names): a search's template,
 * a name matched against it, the search attributes, a byte's upper case in the code page. Plain C, host-tested
 * (tests/host/names_test.c). The rules are RBIL's (INT 21h 4Eh, 29h) and MS-DOS 7.1's as programs see them. */
#ifndef K_NDOS_NAMES_H
#define K_NDOS_NAMES_H

/* A byte of code page cp in upper case for a file name, as MS-DOS 7.1's tables have it (6504h: fucase.c, made
   from its COUNTRY.SYS by tools/ndos/fucase.py, the owner's choice: 437's e acute is E); in a page without a
   table, the capital where the page has it, else the letter without its accent, else the byte; Greek only in the
   Greek pages. */
unsigned char dos_upper(int cp, unsigned char b);

/* MS-DOS's file-name upper-case tables, bytes 80h-FFh, a code page each; ended by cp 0. */
struct dos_fucase {
    int cp;
    unsigned char t[128];
};
extern const struct dos_fucase dos_fucase_tables[];

/* The last part of a file specification ("*.TXT", "FOO?.C", "README") as the 11-byte template FINDFIRST keeps:
   upper case, name and extension blank-padded, '*' filling the rest of its part with '?' (what follows it in
   that part is ignored), a part too long cut short. 1 if it has wildcards, 0 if not, -1 if it can't be a DOS
   name (a character DOS refuses in a name: " * / : < > ? \ | as a part, controls, + , ; = [ ]). */
int dos_template(int cp, const char *spec, unsigned char tmpl[11]);

/* A short name (11 bytes, as the directory entry has it) against a template: '?' matches any byte, the padding
   too, so "?.?" finds one-letter names with no extension or one of a letter. */
int dos_match(const unsigned char tmpl[11], const unsigned char name[11]);

/* Whether an entry with attributes attr is found by a search with attributes search (4Eh's CX, 11h's FCB):
   08h alone finds the volume label and nothing else; otherwise an entry is found when its hidden, system and
   directory bits are all in search (read-only and archive don't count), and the label when search has 08h. */
int dos_attr_match(unsigned search, unsigned char attr);

#endif
