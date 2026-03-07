# BIOS Loader

The BIOS loader targets legacy x86 PC firmware (MBR boot).  
It is minimal by design — MBR constraints limit stage 1 to 446 bytes.

## Stages

### Stage 1 — MBR (`bios/stage1.asm`)

- 446-byte NASM assembly stub
- Written to the Master Boot Record (sector 0)
- Loads stage 2 from a fixed LBA offset on disk
- Enters protected mode and jumps to stage 2

### Stage 2 — loader (`bios/stage2.c` + `bios/stage2.asm`)

- Reads `sakuru.cfg` from the active partition
- Displays the boot menu (VGA text mode)
- Loads the kernel via INT 13h
- Parses the Linux `boot_params` / `setup_header`
- Passes the command line and jumps to the kernel entry point

## Limitations

| Feature | BIOS support |
|---------|-------------|
| LUKS encryption | ✗ — handle via initramfs |
| ext4 filesystem | ✓ (read-only) |
| FAT filesystem | ✓ |
| AArch64 | ✗ — x86 only |
| Secure Boot | ✗ |

> LUKS support is intentionally omitted from the BIOS target.  
> Unlocking 64 MB of Argon2 memory is not practical in real mode / protected mode before paging.  
> Use an initramfs with `cryptsetup` for BIOS + LUKS setups.

## Writing the MBR

```bash
# Build the BIOS target
make bios

# Write MBR (stage 1) to disk — DESTRUCTIVE, double-check /dev/sdX
dd if=build/sakuruboot.bin of=/dev/sdX bs=446 count=1

# Write stage 2 immediately after the MBR
dd if=build/sakuruboot.bin of=/dev/sdX bs=512 skip=1 seek=1
```

> **Warning:** Writing to the wrong device will destroy data.  
> Use `lsblk` to confirm the correct device before running `dd`.
