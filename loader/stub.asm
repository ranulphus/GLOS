; GLOS.EXE's resident stub (docs/supervisor.md §2.2): everything of GLOS
; that must stay in conventional memory once the kernel runs. It is written
; to run from any segment (all data through CS, no segment fixups), because
; going resident copies it down to the paragraph after GLOS.EXE's PSP and
; frees the rest of GLOS.EXE: the C code, its runtime and its data.
;
; Far procedures for the C code, before going resident:
;   unsigned long pm_enter(unsigned gdtr, unsigned long cr3, unsigned long src,
;                          unsigned long dst, unsigned long file_dwords,
;                          unsigned long bss_dwords, unsigned long entry,
;                          unsigned long arg)
;       enters protected mode with paging (this code is identity-mapped),
;       copies the kernel image from src to dst (linear) and zeroes its bss,
;       and jumps to entry (CS=08h, DS=ES=SS=10h, ESI=arg). The kernel comes
;       back with a far jump to 38h:_pm_ret, EAX = its result, interrupts off;
;       that leaves protected mode and returns EAX in DX:AX. When the kernel
;       keeps DOS running, it instead resumes this procedure's caller in V86
;       mode at _vm_resume, which returns 10000h.
;   unsigned long glos_call(unsigned fn, unsigned long arg)
;       a call into the kernel from V86 mode (the ARPL at _glos_bp_call
;       raises #UD there). LEAVE comes back through _pm_ret, in real mode.
;   void stub_resident(void)
;       does not return: copies the stub down, tells the kernel (RESIDENT),
;       continues in the copy, shrinks GLOS.EXE's memory block to the PSP and
;       the stub, installs the master environment the C code staged (as the
;       shell), and from then on runs what the kernel says (NEXT): EXEC the
;       program in stub_data, report how it ended, ask again; or, headless
;       with nothing to run, halt until an interrupt and ask again. When the
;       kernel leaves, _pm_ret ends here too: A20 and XMS as found, the
;       GLOS-EXIT line on COM1, INT 21h 4Ch with the kernel's code; as the
;       shell, COMSPEC with the fallback tail instead, again and again.
;       With realmode set there is no kernel at all: the same, without the
;       kernel calls (the shell's fallback when GLOS can't start).
; Entry points for the kernel and programs (offsets in bootinfo):
;   _glos_xms_entry  the XMS entry under GLOS: five bytes for hooks, then the
;                    ARPL at _glos_bp_xms; the kernel serves the call and
;                    resumes at the RETF
;   _glos_kill       where the kernel sends a killed program: INT 21h 4CFFh
;                    as that program, on the stack below _glos_kill_top
.586p

GLOS_CALL_NEXT     equ 4
GLOS_CALL_RESIDENT equ 5

GLOSSTUB segment para public 'STUB' use16
        assume  cs:GLOSSTUB, ds:nothing, es:nothing, ss:nothing

        public  _stub_data, _pm_enter, _pm_ret, _vm_resume, _vm_state, _glos_call, _glos_bp_call
        public  _glos_xms_entry, _glos_bp_xms, _glos_kill, _glos_kill_top, _stub_resident, _stub_end

stub_start:

; ---- what the C code fills in (struct stub_data, include/glos/bootinfo.h)
_stub_data label byte
sd_mode         db      0               ; 1: XMS mode
sd_a20init      db      0               ; A20 was on when GLOS started
sd_resident     db      0               ; set once resident: _pm_ret then exits
sd_nxms         db      0
sd_shell        db      0               ; GLOS is the DOS shell: it never ends
sd_realmode     db      0               ; no kernel: EXEC path/tail again and again
                db      0, 0
sd_xms          dd      0               ; the XMS driver's entry
sd_handles      dw      4 dup (0)
sd_env_src      dd      0               ; the master environment the C code staged
sd_env_len      dw      0
sd_env_paras    dw      0               ; 0: keep the environment we have
sd_path         db      80 dup (0)      ; EXEC: the program, ASCIIZ
sd_tail         db      128 dup (0)     ; EXEC: length, text, CR
sd_comspec      db      80 dup (0)      ; the shell's fallback: COMSPEC ...
sd_fbtail       db      64 dup (0)      ; ... with this tail
sd_dregs        dw      9 dup (0)       ; NEXT 3: AX BX CX DX SI DI DS ES in, the same and FLAGS out

; ---- the stub's own data
_vm_state label word                    ; the kernel reads these three to resume
save_ss         dw      0
save_sp         dw      0
save_ds         dw      0
new_seg         dw      0
psp_seg         dw      0
exec_sp         dw      0
exit_code       db      0
                db      0
pm_esp          dd      0
p_src           dd      0
p_dst           dd      0
p_filedw        dd      0
p_bssdw         dd      0
p_entry         dd      0
p_arg           dd      0
rm_idtr         dw      3FFh            ; the real-mode interrupt vector table
                dd      0
pblock          dw      0               ; EXEC parameter block: environment (0: a copy of ours)
                dw      offset sd_tail, 0
                dw      offset fcb1, 0
                dw      offset fcb2, 0
fcb1            db      37 dup (0)
fcb2            db      37 dup (0)
msg_exit        db      'GLOS-EXIT code=', 0
msg_a20         db      ' a20=', 0
msg_crlf        db      13, 10, 0
msg_noshell     db      'GLOS-SHELL error=cannot-run', 13, 10, 0

; ---- pm_enter
_pm_enter proc far
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        pushf
        cli
        mov     cs:save_ss, ss
        mov     cs:save_sp, sp
        mov     cs:save_ds, ds
        mov     ax, cs
        mov     cs:rm_cs_imm, ax
        xor     eax, eax                ; the kernel's first stack: below ours
        mov     ax, ss
        shl     eax, 4
        movzx   ecx, sp
        add     eax, ecx
        sub     eax, 64
        mov     cs:pm_esp, eax
        mov     eax, [bp+12]
        mov     cs:p_src, eax
        mov     eax, [bp+16]
        mov     cs:p_dst, eax
        mov     eax, [bp+20]
        mov     cs:p_filedw, eax
        mov     eax, [bp+24]
        mov     cs:p_bssdw, eax
        mov     eax, [bp+28]
        mov     cs:p_entry, eax
        mov     eax, [bp+32]
        mov     cs:p_arg, eax
        mov     eax, [bp+8]
        mov     cr3, eax
        mov     di, [bp+6]
        db      66h                     ; 32-bit GDT base
        lgdt    fword ptr [di]
        mov     eax, cr0
        or      eax, 80000001h          ; PG and PE: this code is identity-mapped
        mov     cr0, eax
        db      0EAh                    ; jmp 38h:pm16 (38h's base is this segment)
        dw      offset pm16
        dw      38h
pm16:
        mov     ax, 10h
        mov     ds, ax
        mov     es, ax
        mov     fs, ax
        mov     gs, ax
        mov     ss, ax
        mov     esp, cs:pm_esp
        cld
        mov     esi, cs:p_src           ; the image, to its linear address
        mov     edi, cs:p_dst
        mov     ecx, cs:p_filedw
        db      67h                     ; 32-bit addressing (ESI, EDI, ECX)
        rep     movsd
        xor     eax, eax                ; then the bss
        mov     ecx, cs:p_bssdw
        db      67h
        rep     stosd
        mov     esi, cs:p_arg
        mov     ebx, cs:p_entry
        mov     eax, 8
        push    eax                     ; 32-bit far return to 08h:entry
        push    ebx
        db      66h
        retf

_pm_ret label near
        mov     bx, 40h                 ; 16-bit data: real-mode limits in the caches
        mov     ds, bx
        mov     es, bx
        mov     fs, bx
        mov     gs, bx
        mov     ss, bx
        mov     ecx, cr0
        and     ecx, 7FFFFFFEh          ; paging and protection off
        mov     cr0, ecx
        db      0EAh                    ; jmp far rm_cs:rm_back
        dw      offset rm_back
rm_cs_imm dw    0
rm_back:
        xor     ecx, ecx
        mov     cr3, ecx
        mov     ss, cs:save_ss
        mov     sp, cs:save_sp
        mov     ds, cs:save_ds
        lidt    fword ptr cs:rm_idtr
        cmp     cs:sd_resident, 0
        jne     stub_exit
        mov     edx, eax
        shr     edx, 16                 ; DX:AX = the kernel's result
        popf
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        retf
_pm_enter endp

; Resumed here in V86 mode with SS:SP and DS as pm_enter saved them.
_vm_resume label near
        mov     ax, 0
        mov     dx, 1                   ; 10000h: running under GLOS
        popf
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        retf

; ---- calls into the kernel: AX = function, EBX = argument; EAX back
_glos_call proc far
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        pushf
        mov     cs:save_ss, ss          ; LEAVE comes back through _pm_ret to here
        mov     cs:save_sp, sp
        mov     cs:save_ds, ds
        mov     ax, [bp+6]
        mov     ebx, [bp+8]
        call    kcall
        mov     edx, eax
        shr     edx, 16
        popf
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        retf
_glos_call endp

kcall proc near
_glos_bp_call label near
        db      63h, 0C0h               ; arpl ax, ax: #UD in V86 mode
        ret
kcall endp

_glos_xms_entry label far
        db      0EBh, 03h, 90h, 90h, 90h        ; jmp short +3: where hooks patch (XMS 3.0)
_glos_bp_xms label near
        db      63h, 0C0h
        retf

_glos_kill label near
        mov     ax, 4CFFh
        int     21h
        jmp     short _glos_kill

; ---- going resident
_stub_resident proc far
        mov     ah, 62h
        int     21h                     ; BX = our PSP
        mov     cs:psp_seg, bx
        add     bx, 10h
        mov     cs:new_seg, bx          ; the copy's segment
        cld
        push    cs
        pop     ds
        mov     es, bx
        xor     si, si
        xor     di, di
        mov     cx, offset _stub_end
        rep     movsb                   ; downwards: the source lies above its copy
        cmp     cs:sd_realmode, 0
        jne     moved
        mov     ax, GLOS_CALL_RESIDENT  ; from here, where the kernel knows the stub
        movzx   ebx, bx
        call    kcall
moved:  push    cs:new_seg
        push    offset resident
        retf
_stub_resident endp

resident:
        cli
        mov     ax, cs
        mov     ss, ax
        mov     sp, offset stub_stack_top
        mov     ds, ax
        mov     es, ax
        mov     cs:rm_cs_imm, ax
        sti
        mov     es, cs:psp_seg          ; the PSP and the stub; the rest goes back to DOS
        mov     bx, (_stub_end - stub_start + 15) / 16 + 10h
        mov     ah, 4Ah
        int     21h
        mov     cs:sd_resident, 1
        call    set_env
        mov     ebx, 0FFFFFFFFh         ; no program has run yet
next:
        mov     cs:save_ss, ss
        mov     cs:save_sp, sp
        mov     cs:save_ds, ds
        cmp     cs:sd_realmode, 0
        jne     exec
        mov     ax, GLOS_CALL_NEXT
        call    kcall                   ; EAX = 1: EXEC sd_path with sd_tail
        cmp     eax, 3
        je      dcall
        cmp     eax, 2                  ; 2: nothing yet (the agent): halt until an
        jne     exec                    ; interrupt or a command, then ask again
        sti
        hlt
        mov     ebx, 0FFFFFFFFh         ; nothing ran
        jmp     next
; NEXT 3, the DOS server: INT 21h for the kernel (SFTP), with the registers
; in sd_dregs, which then hold what came back. Only this loop makes one, so
; it never runs while a program does.
dcall:  push    ds
        push    es
        mov     ax, cs:sd_dregs[0]
        mov     bx, cs:sd_dregs[2]
        mov     cx, cs:sd_dregs[4]
        mov     dx, cs:sd_dregs[6]
        mov     si, cs:sd_dregs[8]
        mov     di, cs:sd_dregs[10]
        mov     es, cs:sd_dregs[14]
        mov     ds, cs:sd_dregs[12]
        int     21h
        mov     cs:sd_dregs[0], ax
        mov     cs:sd_dregs[2], bx
        mov     cs:sd_dregs[4], cx
        mov     cs:sd_dregs[6], dx
        mov     cs:sd_dregs[8], si
        mov     cs:sd_dregs[10], di
        mov     cs:sd_dregs[12], ds
        mov     cs:sd_dregs[14], es
        pushf
        pop     cs:sd_dregs[16]
        pop     es
        pop     ds
        mov     ebx, 0FFFFFFFEh         ; a DOS call ran
        jmp     next
exec:   push    cs
        pop     ds
        push    cs
        pop     es
        mov     si, offset sd_tail + 1  ; the FCBs from the tail, as COMMAND.COM does
        mov     di, offset fcb1
        mov     ax, 2901h
        int     21h
        mov     di, offset fcb2
        mov     ax, 2901h
        int     21h
        mov     ax, cs
        mov     word ptr pblock[4], ax
        mov     word ptr pblock[8], ax
        mov     word ptr pblock[12], ax
        mov     cs:exec_sp, sp
        mov     dx, offset sd_path
        mov     bx, offset pblock
        mov     ax, 4B00h
        int     21h
        cli                             ; DOS 2 lost SS:SP across EXEC
        mov     bx, cs
        mov     ss, bx
        mov     sp, cs:exec_sp
        mov     ds, bx
        sti
        jc      cannot
        mov     ah, 4Dh                 ; AH = how it ended, AL = its code
        int     21h
        movzx   ebx, ax
        jmp     next
cannot:
        movzx   ebx, ax
        or      ebx, 10000h
        cmp     cs:sd_realmode, 0
        je      next
        mov     si, offset msg_noshell  ; no kernel and no COMMAND.COM: nothing more to do
        call    puts
halt:   sti
        hlt
        jmp     halt

; The master environment, as the shell: a block just above the stub, filled
; with what the C code staged in its (now free, still intact) data, and then
; grown to its full size. Allocating only what the text needs first keeps
; DOS's new arena header below the staged text while it is copied.
set_env proc near
        mov     cx, cs:sd_env_len
        or      cx, cx
        jz      se_done
        mov     bx, cx
        add     bx, 15
        shr     bx, 4
        mov     ah, 48h
        int     21h
        jc      se_done
        mov     es, ax
        lds     si, cs:sd_env_src
        xor     di, di
        cld
        rep     movsb
        mov     bx, cs:sd_env_paras
        mov     ah, 4Ah
        int     21h                     ; ES: the new block, grown in place
        mov     ax, es
        mov     es, cs:psp_seg
        mov     bx, es:[2Ch]            ; the environment DOS gave us ...
        mov     es:[2Ch], ax            ; ... is replaced, and freed
        or      bx, bx
        jz      se_done
        mov     es, bx
        mov     ah, 49h
        int     21h
se_done:
        push    cs
        pop     ds
        push    cs
        pop     es
        ret
set_env endp

; ---- the end: back in real mode, interrupts off, EAX = the kernel's code
stub_exit:
        mov     cs:exit_code, al
        mov     ax, cs
        mov     ds, ax
        mov     ss, ax
        mov     sp, offset stub_stack_top
        sti
        assume  ds:GLOSSTUB
        cmp     sd_mode, 1
        jne     raw_a20
        mov     ah, 06h                 ; XMS: local A20 disable, then each block
        call    dword ptr sd_xms
        movzx   si, sd_nxms
xms_next:
        or      si, si
        jz      report
        dec     si
        mov     bx, si
        shl     bx, 1
        push    si
        mov     dx, sd_handles[bx]
        push    dx
        mov     ah, 0Dh                 ; unlock
        call    dword ptr sd_xms
        pop     dx
        mov     ah, 0Ah                 ; free
        call    dword ptr sd_xms
        pop     si
        jmp     xms_next
raw_a20:
        cmp     sd_a20init, 0
        jne     report
        cli
        in      al, 92h                 ; fast A20 off, never the reset bit
        and     al, 0FCh
        out     92h, al
        call    a20_test
        jz      a20_done
        call    kbc_wait
        mov     al, 0D1h
        out     64h, al
        call    kbc_wait
        mov     al, 0DDh
        out     60h, al
        call    kbc_wait
a20_done:
        sti
report:
        mov     si, offset msg_exit
        call    puts
        mov     al, exit_code
        call    putdec
        mov     si, offset msg_a20
        call    puts
        call    a20_test
        mov     al, '1'
        jnz     a20_put
        mov     al, '0'
a20_put:
        call    putc
        mov     si, offset msg_crlf
        call    puts
        cmp     sd_shell, 0
        jne     shell_on
        mov     al, exit_code
        mov     ah, 4Ch
        int     21h
shell_on:                               ; the shell never ends: COMMAND.COM from now on
        push    cs
        pop     es
        cld
        mov     si, offset sd_comspec
        mov     di, offset sd_path
        mov     cx, 80
        rep     movsb
        mov     si, offset sd_fbtail
        mov     di, offset sd_tail
        mov     cx, 64
        rep     movsb
        mov     sd_realmode, 1
        mov     ebx, 0FFFFFFFFh
        jmp     next
        assume  ds:nothing

; ZF clear when A20 is on: 0000:0500 and FFFF:0510 are different bytes.
a20_test proc near
        push    ds
        push    es
        push    bx
        xor     ax, ax
        mov     ds, ax
        dec     ax
        mov     es, ax
        mov     bl, ds:[0500h]
        mov     bh, es:[0510h]
        mov     byte ptr es:[0510h], 0A5h
        mov     byte ptr ds:[0500h], 5Ah
        cmp     byte ptr es:[0510h], 5Ah        ; the same byte when A20 is off
        mov     es:[0510h], bh                  ; MOV leaves the flags alone
        mov     ds:[0500h], bl
        pop     bx
        pop     es
        pop     ds
        ret
a20_test endp

kbc_wait proc near
        push    cx
        mov     cx, 0FFFFh
kw:     in      al, 64h
        test    al, 2
        loopnz  kw
        pop     cx
        ret
kbc_wait endp

putc proc near                          ; AL to COM1
        push    cx
        push    ax
        mov     dx, 3FDh
        mov     cx, 0FFFFh
pw:     in      al, dx
        test    al, 20h
        loopz   pw
        pop     ax
        mov     dx, 3F8h
        out     dx, al
        pop     cx
        ret
putc endp

puts proc near                          ; CS:SI, ASCIIZ
ps:     mov     al, cs:[si]
        or      al, al
        jz      pd
        call    putc
        inc     si
        jmp     ps
pd:     ret
puts endp

putdec proc near                        ; AL in decimal
        xor     ah, ah
        mov     cl, 100
        div     cl
        mov     ch, ah                  ; the remainder
        or      al, al
        jz      tens
        add     al, '0'
        call    putc
        mov     al, ch
        xor     ah, ah
        mov     cl, 10
        div     cl
        jmp     both
tens:   mov     al, ch
        xor     ah, ah
        mov     cl, 10
        div     cl
        or      al, al
        jz      ones
both:   add     al, '0'
        mov     ch, ah
        call    putc
        mov     ah, ch
ones:   mov     al, ah
        add     al, '0'
        call    putc
        ret
putdec endp

        align   2
        dw      128 dup (0)
_glos_kill_top label word
        dw      256 dup (0)
stub_stack_top label word

_stub_end label byte

GLOSSTUB ends
        end
