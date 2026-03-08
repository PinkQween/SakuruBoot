# Loader Entries (systemd-boot Type 1)

SakuruBoot supports the **Boot Loader Specification Type 1** entry format, also known as *loader entries*.  Tools like `kernel-install`, `pacman` hooks, and Arch Linux's `mkinitcpio` automatically drop `.conf` files into `/loader/entries/` on the ESP.  SakuruBoot picks these up and merges them into the boot menu alongside entries from `sakuru.cfg`.

---

## Entry format

Each entry is a plain-text `.conf` file located at:

```
/loader/entries/<machine-id>-<kernel-version>.conf
```

Supported keys:

| Key | Description |
|-----|-------------|
| `title` | Display name shown in the boot menu |
| `linux` | Absolute path to the kernel image on the ESP |
| `initrd` | Path to the initial RAM disk (optional; may appear multiple times) |
| `options` | Kernel command-line arguments |

### Example

```ini
title   Arch Linux (6.12.1-arch1)
linux   /vmlinuz-linux
initrd  /intel-ucode.img
initrd  /initramfs-linux.img
options root=UUID=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx rw quiet
```

---

## How SakuruBoot scans entries

At startup, after loading `sakuru.cfg`, SakuruBoot:

1. Opens the `/loader/entries/` directory on the ESP root.
2. Iterates every `.conf` file found (alphabetical order, firmware-dependent).
3. Parses each file and appends the resulting `BootEntry` to the live configuration.
4. Stops when `MAX_ENTRIES` (16) is reached.

Entries from `sakuru.cfg` are listed **first**; loader entries follow.

---

## Automatic kernel updates with kernel-install

Distributions using `kernel-install` will automatically create and remove loader entries.  No manual `sakuru.cfg` editing is needed for new kernels.

### Arch Linux

```bash
# Install a kernel — kernel-install writes /loader/entries/
sudo pacman -S linux linux-headers

# SakuruBoot will pick it up on next boot automatically.
```

### Debian / Ubuntu (with systemd ≥ 250)

```bash
# Enable the EFI stub installation path
echo 'GRUB_CMDLINE_LINUX=""' | sudo tee /etc/default/grub
sudo kernel-install add $(uname -r) /boot/vmlinuz-$(uname -r)
```

---

## Fallback

If `/loader/entries/` does not exist or is empty, SakuruBoot proceeds with only the entries defined in `sakuru.cfg`.  There is no error — the absence of loader entries is a normal configuration.

---

## Disabling loader entry scanning

Add the following line to `sakuru.cfg` to suppress the directory scan entirely:

```ini
scan_loader_entries = no
```

This is useful if you have many kernel versions installed but want precise control over which ones appear in the menu.
