; DPMICONF-16's low-level part (dpmi16.c has the checks): the mode switch,
; INT 31h with every register, and the handlers a host calls, with 16-bit
; frames. Open Watcom WASM, small model, __cdecl. Handlers reach the data
; through hd_ds, the DS selector, which init_handlers() writes into the code
; segment through an alias.
.386
.387
DGROUP  group   _DATA
_DATA   segment word use16 public 'DATA'
_DATA   ends
_TEXT   segment word use16 public 'CODE'
_TEXT   ends

_DATA   segment
        public  _psp_sel, _exc_hits, _exc_err, _exc_ip, _exc_cs, _exc_fl, _exc_sp, _exc_ss, _exc_hss
        public  _exc_skip, _exc_new_ip, _exc_new_sp, _exc_sp_before, _gp_len, _pf_len
        public  _irq8_hits, _irq8_vif, _irq8_ss, _irq8_mode, _i1c_hits, _hits60, _hits61
        public  _irq13_hits, _mf_hits, _rmcb_hits, _rmcb_eax, _rmcb_si, _rmcb_ds
        public  _rr_pm2rm, _rr_rm2pm, _rr_dgroup, _rr_code, _rr_count, _rr_dosver, _rr_save, _rr_state
        public  _x10_hits, _x10_words, _x10_handle, _x10_off, _x10_ret32
entry           dd      0
_psp_sel        dw      0
_exc_hits       dw      0
_exc_err        dw      0
_exc_ip         dw      0
_exc_cs         dw      0
_exc_fl         dw      0
_exc_sp         dw      0
_exc_ss         dw      0
_exc_hss        dw      0
_exc_skip       dw      0
_exc_new_ip     dw      0
_exc_new_sp     dw      0
_exc_sp_before  dw      0
_gp_len         dw      gp_end - _gp_insn
_pf_len         dw      pf_end - _pf_insn
_irq8_hits      dw      0
_irq8_vif       dw      0
_irq8_ss        dw      0
_irq8_mode      dw      0
_i1c_hits       dw      0
_hits60         dw      0
_hits61         dw      0
_irq13_hits     dw      0
_mf_hits        dw      0
_rmcb_hits      dw      0
_rmcb_eax       dd      0
_rmcb_si        dw      0
_rmcb_ds        dw      0
_rr_pm2rm       dd      0               ; 0306h: SI:DI, protected to real
_rr_rm2pm       dd      0               ; 0306h: BX:CX, real to protected
_rr_save        dd      0               ; 0305h: SI:DI, the protected-mode save/restore
_rr_dgroup      dw      0               ; DGROUP's paragraph
_rr_code        dw      0               ; _TEXT's paragraph
_rr_count       dw      0
_rr_dosver      dw      0
_rr_state       db      64 dup (0)
_x10_hits       dw      0
_x10_words      dw      48 dup (0)      ; the handler's stack as it found it
_x10_handle     dd      0               ; the page to commit (0507h ESI, EBX)
_x10_off        dd      0
_x10_ret32      dw      0               ; 1 if it returned with a 32-bit RETF
x10_attr        dw      9
rr_ds           dw      0
rr_ss           dw      0
rr_sp           dw      0
rr_cs           dw      0
saved_sp        dw      0
fpu_cw          dw      0
_DATA   ends

_TEXT   segment
        assume  cs:_TEXT, ds:DGROUP, ss:nothing, es:nothing
hd_ds   dw      0                       ; the DS selector, for handlers
old8    dd      0                       ; chained vectors (offset, selector)
old1c   dd      0

        public  _dpmi_enter, _i31, _i21, _i31_words, _dos_exit, _init_handlers, _set_chain, _espfix_hi
        public  _fault_gp, _gp_insn, _fault_resume, _exc_resume, _fault_pf, _pf_insn, _int60, _int61, _dos_ver
        public  _exc_handler, _irq8_handler, _i1c_handler, _pm60_handler, _pm61_handler
        public  _irq13_handler, _mf_handler, _rmcb_far, _rmcb_int, _fpu_divzero, _raw_roundtrip, _state_save
        public  _get_cs, _get_ds, _lsl32, _x10_handler

; int dpmi_enter(void): INT 2Fh 1687h and the switch as a 16-bit client.
; 0 in protected mode (DS, SS: selectors for the same memory; ES = DS; the
; PSP selector in psp_sel), else 'H' (no host), 'M' (no memory), 'S'.
_dpmi_enter proc near
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    es
        mov     ax, 1687h
        int     2Fh
        or      ax, ax
        mov     ax, 'H'
        jnz     de_fail
        mov     word ptr entry, di
        mov     word ptr entry+2, es
        mov     bx, si
        or      bx, bx
        jz      de_go
        mov     ah, 48h
        int     21h
        mov     bx, ax
        mov     ax, 'M'
        jc      de_fail
        mov     es, bx
de_go:  xor     ax, ax                  ; a 16-bit client
        call    dword ptr entry
        mov     ax, 'S'
        jc      de_fail
        mov     _psp_sel, es
        add     sp, 2                   ; the real-mode ES: no use now
        push    ds
        pop     es
        xor     ax, ax
        pop     di
        pop     si
        pop     bp
        ret
de_fail:
        pop     es
        pop     di
        pop     si
        pop     bp
        ret
_dpmi_enter endp

; struct r32 { u32 eax, ebx, ecx, edx, esi, edi; u16 ds, es, fl; }
R_EAX   equ     0
R_EBX   equ     4
R_ECX   equ     8
R_EDX   equ     12
R_ESI   equ     16
R_EDI   equ     20
R_DS    equ     24
R_ES    equ     26
R_FL    equ     28

; int i31(struct r32 *r): INT 31h with r's registers (DS, ES included);
; r takes the results and FLAGS. Returns CF.
_i31    proc    near
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        mov     bx, [bp+4]
        push    bx                      ; [bp-10]: r
        mov     es, word ptr [bx+R_ES]
        mov     eax, [bx+R_EAX]
        mov     ecx, [bx+R_ECX]
        mov     edx, [bx+R_EDX]
        mov     esi, [bx+R_ESI]
        mov     edi, [bx+R_EDI]
        push    word ptr [bx+R_DS]
        mov     ebx, [bx+R_EBX]
        pop     ds
        int     31h
i31_out:
        push    ds
        push    ebx
        mov     bx, [bp-6]
        mov     ds, bx
        mov     bx, [bp-10]
        mov     [bx+R_EAX], eax
        pop     dword ptr [bx+R_EBX]
        pop     word ptr [bx+R_DS]
        mov     [bx+R_ECX], ecx
        mov     [bx+R_EDX], edx
        mov     [bx+R_ESI], esi
        mov     [bx+R_EDI], edi
        mov     [bx+R_ES], es
        pushf
        pop     word ptr [bx+R_FL]
        mov     ax, [bx+R_FL]
        and     ax, 1
        add     sp, 2
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        ret
_i31    endp

; int i21(struct r32 *r): the same for INT 21h from protected mode (the
; host's DOS translation for a 16-bit client).
_i21    proc    near
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        mov     bx, [bp+4]
        push    bx                      ; [bp-10]: r
        mov     es, word ptr [bx+R_ES]
        mov     eax, [bx+R_EAX]
        mov     ecx, [bx+R_ECX]
        mov     edx, [bx+R_EDX]
        mov     esi, [bx+R_ESI]
        mov     edi, [bx+R_EDI]
        push    word ptr [bx+R_DS]
        mov     ebx, [bx+R_EBX]
        pop     ds
        int     21h
        jmp     i31_out
_i21    endp

; int i31_words(struct r32 *r, unsigned w0, unsigned w1): the same, with w0
; and w1 on the stack (w0 on top) for 0300h-0302h with CX=2.
_i31_words proc near
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    ds
        push    es
        mov     bx, [bp+4]
        push    bx
        mov     es, word ptr [bx+R_ES]
        mov     eax, [bx+R_EAX]
        mov     ecx, [bx+R_ECX]
        mov     edx, [bx+R_EDX]
        mov     esi, [bx+R_ESI]
        mov     edi, [bx+R_EDI]
        push    word ptr [bx+R_DS]
        mov     ebx, [bx+R_EBX]
        pop     ds
        push    word ptr [bp+8]
        push    word ptr [bp+6]
        int     31h
        lea     sp, [bp-10]             ; (no flags changed)
        jmp     i31_out
_i31_words endp

; void dos_exit(int code): INT 21h 4Ch, from protected mode.
_dos_exit proc near
        push    bp
        mov     bp, sp
        mov     al, [bp+4]
        mov     ah, 4Ch
        int     21h
_dos_exit endp

; unsigned get_cs(void), get_ds(void)
_get_cs proc    near
        mov     ax, cs
        ret
_get_cs endp
_get_ds proc    near
        mov     ax, ds
        ret
_get_ds endp

; unsigned long lsl32(unsigned sel): the segment limit, or FFFFFFFFh.
_lsl32  proc    near
        push    bp
        mov     bp, sp
        movzx   eax, word ptr [bp+4]
        lsl     eax, eax
        jz      lsl_ok
        or      eax, -1
lsl_ok: mov     edx, eax
        shr     edx, 16
        pop     bp
        ret
_lsl32  endp

; void init_handlers(unsigned alias): hd_ds = DS, through alias (a data
; selector for this code segment).
_init_handlers proc near
        push    bp
        mov     bp, sp
        push    es
        mov     es, word ptr [bp+4]
        mov     word ptr es:hd_ds, ds
        pop     es
        pop     bp
        ret
_init_handlers endp

; void set_chain(unsigned alias, int which, unsigned off, unsigned sel):
; where the INT 8 (which 0) or INT 1Ch (1) handler chains to.
_set_chain proc near
        push    bp
        mov     bp, sp
        push    es
        push    di
        mov     es, word ptr [bp+4]
        mov     di, offset old8
        cmp     word ptr [bp+6], 0
        je      sc_go
        mov     di, offset old1c
sc_go:  mov     ax, [bp+8]
        mov     es:[di], ax
        mov     ax, [bp+10]
        mov     es:[di+2], ax
        pop     di
        pop     es
        pop     bp
        ret
_set_chain endp

; unsigned espfix_hi(void): ESP[31:16] after an INT 31h on this 16-bit stack
; with it set to 1234h (an IRET to a 16-bit SS loads only SP: espfix).
_espfix_hi proc near
        push    bp
        mov     bp, sp
        and     esp, 0FFFFh
        or      esp, 12340000h
        mov     ax, 0400h
        int     31h
        mov     eax, esp
        and     esp, 0FFFFh
        shr     eax, 16
        pop     bp
        ret
_espfix_hi endp

; unsigned fault_gp(unsigned bx): a #GP (a write to CS); the handler skips it.
; Returns BX as the handler left it.
_fault_gp proc  near
        push    bp
        mov     bp, sp
        push    bx
        mov     bx, [bp+4]
        mov     _exc_sp_before, sp
_gp_insn:
        mov     byte ptr cs:[_gp_insn], al
gp_end:
        mov     ax, bx
        pop     bx
        pop     bp
        ret
_fault_gp endp

; unsigned fault_resume(void): a #GP whose handler moves IP to exc_resume and
; SP to exc_new_sp; returns the SP it resumed with.
_fault_resume proc near
        push    bp
        mov     bp, sp
        mov     saved_sp, sp
        mov     byte ptr cs:[_gp_insn], al
_exc_resume:
        mov     ax, sp
        mov     sp, saved_sp
        pop     bp
        ret
_fault_resume endp

; unsigned fault_pf(unsigned sel, unsigned off): a read of sel:off, which
; faults; the handler skips it. 0 if skipped.
_fault_pf proc  near
        push    bp
        mov     bp, sp
        push    es
        push    bx
        mov     es, word ptr [bp+4]
        mov     bx, [bp+6]
        xor     ax, ax
_pf_insn:
        mov     ax, es:[bx]
pf_end:
        pop     bx
        pop     es
        pop     bp
        ret
_fault_pf endp

; unsigned int60(void), int61(void): INT 60h/61h with CX=0; returns CX.
_int60  proc    near
        xor     cx, cx
        int     60h
        mov     ax, cx
        ret
_int60  endp
_int61  proc    near
        xor     cx, cx
        int     61h
        mov     ax, cx
        ret
_int61  endp

; unsigned dos_ver(void): INT 21h 30h from protected mode (reflected).
_dos_ver proc   near
        push    bx
        push    cx
        mov     ax, 3000h
        int     21h
        pop     cx
        pop     bx
        ret
_dos_ver endp

; ---- handlers

; Exception handler, DPMI 0.9 16-bit frame: [bp+2] the host's return IP,
; CS, error code, IP, CS, FLAGS, SP, SS. Skips exc_skip bytes, or resumes at
; exc_new_ip on exc_new_sp; RETF to the host.
_exc_handler:
        push    bp
        mov     bp, sp
        push    ds
        push    ax
        mov     ds, word ptr cs:hd_ds
        inc     _exc_hits
        mov     ax, [bp+6]
        mov     _exc_err, ax
        mov     ax, [bp+8]
        mov     _exc_ip, ax
        mov     ax, [bp+10]
        mov     _exc_cs, ax
        mov     ax, [bp+12]
        mov     _exc_fl, ax
        mov     ax, [bp+14]
        mov     _exc_sp, ax
        mov     ax, [bp+16]
        mov     _exc_ss, ax
        mov     _exc_hss, ss
        cmp     _exc_new_ip, 0
        je      eh_skip
        mov     ax, _exc_new_ip
        mov     [bp+8], ax
        mov     ax, _exc_new_sp
        mov     [bp+14], ax
        jmp     eh_out
eh_skip:
        mov     ax, _exc_skip
        add     [bp+8], ax
eh_out: pop     ax
        pop     ds
        pop     bp
        retf

; Extended (1.0, 0212h) handler for the #PF: records 48 words of its stack,
; commits the page (0507h) so the read succeeds when retried, and returns
; through the host: with a 32-bit RETF if the return address is 32-bit (its
; high word, the second word, 0), else a 16-bit one.
_x10_handler:
        push    bp
        mov     bp, sp
        push    ds
        push    es
        pushad
        mov     ds, word ptr cs:hd_ds
        inc     _x10_hits
        lea     si, [bp+2]
        mov     di, offset _x10_words
        mov     cx, 48
        mov     dx, ss
        lsl     edx, edx                ; the stack segment's limit: copy no further
x10_cp: movzx   eax, si
        inc     eax
        cmp     eax, edx
        ja      x10_cpd
        mov     ax, ss:[si]
        mov     [di], ax
        add     si, 2
        add     di, 2
        loop    x10_cp
x10_cpd:
        mov     ax, 0507h
        mov     esi, _x10_handle
        mov     ebx, _x10_off
        mov     ecx, 1
        push    ds
        pop     es
        mov     edx, offset x10_attr
        int     31h
        xor     ax, ax
        cmp     word ptr [bp+4], 0
        jne     x10_16
        inc     ax
x10_16: mov     _x10_ret32, ax
        popad
        pop     es
        pop     ds
        pop     bp
        push    ax
        push    ds
        mov     ds, word ptr cs:hd_ds
        cmp     _x10_ret32, 0
        pop     ds
        pop     ax
        je      x10_r16
        db      66h
x10_r16:
        retf

; IRQ 0 (INT 8): count, read the virtual IF (0902h) and SS; then chain, or
; with irq8_mode set EOI and IRET without STI.
_irq8_handler:
        push    ds
        push    ax
        mov     ds, word ptr cs:hd_ds
        inc     _irq8_hits
        mov     ax, 0902h
        int     31h
        xor     ah, ah
        mov     _irq8_vif, ax
        mov     _irq8_ss, ss
        cmp     _irq8_mode, 0
        jne     i8_own
        pop     ax
        pop     ds
        jmp     dword ptr cs:old8
i8_own: mov     al, 20h
        out     20h, al
        pop     ax
        pop     ds
        iret

; INT 1Ch passed up from the BIOS's tick: count and chain.
_i1c_handler:
        push    ds
        mov     ds, word ptr cs:hd_ds
        inc     _i1c_hits
        pop     ds
        jmp     dword ptr cs:old1c

; INT 60h/61h in protected mode: count, CX=1, IRET.
_pm60_handler:
        push    ds
        mov     ds, word ptr cs:hd_ds
        inc     _hits60
        pop     ds
        mov     cx, 1
        iret
_pm61_handler:
        push    ds
        mov     ds, word ptr cs:hd_ds
        inc     _hits61
        pop     ds
        mov     cx, 1
        iret

; IRQ13 (INT 75h): the FPU error. Port F0h, EOI both PICs, FNCLEX.
_irq13_handler:
        push    ds
        push    ax
        mov     ds, word ptr cs:hd_ds
        inc     _irq13_hits
        xor     al, al
        out     0F0h, al
        mov     al, 20h
        out     0A0h, al
        out     20h, al
        fnclex
        pop     ax
        pop     ds
        iret

; Exception 10h (#MF), should a host set NE: count, FNCLEX, back.
_mf_handler:
        push    ds
        mov     ds, word ptr cs:hd_ds
        inc     _mf_hits
        pop     ds
        fnclex
        retf

; Real-mode callbacks: DS:SI the real-mode SS:SP, ES:DI the register
; structure (16-bit offsets). rmcb_far returns as a RETF, rmcb_int as an IRET.
RM_EBX  equ     16
RM_ECX  equ     24
RM_EAX  equ     28
RM_IP   equ     42
RM_CS   equ     44
RM_SP   equ     46
RM_FL   equ     32
_rmcb_far:
        push    ax
        push    ds
        mov     ax, ds
        mov     ds, word ptr cs:hd_ds
        inc     _rmcb_hits
        mov     _rmcb_ds, ax
        mov     _rmcb_si, si
        mov     ax, es:[di+RM_EAX]
        mov     word ptr _rmcb_eax, ax
        mov     ax, es:[di+RM_EAX+2]
        mov     word ptr _rmcb_eax+2, ax
        mov     word ptr es:[di+RM_EBX], 0BEEFh
        pop     ds
        mov     ax, [si]
        mov     es:[di+RM_IP], ax
        mov     ax, [si+2]
        mov     es:[di+RM_CS], ax
        add     word ptr es:[di+RM_SP], 4
        pop     ax
        iret
_rmcb_int:
        push    ax
        push    ds
        mov     ds, word ptr cs:hd_ds
        inc     _rmcb_hits
        pop     ds
        mov     word ptr es:[di+RM_ECX], 0CAFEh
        mov     ax, [si]
        mov     es:[di+RM_IP], ax
        mov     ax, [si+2]
        mov     es:[di+RM_CS], ax
        mov     ax, [si+4]
        mov     es:[di+RM_FL], ax
        add     word ptr es:[di+RM_SP], 6
        pop     ax
        iret

; void fpu_divzero(void): 1/0 with the zero-divide exception unmasked, then
; a moment for IRQ13; the FPU reset after.
_fpu_divzero proc near
        fninit
        fstcw   fpu_cw
        and     fpu_cw, not 4
        fldcw   fpu_cw
        fld1
        fldz
        fdivp   st(1), st
        fwait
        mov     cx, 4000
fz_w:   loop    fz_w
        fninit
        ret
_fpu_divzero endp

; void state_save(int restore): 0305h's protected-mode routine, AL=0 save,
; 1 restore, ES:DI = rr_state (when the host has state to keep).
_state_save proc near
        push    bp
        mov     bp, sp
        push    es
        push    di
        mov     al, [bp+4]
        push    ds
        pop     es
        mov     di, offset _rr_state
        call    dword ptr _rr_save
        pop     di
        pop     es
        pop     bp
        ret
_state_save endp

; int raw_roundtrip(void): 0306h to real mode (on DGROUP's paragraph, same
; SP), INT 21h 30h there, count, and back to protected mode with the same
; selectors. Returns rr_count.
_raw_roundtrip proc near
        push    bp
        mov     bp, sp
        push    si
        push    di
        mov     rr_ds, ds
        mov     rr_ss, ss
        mov     rr_sp, sp
        mov     rr_cs, cs
        mov     ax, _rr_dgroup
        mov     cx, ax
        mov     dx, ax
        mov     bx, sp
        mov     si, _rr_code
        mov     di, offset rr_in_rm
        jmp     dword ptr _rr_pm2rm
rr_in_rm:                               ; real mode: DS = SS = DGROUP's paragraph
        inc     _rr_count
        mov     ah, 30h
        int     21h
        mov     _rr_dosver, ax
        mov     ax, rr_ds
        mov     cx, ax
        mov     dx, rr_ss
        mov     bx, rr_sp
        mov     si, rr_cs
        mov     di, offset rr_back
        jmp     dword ptr _rr_rm2pm
rr_back:
        mov     ax, _rr_count
        pop     di
        pop     si
        pop     bp
        ret
_raw_roundtrip endp

_TEXT   ends
        end
