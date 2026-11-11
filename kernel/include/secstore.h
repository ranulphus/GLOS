/* The secrets store's arithmetic and format (kernel/core/secstore.c; API 2.0's SECRETS, phase B step 13): no files
 * and no dialogs here: kernel/core/secrets.c has those (tests/host/secstore_test.c). */
#ifndef K_SECSTORE_H
#define K_SECSTORE_H
#include "types.h"

/* Keys. */
void hmac_sha256(const u8 *key, u32 klen, const u8 *msg, u32 mlen, u8 out[32]);       /* (msg: 192 bytes at most) */
void pbkdf2_sha256(const u8 *pw, u32 pwlen, const u8 *salt, u32 saltlen, u32 iters, u8 out[32]);      /* (salt: 32 at most) */

/* The store in memory: records one after another, each
     kind (1), owner's length (1), name's length (1), value's length (2, low byte first), owner, name, value.
   A secret's owner is its program's file name ("TERM.EXE"), or "*" for one that is shared; a grant (the user let
   a program use a shared secret) has the program as owner, the secret's name, and no value. */
#define SS_SECRET 1
#define SS_GRANT  2
#define SS_NAME_MAX  63
#define SS_VALUE_MAX 1024
#define SS_ROOM      (16 * 1024)        /* all of them together */
struct secstore {
    u32 len;
    u8 b[SS_ROOM];
};
int ss_put(struct secstore *s, int kind, const char *owner, const char *name, const u8 *value, u32 vlen); /* in place
                                           of one of the same kind, owner and name: 0; -1 no room; -2 too long */
int ss_get(const struct secstore *s, int kind, const char *owner, const char *name, u8 *out, u32 room, u32 *vlen);
                                        /* 0 (as much as fits copied; *vlen its whole length), -1 none */
int ss_remove(struct secstore *s, int kind, const char *owner, const char *name);      /* 0, -1 none */
u32 ss_count(const struct secstore *s, int kind);

/* The file: a header, the records encrypted, a tag over both.
     "GLOSSEC1" (8), flags (4), iterations (4), salt (16), nonce (8), length (4) | ciphertext | tag (16)
   ChaCha20 with the key and the nonce makes the stream: its first 32 bytes are Poly1305's key for this file, the
   records are XORed with it from byte 64 on (as OpenSSH's chacha20-poly1305 does a packet), and the tag is
   Poly1305 of the header and the ciphertext. A nonce is used for one writing only. */
#define SS_HEAD 44
#define SS_TAG  16
#define SS_FILE_MAX (SS_HEAD + SS_ROOM + SS_TAG)
#define SS_F_PASSWORD 1                 /* the key is derived from a master password (else it is the PC's own) */
struct ss_head {
    u32 flags, iters, length;
    u8 salt[16], nonce[8];
};
int ss_head(const u8 *file, u32 len, struct ss_head *h);        /* 0, or -1: not a store's file */
u32 ss_seal(const struct secstore *s, const struct ss_head *h, const u8 key[32], u8 *file);    /* the file's bytes
                                           (SS_FILE_MAX at most): how many */
int ss_open(const u8 *file, u32 len, const u8 key[32], struct secstore *s);    /* 0; -1 not a store's file; -2 the
                                           key is wrong (or the file was changed) */

#endif
