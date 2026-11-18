/* TEXT.SCR: a screen saver, a word in solid letters that turns and wanders (phase D, docs/v1/phase-d.md D6;
 * saver.h).
 *
 * The word ([saver] text, GLOS unless the user has set another) is made of cubes, a letter
 * five wide and seven high from the small table below, each cube's sides lit by which way they face. It swings
 * from side to side about its upright (never so far that it can't be read), tips a little, drifts across the
 * screen and back, and its colour goes slowly round. */
#include "saver.h"

/* Capitals and digits, five columns by seven rows, a row a byte (bit 4 the left column). */
static const unsigned char font[36][7] = {
    { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },    /* A B */
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },    /* C D */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },    /* E F */
    { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },    /* G H */
    { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C },    /* I J */
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },    /* K L */
    { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },    /* M N */
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },    /* O P */
    { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },    /* Q R */
    { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },    /* S T */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },    /* U V */
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 }, { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },    /* W X */
    { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },    /* Y Z */
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },    /* 0 1 */
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },    /* 2 3 */
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },    /* 4 5 */
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },    /* 6 7 */
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },    /* 8 9 */
};

static char word[16] = "GLOS";          /* ([saver] text: capitals and digits; a space is a gap) */

/* A colour of the wheel at h (0 to 6), bright. */
static void wheel(float h, float *c)
{
    int i = (int)h % 6;
    float f = h - (float)(int)h, up = 0.25f + 0.75f * f, down = 1.0f - 0.75f * f;
    c[0] = i == 0 || i == 5 ? 1.0f : i == 1 ? down : i == 4 ? up : 0.25f;
    c[1] = i == 1 || i == 2 ? 1.0f : i == 3 ? down : i == 0 ? up : 0.25f;
    c[2] = i == 3 || i == 4 ? 1.0f : i == 5 ? down : i == 2 ? up : 0.25f;
}

static void frame(float turn, float tip, float x, float y, float hue, float aspect)
{
    float c[3], wide, far;
    int n = (int)strlen(word), k, row, col;
    wheel(hue, c);
    wide = (float)(n * 6 - 1);
    far = wide > 23.0f ? 40.0f * wide / 23.0f : 40.0f;      /* (a longer word is further off: all of it in view) */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5f * aspect, 0.5f * aspect, -0.5f, 0.5f, 1.0f, 400.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(x * far / 40.0f, y * far / 40.0f, -far);
    glRotatef(tip, 1.0f, 0.0f, 0.0f);
    glRotatef(turn, 0.0f, 1.0f, 0.0f);
    glBegin(GL_TRIANGLES);
    for (k = 0; k < n; k++) {
        char ch = word[k];
        const unsigned char *g = ch >= 'A' && ch <= 'Z' ? font[ch - 'A'] : ch >= '0' && ch <= '9' ? font[26 + ch - '0'] : 0;
        if (!g)
            continue;
        for (row = 0; row < 7; row++)
            for (col = 0; col < 5; col++)
                if (g[row] >> (4 - col) & 1) {
                    float x0 = (float)(k * 6 + col) - wide * 0.5f, y0 = 3.0f - (float)row;
                    saver_box(x0, y0, -1.0f, x0 + 1.0f, y0 + 1.0f, 1.0f, c);
                }
    }
    glEnd();
}

int main(int argc, char **argv)
{
    int w, h, r = saver_begin(argc, argv, "Text", 1, &w, &h);
    float t = 0.0f, hue = 0.0f;
    char set[16];
    int i, is_set = 0;
    if (r)
        return r;
    set[0] = 0;
    if (glos_setting_get("saver", "text", set, sizeof set, &is_set) == 0 && set[0]) {
        for (i = 0; set[i] && i < (int)sizeof word - 1; i++)
            word[i] = (char)(set[i] >= 'a' && set[i] <= 'z' ? set[i] - 32 : set[i]);
        word[i] = 0;
    }
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    do {
        float s = (float)saver_ms() * 0.001f, a, b, c;
        t += s;
        hue += s * 0.12f;
        if (hue >= 6.0f)
            hue -= 6.0f;
        /* (there and back, each way in its own time: a triangle wave is all the wandering needs) */
        a = t * 0.11f - (float)(int)(t * 0.11f);
        b = t * 0.17f - (float)(int)(t * 0.17f);
        c = t * 0.23f - (float)(int)(t * 0.23f);
        a = a < 0.5f ? a * 4.0f - 1.0f : 3.0f - a * 4.0f;
        b = b < 0.5f ? b * 4.0f - 1.0f : 3.0f - b * 4.0f;
        c = c < 0.5f ? c * 4.0f - 1.0f : 3.0f - c * 4.0f;
        c = c * (1.5f - 0.5f * c * c);          /* (slower at the ends of its swing) */
        frame(c * 58.0f, b * 16.0f, a * 7.0f, b * 5.0f, hue, (float)w / (float)h);
    } while (saver_shown());
    return saver_done();
}
