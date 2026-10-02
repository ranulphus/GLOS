; GLOS.EXE: the parts of the loader that need 32-bit registers or privileged
; instructions (Open Watcom wasm, small model, __cdecl).
;
;   int  cpu_is486(void)                    1 if EFLAGS.AC toggles (a 486 or later)
;   int  cpu_v86(void)                      1 if the CPU is already in protected/V86 mode
;   int  cpu_id1(unsigned long *eax, unsigned long *edx)   CPUID 1, 0 without CPUID
;   int  bios_e820(void *buf20, unsigned long *cont)       one INT 15h E820h entry
;   unsigned long pm_enter(unsigned gdtr, unsigned long cr3, unsigned long src,
;                          unsigned long dst, unsigned long file_dwords,
;                          unsigned long bss_dwords, unsigned long entry,
;                          unsigned long arg)
;       enters protected mode with paging (this code is identity-mapped),
;       copies the kernel image from src to dst (linear) and zeroes its bss,
;       and jumps to entry (CS=08h, DS=ES=SS=10h, ESI=arg). The kernel comes
;       back with a far jump to 38h:_pm_ret, EAX = its result, interrupts off;
;       that leaves protected mode and returns EAX in DX:AX.
;       When the kernel keeps DOS running (M2), it instead resumes this
;       program in virtual-8086 mode at _vm_resume, which returns 10000h.
;   unsigned long glos_call(unsigned fn, unsigned long arg)
;       a call into the kernel from V86 mode (an ARPL at _glos_bp_call, which
;       raises #UD there). fn 1 (leave) comes back through _pm_ret in real
;       mode, like pm_enter, with the kernel's result.
;   _glos_xms_entry
;       the XMS entry point INT 2Fh 4310h hands out under GLOS: five bytes
;       for hooks, then the ARPL at _glos_bp_xms (the kernel serves the call
;       and resumes at the RETF).
;   _glos_kill
;       where the kernel sends a killed program: INT 21h 4CFFh as that
;       program, on the small stack below _glos_kill_top.
.586p

_TEXT   segment word public 'CODE' use16
        assume  cs:_TEXT

        public  _cpu_is486, _cpu_v86, _cpu_id1, _bios_e820, _pm_enter, _pm_ret
        public  _vm_resume, _vm_state, _glos_call, _glos_bp_call, _glos_xms_entry, _glos_bp_xms
        public  _glos_kill, _glos_kill_top

_cpu_is486 proc near
        pushf
        pushf
        pop     ax
        mov     cx, ax
        and     ax, 0FFFh               ; 8086/8088: bits 12-15 stick at 1
        push    ax
        popf
        pushf
        pop     ax
        and     ax, 0F000h
        cmp     ax, 0F000h
        je      no
        mov     ax, cx
        or      ax, 0F000h              ; 286 in real mode: bits 12-15 stick at 0
        push    ax
        popf
        pushf
        pop     ax
        and     ax, 0F000h
        jz      no
        pushfd                          ; 386 or later: AC (bit 18) toggles on a 486
        pop     eax
        mov     ecx, eax
        xor     eax, 40000h
        push    eax
        popfd
        pushfd
        pop     eax
        push    ecx
        popfd
        xor     eax, ecx
        test    eax, 40000h
        jz      no
        popf
        mov     ax, 1
        ret
no:     popf
        xor     ax, ax
        ret
_cpu_is486 endp

_cpu_v86 proc near
        smsw    ax
        and     ax, 1
        ret
_cpu_v86 endp

_cpu_id1 proc near
        push    bp
        mov     bp, sp
        push    si
        push    bx
        pushfd                          ; CPUID exists if EFLAGS.ID (bit 21) toggles
        pop     eax
        mov     ecx, eax
        xor     eax, 200000h
        push    eax
        popfd
        pushfd
        pop     eax
        push    ecx
        popfd
        xor     eax, ecx
        test    eax, 200000h
        jz      noid
        mov     eax, 1
        cpuid
        mov     si, [bp+4]
        mov     [si], eax
        mov     si, [bp+6]
        mov     [si], edx
        mov     ax, 1
        jmp     idret
noid:   xor     ax, ax
idret:  pop     bx
        pop     si
        pop     bp
        ret
_cpu_id1 endp

_bios_e820 proc near
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    bx
        push    es
        push    ds
        pop     es
        mov     di, [bp+4]
        mov     si, [bp+6]
        mov     ebx, [si]
        mov     eax, 0E820h
        mov     edx, 534D4150h          ; "SMAP"
        mov     ecx, 20
        int     15h
        jc      e8fail
        cmp     eax, 534D4150h
        jne     e8fail
        mov     si, [bp+6]
        mov     [si], ebx
        mov     ax, 1
        jmp     e8ret
e8fail: xor     ax, ax
e8ret:  pop     es
        pop     bx
        pop     di
        pop     si
        pop     bp
        ret
_bios_e820 endp

_pm_enter proc near
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
        mov     cs:rm_cs, ax
        xor     eax, eax                ; the kernel's first stack: below ours
        mov     ax, ss
        shl     eax, 4
        movzx   ecx, sp
        add     eax, ecx
        sub     eax, 64
        mov     cs:pm_esp, eax
        mov     eax, [bp+10]
        mov     cs:p_src, eax
        mov     eax, [bp+14]
        mov     cs:p_dst, eax
        mov     eax, [bp+18]
        mov     cs:p_filedw, eax
        mov     eax, [bp+22]
        mov     cs:p_bssdw, eax
        mov     eax, [bp+26]
        mov     cs:p_entry, eax
        mov     eax, [bp+30]
        mov     cs:p_arg, eax
        mov     eax, [bp+6]
        mov     cr3, eax
        mov     di, [bp+4]
        db      66h                     ; 32-bit GDT base
        lgdt    fword ptr [di]
        mov     eax, cr0
        or      eax, 80000001h          ; PG and PE: this code is identity-mapped
        mov     cr0, eax
        db      0EAh                    ; jmp 38h:pm16
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
rm_cs   dw      0
rm_back:
        xor     ecx, ecx
        mov     cr3, ecx
        mov     ss, cs:save_ss
        mov     sp, cs:save_sp
        mov     ds, cs:save_ds
        lidt    fword ptr cs:rm_idtr
        mov     edx, eax
        shr     edx, 16                 ; DX:AX = the kernel's result
        popf
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret
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
        ret

_glos_call proc near
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        pushf
        mov     cs:save_ss, ss          ; for the way back through _pm_ret
        mov     cs:save_sp, sp
        mov     cs:save_ds, ds
        mov     ax, [bp+4]              ; fn
        mov     ebx, [bp+6]             ; arg
_glos_bp_call label near
        db      63h, 0C0h               ; arpl ax, ax: #UD in V86 mode
        mov     edx, eax
        shr     edx, 16
        popf
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret
_glos_call endp

_glos_xms_entry label far
        db      0EBh, 03h, 90h, 90h, 90h        ; jmp short +3: where hooks patch (XMS 3.0)
_glos_bp_xms label near
        db      63h, 0C0h
        retf

_glos_kill label near
        mov     ax, 4CFFh
        int     21h
        jmp     short _glos_kill
        dw      128 dup (0)
_glos_kill_top label word

_vm_state label word                    ; the kernel reads these three to resume
save_ss  dw     0
save_sp  dw     0
save_ds  dw     0
pm_esp   dd     0
p_src    dd     0
p_dst    dd     0
p_filedw dd     0
p_bssdw  dd     0
p_entry  dd     0
p_arg    dd     0
rm_idtr  dw     3FFh                    ; the real-mode interrupt vector table
         dd     0

_TEXT   ends
        end
