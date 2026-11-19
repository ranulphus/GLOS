/* NOTEPAD's highlighting (phase E, docs/v1/apps/notepad.md): which lexer a file gets by its name's end, and the
 * lexers the toolkit hasn't: batch files, INI files (GLOS.CFG among them), assembly (NASM, MASM and Watcom's
 * look alike enough), Pascal and HTML. A lexer says a class (GUI_TOK_) for each byte of a line, and the state
 * the line ends in (a comment still open), which it is given again with the next line: the toolkit's code
 * editor asks line by line (include/glos/gui.h, gui_lexer). The host tests them against tables
 * (tests/host/lex_test.c). */
#include <ctype.h>
#include <string.h>
#include <strings.h>

#include "lexers.h"

static int word_char(int c) { return isalnum(c) || c == '_'; }

/* The word at s (n bytes) among a list, whatever its letters' case: 1 if it is there. */
static int in_list(const char *s, int n, const char *const *list)
{
    for (; *list; list++)
        if ((int)strlen(*list) == n && !strncasecmp(*list, s, (size_t)n))
            return 1;
    return 0;
}

static void fill(unsigned char *cls, int from, int to, int c)
{
    for (; from < to; from++)
        cls[from] = (unsigned char)c;
}

/* A number at i (decimal, 0x.., a fraction, an exponent; hex with a letter after its digits where `tail` allows
   it, "1Fh"): the place after it. */
static int number(const char *s, int n, int i, int tail)
{
    int j = i;
    if (j + 1 < n && s[j] == '0' && (s[j + 1] | 32) == 'x') {
        for (j += 2; j < n && isxdigit((unsigned char)s[j]); j++)
            ;
        return j;
    }
    while (j < n && (isdigit((unsigned char)s[j]) || (tail && isxdigit((unsigned char)s[j]))))
        j++;
    if (tail && j < n && strchr("hHbBoOqQ", s[j]))
        return j + 1;
    if (j < n && s[j] == '.' && j + 1 < n && isdigit((unsigned char)s[j + 1]))
        for (j++; j < n && isdigit((unsigned char)s[j]); j++)
            ;
    return j;
}

/* ---- batch files */

int lex_batch(void *ctx, const char *s, int n, int state, unsigned char *cls)
{
    static const char *const kw[] = { "echo", "set", "if", "else", "for", "in", "do", "goto", "call", "exit", "not",
        "exist", "errorlevel", "pause", "shift", "cd", "chdir", "md", "mkdir", "rd", "rmdir", "copy", "del", "erase",
        "ren", "rename", "type", "path", "prompt", "cls", "dir", "ver", "choice", "defined", "off", "on", NULL };
    int i = 0, j;
    (void)ctx, (void)state;
    fill(cls, 0, n, GUI_TOK_TEXT);
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '@'))
        i++;
    if ((i + 3 <= n && !strncasecmp(s + i, "rem", 3) && (i + 3 == n || s[i + 3] == ' ' || s[i + 3] == '\t'))
        || (i + 1 < n && s[i] == ':' && s[i + 1] == ':')) {
        fill(cls, i, n, GUI_TOK_COMMENT);
        return 0;
    }
    if (i < n && s[i] == ':') {                 /* a label */
        fill(cls, i, n, GUI_TOK_PREPROC);
        return 0;
    }
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') {
            for (j = i + 1; j < n && s[j] != '"'; j++)
                ;
            j = j < n ? j + 1 : n;
            fill(cls, i, j, GUI_TOK_STRING);
            i = j;
        } else if (c == '%') {                  /* %NAME%, %1, %%a */
            j = i + 1;
            if (j < n && s[j] == '%')
                j++;
            if (j < n && isdigit((unsigned char)s[j])) {
                j++;
            } else {
                while (j < n && word_char((unsigned char)s[j]))
                    j++;
                if (j < n && s[j] == '%')
                    j++;
            }
            fill(cls, i, j, GUI_TOK_TYPE);
            i = j;
        } else if (isdigit(c) && (i == 0 || !word_char((unsigned char)s[i - 1]))) {
            j = number(s, n, i, 0);
            fill(cls, i, j, GUI_TOK_NUMBER);
            i = j;
        } else if (isalpha(c)) {
            for (j = i; j < n && word_char((unsigned char)s[j]); j++)
                ;
            if (in_list(s + i, j - i, kw))
                fill(cls, i, j, GUI_TOK_KEYWORD);
            i = j;
        } else {
            if (strchr("=<>|&()", c))
                cls[i] = GUI_TOK_OP;
            i++;
        }
    }
    return 0;
}

/* ---- INI files */

int lex_ini(void *ctx, const char *s, int n, int state, unsigned char *cls)
{
    int i = 0, j, eq = -1;
    (void)ctx, (void)state;
    fill(cls, 0, n, GUI_TOK_TEXT);
    while (i < n && (s[i] == ' ' || s[i] == '\t'))
        i++;
    if (i == n)
        return 0;
    if (s[i] == ';' || s[i] == '#') {
        fill(cls, i, n, GUI_TOK_COMMENT);
        return 0;
    }
    if (s[i] == '[') {
        fill(cls, i, n, GUI_TOK_KEYWORD);
        return 0;
    }
    for (j = i; j < n; j++)
        if (s[j] == '=') {
            eq = j;
            break;
        }
    if (eq < 0)
        return 0;
    fill(cls, i, eq, GUI_TOK_TYPE);             /* the key */
    cls[eq] = GUI_TOK_OP;
    for (i = eq + 1; i < n;) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') {
            for (j = i + 1; j < n && s[j] != '"'; j++)
                ;
            j = j < n ? j + 1 : n;
            fill(cls, i, j, GUI_TOK_STRING);
            i = j;
        } else if (isdigit(c) && !word_char((unsigned char)s[i - 1])) {
            j = number(s, n, i, 0);
            if (j == n || !word_char((unsigned char)s[j]))
                fill(cls, i, j, GUI_TOK_NUMBER);
            else
                while (j < n && word_char((unsigned char)s[j]))
                    j++;
            i = j;
        } else {
            i++;
        }
    }
    return 0;
}

/* ---- assembly */

int lex_asm(void *ctx, const char *s, int n, int state, unsigned char *cls)
{
    static const char *const reg[] = { "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp", "ax", "bx", "cx", "dx",
        "si", "di", "bp", "sp", "al", "ah", "bl", "bh", "cl", "ch", "dl", "dh", "cs", "ds", "es", "fs", "gs", "ss",
        "cr0", "cr2", "cr3", "cr4", "st0", "st1", "byte", "word", "dword", "qword", "ptr", "near", "far", "short", NULL };
    static const char *const dir[] = { "db", "dw", "dd", "dq", "equ", "section", "segment", "ends", "org", "bits",
        "global", "extern", "public", "extrn", "proc", "endp", "macro", "endm", "times", "resb", "resw", "resd",
        "align", "include", "incbin", "use16", "use32", "end", "assume", "struc", "endstruc", NULL };
    static const char *const op[] = { "mov", "add", "sub", "adc", "sbb", "inc", "dec", "mul", "imul", "div", "idiv",
        "and", "or", "xor", "not", "neg", "shl", "shr", "sal", "sar", "rol", "ror", "rcl", "rcr", "cmp", "test", "jmp",
        "je", "jne", "jz", "jnz", "ja", "jae", "jb", "jbe", "jg", "jge", "jl", "jle", "jc", "jnc", "js", "jns", "jo",
        "jno", "jcxz", "jecxz", "loop", "call", "ret", "retf", "iret", "iretd", "int", "push", "pop", "pusha", "popa",
        "pushad", "popad", "pushf", "popf", "pushfd", "popfd", "lea", "xchg", "movzx", "movsx", "nop", "hlt", "cli",
        "sti", "cld", "std", "in", "out", "rep", "repe", "repne", "movsb", "movsw", "movsd", "stosb", "stosw",
        "stosd", "lodsb", "lodsw", "lodsd", "cmpsb", "scasb", "cbw", "cwd", "cdq", "cwde", "leave", "enter", "lgdt",
        "lidt", "ltr", "lldt", "cpuid", "rdtsc", "rdmsr", "wrmsr", "bt", "bts", "btr", "bsf", "bsr", "setz", "setnz",
        "fld", "fstp", "fadd", "fmul", "fsub", "fdiv", "finit", "fwait", NULL };
    int i = 0, j;
    (void)ctx, (void)state;
    fill(cls, 0, n, GUI_TOK_TEXT);
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c == ';') {
            fill(cls, i, n, GUI_TOK_COMMENT);
            break;
        }
        if (c == '"' || c == '\'') {
            for (j = i + 1; j < n && s[j] != (char)c; j++)
                ;
            j = j < n ? j + 1 : n;
            fill(cls, i, j, GUI_TOK_STRING);
            i = j;
        } else if (isdigit(c) && (i == 0 || !word_char((unsigned char)s[i - 1]))) {
            j = number(s, n, i, 1);
            fill(cls, i, j, GUI_TOK_NUMBER);
            i = j;
        } else if (c == '%' || c == '.') {      /* %define, .model: a directive */
            for (j = i + 1; j < n && word_char((unsigned char)s[j]); j++)
                ;
            if (j > i + 1)
                fill(cls, i, j, GUI_TOK_PREPROC);
            i = j;
        } else if (isalpha(c) || c == '_') {
            for (j = i; j < n && word_char((unsigned char)s[j]); j++)
                ;
            if (in_list(s + i, j - i, op))
                fill(cls, i, j, GUI_TOK_KEYWORD);
            else if (in_list(s + i, j - i, reg))
                fill(cls, i, j, GUI_TOK_TYPE);
            else if (in_list(s + i, j - i, dir))
                fill(cls, i, j, GUI_TOK_PREPROC);
            i = j;
        } else {
            if (strchr("+-*/[],:", c))
                cls[i] = GUI_TOK_OP;
            i++;
        }
    }
    return 0;
}

/* ---- Pascal: state 1 inside { }, 2 inside (* *) */

int lex_pascal(void *ctx, const char *s, int n, int state, unsigned char *cls)
{
    static const char *const kw[] = { "begin", "end", "if", "then", "else", "for", "to", "downto", "do", "while",
        "repeat", "until", "var", "const", "type", "procedure", "function", "program", "unit", "uses", "interface",
        "implementation", "record", "array", "of", "case", "with", "nil", "not", "and", "or", "xor", "div", "mod",
        "shl", "shr", "in", "set", "file", "goto", "label", "object", "constructor", "destructor", "inherited",
        "virtual", "forward", "external", "inline", "packed", "exit", NULL };
    static const char *const ty[] = { "integer", "real", "boolean", "char", "string", "byte", "word", "longint",
        "shortint", "pointer", "text", "single", "double", "extended", "comp", "true", "false", NULL };
    int i = 0, j;
    (void)ctx;
    fill(cls, 0, n, GUI_TOK_TEXT);
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (state) {                            /* a comment still open: to its end, if it is on this line */
            for (j = i; j < n; j++)
                if (state == 1 ? s[j] == '}' : (s[j] == '*' && j + 1 < n && s[j + 1] == ')'))
                    break;
            if (j < n) {
                j += state == 1 ? 1 : 2;
                state = 0;
            }
            fill(cls, i, j, GUI_TOK_COMMENT);
            i = j;
        } else if (c == '{') {
            state = 1;
        } else if (c == '(' && i + 1 < n && s[i + 1] == '*') {
            state = 2;
        } else if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            fill(cls, i, n, GUI_TOK_COMMENT);
            break;
        } else if (c == '\'') {
            for (j = i + 1; j < n && s[j] != '\''; j++)
                ;
            j = j < n ? j + 1 : n;
            fill(cls, i, j, GUI_TOK_STRING);
            i = j;
        } else if (c == '$' && i + 1 < n && isxdigit((unsigned char)s[i + 1])) {
            for (j = i + 1; j < n && isxdigit((unsigned char)s[j]); j++)
                ;
            fill(cls, i, j, GUI_TOK_NUMBER);
            i = j;
        } else if (isdigit(c) && (i == 0 || !word_char((unsigned char)s[i - 1]))) {
            j = number(s, n, i, 0);
            fill(cls, i, j, GUI_TOK_NUMBER);
            i = j;
        } else if (isalpha(c) || c == '_') {
            for (j = i; j < n && word_char((unsigned char)s[j]); j++)
                ;
            if (in_list(s + i, j - i, kw))
                fill(cls, i, j, GUI_TOK_KEYWORD);
            else if (in_list(s + i, j - i, ty))
                fill(cls, i, j, GUI_TOK_TYPE);
            i = j;
        } else {
            if (strchr("+-*/=<>:;,.()[]^@", c))
                cls[i] = GUI_TOK_OP;
            i++;
        }
    }
    return state;
}

/* ---- HTML: state 1 inside <!-- -->, 2 inside a tag, 2 + the quote's character inside a tag's string */

int lex_html(void *ctx, const char *s, int n, int state, unsigned char *cls)
{
    int i = 0, j;
    (void)ctx;
    fill(cls, 0, n, GUI_TOK_TEXT);
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (state == 1) {
            for (j = i; j < n; j++)
                if (j + 2 < n && s[j] == '-' && s[j + 1] == '-' && s[j + 2] == '>')
                    break;
            if (j < n) {
                j += 3;
                state = 0;
            }
            fill(cls, i, j, GUI_TOK_COMMENT);
            i = j;
        } else if (state > 2) {                 /* a string in a tag, to its quote */
            for (j = i; j < n && s[j] != (char)(state - 2); j++)
                ;
            if (j < n) {
                j++;
                state = 2;
            }
            fill(cls, i, j, GUI_TOK_STRING);
            i = j;
        } else if (state == 2) {
            if (c == '>') {
                cls[i++] = GUI_TOK_KEYWORD;
                state = 0;
            } else if (c == '"' || c == '\'') {
                cls[i++] = GUI_TOK_STRING;
                state = 2 + c;
            } else if (isalpha(c)) {            /* an attribute's name */
                for (j = i; j < n && (word_char((unsigned char)s[j]) || s[j] == '-'); j++)
                    ;
                fill(cls, i, j, GUI_TOK_TYPE);
                i = j;
            } else {
                if (c == '=' || c == '/')
                    cls[i] = GUI_TOK_OP;
                i++;
            }
        } else if (c == '<' && i + 3 < n && !strncmp(s + i, "<!--", 4)) {
            state = 1;
        } else if (c == '<' && i + 1 < n && (isalpha((unsigned char)s[i + 1]) || s[i + 1] == '/' || s[i + 1] == '!')) {
            for (j = i + 1; j < n && (word_char((unsigned char)s[j]) || s[j] == '/' || s[j] == '!'); j++)
                ;
            fill(cls, i, j, GUI_TOK_KEYWORD);   /* "<name" */
            i = j;
            state = 2;
        } else if (c == '&') {                  /* an entity */
            for (j = i + 1; j < n && j < i + 10 && (word_char((unsigned char)s[j]) || s[j] == '#'); j++)
                ;
            if (j < n && s[j] == ';') {
                fill(cls, i, j + 1, GUI_TOK_NUMBER);
                i = j + 1;
            } else {
                i++;
            }
        } else {
            i++;
        }
    }
    return state;
}

/* ---- which lexer a file gets */

/* ---- Markdown: headings, lists' and quotes' marks, rules, code (spans, and fenced over lines), emphasis, links.
   The state: 1 inside a fenced block. */
int lex_markdown(void *ctx, const char *s, int n, int state, unsigned char *cls)
{
    int i = 0, j, k;
    (void)ctx;
    fill(cls, 0, n, GUI_TOK_TEXT);
    while (i < n && i < 3 && s[i] == ' ')
        i++;
    if (i + 3 <= n && (!strncmp(s + i, "```", 3) || !strncmp(s + i, "~~~", 3))) {     /* a fence: in, or out */
        fill(cls, 0, n, GUI_TOK_PREPROC);
        return !state;
    }
    if (state) {
        fill(cls, 0, n, GUI_TOK_STRING);
        return 1;
    }
    if (i < n && s[i] == '#') {                 /* a heading: one to six #, a space */
        for (j = i; j < n && s[j] == '#'; j++)
            ;
        if (j - i <= 6 && (j == n || s[j] == ' ')) {
            fill(cls, 0, n, GUI_TOK_KEYWORD);
            return 0;
        }
    }
    if (i < n && (s[i] == '-' || s[i] == '*' || s[i] == '_')) {         /* a rule: three or more of one, and spaces */
        for (j = i, k = 0; j < n && (s[j] == s[i] || s[j] == ' '); j++)
            k += s[j] == s[i];
        if (j == n && k >= 3) {
            fill(cls, 0, n, GUI_TOK_OP);
            return 0;
        }
    }
    while (i < n && s[i] == '>') {              /* a quote's marks */
        cls[i++] = GUI_TOK_OP;
        while (i < n && s[i] == ' ')
            i++;
    }
    if (i + 1 < n && (s[i] == '-' || s[i] == '*' || s[i] == '+') && s[i + 1] == ' ') {
        cls[i] = GUI_TOK_OP;                    /* a list's mark */
        i += 2;
    } else if (i < n && isdigit((unsigned char)s[i])) {
        for (j = i; j < n && isdigit((unsigned char)s[j]); j++)
            ;
        if (j + 1 < n && (s[j] == '.' || s[j] == ')') && s[j + 1] == ' ') {
            fill(cls, i, j + 1, GUI_TOK_OP);    /* a numbered list's */
            i = j + 2;
        }
    }
    while (i < n) {
        if (s[i] == '\\' && i + 1 < n) {        /* (a mark taken as itself) */
            i += 2;
        } else if (s[i] == '`') {               /* a code span: to the next ` */
            for (j = i + 1; j < n && s[j] != '`'; j++)
                ;
            if (j == n) {
                i++;
                continue;
            }
            fill(cls, i, j + 1, GUI_TOK_STRING);
            i = j + 1;
        } else if (s[i] == '*' || s[i] == '_') {        /* emphasis: * or ** (or _), to the same again on the line */
            int w = i + 1 < n && s[i + 1] == s[i] ? 2 : 1;
            if (s[i] == '_' && i && word_char((unsigned char)s[i - 1])) {       /* (snake_case is no emphasis) */
                i += w;
                continue;
            }
            for (j = i + w; j + w <= n; j++)
                if (s[j] == s[i] && (w == 1 || s[j + 1] == s[i]) && s[j - 1] != ' ' && j > i + w)
                    break;
            if (j + w > n || i + w >= n || s[i + w] == ' ') {
                i += w;
                continue;
            }
            fill(cls, i, j + w, GUI_TOK_TYPE);
            i = j + w;
        } else if (s[i] == '[') {               /* a link: [its words](where to) */
            for (j = i + 1; j < n && s[j] != ']'; j++)
                ;
            if (j + 1 < n && s[j + 1] == '(') {
                for (k = j + 2; k < n && s[k] != ')'; k++)
                    ;
                if (k < n) {
                    fill(cls, i, j + 1, GUI_TOK_KEYWORD);
                    fill(cls, j + 1, k + 1, GUI_TOK_STRING);
                    i = k + 1;
                    continue;
                }
            }
            i++;
        } else if (s[i] == '<' && i + 4 <= n && !strncmp(s + i, "<!--", 4)) {  /* a comment, on one line */
            for (j = i + 4; j + 3 <= n && strncmp(s + j, "-->", 3); j++)
                ;
            j = j + 3 <= n ? j + 3 : n;
            fill(cls, i, j, GUI_TOK_COMMENT);
            i = j;
        } else {
            i++;
        }
    }
    return 0;
}

static const struct {
    const char *name;
    gui_lexer lex;
} kinds[] = {
    { "Text", NULL }, { "C", gui_code_lex_c }, { "Batch", lex_batch }, { "INI", lex_ini }, { "Assembly", lex_asm },
    { "Pascal", lex_pascal }, { "HTML", lex_html }, { "Markdown", lex_markdown },
};

gui_lexer lexer_kind(int i, const char **name)
{
    if (i < 0 || i >= (int)(sizeof kinds / sizeof kinds[0])) {
        *name = NULL;
        return NULL;
    }
    *name = kinds[i].name;
    return kinds[i].lex;
}

gui_lexer lexer_for(const char *path, const char **name)
{
    static const struct {
        const char *ext, *name;
        gui_lexer lex;
    } known[] = {
        { "C", "C", gui_code_lex_c }, { "H", "C", gui_code_lex_c }, { "CPP", "C", gui_code_lex_c },
        { "BAT", "Batch", lex_batch }, { "CMD", "Batch", lex_batch },
        { "INI", "INI", lex_ini }, { "CFG", "INI", lex_ini },
        { "ASM", "Assembly", lex_asm }, { "S", "Assembly", lex_asm }, { "INC", "Assembly", lex_asm },
        { "NAS", "Assembly", lex_asm },
        { "PAS", "Pascal", lex_pascal }, { "PP", "Pascal", lex_pascal },
        { "HTM", "HTML", lex_html }, { "HTML", "HTML", lex_html },
        { "MD", "Markdown", lex_markdown }, { "MARKDOWN", "Markdown", lex_markdown },
    };
    const char *dot = strrchr(path, '.'), *slash = strrchr(path, '\\');
    size_t i;
    if (name)
        *name = "Text";
    if (!dot || (slash && dot < slash))
        return NULL;
    for (i = 0; i < sizeof known / sizeof known[0]; i++)
        if (!strcasecmp(dot + 1, known[i].ext)) {
            if (name)
                *name = known[i].name;
            return known[i].lex;
        }
    return NULL;
}
