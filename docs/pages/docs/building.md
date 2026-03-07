# Building SakuruBoot

SakuruBoot uses a single `Makefile` with three targets: UEFI x86_64, UEFI AArch64, and legacy BIOS.

## Prerequisites

| Target | Toolchain |
|--------|-----------|
| UEFI x86_64 | `x86_64-w64-mingw32-gcc` (MinGW-w64) |
| UEFI AArch64 | `aarch64-elf-gcc` + `aarch64-elf-objcopy` |
| BIOS | native `gcc` + `nasm` |

On Arch Linux:

```bash
pacman -S mingw-w64-gcc nasm
# AArch64 cross-compiler (AUR or manual)
yay -S aarch64-elf-gcc
```

On Debian / Ubuntu:

```bash
apt install gcc-mingw-w64-x86-64 nasm gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu
```

## Build commands

```bash
# UEFI x86_64 (produces build/BOOTX64.EFI)
make uefi-x86

# UEFI AArch64 (produces build/BOOTAA64.EFI)
make uefi-aarch64

# Legacy BIOS (produces build/sakuruboot.bin)
make bios

# All targets
make all

# Clean
make clean
```

## Output files

| File | Description |
|------|-------------|
| `build/BOOTX64.EFI` | UEFI PE32+ binary for x86_64 |
| `build/BOOTAA64.EFI` | UEFI flat binary for AArch64 |
| `build/sakuruboot.bin` | BIOS MBR + stage-2 blob |

## Installing to an EFI partition

```bash
# Mount the EFI System Partition
mount /dev/sdXY /mnt/efi

# Copy the EFI binary
mkdir -p /mnt/efi/EFI/BOOT
cp build/BOOTX64.EFI /mnt/efi/EFI/BOOT/BOOTX64.EFI

# Copy your config
cp sakuru.cfg.example /mnt/efi/sakuru.cfg
# Edit /mnt/efi/sakuru.cfg to match your setup
```

> **Note:** LUKS support is compiled into the UEFI targets only.  
> The BIOS target omits the crypto stack due to MBR size constraints; handle LUKS via initramfs instead.
