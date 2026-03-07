; SakuruBoot MBR (Stage 1)
; ========================
; Loaded by BIOS at 0x7C00. Relocates to 0x0600 to free up
; space, then loads Stage 2 (sectors 1-64) at 0x7E00.
;
; Assemble: nasm -f bin -o mbr.bin mbr.asm

[BITS 16]
[ORG 0x7C00]

start:
    cli
    cld
    xor     ax, ax
    mov     ds, ax
    mov     es, ax
    mov     ss, ax
    mov     sp, 0x7C00

    ; Relocate MBR from 0x7C00 to 0x0600
    mov     si, 0x7C00
    mov     di, 0x0600
    mov     cx, 512
    rep     movsb
    jmp     0x0000:relocated

relocated:
    sti
    mov     [boot_drive], dl        ; Save boot drive number (DL from BIOS)

    ; Load Stage 2: 64 sectors starting at LBA 1, to segment 0x0000 offset 0x7E00
    mov     ah, 0x42                ; INT 13h Extended Read
    mov     dl, [boot_drive]
    mov     si, dap
    int     0x13
    jc      disk_error

    ; Jump to Stage 2 entry
    mov     dl, [boot_drive]        ; Restore DL for stage2
    jmp     0x0000:0x7E00

disk_error:
    mov     si, msg_error
    call    print16
.halt:
    cli
    hlt
    jmp     .halt

; Print null-terminated string via BIOS teletype (INT 10h AH=0Eh)
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

; --- Data ---
boot_drive: db 0
msg_error:  db 'SakuruBoot: disk error!', 0x0D, 0x0A, 0

; Disk Address Packet for INT 13h AH=42h
align 4
dap:
    db  0x10        ; size of DAP = 16 bytes
    db  0x00        ; reserved
    dw  64          ; number of sectors to transfer (Stage 2 max size)
    dw  0x7E00      ; destination offset
    dw  0x0000      ; destination segment
    dq  1           ; source LBA (sector 1)

; Pad to 446 bytes (before partition table)
times 446 - ($ - $$) db 0

; Partition table (4 × 16 bytes = 64 bytes) — filled by partitioning tool
times 64 db 0

; Boot signature
dw 0xAA55
