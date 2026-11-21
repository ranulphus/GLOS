/* Locked PDFs opened, for the PDF Viewer (phase F; docs/v1/apps/pdf-viewer.md). Most locked files have no
   password to open them: they are encrypted only so that a reader may refuse to print or copy them, and their
   key is made from what the file itself says. Others want a password. The format's standard handler, all its
   kinds: RC4 with keys of 40 to 128 bits, AES of 128 bits, AES of 256 bits (both ways of making its key).
   Hashes and AES are BearSSL's (third_party/bearssl); RC4 is here. */
#ifndef PDFCRYPT_H
#define PDFCRYPT_H

#include "pdf.h"

struct pdf_crypt;

/* The file's lock tried with a password (NULL or "": none, which opens most): its "Encrypt" dictionary (read as
   it lies in the file: it is never locked itself) and the first of the trailer's two "ID" strings. The password
   is UTF-8; it may be the user's (which opens the file) or the owner's (which allows everything). NULL: it is
   not the file's password, or the file is locked in a way not known here, or its dictionary is damaged. */
struct pdf_crypt *pdf_crypt_open(struct pdf *d, struct pdf_obj *encrypt, const unsigned char *id, unsigned idn, const char *password);
void pdf_crypt_close(struct pdf_crypt *c);

/* A string or a stream of object num (generation gen) unlocked where it lies: its n bytes; how many they are
   after (AES's first block and its padding go). A stream's bytes are as the file has them, before any filter. */
unsigned long pdf_crypt_string(const struct pdf_crypt *c, unsigned num, unsigned gen, unsigned char *b, unsigned long n);
unsigned long pdf_crypt_stream(const struct pdf_crypt *c, unsigned num, unsigned gen, unsigned char *b, unsigned long n);
int pdf_crypt_meta(const struct pdf_crypt *c);         /* 1: the file's metadata streams are locked as the rest */

#endif
