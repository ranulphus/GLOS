/* CALC's arithmetic apart from its window (calcmath.h; phase E, docs/v1/apps/calculator.md). */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calcmath.h"

#define CALC_PI 3.14159265358979323846 /* (not PI: DJGPP's math.h has one) */
#define CALC_E  2.71828182845904523536

/* ---- the scientific mode */

static double to_rad(double x, int angle) { return angle == CALC_DEG ? x * CALC_PI / 180 : angle == CALC_GRAD ? x * CALC_PI / 200 : x; }
static double from_rad(double x, int angle) { return angle == CALC_DEG ? x * 180 / CALC_PI : angle == CALC_GRAD ? x * 200 / CALC_PI : x; }

/* (sin(180) is 0, not 1.2e-16: a result within the arithmetic's own error of nothing is nothing) */
static double tidy(double v) { return fabs(v) < 1e-15 ? 0 : v; }

int calc_fn(const char *name, double x, int angle, double *out)
{
    double v;
    if (!strcmp(name, "sin")) {
        v = tidy(sin(to_rad(x, angle)));
    } else if (!strcmp(name, "cos")) {
        v = tidy(cos(to_rad(x, angle)));
    } else if (!strcmp(name, "tan")) {
        double c = tidy(cos(to_rad(x, angle)));
        if (c == 0)
            return CALC_E_DOMAIN;
        v = tidy(sin(to_rad(x, angle))) / c;
    } else if (!strcmp(name, "asin") || !strcmp(name, "acos")) {
        if (x < -1 || x > 1)
            return CALC_E_DOMAIN;
        v = from_rad(name[1] == 's' ? asin(x) : acos(x), angle);
    } else if (!strcmp(name, "atan")) {
        v = from_rad(atan(x), angle);
    } else if (!strcmp(name, "sinh")) {
        v = sinh(x);
    } else if (!strcmp(name, "cosh")) {
        v = cosh(x);
    } else if (!strcmp(name, "tanh")) {
        v = tanh(x);
    } else if (!strcmp(name, "ln") || !strcmp(name, "log")) {
        if (x <= 0)
            return CALC_E_DOMAIN;
        v = name[1] == 'n' ? log(x) : log10(x);
    } else if (!strcmp(name, "exp")) {
        v = exp(x);
    } else if (!strcmp(name, "sqrt")) {
        if (x < 0)
            return CALC_E_DOMAIN;
        v = sqrt(x);
    } else if (!strcmp(name, "cbrt")) {
        v = x < 0 ? -pow(-x, 1.0 / 3) : pow(x, 1.0 / 3);
    } else if (!strcmp(name, "abs")) {
        v = fabs(x);
    } else if (!strcmp(name, "sqr")) {
        v = x * x;
    } else if (!strcmp(name, "inv")) {
        if (x == 0)
            return CALC_E_DIV0;
        v = 1 / x;
    } else if (!strcmp(name, "fact")) {
        int i;
        if (x < 0 || x > 170 || x != floor(x))
            return CALC_E_DOMAIN;
        for (v = 1, i = 2; i <= (int)x; i++)
            v *= i;
    } else {
        return CALC_E_SYNTAX;
    }
    if (v != v || v - v != 0)                   /* (not a number, or past what a double holds) */
        return CALC_E_DOMAIN;
    *out = v;
    return CALC_OK;
}

/* The expression, by descent: sum := product { +|- product }; product := power { *|/|% power };
   power := sign [ ^ power ]; sign := -sign | +sign | atom; atom := number | (sum) | name | name(sum). */
struct parse {
    const char *p;
    int angle, err, depth;
};

static double sum(struct parse *s);

static void blanks(struct parse *s)
{
    while (*s->p == ' ' || *s->p == '\t')
        s->p++;
}

static double atom(struct parse *s)
{
    double v = 0;
    blanks(s);
    if (*s->p == '(') {
        s->p++;
        if (++s->depth > 40) {
            s->err = CALC_E_SYNTAX;
            return 0;
        }
        v = sum(s);
        s->depth--;
        blanks(s);
        if (*s->p == ')')
            s->p++;
        else if (!s->err)
            s->err = CALC_E_SYNTAX;
    } else if (isdigit((unsigned char)*s->p) || *s->p == '.') {
        char *end;
        v = strtod(s->p, &end);
        if (end == s->p)
            s->err = CALC_E_SYNTAX;
        s->p = end;
    } else if (isalpha((unsigned char)*s->p)) {
        char name[8];
        size_t n = 0;
        while (isalpha((unsigned char)*s->p) && n < sizeof name - 1)
            name[n++] = (char)tolower((unsigned char)*s->p++);
        name[n] = 0;
        blanks(s);
        if (*s->p == '(') {
            double x = atom(s);
            int e;
            if (!s->err && (e = calc_fn(name, x, s->angle, &v)) != CALC_OK)
                s->err = e;
        } else if (!strcmp(name, "pi")) {
            v = CALC_PI;
        } else if (!strcmp(name, "e")) {
            v = CALC_E;
        } else {
            s->err = CALC_E_SYNTAX;
        }
    } else {
        s->err = CALC_E_SYNTAX;
    }
    return v;
}

static double power(struct parse *s);

static double sign(struct parse *s)
{
    blanks(s);
    if (*s->p == '-') {
        s->p++;
        return -power(s);                       /* (-2^2 is -4, as it is written) */
    }
    if (*s->p == '+') {
        s->p++;
        return power(s);
    }
    return atom(s);
}

static double power(struct parse *s)
{
    double v = sign(s);
    blanks(s);
    if (!s->err && *s->p == '^') {
        double x;
        s->p++;
        x = power(s);
        if (!s->err) {
            if ((v == 0 && x < 0) || (v < 0 && x != floor(x)))
                s->err = v == 0 ? CALC_E_DIV0 : CALC_E_DOMAIN;
            else
                v = pow(v, x);
        }
    }
    return v;
}

static double product(struct parse *s)
{
    double v = power(s);
    for (;;) {
        double x;
        char op;
        blanks(s);
        op = *s->p;
        if (s->err || (op != '*' && op != '/' && op != '%'))
            return v;
        s->p++;
        x = power(s);
        if (s->err)
            return v;
        if (op == '*')
            v *= x;
        else if (x == 0)
            s->err = CALC_E_DIV0;
        else
            v = op == '/' ? v / x : fmod(v, x);
    }
}

static double sum(struct parse *s)
{
    double v = product(s);
    for (;;) {
        double x;
        char op;
        blanks(s);
        op = *s->p;
        if (s->err || (op != '+' && op != '-'))
            return v;
        s->p++;
        x = product(s);
        if (!s->err)
            v = op == '+' ? v + x : v - x;
    }
}

int calc_eval(const char *text, int angle, double *out)
{
    struct parse s = { text, angle, CALC_OK, 0 };
    double v = sum(&s);
    blanks(&s);
    if (!s.err && *s.p)
        s.err = CALC_E_SYNTAX;                  /* (something after it that is no part of it) */
    if (!s.err && (v != v || v - v != 0))
        s.err = CALC_E_DOMAIN;
    if (s.err)
        return s.err;
    *out = v;
    return CALC_OK;
}

/* ---- the programmer's words */

unsigned long long calc_word(unsigned long long v, int bits)
{
    return bits >= 64 ? v : v & ((1ull << bits) - 1);
}

long long calc_signed(unsigned long long v, int bits)
{
    v = calc_word(v, bits);
    if (bits < 64 && v >> (bits - 1) & 1)
        v |= ~0ull << bits;
    return (long long)v;
}

int calc_word_op(int op, unsigned long long a, unsigned long long b, int bits, int is_signed, unsigned long long *out)
{
    unsigned long long v = 0;
    unsigned n;
    a = calc_word(a, bits), b = calc_word(b, bits);
    n = (unsigned)(b % (unsigned)bits);         /* (shifts and rotates: by the count modulo the word's size) */
    switch (op) {
    case CALC_AND: v = a & b; break;
    case CALC_OR: v = a | b; break;
    case CALC_XOR: v = a ^ b; break;
    case CALC_ADD: v = a + b; break;
    case CALC_SUB: v = a - b; break;
    case CALC_MUL: v = a * b; break;
    case CALC_SHL: v = a << n; break;
    case CALC_SHR: v = is_signed ? (unsigned long long)(calc_signed(a, bits) >> n) : a >> n; break;
    case CALC_ROL: v = n ? a << n | a >> ((unsigned)bits - n) : a; break;
    case CALC_ROR: v = n ? a >> n | a << ((unsigned)bits - n) : a; break;
    case CALC_DIV:
    case CALC_MOD:
        if (!b)
            return CALC_E_DIV0;
        if (is_signed) {
            long long x = calc_signed(a, bits), y = calc_signed(b, bits);
            if (y == -1)                        /* (the least number by -1 has no place in the word: it wraps) */
                v = op == CALC_DIV ? (unsigned long long)0 - (unsigned long long)x : 0;
            else
                v = (unsigned long long)(op == CALC_DIV ? x / y : x % y);
        } else {
            v = op == CALC_DIV ? a / b : a % b;
        }
        break;
    default: return CALC_E_SYNTAX;
    }
    *out = calc_word(v, bits);
    return CALC_OK;
}

void calc_word_show(unsigned long long v, int bits, int base, int is_signed, char *out, size_t room)
{
    char t[96];
    int n = 0, i;
    v = calc_word(v, bits);
    if (base == 10) {
        if (is_signed)
            snprintf(out, room, "%lld", calc_signed(v, bits));
        else
            snprintf(out, room, "%llu", v);
        return;
    }
    if (base == 16) {
        snprintf(out, room, "%llX", v);
        return;
    }
    if (base == 8) {
        snprintf(out, room, "%llo", v);
        return;
    }
    for (i = bits - 1; i >= 0; i--) {           /* base 2: every bit of the word, in fours */
        t[n++] = (char)('0' + (v >> i & 1));
        if (i && i % 4 == 0)
            t[n++] = ' ';
    }
    t[n] = 0;
    snprintf(out, room, "%s", t);
}

int calc_word_parse(const char *text, int base, int bits, unsigned long long *out)
{
    unsigned long long v = 0, top = calc_word(~0ull, bits);
    int neg = 0, any = 0;
    while (*text == ' ')
        text++;
    if (base == 10 && (*text == '-' || *text == '+'))
        neg = *text++ == '-';
    for (; *text; text++) {
        int d;
        if (*text == ' ')
            continue;
        d = isdigit((unsigned char)*text) ? *text - '0' : isalpha((unsigned char)*text) ? toupper((unsigned char)*text) - 'A' + 10 : 99;
        if (d >= base)
            return CALC_E_SYNTAX;
        if (v > (top - (unsigned)d) / (unsigned)base)
            return CALC_E_SYNTAX;               /* (more than the word holds) */
        v = v * (unsigned)base + (unsigned)d;
        any = 1;
    }
    if (!any)
        return CALC_E_SYNTAX;
    if (neg) {
        if (v > (top >> 1) + 1)
            return CALC_E_SYNTAX;
        v = 0 - v;
    }
    *out = calc_word(v, bits);
    return CALC_OK;
}
