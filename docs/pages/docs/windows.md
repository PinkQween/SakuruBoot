# Windows Dual-Boot

SakuruBoot can boot Windows alongside Linux by chain-loading the Windows Boot Manager (`bootmgfw.efi`) on UEFI systems, or by chain-loading the Windows Volume Boot Record (VBR) on legacy BIOS systems.

## How it works

### UEFI (Modern systems)

1. SakuruBoot scans every EFI System Partition for `\EFI\Microsoft\Boot\bootmgfw.efi`.
2. It calls `LoadImage()` to load the Windows Boot Manager as a UEFI application.
3. It calls `StartImage()` — Windows takes full control from this point.
4. Windows Boot Manager reads the **BCD store**, handles **BitLocker**, checks for **fast startup**, and eventually loads `winload.efi` → Windows kernel.

> **SakuruBoot does NOT call ExitBootServices before handing off.**  
> Windows Boot Manager calls it itself — this is required for correct operation.

### BIOS (Legacy MBR systems)

1. SakuruBoot finds the first NTFS partition marked **active** (bootable flag) in the MBR partition table, or the first NTFS partition if none are flagged.
2. It reads the 512-byte **Volume Boot Record** (VBR) of that partition.
3. It copies the VBR to address `0x7C00` (standard BIOS boot address) and jumps to it.
4. The Windows VBR loads `bootmgr`, which then loads Windows.

## Configuration

Add a Windows entry to `sakuru.cfg`:

```ini
[entry:Windows 11]
type = windows
```

That's all — SakuruBoot auto-detects `bootmgfw.efi`. If you need a custom path:

```ini
[entry:Windows 11]
type   = windows
kernel = \EFI\Microsoft\Boot\bootmgfw.efi
```

## Installing Windows for dual-boot

### Prerequisites

- A PC with UEFI firmware (recommended) or BIOS
- Windows 10 or 11 installation media (USB)
- At least one free, unpartitioned region on your disk (20 GB+ for Windows)

### Step 1 — Partition your disk

Use `fdisk`, `gdisk`, or your distro's installer to leave space for Windows.

**Recommended partition layout (UEFI / GPT):**

| Partition | Type | Size | Purpose |
|-----------|------|------|---------|
| `/dev/sda1` | EFI System (FAT32) | 512 MB | Shared ESP — SakuruBoot + bootmgfw.efi |
| `/dev/sda2` | Linux root (ext4) | varies | Your Linux install |
| `/dev/sda3` | Linux swap | 2–8 GB | Optional |
| `/dev/sda4` | Windows (NTFS) | 60 GB+ | Windows system |

> **Tip:** Install Linux first, then Windows — Windows overwrites the MBR/ESP bootloader. After Windows installs, copy `BOOTX64.EFI` back to `\EFI\BOOT\BOOTX64.EFI` on the ESP.

### Step 2 — Install Windows

1. Boot the Windows installer from USB.
2. On the "Where do you want to install Windows?" screen, select the unpartitioned space (or the NTFS partition you created).
3. Let the installer finish. Your system will reboot into Windows directly — the ESP is now controlled by Windows Boot Manager.

### Step 3 — Restore SakuruBoot as the primary bootloader

After Windows installs it places `bootmgfw.efi` at `\EFI\Microsoft\Boot\bootmgfw.efi`.  
Put SakuruBoot back as the default UEFI boot entry:

```bash
# Boot from a Linux live USB, then mount the ESP:
mount /dev/sda1 /mnt/efi

# Copy SakuruBoot over the default EFI boot path:
cp /path/to/BOOTX64.EFI /mnt/efi/EFI/BOOT/BOOTX64.EFI

# Add your sakuru.cfg with a Windows entry:
cat > /mnt/efi/sakuru.cfg << 'EOF'
default  = Linux
timeout  = 5

[entry:Linux]
type    = linux
kernel  = /vmlinuz-linux
initrd  = /initramfs-linux.img
cmdline = root=/dev/sda2 rw quiet

[entry:Windows 11]
type = windows
EOF
```

### Step 4 — Register SakuruBoot with UEFI firmware (recommended)

```bash
# Install efibootmgr (if not already installed):
pacman -S efibootmgr   # Arch Linux
apt install efibootmgr # Debian/Ubuntu

# Register SakuruBoot as a UEFI boot entry:
efibootmgr --create \
  --disk /dev/sda \
  --part 1 \
  --loader '\EFI\BOOT\BOOTX64.EFI' \
  --label 'SakuruBoot'

# Move SakuruBoot to the top of the boot order:
efibootmgr --bootorder XXXX,YYYY   # replace XXXX with SakuruBoot's entry number
```

### Step 5 — Test

Reboot. SakuruBoot menu should appear with both Linux and Windows entries.  
Selecting **Windows 11** chains to Windows Boot Manager.

## BitLocker compatibility

BitLocker works transparently — SakuruBoot loads `bootmgfw.efi` before BitLocker runs.  
Windows Boot Manager decrypts the volume itself using the TPM or recovery key.

> **Do NOT set `encrypted = yes`** on a Windows entry.  
> `encrypted` is SakuruBoot's LUKS feature and is unrelated to BitLocker.

## Fast Startup / Hibernation

If Windows is hibernated (Fast Startup or `shutdown /h`), booting Linux and mounting the Windows NTFS partition read-write **can corrupt it**.

```bash
# In Windows (run as Administrator) — disable Fast Startup:
powercfg /h off

# In Linux — always mount Windows NTFS read-only or with ntfs-3g:
mount -t ntfs-3g -o ro /dev/sda4 /mnt/windows
```

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `bootmgfw.efi not found` | Windows not installed or ESP not mounted | Verify `\EFI\Microsoft\Boot\bootmgfw.efi` exists; set `kernel =` explicitly |
| Black screen after selection | Secure Boot rejects SakuruBoot | Enroll SakuruBoot in MOK or disable Secure Boot in firmware |
| Windows boots but no SakuruBoot menu | Windows reset the boot order | Re-run `efibootmgr` to restore SakuruBoot at position 0 |
| BIOS: Windows VBR not found | Windows partition not marked active | Use `fdisk` → `a` to toggle the active flag on the Windows partition |
