; GLOS.EXE: the transient parts of the loader that need 32-bit registers or
; privileged instructions (Open Watcom wasm, small model, __cdecl). The mode
; switch and everything the kernel calls back into are in stub.asm.
;
;   int  cpu_is486(void)                    1 if EFLAGS.AC toggles (a 486 or later)
;   int  cpu_v86(void)                      1 if the CPU is already in protected/V86 mode
;   int  cpu_id1(unsigned long *eax, unsigned long *edx)   CPUID 1, 0 without CPUID
;   int  bios_e820(void *buf20, unsigned long *cont)       one INT 15h E820h entry
;   unsigned long vga_font(unsigned which)  INT 10h 1130h: a ROM font's linear address
.586p

_TEXT   segment word public 'CODE' use16
        assume  cs:_TEXT

        public  _cpu_is486, _cpu_v86, _cpu_id1, _bios_e820, _vga_font

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

; BH = which (2: 8x14, 3 and 4: 8x8's two halves, 6: 8x16); the BIOS answers
; in ES:BP.
_vga_font proc near
        push    bp
        mov     bp, sp
        push    es
        push    bx
        mov     bh, [bp+4]
        mov     ax, 1130h
        xor     cx, cx
        push    bp
        int     10h
        mov     ax, bp
        pop     bp
        mov     dx, es
        mov     cx, dx                  ; DX:AX = DX * 16 + AX
        shr     dx, 12
        shl     cx, 4
        add     ax, cx
        adc     dx, 0
        pop     bx
        pop     es
        pop     bp
        ret
_vga_font endp

_TEXT   ends
        end
