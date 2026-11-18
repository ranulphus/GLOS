/* CALC's arithmetic apart from its window (phase E, docs/v1/apps/calculator.md): the scientific mode's
 * expressions and functions, and the programmer mode's words. Plain C with libm: the host tests it against
 * tables (tests/host/calc_test.c). */
#ifndef CALCMATH_H
#define CALCMATH_H

#include <stddef.h>

enum { CALC_OK, CALC_E_SYNTAX, CALC_E_DIV0, CALC_E_DOMAIN };   /* (an expression not one; x / 0; sqrt(-1), ln(0)) */
enum { CALC_DEG, CALC_RAD, CALC_GRAD };                         /* the angles' unit */

/* An expression's value: numbers (1.5, .5, 2e3), + - * / (the usual order), ^ (right to left), % as "mod",
   brackets, a sign before anything, the names pi and e, and the functions of calc_fn by their names with a
   bracketed argument ("sin(30)", "sqrt(2)*ln(e)"). Spaces anywhere. CALC_OK, or why not. */
int calc_eval(const char *text, int angle, double *out);

/* A function of one number, by its name: sin cos tan asin acos atan sinh cosh tanh ln log exp sqrt cbrt abs
   fact (0 to 170, whole numbers) sqr inv (1/x). CALC_OK, CALC_E_DOMAIN, CALC_E_DIV0, or CALC_E_SYNTAX for no
   such name. */
int calc_fn(const char *name, double x, int angle, double *out);

/* ---- the programmer's words: 8, 16, 32 or 64 bits, kept as the low bits of 64 */

enum { CALC_AND = 1, CALC_OR, CALC_XOR, CALC_SHL, CALC_SHR, CALC_ROL, CALC_ROR, CALC_ADD, CALC_SUB, CALC_MUL,
       CALC_DIV, CALC_MOD };

unsigned long long calc_word(unsigned long long v, int bits);  /* v's low `bits` bits */
long long calc_signed(unsigned long long v, int bits);         /* the word as a signed number */

/* a op b in a word of `bits` bits (is_signed: DIV, MOD and SHR treat the words as signed). CALC_OK or
   CALC_E_DIV0. Shifts and rotates take b modulo the word's size. */
int calc_word_op(int op, unsigned long long a, unsigned long long b, int bits, int is_signed, unsigned long long *out);

/* The word written in a base (2, 8, 10 or 16; base 10 signed if is_signed; base 2 in groups of four with
   spaces, base 16 in capitals). */
void calc_word_show(unsigned long long v, int bits, int base, int is_signed, char *out, size_t room);

/* A word read in a base (spaces and, in base 10, a sign allowed): CALC_OK, or CALC_E_SYNTAX (a digit that is
   none of the base's, nothing at all, or more than the word holds). */
int calc_word_parse(const char *text, int base, int bits, unsigned long long *out);

#endif
