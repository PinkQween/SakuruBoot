; SakuruBoot Stage 2 Entry — real mode → long mode transition
; ============================================================
; Loaded at 0x7E00 (placed there by the linker script).
; Transitions from 16-bit real mode through 32-bit protected mode
; into 64-bit long mode, then calls stage2_main(boot_drive).
;
; Assemble: nasm -f elf64 -o entry.o entry.asm
; The linker script places .text.entry at 0x7E00.
;
; Memory layout:
;   0x7E00       This code
;   0x1000-3FFF  Page tables (PML4 / PDPT / PD)
;   0x90000      Stack top

section .text.entry

global _start
_start:
    [BITS 16]
    cli
    cld

    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     sp, 0x7C00              ; Stack just below the MBR

    mov     [boot_drive], dl        ; Save BIOS drive number

    ; Print banner via BIOS teletype
    mov     si, msg_banner
    call    print16

    ; --- Enable A20 line via fast A20 (port 0x92) ---
    in      al, 0x92
    test    al, 2
    jnz     .a20_done
    or      al, 2
    and     al, 0xFE                ; Clear bit 0 (reset line)
    out     0x92, al
.a20_done:

    ; Load the 64-bit GDT descriptor (pointer has 32-bit base — fine for <4GB)
    lgdt    [gdt64_ptr]

    ; Switch to 32-bit protected mode (paging off)
    mov     eax, cr0
    or      eax, 1                  ; Set PE bit
    mov     cr0, eax
    jmp     0x08:pmode32            ; Far jump to flush pipeline & load CS

; ===========================================================
[BITS 32]
pmode32:
    mov     ax, 0x10                ; Data segment selector
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax
    mov     esp, 0x00090000         ; Stack at 576 KB

    ; --- Build identity-mapped page tables ---
    ; PML4  @ 0x1000
    ; PDPT  @ 0x2000
    ; PD    @ 0x3000  (512 × 2 MB huge pages = 1 GB)

    ; Zero all three tables (3 × 4096 bytes)
    mov     edi, 0x1000
    xor     eax, eax
    mov     ecx, (0x3000 / 4)       ; 3 pages / 4 bytes
    rep     stosd

    ; PML4[0] → PDPT, present + writable
    mov     dword [0x1000], 0x2003

    ; PDPT[0] → PD, present + writable
    mov     dword [0x2000], 0x3003

    ; PD: 512 entries, each maps 2 MB (huge page)
    mov     edi, 0x3000
    mov     eax, 0x00000083         ; Base=0, P=1, W=1, PS=1 (2 MB huge)
    mov     ecx, 512
.fill_pd:
    mov     [edi],     eax
    mov     dword [edi + 4], 0      ; High 32 bits = 0
    add     eax, 0x200000           ; Advance by 2 MB
    add     edi, 8
    loop    .fill_pd

    ; --- Enable PAE (required for long mode) ---
    mov     eax, cr4
    or      eax, (1 << 5)           ; CR4.PAE
    mov     cr4, eax

    ; --- Load PML4 into CR3 ---
    mov     eax, 0x1000
    mov     cr3, eax

    ; --- Set EFER.LME (Long Mode Enable) ---
    mov     ecx, 0xC0000080         ; EFER MSR
    rdmsr
    or      eax, (1 << 8)           ; LME bit
    wrmsr

    ; --- Enable paging → activate long mode ---
    mov     eax, cr0
    or      eax, (1 << 31)          ; PG bit
    mov     cr0, eax

    ; Far jump into 64-bit code segment to complete mode switch
    jmp     0x08:lmode64

; ===========================================================
[BITS 64]
lmode64:
    mov     ax, 0x10
    mov     ds, ax
    mov     es, ax
    mov     fs, ax
    mov     gs, ax
    mov     ss, ax
    mov     rsp, 0x0000000000090000

    ; Reload GDT with 64-bit pointer
    lgdt    [rel gdt64_ptr]

    ; Call C stage2_main(boot_drive)
    movzx   rdi, byte [rel boot_drive]
    extern  stage2_main
    call    stage2_main

.halt:
    cli
    hlt
    jmp     .halt

; ===========================================================
; Print null-terminated string (16-bit helper, called before pmode)
[BITS 16]
print16:
    lodsb
    test    al, al
    jz      .done
    mov     ah, 0x0E
    mov     bx, 0x000F
    int     0x10
    jmp     print16
.done:
    ret

; ===========================================================
; Data
boot_drive: db 0
msg_banner: db 'SakuruBoot Stage2', 0x0D, 0x0A, 0

; --- 64-bit GDT ---
align 8
gdt64:
    dq 0x0000000000000000           ; 0x00  Null descriptor
    dq 0x00AF9A000000FFFF           ; 0x08  64-bit code  (ring 0, L=1, G=1)
    dq 0x00AF92000000FFFF           ; 0x10  64-bit data  (ring 0)
gdt64_end:

gdt64_ptr:
    dw  gdt64_end - gdt64 - 1       ; Limit
    dq  gdt64                       ; 64-bit base (NASM pads to 10 bytes total)
