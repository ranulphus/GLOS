/* A session's log (phase F; the owner's choice of 2026-11-20, as PuTTY has it): what a session writes, kept in
   a text file as it comes. What is kept is what can be read: the terminal's sequences (colours, the cursor's
   moves, titles) and the control characters are left out; the lines end as the file's system has them. A log
   that is there already is added to, under a line that says when this part began.

   One log a program (a program is one session): SSH.EXE's, and the other session programs' as they come. */
#ifndef SESSLOG_H
#define SESSLOG_H

int sesslog_open(const char *path, const char *what);   /* what: the session, for the first line ("ssh office"):
                                                           0, or -1 (the file can't be written) */
void sesslog_write(const void *data, int n);            /* the session's bytes as they came (nothing open: nothing) */
void sesslog_close(void);
int sesslog_on(void);

/* The part that chooses: bytes as a terminal is sent them in, the text of them out (out has room for n bytes at
   least: nothing is added). state is the caller's, 0 at first. How many bytes came out. */
int sesslog_text(unsigned *state, const unsigned char *in, int n, char *out);

#endif
