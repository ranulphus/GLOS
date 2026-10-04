; DPMIMINI.COM [/32]: the smallest DPMI client (M4a's first test,
; tests/loopa/jobs.py dpmi), in assembly so that only the host is under
; test. A 16-bit client, or with /32 a 32-bit one (HDPMI32 takes only
; those); its code stays 16-bit either way. On COM1:
;   HX-DPMI host flags=F cpu=C ver=V paras=P   INT 2Fh 1687h
;   HX-DPMI pm cs= ds= ss= es=                  after the mode switch
;   HX-DPMI ver=V flags=F cpu=C pic=P           0400h
;   HX-DPMI mem lin=L rw=ok                     0000h, 0501h, 0007h, 0008h,
;                                               writes and reads, 0502h, 0001h
;   HX-DPMI rm dos=V                            0300h: INT 21h 30h
;   HX-DPMI int21 dos=V                         INT 21h 30h, reflected
;   HX-DPMI vendor glos=0 dos4g=1               0A00h: CF for each name
;   HX-DPMI espfix hi=1234                      16-bit client only: ESP[31:16]
;                                               survives a trap on a 16-bit stack
;   HX-DPMI done
; then INT 21h 4C07h in protected mode. With no host: HX-DPMI none.
; A failed step prints HX-DPMI fail=STEP ax=AX and exits with 4C01h.
.386
_TEXT   segment use16 public 'CODE'
        assume  cs:_TEXT, ds:_TEXT, es:nothing, ss:_TEXT
        org     100h
start:
        mov     ah, 4Ah                 ; a .COM owns all of memory: keep 64 KB
        mov     bx, 1000h
        int     21h
        mov     si, 81h                 ; /32 on the command line: a 32-bit client
        mov     cl, ds:[80h]
        xor     ch, ch
scan:   jcxz    scanned
        lodsb
        dec     cx
        cmp     al, '3'
        jne     scan
        mov     byte ptr bits, 1
scanned:
        mov     ax, 1687h
        int     2Fh
        or      ax, ax
        jz      have
        mov     dx, offset s_none
        call    puts
        mov     ax, 4C00h
        int     21h
have:   mov     hostver, dx
        mov     word ptr entry, di
        mov     word ptr entry+2, es
        mov     paras, si
        mov     dx, offset s_host
        call    puts
        mov     ax, bx
        call    hex4
        mov     dx, offset s_cpu
        call    puts
        mov     al, cl
        call    hex2
        mov     dx, offset s_ver
        call    puts
        mov     ax, hostver
        call    hex4
        mov     dx, offset s_paras
        call    puts
        mov     ax, paras
        call    hex4
        call    crlf
        mov     bx, paras               ; the host's private memory
        or      bx, bx
        jz      nopriv
        mov     ah, 48h
        int     21h
        mov     byte ptr step, 'A'
        jc      fail
        mov     es, ax
nopriv: movzx   ax, bits                ; 0: a 16-bit client, 1: 32-bit
        call    dword ptr entry
        mov     byte ptr step, 'S'
        jc      fail

        mov     dx, offset s_pm         ; protected mode
        call    puts
        mov     ax, cs
        call    hex4
        mov     dx, offset s_ds
        call    puts
        mov     ax, ds
        call    hex4
        mov     dx, offset s_ss
        call    puts
        mov     ax, ss
        call    hex4
        mov     dx, offset s_es
        call    puts
        mov     ax, es
        call    hex4
        call    crlf

        mov     ax, 0400h
        int     31h
        mov     byte ptr step, 'V'
        jc      fail
        push    dx
        push    cx
        push    bx
        push    ax
        mov     dx, offset s_ver0
        call    puts
        pop     ax
        call    hex4
        mov     dx, offset s_flags
        call    puts
        pop     ax
        call    hex4
        mov     dx, offset s_cpu
        call    puts
        pop     ax
        call    hex2
        mov     dx, offset s_pic
        call    puts
        pop     ax
        call    hex4
        call    crlf

        xor     ax, ax                  ; 0000h: a descriptor
        mov     cx, 1
        int     31h
        mov     byte ptr step, 'D'
        jc      fail
        mov     sel, ax
        mov     ax, 0501h               ; 16 KB of linear memory
        xor     bx, bx
        mov     cx, 4000h
        int     31h
        mov     byte ptr step, 'M'
        jc      fail
        mov     linhi, bx
        mov     linlo, cx
        mov     hsi, si
        mov     hdi, di
        mov     ax, 0007h               ; the descriptor over it
        mov     bx, sel
        mov     cx, linhi
        mov     dx, linlo
        int     31h
        mov     byte ptr step, 'B'
        jc      fail
        mov     ax, 0008h
        mov     bx, sel
        xor     cx, cx
        mov     dx, 3FFFh
        int     31h
        mov     byte ptr step, 'L'
        jc      fail
        mov     es, sel
        mov     word ptr es:[0], 1234h
        mov     word ptr es:[3FFEh], 5678h
        mov     byte ptr step, 'R'
        cmp     word ptr es:[0], 1234h
        jne     fail
        cmp     word ptr es:[3FFEh], 5678h
        jne     fail
        push    ds
        pop     es
        mov     dx, offset s_mem
        call    puts
        mov     ax, linhi
        call    hex4
        mov     ax, linlo
        call    hex4
        mov     dx, offset s_rwok
        call    puts
        mov     ax, 0502h
        mov     si, hsi
        mov     di, hdi
        int     31h
        mov     byte ptr step, 'F'
        jc      fail
        mov     ax, 0001h
        mov     bx, sel
        int     31h
        mov     byte ptr step, 'X'
        jc      fail

        mov     word ptr rm_eax, 3000h  ; 0300h: INT 21h 30h in real mode
        push    ds
        pop     es
        xor     edi, edi                ; ES:EDI for a 32-bit client
        mov     di, offset rmregs
        mov     ax, 0300h
        mov     bx, 21h
        xor     cx, cx
        int     31h
        mov     byte ptr step, 'C'
        jc      fail
        mov     dx, offset s_rm
        call    puts
        mov     ax, word ptr rm_eax
        call    hex4
        call    crlf

        mov     ax, 3000h               ; the same, reflected by the host
        int     21h
        push    ax
        mov     dx, offset s_i21
        call    puts
        pop     ax
        call    hex4
        call    crlf

        mov     dx, offset s_vend
        call    puts
        xor     esi, esi
        mov     si, offset s_glos
        mov     ax, 0A00h
        int     31h
        sbb     al, al
        and     al, 1
        call    hex1
        mov     dx, offset s_d4g
        call    puts
        xor     esi, esi
        mov     si, offset s_rational
        mov     ax, 0A00h
        int     31h
        sbb     al, al
        and     al, 1
        call    hex1
        call    crlf

        cmp     byte ptr bits, 0            ; a 16-bit stack (a 32-bit client's is Big)
        jne     noesp
        mov     eax, esp
        and     eax, 0FFFFh
        or      eax, 12340000h
        mov     esp, eax                ; SP as it was: a 16-bit stack only uses SP
        mov     ax, 0400h
        int     31h                     ; the host returns on a 16-bit stack: espfix
        mov     eax, esp
        shr     eax, 16
        and     esp, 0FFFFh
        push    ax
        mov     dx, offset s_esp
        call    puts
        pop     ax
        call    hex4
        call    crlf
noesp:
        mov     dx, offset s_done
        call    puts
        mov     ax, 4C07h
        int     21h

fail:   push    ax
        mov     dx, offset s_fail
        call    puts
        mov     al, step
        call    putc
        mov     dx, offset s_ax
        call    puts
        pop     ax
        call    hex4
        call    crlf
        mov     ax, 4C01h
        int     21h

; ---- COM1, polled (the data port traps; GLOS emulates it in either mode)
putc:   push    dx
        push    ax
        mov     dx, 3FDh
@@:     in      al, dx
        test    al, 20h
        jz      @b
        pop     ax
        mov     dx, 3F8h
        out     dx, al
        pop     dx
        ret
puts:   push    si
        push    ax
        mov     si, dx
@@:     lodsb
        or      al, al
        jz      @f
        call    putc
        jmp     @b
@@:     pop     ax
        pop     si
        ret
crlf:   mov     al, 13
        call    putc
        mov     al, 10
        jmp     putc
hex1:   and     al, 15
        add     al, '0'
        cmp     al, '9'
        jbe     putc
        add     al, 'a' - '9' - 1
        jmp     putc
hex2:   push    ax
        shr     al, 4
        call    hex1
        pop     ax
        jmp     hex1
hex4:   push    ax
        mov     al, ah
        call    hex2
        pop     ax
        jmp     hex2

entry   dd      0
paras   dw      0
hostver dw      0
bits    db      0
sel     dw      0
linhi   dw      0
linlo   dw      0
hsi     dw      0
hdi     dw      0
step    db      0
rmregs  label byte                      ; 0300h's structure: 50 bytes
rm_edi  dd      0
rm_esi  dd      0
rm_ebp  dd      0
        dd      0
rm_ebx  dd      0
rm_edx  dd      0
rm_ecx  dd      0
rm_eax  dd      0
rm_flags dw     0
        dw      0, 0, 0, 0, 0, 0
rm_sp   dw      0
rm_ss   dw      0
s_none  db      'HX-DPMI none', 13, 10, 0
s_host  db      'HX-DPMI host flags=', 0
s_cpu   db      ' cpu=', 0
s_ver   db      ' ver=', 0
s_paras db      ' paras=', 0
s_pm    db      'HX-DPMI pm cs=', 0
s_ds    db      ' ds=', 0
s_ss    db      ' ss=', 0
s_es    db      ' es=', 0
s_ver0  db      'HX-DPMI ver=', 0
s_flags db      ' flags=', 0
s_pic   db      ' pic=', 0
s_mem   db      'HX-DPMI mem lin=', 0
s_rwok  db      ' rw=ok', 13, 10, 0
s_rm    db      'HX-DPMI rm dos=', 0
s_i21   db      'HX-DPMI int21 dos=', 0
s_vend  db      'HX-DPMI vendor glos=', 0
s_d4g   db      ' dos4g=', 0
s_glos  db      'GLOS', 0
s_rational db   'RATIONAL DOS/4G', 0
s_esp   db      'HX-DPMI espfix hi=', 0
s_done  db      'HX-DPMI done', 13, 10, 0
s_fail  db      'HX-DPMI fail=', 0
s_ax    db      ' ax=', 0
_TEXT   ends
        end     start
