/* Ways about a PDF (pdfnav.h): the outline and destinations.
 *
 * The outline is a chain of dictionaries from the catalog's "Outlines": each has a "Title", a "Next", maybe a
 * "First" (the chain under it) and a "Count" (above 0: shown open), and where it leads: a "Dest", or an action
 * "A" of the kind "GoTo" with its "D". A destination is an array whose first item is the page (a reference to
 * it), or a name or a string looked up: names in the catalog's "Dests" dictionary, strings in the tree of names
 * under the catalog's "Names", "Dests" (nodes with "Kids" and the "Limits" their keys lie between, leaves with
 * "Names": key, value, key, value). The file is whatever its writer made it: chains that never end and trees
 * that go round are cut by counting. */
#include <stdlib.h>
#include <string.h>

#include "pdfnav.h"

#define MAX_MARKS 4000u

static int order(const char *a, unsigned an, const char *b, unsigned bn)       /* as the tree of names orders its keys */
{
    int c = memcmp(a, b, an < bn ? an : bn);
    return c ? c : an < bn ? -1 : an > bn;
}

static struct pdf_obj *tree_find(struct pdf *d, struct pdf_obj *node, const char *key, unsigned len, int depth)
{
    struct pdf_obj *names = pdf_get(d, node, "Names"), *kids = pdf_get(d, node, "Kids");
    unsigned i;
    if (depth > 12)
        return NULL;
    if (pdf_is(names, PDF_ARRAY))
        for (i = 0; i + 1 < names->n; i += 2) {
            unsigned n = 0;
            const char *s = pdf_string(pdf_item(d, names, i), &n);
            if (n == len && !memcmp(s, key, len))
                return pdf_item(d, names, i + 1);
        }
    if (pdf_is(kids, PDF_ARRAY))
        for (i = 0; i < kids->n && i < 4096; i++) {
            struct pdf_obj *kid = pdf_item(d, kids, i), *limits = pdf_get(d, kid, "Limits"), *found;
            if (pdf_is(limits, PDF_ARRAY) && limits->n >= 2) {
                unsigned an = 0, bn = 0;
                const char *a = pdf_string(pdf_item(d, limits, 0), &an), *b;
                b = pdf_string(pdf_item(d, limits, 1), &bn);
                if (order(key, len, a, an) < 0 || order(key, len, b, bn) > 0)
                    continue;
            }
            if ((found = tree_find(d, kid, key, len, depth + 1)) != NULL)
                return found;
        }
    return NULL;
}

long pdf_dest_page(struct pdf *d, struct pdf_obj *dest)
{
    int hops;
    for (hops = 0; hops < 4; hops++) {
        dest = pdf_resolve(d, dest);
        if (pdf_is(dest, PDF_ARRAY)) {          /* written out: [page /XYZ left top zoom] and its like */
            const struct pdf_obj *first = dest->n ? dest->u.item[0] : NULL;
            if (first && first->type == PDF_REF)
                return pdf_page_number(d, first->u.ref.num);
            if (pdf_is(first, PDF_NUMBER) && first->u.num >= 0 && first->u.num < pdf_pages(d))
                return (long)first->u.num;      /* (a page by its number: meant for another file's, seen for this one's) */
            return -1;
        }
        if (pdf_is(dest, PDF_DICT)) {           /* (a named one's value may be a dictionary with the array as "D") */
            dest = pdf_get(d, dest, "D");
        } else if (pdf_is(dest, PDF_NAME)) {
            dest = pdf_get(d, pdf_get(d, pdf_catalog(d), "Dests"), pdf_name(dest));
        } else if (pdf_is(dest, PDF_STRING)) {
            unsigned n = 0;
            const char *s = pdf_string(dest, &n);
            dest = tree_find(d, pdf_get(d, pdf_get(d, pdf_catalog(d), "Names"), "Dests"), s, n, 0);
        } else {
            return -1;
        }
    }
    return -1;
}

struct marks {
    struct pdf_mark *m;
    unsigned n, room, steps;
    int failed;
};

static void chain(struct pdf *d, struct pdf_obj *item, int depth, struct marks *out)
{
    for (; pdf_is(item, PDF_DICT) && out->n < MAX_MARKS && out->steps < 4 * MAX_MARKS && !out->failed; item = pdf_get(d, item, "Next")) {
        struct pdf_obj *dest = pdf_get(d, item, "Dest"), *a = pdf_get(d, item, "A"), *first;
        struct pdf_mark *m;
        out->steps++;
        if (!pdf_is(pdf_get(d, item, "Title"), PDF_STRING))    /* (no entry: the top itself given as one, or damage) */
            continue;
        if (out->n == out->room) {
            unsigned room = out->room ? out->room * 2 : 64;
            if (!(m = realloc(out->m, room * sizeof *m))) {
                out->failed = 1;
                return;
            }
            out->m = m, out->room = room;
        }
        m = &out->m[out->n];
        if (!(m->title = pdf_text_string(pdf_get(d, item, "Title")))) {
            out->failed = 1;
            return;
        }
        if (!dest && pdf_is(a, PDF_DICT) && pdf_name_is(pdf_get(d, a, "S"), "GoTo"))
            dest = pdf_get(d, a, "D");
        m->depth = depth, m->open = pdf_int(pdf_get(d, item, "Count"), 0) > 0, m->page = pdf_dest_page(d, dest);
        out->n++;
        first = pdf_get(d, item, "First");
        if (depth < 15 && pdf_is(first, PDF_DICT))
            chain(d, first, depth + 1, out);
    }
}

int pdf_outline(struct pdf *d, struct pdf_mark **marks, unsigned *count)
{
    struct marks out;
    memset(&out, 0, sizeof out);
    *marks = NULL, *count = 0;
    chain(d, pdf_get(d, pdf_get(d, pdf_catalog(d), "Outlines"), "First"), 0, &out);
    if (out.failed) {
        pdf_marks_free(out.m, out.n);
        return PDF_E_MEMORY;
    }
    *marks = out.m, *count = out.n;
    return PDF_OK;
}

void pdf_marks_free(struct pdf_mark *m, unsigned count)
{
    unsigned i;
    for (i = 0; m && i < count; i++)
        free(m[i].title);
    free(m);
}
