/* The region's formats (GLOS.CFG's [region]; API 2.0's SETTINGS REGION): how dates, times and numbers are
 * written, the week's first day, the code page, the keyboard's layout and the language. One piece of code for
 * the kernel (the task bar's clock), libglos 2.0 and the host's tests (sdk/config/region.c). */
#ifndef GLOS_REGION_H
#define GLOS_REGION_H

enum { REGION_YMD, REGION_DMY, REGION_MDY };

struct region {
    unsigned char date;         /* REGION_: the order of a date's parts */
    char date_sep;              /* between them: '-', '/' or '.' */
    unsigned char clock12;      /* 1: twelve hours with AM and PM; 0: twenty-four */
    char decimal;               /* a number's decimal mark: '.' or ',' */
    char thousands;             /* between a number's groups of three digits: ',', '.', ' ', or 0 for none */
    unsigned char first_day;    /* the week's first day: 0 Sunday, 1 Monday */
    unsigned short codepage;    /* the code page of file names and DOS programs' text: 437, 850, ...; 0: DOS's own */
    char layout[8];             /* the keyboard's layout: "us" */
    char language[8];           /* the language of GLOS's own words: "en" */
};

void region_default(struct region *r);  /* 2026-11-10, 14:05, 1,234.5, Monday, DOS's code page, us, en */
/* A [region] setting's value into r: 0, or -1 if the key or the value is not one of the schema's. */
int region_set(struct region *r, const char *key, const char *value);
/* Each writes into out (room bytes, a NUL always) and returns the text's length. */
int region_date(const struct region *r, char *out, int room, int year, int month, int day);
int region_time(const struct region *r, char *out, int room, int hour, int minute, int second);  /* second < 0: none */
/* value in units of 10 to the power -decimals: (123456, 2) is 1,234.56. */
int region_number(const struct region *r, char *out, int room, long value, int decimals);

#endif
