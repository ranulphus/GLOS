/* CALC's arithmetic against tables (apps/calc/calcmath.c; phase E, docs/v1/apps/calculator.md): the scientific
 * mode's expressions and functions, the programmer mode's words. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../apps/calc/calcmath.h"

static int fails;

static void expr(const char *text, int angle, int want_err, double want)
{
    double v = 0;
    int e = calc_eval(text, angle, &v);
    if (e != want_err || (!e && fabs(v - want) > 1e-9 * (fabs(want) > 1 ? fabs(want) : 1))) {
        printf("calc_test: \"%s\": e=%d v=%.15g, wanted e=%d v=%.15g\n", text, e, v, want_err, want);
        fails++;
    }
}

static void word(int op, unsigned long long a, unsigned long long b, int bits, int sg, int want_err, unsigned long long want)
{
    unsigned long long v = 0;
    int e = calc_word_op(op, a, b, bits, sg, &v);
    if (e != want_err || (!e && v != want)) {
        printf("calc_test: op %d %llx %llx bits=%d signed=%d: e=%d v=%llx, wanted e=%d v=%llx\n", op, a, b, bits, sg, e, v,
               want_err, want);
        fails++;
    }
}

static void show(unsigned long long v, int bits, int base, int sg, const char *want)
{
    char t[96];
    calc_word_show(v, bits, base, sg, t, sizeof t);
    if (strcmp(t, want)) {
        printf("calc_test: show %llx bits=%d base=%d signed=%d: \"%s\", wanted \"%s\"\n", v, bits, base, sg, t, want);
        fails++;
    }
}

static void parse(const char *text, int base, int bits, int want_err, unsigned long long want)
{
    unsigned long long v = 0;
    int e = calc_word_parse(text, base, bits, &v);
    if (e != want_err || (!e && v != want)) {
        printf("calc_test: parse \"%s\" base=%d bits=%d: e=%d v=%llx, wanted e=%d v=%llx\n", text, base, bits, e, v, want_err, want);
        fails++;
    }
}

int main(void)
{
    /* the order of operations, brackets, signs, powers */
    expr("2+3*4", CALC_DEG, 0, 14);
    expr("(2+3)*4", CALC_DEG, 0, 20);
    expr(" 10 / 4 - 1 ", CALC_DEG, 0, 1.5);
    expr("2^3^2", CALC_DEG, 0, 512);
    expr("-2^2", CALC_DEG, 0, -4);
    expr("2^-2", CALC_DEG, 0, 0.25);
    expr("(-2)^2", CALC_DEG, 0, 4);
    expr("7%4", CALC_DEG, 0, 3);
    expr("2e3+.5", CALC_DEG, 0, 2000.5);
    expr("-(3-5)*-2", CALC_DEG, 0, -4);
    expr("((((1))))", CALC_DEG, 0, 1);
    /* the names and the functions, in each unit of angle */
    expr("pi", CALC_DEG, 0, 3.14159265358979);
    expr("e^1", CALC_DEG, 0, 2.71828182845905);
    expr("sin(30)", CALC_DEG, 0, 0.5);
    expr("sin(180)", CALC_DEG, 0, 0);
    expr("cos(pi)", CALC_RAD, 0, -1);
    expr("tan(50)", CALC_GRAD, 0, 1);
    expr("asin(1)", CALC_DEG, 0, 90);
    expr("atan(1)", CALC_RAD, 0, 0.785398163397448);
    expr("acos(0)", CALC_GRAD, 0, 100);
    expr("sqrt(2)*sqrt(2)", CALC_DEG, 0, 2);
    expr("ln(e)+log(1000)", CALC_DEG, 0, 4);
    expr("exp(0)+abs(-3)+cbrt(-27)", CALC_DEG, 0, 1);
    expr("fact(5)+sqr(3)+inv(4)", CALC_DEG, 0, 129.25);
    expr("fact(170)/fact(169)", CALC_DEG, 0, 170);
    expr("sinh(0)+cosh(0)+tanh(0)", CALC_DEG, 0, 1);
    expr("SIN(90)", CALC_DEG, 0, 1);
    /* what is refused, and why */
    expr("1/0", CALC_DEG, CALC_E_DIV0, 0);
    expr("5%0", CALC_DEG, CALC_E_DIV0, 0);
    expr("0^-1", CALC_DEG, CALC_E_DIV0, 0);
    expr("inv(0)", CALC_DEG, CALC_E_DIV0, 0);
    expr("sqrt(-1)", CALC_DEG, CALC_E_DOMAIN, 0);
    expr("ln(0)", CALC_DEG, CALC_E_DOMAIN, 0);
    expr("asin(2)", CALC_DEG, CALC_E_DOMAIN, 0);
    expr("tan(90)", CALC_DEG, CALC_E_DOMAIN, 0);
    expr("fact(171)", CALC_DEG, CALC_E_DOMAIN, 0);
    expr("fact(2.5)", CALC_DEG, CALC_E_DOMAIN, 0);
    expr("(-8)^0.5", CALC_DEG, CALC_E_DOMAIN, 0);
    expr("1e308*10", CALC_DEG, CALC_E_DOMAIN, 0);
    expr("", CALC_DEG, CALC_E_SYNTAX, 0);
    expr("2+", CALC_DEG, CALC_E_SYNTAX, 0);
    expr("(2+3", CALC_DEG, CALC_E_SYNTAX, 0);
    expr("2 3", CALC_DEG, CALC_E_SYNTAX, 0);
    expr("foo(1)", CALC_DEG, CALC_E_SYNTAX, 0);
    expr("sin 30", CALC_DEG, CALC_E_SYNTAX, 0);
    expr("2*x", CALC_DEG, CALC_E_SYNTAX, 0);

    /* the programmer's words */
    word(CALC_ADD, 0xFF, 1, 8, 0, 0, 0);
    word(CALC_ADD, 0xFFFF, 1, 16, 0, 0, 0);
    word(CALC_SUB, 0, 1, 32, 0, 0, 0xFFFFFFFFull);
    word(CALC_SUB, 0, 1, 64, 0, 0, 0xFFFFFFFFFFFFFFFFull);
    word(CALC_MUL, 0x10, 0x10, 8, 0, 0, 0);
    word(CALC_MUL, 0x1234, 0x100, 32, 0, 0, 0x123400);
    word(CALC_DIV, 100, 7, 8, 0, 0, 14);
    word(CALC_MOD, 100, 7, 8, 0, 0, 2);
    word(CALC_DIV, 0xF6, 3, 8, 1, 0, 0xFD);              /* -10 / 3 = -3 */
    word(CALC_MOD, 0xF6, 3, 8, 1, 0, 0xFF);              /* -10 % 3 = -1 */
    word(CALC_DIV, 0x80, 0xFF, 8, 1, 0, 0x80);           /* -128 / -1 wraps */
    word(CALC_DIV, 5, 0, 16, 0, CALC_E_DIV0, 0);
    word(CALC_MOD, 5, 0, 16, 1, CALC_E_DIV0, 0);
    word(CALC_AND, 0xF0F0, 0xFF00, 16, 0, 0, 0xF000);
    word(CALC_OR, 0xF0F0, 0x0F00, 16, 0, 0, 0xFFF0);
    word(CALC_XOR, 0xFFFF, 0x00FF, 16, 0, 0, 0xFF00);
    word(CALC_SHL, 0x81, 1, 8, 0, 0, 0x02);
    word(CALC_SHR, 0x81, 1, 8, 0, 0, 0x40);
    word(CALC_SHR, 0x81, 1, 8, 1, 0, 0xC0);              /* the sign kept */
    word(CALC_ROL, 0x81, 1, 8, 0, 0, 0x03);
    word(CALC_ROR, 0x81, 1, 8, 0, 0, 0xC0);
    word(CALC_ROL, 0x80000001ull, 4, 32, 0, 0, 0x18);
    word(CALC_ROR, 1, 1, 64, 0, 0, 0x8000000000000000ull);
    word(CALC_SHL, 1, 9, 8, 0, 0, 2);                    /* (by the count modulo the size) */
    word(CALC_ROL, 0x12, 8, 8, 0, 0, 0x12);

    show(0xFF, 8, 10, 0, "255");
    show(0xFF, 8, 10, 1, "-1");
    show(0x8000, 16, 10, 1, "-32768");
    show(0xFFFFFFFFFFFFFFFFull, 64, 10, 0, "18446744073709551615");
    show(0xBEEF, 16, 16, 0, "BEEF");
    show(0x1FF, 8, 16, 0, "FF");
    show(8, 8, 8, 0, "10");
    show(0xA5, 8, 2, 0, "1010 0101");
    show(5, 16, 2, 0, "0000 0000 0000 0101");

    parse("255", 10, 8, 0, 255);
    parse("256", 10, 8, CALC_E_SYNTAX, 0);
    parse("-1", 10, 8, 0, 0xFF);
    parse("-128", 10, 8, 0, 0x80);
    parse("-129", 10, 8, CALC_E_SYNTAX, 0);
    parse("beef", 16, 16, 0, 0xBEEF);
    parse("1BEEF", 16, 16, CALC_E_SYNTAX, 0);
    parse("1010 0101", 2, 8, 0, 0xA5);
    parse("12", 2, 8, CALC_E_SYNTAX, 0);
    parse("777", 8, 16, 0, 0x1FF);
    parse("8", 8, 16, CALC_E_SYNTAX, 0);
    parse("", 10, 16, CALC_E_SYNTAX, 0);
    parse("FFFFFFFFFFFFFFFF", 16, 64, 0, 0xFFFFFFFFFFFFFFFFull);
    parse("18446744073709551616", 10, 64, CALC_E_SYNTAX, 0);

    printf("calc_test: %d failures\n", fails);
    return fails != 0;
}
