# SakuruBoot

> A lightweight, config-driven bootloader for x86\_64 and AArch64 — supporting both UEFI and legacy BIOS.

SakuruBoot presents an interactive boot menu read from a simple `sakuru.cfg` file on the boot partition. It supports loading **ELF64 kernels** (including ViOS), **Linux bzImage** kernels with an optional initrd, and **Multiboot2**-compliant kernels.

---

## Features

- **Dual firmware support** — UEFI (x86\_64 `BOOTX64.EFI` · AArch64 `BOOTAA64.EFI`) and legacy BIOS (MBR + Stage 2)
- **Config-driven menu** — human-readable `sakuru.cfg` with theming, timeout, and per-entry options
- **Multiple kernel types** — `elf64`, `linux`, `multiboot2`, `windows`, `uefi_shell`
- **Windows dual-boot** — UEFI: chain-loads `bootmgfw.efi` via `LoadImage`/`StartImage` (BitLocker & BCD transparent); BIOS: VBR chain-load from the active NTFS partition
- **LUKS full-disk encryption** — LUKS1 (PBKDF2) and LUKS2 (Argon2id/Argon2i/PBKDF2) in-bootloader unlock; passphrase prompt with masking, optional key file
- **Theming** — configurable background and accent colors via EFI color names
- **ext4 read support** (UEFI path) and FAT32 (BIOS path), including ext4-on-LUKS layering
- **Auto-discovery** — scans mounted volumes for kernels when no config is present

---

## Building

### Dependencies

| Target | Toolchain |
|--------|-----------|
| UEFI x86\_64 | `x86\_64-w64-mingw32-gcc` |
| UEFI AArch64 | `aarch64-elf-gcc`, `aarch64-elf-ld`, `aarch64-elf-objcopy` |
| BIOS | `gcc` (native x86\_64), `nasm`, `ld`, `objcopy` |
| Disk images | `mtools` (`mformat`, `mmd`, `mcopy`), `dd` |

```sh
# Debian / Ubuntu
sudo apt install gcc nasm mtools binutils \
    gcc-x86-64-linux-gnu mingw-w64 \
    gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
```

### Make targets

```sh
make uefi-x86_64     # → build/BOOTX64.EFI
make uefi-aarch64    # → build/BOOTAA64.EFI
make bios            # → build/bios/mbr.bin + build/bios/stage2.bin
make vios            # → build/vios-x86_64.elf  (companion kernel)
make efi-disk        # → build/efi_disk.img      (bootable FAT32 ESP)
make img             # → build/sakuruboot.img    (raw BIOS disk image)
make all             # build everything
make clean           # remove build/
```

---

## Configuration

Copy `sakuru.cfg.example` to `sakuru.cfg` on your boot partition root:

```ini
timeout = 5
default = 0
theme   = magenta
accent  = yellow

[entry:ViOS]
type    = elf64
kernel  = /boot/vios/kernel.elf
cmdline = console=ttyS0,115200 loglevel=3

[entry:Linux]
type    = linux
kernel  = /boot/vmlinuz
initrd  = /boot/initrd.img
cmdline = root=/dev/sda2 rw quiet splash
```

| Key | Description |
|-----|-------------|
| `timeout` | Seconds before auto-booting the default entry (0 = immediate) |
| `default` | Zero-based index of the default entry |
| `theme` | Background color of the selection box (EFI bg color name, 0–7) |
| `accent` | Color of the title, arrow, and hint text (EFI fg color name, 0–15) |
| `type` | `elf64` · `linux` · `multiboot2` · `uefi_shell` |
| `kernel` | Path to the kernel image on the boot partition |
| `initrd` | *(optional)* Path to the initial ramdisk |
| `cmdline` | *(optional)* Kernel command-line arguments |
| `encrypted` | `yes` to enable LUKS unlock for this entry |
| `luks_keyfile` | *(optional)* Path to a key file; if unset, prompts for passphrase |
| `luks_tries` | Passphrase attempts before giving up (default: `3`) |

---

## Running in QEMU

A `run.sh` is provided at the repository root. It boots the UEFI disk image using OVMF:

```sh
./run.sh
```

---

## Project Structure

```
SakuruBoot/
├── bios/
│   ├── stage1/       MBR (NASM)
│   └── stage2/       BIOS Stage 2 loader (C + NASM)
├── common/
│   ├── config.c/h    sakuru.cfg parser
│   ├── menu.c/h      Interactive boot menu
│   └── version.h     Version constant
├── os/
│   ├── elf_loader.c/h  ELF64 kernel loader
│   ├── linux.c         Linux bzImage loader
│   └── vios.c          ViOS-specific boot path
├── uefi/
│   ├── x86_64/       UEFI x86_64 entry point
│   ├── aarch64/      UEFI AArch64 entry point
│   ├── uefi_loader.c/h UEFI boot logic
│   └── ext4.c/h      ext4 filesystem driver
├── Makefile
└── sakuru.cfg.example
```

---

## License

[MIT](LICENSE)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).

## Security

See [SECURITY.md](SECURITY.md).
