; GLOS's COMMAND.COM (docs/v1/shell.md, step S4c): what a DOS program is given when it shells out under GLOS
; (COMMAND /C line, the C library's system(), a "DOS shell" in its menu). COMSPEC points here.
;
; There is no interpreter in it: the shell is the kernel's (kernel/shell/cmdcom.c, sdk/shell). This program
; tells the kernel it has begun and then asks what to do next, again and again, as GLOS.EXE's resident stub
; does (loader/stub.asm): start a program, make a DOS call, wait, or leave with an exit code. The programs it
; starts are its children, so the grandchildren of whoever shelled out, as under DOS; the DOS calls are made
; by this process, so its files and its standard output are the ones the calling program gave it.
;
; The calls are the stub's own instruction, ARPL (an invalid opcode in V86 mode), followed by "GCMD":
;   AX 1 BEGIN: BX = blk, DX:CX = a block for children's environments (segment, paragraphs; 0: none),
;               SI:DI = a block for the DOS calls' data (segment, paragraphs) -> AX 0
;   AX 2 NEXT:  EBX = how the last thing ended -> AX 1 EXEC blk's program, 2 wait, 3 DOS call, 0 leave
; Without GLOS the instruction is an invalid opcode all the same (INT 6): caught here, said, and left.
.386
_TEXT   segment use16 public 'CODE'
        assume  cs:_TEXT, ds:_TEXT, es:nothing, ss:_TEXT
        org     100h
start:
        mov     sp, offset stack_top
        mov     bx, offset stack_top + 15       ; a .COM owns all of memory: keep this much
        shr     bx, 4
        mov     ah, 4Ah
        int     21h
        mov     ax, 3506h                       ; INT 6, while the first call is tried
        int     21h
        mov     word ptr old6, bx
        mov     word ptr old6 + 2, es
        mov     dx, offset no_glos
        mov     ax, 2506h
        int     21h
        mov     dx, offset on_break             ; Ctrl+C is ours while we run (DOS puts the vector back after us)
        mov     ax, 2523h
        int     21h
        mov     bx, 256                         ; 4 KB for the environments of the programs it starts
        mov     ah, 48h
        int     21h
        jc      short xfer
        mov     env_seg, ax
        mov     word ptr env_par, 256
xfer:   mov     bx, 256                         ; and 4 KB for the DOS calls' data (1.5 KB where memory is short):
        mov     ah, 48h                         ; our own, not the DOS server's 32 KB, which the program that
        int     21h                             ; shelled out may not have to spare
        jnc     short got
        mov     bx, 96
        mov     ah, 48h
        int     21h
        jc      short begin                     ; (none: the kernel refuses)
        mov     bx, 96
        jmp     short got2
got:    mov     bx, 256
got2:   mov     xfer_seg, ax
        mov     xfer_par, bx
begin:  mov     ax, 1
        mov     bx, offset blk
        mov     cx, env_par
        mov     dx, env_seg
        mov     si, xfer_seg
        mov     di, xfer_par
        call    kcall
        push    ax
        push    ds
        lds     dx, old6                        ; (GLOS answered: the vector as it was)
        mov     ax, 2506h
        int     21h
        pop     ds
        pop     ax
        or      ax, ax
        jnz     refused
        mov     ebx, 0FFFFFFFFh                 ; nothing has ended yet
next:   mov     ax, 2
        call    kcall
        cmp     ax, 3
        je      dcall
        cmp     ax, 1
        je      exec
        cmp     ax, 2
        jne     quit
        sti                                     ; nothing yet: until an interrupt, or the kernel has something
        hlt
        mov     ebx, 0FFFFFFFFh
        jmp     next
quit:   mov     al, byte ptr b_code
        mov     ah, 4Ch
        int     21h

; A DOS call for the kernel's shell: the registers in blk, which then hold what came back.
dcall:  push    ds
        push    es
        mov     ax, b_regs[0]
        mov     bx, b_regs[2]
        mov     cx, b_regs[4]
        mov     dx, b_regs[6]
        mov     si, b_regs[8]
        mov     di, b_regs[10]
        mov     es, b_regs[14]
        mov     ds, cs:b_regs[12]
        int     21h
        mov     cs:b_regs[0], ax
        mov     cs:b_regs[2], bx
        mov     cs:b_regs[4], cx
        mov     cs:b_regs[6], dx
        mov     cs:b_regs[8], si
        mov     cs:b_regs[10], di
        mov     cs:b_regs[12], ds
        mov     cs:b_regs[14], es
        pushf
        pop     cs:b_regs[16]
        pop     es
        pop     ds
        mov     ebx, 0FFFFFFFEh                 ; a DOS call was made
        jmp     next

; A program started, its end waited for: EBX the word INT 21h 4Dh gives, or 10000h and DOS's error.
exec:   mov     ax, b_env
        mov     pblock, ax                      ; its environment (0: a copy of ours)
        mov     word ptr pblock + 2, offset b_tail
        mov     word ptr pblock + 4, cs
        mov     word ptr pblock + 6, 5Ch
        mov     word ptr pblock + 8, cs
        mov     word ptr pblock + 10, 6Ch
        mov     word ptr pblock + 12, cs
        mov     save_sp, sp
        mov     byte ptr in_exec, 1
        push    cs
        pop     es
        mov     bx, offset pblock
        mov     dx, offset b_path
        mov     ax, 4B00h
        int     21h
        cli                                     ; (DOS 2's EXEC keeps nothing: the stack and the segments again)
        mov     bx, cs
        mov     ss, bx
        mov     sp, cs:save_sp
        mov     ds, bx
        sti
        mov     byte ptr in_exec, 0
        jc      failed
        mov     ah, 4Dh
        int     21h
        movzx   ebx, ax
        jmp     next
failed: movzx   ebx, ax
        or      ebx, 10000h
        jmp     next

refused:
        mov     dx, offset s_refused
        jmp     short say
no_glos:                                        ; INT 6 at the first call: no GLOS under this DOS
        mov     ax, cs
        mov     ds, ax
        mov     ss, ax
        mov     sp, offset stack_top
        lds     dx, old6
        mov     ax, 2506h
        int     21h
        push    cs
        pop     ds
        mov     dx, offset s_none
say:    mov     ah, 9
        int     21h
        mov     ax, 4C01h
        int     21h

; Ctrl+C (INT 23h, from inside a DOS call). While a program we started runs and has no handler of its own, it
; is that program's end (carry set and a far return: DOS ends the process that made the call). While the DOS
; call is our own: noted for the kernel's shell (a batch file goes no further), and the call goes on (IRET).
; (The kernel's shell asks DOS for no console input while our standard input is a file, where DOS would say
; Ctrl+C at the file's end for ever.)
on_break:
        cmp     byte ptr cs:in_exec, 0
        jne     short brk_end
        mov     byte ptr cs:b_break, 1
        iret
brk_end:
        stc
        retf

kcall   proc    near
        db      63h, 0C0h                       ; arpl ax, ax
        db      'GCMD'
        ret
kcall   endp

s_none          db      'This COMMAND.COM is GLOS''s: it runs under GLOS only.', 13, 10, '$'
s_refused       db      'GLOS has no shell to give this COMMAND.COM (no memory, or too many deep).', 13, 10, '$'
old6            dd      0
env_seg         dw      0
env_par         dw      0
xfer_seg        dw      0
xfer_par        dw      0
save_sp         dw      0
in_exec         db      0
                db      0
pblock          dw      7 dup (0)

; ---- the block the kernel reads and writes (kernel/shell/cmdcom.c: B_REGS, B_PATH, B_TAIL, B_ENV, B_CODE)
blk     label   byte
b_regs          dw      9 dup (0)               ; AX BX CX DX SI DI DS ES, and FLAGS back
b_path          db      80 dup (0)              ; EXEC: the program
b_tail          db      130 dup (0)             ; ... its command tail: length, text, CR
b_env           dw      0                       ; ... its environment's segment (0: a copy of ours)
b_code          db      0                       ; the exit code to leave with
b_break         db      0                       ; set by on_break: Ctrl+C came while the DOS calls were ours

                dw      192 dup (0)             ; the stack
stack_top       label   word

_TEXT   ends
        end     start
