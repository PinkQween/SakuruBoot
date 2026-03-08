# Secure Boot

SakuruBoot supports **UEFI Secure Boot** through two complementary mechanisms:

1. **Self-signing** — sign the `BOOTX64.EFI` / `BOOTAA64.EFI` image with your own Machine Owner Key (MOK) enrolled in the firmware's MOK database.
2. **CMake sign target** — `cmake --build . --target sign-efi` automates signing with `sbsign` or `sbctl`.

> **Note:** Secure Boot only protects the *bootloader* binary itself.  For end-to-end chain verification, also enable `dm-verity` or Linux `IMA` on your root filesystem.

---

## Prerequisites

| Tool | Purpose | Install (Debian/Ubuntu) |
|------|---------|-------------------------|
| `openssl` | Generate MOK key pair | `apt install openssl` |
| `sbsign` | Sign EFI binaries | `apt install sbsigntool` |
| `mokutil` | Enroll MOK in shim | `apt install mokutil` |
| `sbctl` | Unified Secure Boot management | `apt install sbctl` *(or build from source)* |

---

## Option A — Custom MOK (Recommended for most users)

This is the standard approach used by distributions that ship a Shim bootloader.

### 1. Generate a MOK key pair

```bash
openssl req -newkey rsa:4096 -nodes -keyout MOK.key -new -x509 -sha256 \
    -days 3650 -subj "/CN=SakuruBoot MOK $(hostname)/" -out MOK.crt
openssl x509 -outform DER -in MOK.crt -out MOK.cer
```

### 2. Sign the EFI image

```bash
sbsign --key MOK.key --cert MOK.crt \
    --output BOOTX64.EFI.signed BOOTX64.EFI
mv BOOTX64.EFI.signed /boot/EFI/BOOT/BOOTX64.EFI
```

### 3. Enroll the MOK

On the *target machine* (or in a VM with OVMF Secure Boot enabled):

```bash
sudo mokutil --import MOK.cer
sudo reboot
```

During the next boot the MOK Manager (part of shim) will ask you to confirm enrollment with the password you chose. After confirming, reboot again — SakuruBoot will now load successfully.

### 4. Verify the signature

```bash
sbverify --cert MOK.crt /boot/EFI/BOOT/BOOTX64.EFI
```

---

## Option B — sbctl (Custom Secure Boot database)

`sbctl` manages a custom Platform Key / Key Exchange Key / db setup, suitable for systems where you own the full firmware key hierarchy (e.g. custom hardware or a VM where you can set PK/KEK).

```bash
# One-time setup: create and enroll your own key hierarchy
sudo sbctl create-keys
sudo sbctl enroll-keys --microsoft   # keep MS keys for hardware compatibility

# Sign SakuruBoot
sudo sbctl sign -s /boot/EFI/BOOT/BOOTX64.EFI
sudo sbctl sign -s /boot/EFI/BOOT/BOOTAA64.EFI  # AArch64 if applicable

# Verify
sbctl verify /boot/EFI/BOOT/BOOTX64.EFI
```

After signing, enable Secure Boot in your UEFI firmware setup.

---

## Building a signed image with CMake

The SakuruBoot CMake build includes a `sign-efi` target.  Configure it by passing the key and certificate paths:

```bash
cmake -B build/uefi-x86_64 \
    -DSAKURU_TARGET=uefi-x86_64 \
    -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/uefi-x86_64.cmake \
    -DSAKURU_SIGN=ON \
    -DSAKURU_SIGN_KEY=/path/to/MOK.key \
    -DSAKURU_SIGN_CERT=/path/to/MOK.crt

cmake --build build/uefi-x86_64
# Signed image: build/uefi-x86_64/BOOTX64_signed.EFI
```

The `cmake/SignEFI.cmake` module invokes `sbsign` (preferred) or falls back to `sbctl sign` if `sbsign` is not in `PATH`.

---

## Verifying Secure Boot status at runtime

In a running Linux system:

```bash
# Check if Secure Boot is active
mokutil --sb-state
# or
cat /sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c | xxd

# List enrolled MOK certificates
mokutil --list-enrolled
```

SakuruBoot itself will print `[Secure Boot: enabled]` on the serial console when compiled with `-DSAKURU_DEBUG` and `debug = yes` in `sakuru.cfg`.

---

## Frequently Asked Questions

**Q: Do I need to re-sign after updating SakuruBoot?**  
Yes — any modification to the `.EFI` binary invalidates the signature.  Re-run `sbsign` (or `cmake --build . --target sign-efi`) after every update.

**Q: My firmware only accepts Microsoft-signed binaries.**  
Use `sbctl enroll-keys --microsoft` to keep the Microsoft UEFI CA alongside your own keys, or use a Shim (from your distribution) that is already Microsoft-signed and then enroll your MOK through it.

**Q: Can I use SakuruBoot with Windows Secure Boot enabled?**  
Yes.  Chain-loading `bootmgfw.efi` does not require disabling Secure Boot — Windows verifies its own loader chain independently.  You only need SakuruBoot itself to be trusted by the firmware.

**Q: What hash algorithm does SakuruBoot use for its PE checksum?**  
The PE32+ checksum is computed by `sbsign`/`sbctl` at sign time using SHA-256, matching the UEFI Secure Boot specification requirement.
