; GLOSLDR.SYS's loader, built with no DOS under it (-dNATIVE; docs/v1/native-dos.md, native boot, NB1): its
; entry, in place of Open Watcom's DOS start-up. loader/nprelude.S has applied its relocations and made its
; PSP: DS = ES = the PSP, SS:SP its stack (in DGROUP, small model), DL the BIOS's number of the disk it came
; from. DGROUP's order is set here, as the C library's start-up sets it, so that nbss_start and nbss_end bound
; the BSS to be cleared (no DOS has cleared memory here); _small_code_, which every module of the small
; model asks for, is defined here. Then native_main(); it never returns, and the C library's own start-up and
; initialisers (linked, as sscanf's multibyte support asks DOS for code pages there) never run.
.586p

DGROUP  group   _NULL, _AFTERNULL, CONST, CONST2, _DATA, _BSS, _BSSEND, STACK

_TEXT   segment word public 'CODE' use16
_TEXT   ends
_NULL   segment para public 'BEGDATA' use16
        dd      0                               ; (a null pointer's target is never real data)
_NULL   ends
_AFTERNULL segment word public 'BEGDATA' use16
_AFTERNULL ends
CONST   segment word public 'DATA' use16
CONST   ends
CONST2  segment word public 'DATA' use16
CONST2  ends
_DATA   segment word public 'DATA' use16
_DATA   ends
_BSS    segment word public 'BSS' use16
nbss_start label byte                           ; (this object is linked first: the BSS's first byte)
_BSS    ends
_BSSEND segment word public 'BSS' use16
nbss_end label  byte                            ; (after every module's _BSS)
_BSSEND ends
STACK   segment para stack 'STACK' use16
        db      4096 dup(?)
STACK   ends

        extrn   _native_main:near
        extrn   _native_psp:word
        extrn   _native_disk:byte
        extrn   _native_lba:dword

_TEXT   segment word public 'CODE' use16
        assume  cs:_TEXT, ds:DGROUP

        public  _nstart, _small_code_, main_
_small_code_ label near
main_   label   near                            ; (the C library's start-up is linked, dead, for sscanf's
                                                ; multibyte tables; it asks for a main it never calls here)
_nstart:
        mov     bx, es                          ; the PSP
        mov     ax, ss                          ; the stack as DGROUP sees it (the small model's SS = DS, as
        mov     si, DGROUP                      ; the C library's start-up makes it): the header's SS is the
        sub     ax, si                          ; STACK segment's own paragraph
        mov     cl, 4
        shl     ax, cl
        add     ax, sp
        cli
        mov     ss, si
        mov     sp, ax
        sti
        mov     ds, si
        mov     es, si
        mov     di, offset DGROUP:nbss_start    ; the BSS cleared
        mov     cx, offset DGROUP:nbss_end
        sub     cx, di
        xor     al, al
        cld
        rep     stosb
        mov     _native_psp, bx
        mov     _native_disk, dl
        mov     ax, 7000h                       ; the volume's first sector, where the prelude left it
        mov     fs, ax
        mov     eax, fs:[0070h]
        mov     _native_lba, eax
        call    _native_main
hang:   cli
        hlt
        jmp     hang

; The start-up menu's last step (loader/main.c, native_chain): into the few bytes it put at 8000:0000h, which
; start another system. (A far jump written out: the C compiler's call through a far pointer made of constants
; went elsewhere.)
        public  _native_far8000
_native_far8000:
        db      0EAh
        dw      0000h, 8000h
_TEXT   ends

        end     _nstart
