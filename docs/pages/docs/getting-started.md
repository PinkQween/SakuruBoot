# Introduction

**SakuruBoot** is a lightweight, freestanding bootloader for x86_64 and AArch64 systems.  
It supports both UEFI and legacy BIOS firmware, loads the Linux kernel directly, and can unlock LUKS1 / LUKS2 encrypted volumes before handing off to the OS.

## Design goals

- **Zero dependencies** — freestanding C11 (`-ffreestanding -nostdlib`). No libc, no GRUB, no shim.
- **Readable configuration** — plain-text `sakuru.cfg` that any text editor can modify.
- **First-class encryption** — LUKS passphrase entry happens in the bootloader itself; the OS never sees the raw block device.
- **Dual architecture** — x86_64 PE32+ for modern UEFI and AArch64 flat ELF for embedded / Raspberry Pi targets.

## Project layout

```
SakuruBoot/
├── common/        # Config parser, passphrase UI, shared types
├── crypto/        # SHA, HMAC, PBKDF2, Blake2b, Argon2, AES, XTS/CBC
├── luks/          # LUKS1 + LUKS2 header parsing and key derivation
├── uefi/          # UEFI entry point, ext4 reader, LUKS integration
├── bios/          # MBR stage-1, stage-2 loader (BIOS only)
├── Makefile
├── sakuru.cfg.example
└── docs/          # This website
```

## Quick start

1. Clone the repo and install the cross-compiler toolchain (see [Building](#building)).
2. Copy `sakuru.cfg.example` to your EFI partition as `sakuru.cfg` and edit the entries.
3. Copy the built `BOOTX64.EFI` to `/EFI/BOOT/` on the EFI System Partition.
4. Reboot — SakuruBoot displays the selection menu.

> **Tip:** For encrypted setups, add `encrypted = yes` and optionally `luks_tries = 3` to the entry.
