/* NOTEPAD's lexers against tables (apps/notepad/lexers.c; phase E): each line's classes as letters, a byte a
 * letter: . text, K keyword, T type, S string, N number, C comment, P preprocessor or directive, O operator. */
#include <stdio.h>
#include <string.h>

#include "../../apps/notepad/lexers.h"

int gui_code_lex_c(void *ctx, const char *line, int n, int state, unsigned char *cls)
{
    (void)ctx, (void)line, (void)n, (void)cls;
    return state;                               /* (the toolkit's own, tested with it: only its address is wanted here) */
}

static int fails;

static int line(const char *what, gui_lexer lex, int state, const char *text, const char *want, int want_state)
{
    static const char letter[] = ".KTSNCPO";
    unsigned char cls[200];
    char got[200];
    int n = (int)strlen(text), i, s;
    memset(cls, 0, sizeof cls);
    s = lex(NULL, text, n, state, cls);
    for (i = 0; i < n; i++)
        got[i] = cls[i] < 8 ? letter[cls[i]] : '?';
    got[n] = 0;
    if (strcmp(got, want) || s != want_state) {
        printf("lex_test: %s: \"%s\"\n  got  %s state %d\n  want %s state %d\n", what, text, got, s, want, want_state);
        fails++;
    }
    return s;
}

int main(void)
{
    const char *name;
    line("batch", lex_batch, 0, "@echo off", ".KKKK.KKK", 0);
    line("batch", lex_batch, 0, "rem a remark", "CCCCCCCCCCCC", 0);
    line("batch", lex_batch, 0, ":: another", "CCCCCCCCCC", 0);
    line("batch", lex_batch, 0, ":again", "PPPPPP", 0);
    line("batch", lex_batch, 0, "if \"%1\"==\"\" goto end", "KK.SSSSOOSS.KKKK....", 0);
    line("batch", lex_batch, 0, "set X=%PATH%;%%a 12", "KKK..OTTTTTT.TTT.NN", 0);
    line("batch", lex_batch, 0, "remark is no remark", "...................", 0);

    line("ini", lex_ini, 0, "; a comment", "CCCCCCCCCCC", 0);
    line("ini", lex_ini, 0, "[display]", "KKKKKKKKK", 0);
    line("ini", lex_ini, 0, "mode = 640x480x16", "TTTTTO...........", 0);
    line("ini", lex_ini, 0, "wait=15", "TTTTONN", 0);
    line("ini", lex_ini, 0, "name = \"a b\" 7", "TTTTTO.SSSSS.N", 0);
    line("ini", lex_ini, 0, "no equals here", "..............", 0);

    line("asm", lex_asm, 0, "start: mov eax, 1Fh ; go", ".....O.KKK.TTTO.NNN.CCCC", 0);
    line("asm", lex_asm, 0, "  db 'text', 0x10, 13", "..PP.SSSSSSO.NNNNO.NN", 0);
    line("asm", lex_asm, 0, "%define X 5", "PPPPPPP...N", 0);
    line("asm", lex_asm, 0, ".code", "PPPPP", 0);
    line("asm", lex_asm, 0, "jmp short [bx+si]", "KKK.TTTTT.OTTOTTO", 0);

    line("pascal", lex_pascal, 0, "begin x := $FF + 10; end.", "KKKKK...OO.NNN.O.NNO.KKKO", 0);
    line("pascal", lex_pascal, 0, "var s: string; { note }", "KKK..O.TTTTTTO.CCCCCCCC", 0);
    line("pascal", lex_pascal, 0, "writeln('it''s'); // end", ".......OSSSSSSSOO.CCCCCC", 0);
    line("pascal", lex_pascal, line("pascal", lex_pascal, 0, "a := 1; { open", "..OO.NO.CCCCCC", 1), "still } b := 2;",
         "CCCCCCC...OO.NO", 0);
    line("pascal", lex_pascal, line("pascal", lex_pascal, 0, "(* one", "CCCCCC", 2), "two *) end", "CCCCCC.KKK", 0);

    line("html", lex_html, 0, "<p class=\"x\">a &amp; b</p>", "KK.TTTTTOSSSK..NNNNN..KKKK", 0);
    line("html", lex_html, line("html", lex_html, 0, "text <!-- note", ".....CCCCCCCCC", 1), "more --> <br/>",
         "CCCCCCCC.KKKKK", 0);
    line("html", lex_html, line("html", lex_html, 0, "<a href=\"long", "KK.TTTTOSSSSS", 2 + '"'), "name\">x", "SSSSSK.", 0);
    line("html", lex_html, line("html", lex_html, 0, "<img", "KKKK", 2), " alt='y'>", ".TTTOSSSK", 0);

    line("markdown", lex_markdown, 0, "# A heading", "KKKKKKKKKKK", 0);
    line("markdown", lex_markdown, 0, "#no heading", "...........", 0);
    line("markdown", lex_markdown, 0, "- an *item* and `code`", "O....TTTTTT.....SSSSSS", 0);
    line("markdown", lex_markdown, 0, "12. **bold** snake_case_name", "OOO.TTTTTTTT................", 0);
    line("markdown", lex_markdown, 0, "> see [the page](http://x) <!-- n -->", "O.....KKKKKKKKKKSSSSSSSSSS.CCCCCCCCCC", 0);
    line("markdown", lex_markdown, 0, "* * *", "OOOOO", 0);
    line("markdown", lex_markdown, 0, "a * b and 2*3", ".............", 0);
    line("markdown", lex_markdown, 0, "an \\*escaped* star", "..................", 0);
    line("markdown", lex_markdown, line("markdown", lex_markdown, line("markdown", lex_markdown, 0, "```c", "PPPP", 1),
                                        "# not a heading *here*", "SSSSSSSSSSSSSSSSSSSSSS", 1), "```", "PPP", 0);
    if (lexer_kind(0, &name) != NULL || strcmp(name, "Text") || lexer_kind(7, &name) != lex_markdown
        || strcmp(name, "Markdown") || lexer_kind(8, &name) != NULL || name != NULL
        || lexer_for("README.MD", &name) != lex_markdown) {
        printf("lex_test: the kinds' list is wrong\n");
        fails++;
    }

    if (lexer_for("C:\\SRC\\MAIN.C", &name) != gui_code_lex_c || strcmp(name, "C") || lexer_for("run.bat", &name) != lex_batch
        || strcmp(name, "Batch") || lexer_for("C:\\GLOS\\GLOS.CFG", NULL) != lex_ini || lexer_for("X.ASM", NULL) != lex_asm
        || lexer_for("x.pas", NULL) != lex_pascal || lexer_for("INDEX.HTM", NULL) != lex_html
        || lexer_for("C:\\A.B\\README", &name) != NULL || strcmp(name, "Text") || lexer_for("NOTES.TXT", NULL) != NULL) {
        printf("lex_test: lexer_for: a file's kind is wrong\n");
        fails++;
    }
    printf("lex_test: %d failures\n", fails);
    return fails != 0;
}
