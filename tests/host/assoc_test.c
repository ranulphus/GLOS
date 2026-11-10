/* Host test for the kinds of file and what opens them (sdk/config/assoc.c; API 2.0 step 8). */
#include <stdio.h>
#include <string.h>
#include <glos/assoc.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static char k[ASSOC_EXT], out[ASSOC_VERBS], label[ASSOC_LABEL], cmd[128], line[160];

static int kind(const char *p) { memset(k, 'x', sizeof k); return assoc_kind(p, k); }

int main(void)
{
    char big[ASSOC_VERBS + 40];
    int i;

    /* A file's kind: its extension, upper case, with its dot; a path's dots before the name don't count. */
    CHECK(kind("C:\\DOCS\\A.txt") == 0 && !strcmp(k, ".TXT"));
    CHECK(kind("a.b.Jpg") == 0 && !strcmp(k, ".JPG"));
    CHECK(kind(".zip") == 0 && !strcmp(k, ".ZIP") && kind("zip") == 0 && !strcmp(k, ".ZIP") && kind("c") == 0 && !strcmp(k, ".C"));
    CHECK(kind("C:\\DIR.EXT\\README") == -1 && kind("C:\\DOCS\\") == -1 && kind("README") == -1 && kind("") == -1);
    CHECK(kind("A.HTML") == -1 && kind("A.") == -1 && kind("C:/x/y.z") == 0 && !strcmp(k, ".Z") && kind("C:A.MID") == 0);
    CHECK(kind("http://example.org/page.html") == 0 && !strcmp(k, ".URL") && kind("HTTPS://x") == 0 && !strcmp(k, ".URL"));
    CHECK(assoc_link("http://x") && assoc_link("Https://x") && !assoc_link("C:\\HTTP.TXT") && !assoc_link("ftp://x"));
    CHECK(assoc_program(".EXE") && assoc_program(".COM") && assoc_program(".BAT") && !assoc_program(".TXT") && !assoc_program(".EX"));

    /* Verbs in a program's profile: none; added; one changed in place; taken out; read back in order. */
    CHECK(assoc_verb_name(".zip", "Add to ZIP", label) == 0 && !strcmp(label, ".ZIP:Add to ZIP"));
    CHECK(assoc_verb_name("ZIP", "x", label) == -1 && assoc_verb_name(".ZIPPY", "x", label) == -1 && assoc_verb_name(".Z", "", label) == -1
          && assoc_verb_name(".Z", "a:b", label) == -1 && assoc_verb_name(".Z", "a=b", label) == -1
          && assoc_verb_name(".ZIP", "A label that is far too long to be", label) == -1);
    CHECK(assoc_verb_count("") == 0 && assoc_verb_get("", 0, label, sizeof label, cmd, sizeof cmd) == -1);
    CHECK(assoc_verb_set("", ".ZIP:Add to ZIP", "-a", out) == 0 && !strcmp(out, ".ZIP:Add to ZIP=-a"));
    strcpy(line, out);
    CHECK(assoc_verb_set(line, ".MID:Play", "", out) == 0 && !strcmp(out, ".ZIP:Add to ZIP=-a;.MID:Play="));
    strcpy(line, out);
    CHECK(assoc_verb_count(line) == 2);
    CHECK(assoc_verb_get(line, 0, label, sizeof label, cmd, sizeof cmd) == 0 && !strcmp(label, ".ZIP:Add to ZIP") && !strcmp(cmd, "-a"));
    CHECK(assoc_verb_get(line, 1, label, sizeof label, cmd, sizeof cmd) == 0 && !strcmp(label, ".MID:Play") && !cmd[0]);
    CHECK(assoc_verb_get(line, 2, label, sizeof label, cmd, sizeof cmd) == -1 && assoc_verb_get(line, -1, label, 24, cmd, 128) == -1);
    CHECK(assoc_verb_get(line, 0, label, 5, cmd, sizeof cmd) == -1 && assoc_verb_get(line, 0, label, sizeof label, cmd, 2) == -1);
    CHECK(assoc_verb_set(line, ".zip:add TO zip", "-m", out) == 0 && !strcmp(out, ".zip:add TO zip=-m;.MID:Play="));
    CHECK(assoc_verb_set(line, ".ZIP:Add to ZIP", 0, out) == 0 && !strcmp(out, ".MID:Play="));
    CHECK(assoc_verb_set(out, ".MID:Play", 0, big) == 0 && !big[0]);
    CHECK(assoc_verb_set(line, ".X:Nothing", 0, out) == -1);                    /* nothing to take out */
    /* Refused: bad names and arguments; no room. A value with rubbish in it: what is a verb is kept. */
    CHECK(assoc_verb_set("", "", "-x", out) == -1 && assoc_verb_set("", "A=B", "-x", out) == -1
          && assoc_verb_set("", "A;B", "-x", out) == -1 && assoc_verb_set("", "Name", "-x;-y", out) == -1
          && assoc_verb_set("", "A name that is far too long for a verb", "-x", out) == -1);
    CHECK(assoc_verb_count("rubbish;A=B;;=C;D=;E=F") == 3);
    CHECK(assoc_verb_set("rubbish;A=B;;=C;D=;E=F", "G", "H", out) == 0 && !strcmp(out, "A=B;D=;E=F;G=H"));
    memset(big, 0, sizeof big);
    for (i = 0; i < 8; i++)
        sprintf(big + strlen(big), "%s.T%d:Verb%d=-option%d -x", i ? ";" : "", i, i, i);
    CHECK(strlen(big) < ASSOC_VERBS && assoc_verb_count(big) == 8);
    CHECK(assoc_verb_set(big, ".TXT:One more", "-with -many -more -arguments", out) == -1);
    CHECK(assoc_verb_set(big, ".T3:Verb3", 0, out) == 0 && assoc_verb_count(out) == 7);

    /* The command: a program named alone is in GLOS's folder; one with a folder, or a drive, is where it says. */
    CHECK(assoc_command("NOTEPAD.EXE", "C:\\DOCS\\A.TXT", "C:\\GLOS", line, sizeof line) == 33
          && !strcmp(line, "C:\\GLOS\\NOTEPAD.EXE C:\\DOCS\\A.TXT"));
    CHECK(assoc_command("ZIP.EXE -a", "C:\\A.TXT", "C:\\GLOS\\", line, sizeof line) > 0 && !strcmp(line, "C:\\GLOS\\ZIP.EXE -a C:\\A.TXT"));
    CHECK(assoc_command("D:\\APPS\\P.EXE", "X.MID", "C:\\GLOS", line, sizeof line) > 0 && !strcmp(line, "D:\\APPS\\P.EXE X.MID"));
    CHECK(assoc_command("P.EXE", "", "C:\\GLOS", line, sizeof line) > 0 && !strcmp(line, "C:\\GLOS\\P.EXE"));
    CHECK(assoc_command("P.EXE", "X", "", line, sizeof line) > 0 && !strcmp(line, "P.EXE X"));
    CHECK(assoc_command("", "X", "C:\\GLOS", line, sizeof line) == -1 && assoc_command("NOTEPAD.EXE", "C:\\A.TXT", "C:\\GLOS", line, 20) == -1);

    printf("assoc_test: %d failures\n", fails);
    return fails != 0;
}
