/* HTML read for the Web Reader (phase F; docs/v1/apps/web-reader.md): a page's bytes made into what a reader
   shows: blocks of styled text with links, headings, lists, quotes, preformatted text, simple tables, rules, and
   the places of pictures. GLOS's own reader in the manner of HTML5's tokenizer: whatever the bytes are, a page
   comes out of them. No scripts are run and no styles' sheets are read (a few styles written on the elements
   themselves are heeded: colours, and what is not to be shown). No tree is kept: the page is laid out as it is
   read, into calls the app turns into a rich-text document (include/glos/gui.h's gui_rdoc), and the host's
   tests into lines of text. */
#ifndef HTML_H
#define HTML_H

/* A block's kind, and a run's style: the toolkit's own numbers (GUI_RB_, GUI_RS_), so the app hands them on. */
enum { HTML_PARA, HTML_HEAD1, HTML_HEAD2, HTML_HEAD3, HTML_BULLET, HTML_NUMBER, HTML_QUOTE, HTML_CODE, HTML_RULE, HTML_ROW };
#define HTML_BOLD   1
#define HTML_ITALIC 2
#define HTML_MONO   4
#define HTML_STRIKE 8

struct html_sink {
    void (*block)(void *ctx, int kind, int depth);      /* a new block: the text after it is its (depth: how deep in
                                                           lists and quotes, from 0) */
    void (*text)(void *ctx, const char *text, int n, unsigned style, const char *link);        /* UTF-8; link: where
                                                           it leads, made whole against the page's address, or NULL */
    void (*cell)(void *ctx);                            /* (in an HTML_ROW) its next cell begins */
    void (*colour)(void *ctx, long rgb);                /* the text after it in this colour; -1: as it would be */
    void (*image)(void *ctx, const char *src, const char *alt, int w, int h, const char *link); /* a picture's place:
                                                           its address made whole, the words that stand for it, the
                                                           size the page says (0: not said) */
    void (*title)(void *ctx, const char *title);        /* the page's title */
    void (*anchor)(void *ctx, const char *name);        /* here is what "#name" leads to */
    void (*field)(void *ctx, const char *kind, const char *name, const char *value);   /* a form's field (for the
                                                           forms to come; shown as a word in brackets for now) */
    void *ctx;
};

/* A page read. charset: what its sender said of its characters ("" or NULL: not said: the page's own <meta> is
   looked for, and its bytes looked at); base: the page's address, against which its links are made whole. Any
   of the sink's functions may be NULL. */
void html_read(const char *bytes, unsigned long n, const char *charset, const char *base, const struct html_sink *sink);

/* Bytes in one of the Latin sets (Windows-1252, which ISO 8859-1 is read as) to UTF-8: malloc'd, *len bytes. */
char *html_latin_utf8(const char *bytes, unsigned long n, unsigned long *len);

#endif
