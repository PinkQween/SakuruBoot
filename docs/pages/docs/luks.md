# LUKS Encryption

SakuruBoot can unlock LUKS1 and LUKS2 encrypted block devices before the kernel boots.  
This lets you keep your root filesystem encrypted at rest without a custom initramfs.

## How it works

1. After the menu selection, SakuruBoot checks if `encrypted = yes` is set on the entry.
2. If so, it locates the block device backing the kernel's partition.
3. It detects the LUKS version by reading the magic bytes at offset 0.
4. It prompts for a passphrase (or reads a keyfile from the ESP).
5. It derives the master key using the slot's KDF (PBKDF2 for LUKS1, Argon2id or PBKDF2 for LUKS2).
6. It verifies the key against the digest, then sets up on-the-fly AES decryption.
7. The ext4 reader is given a LUKS-backed I/O context — all reads are transparently decrypted.
8. The kernel and initrd are loaded from the decrypted filesystem.

## LUKS1 support

| Feature | Status |
|---------|--------|
| PBKDF2 key derivation | ✓ |
| AF-split key material | ✓ |
| AES-XTS cipher | ✓ |
| AES-CBC-plain / CBC-plain64 / CBC-ESSIV | ✓ |
| All 8 key slots | ✓ |
| Keyfile unlock | ✓ |

## LUKS2 support

| Feature | Status |
|---------|--------|
| JSON header parsing | ✓ (minimal, slots 0–31) |
| PBKDF2 KDF | ✓ |
| Argon2id / Argon2i / Argon2d | ✓ |
| Blake2b digest verification | ✓ |
| AES-XTS / CBC cipher modes | ✓ |
| Keyfile unlock | ✓ |

## Passphrase entry

- Characters are masked with `*` as you type.
- `Backspace` deletes the last character.
- `ESC` cancels and returns to the menu.
- After `luks_tries` failed attempts, the entry is skipped.

## Keyfile unlock

If `luks_keyfile` is set, SakuruBoot reads the keyfile from the EFI System Partition  
instead of prompting for a passphrase:

```ini
encrypted    = yes
luks_keyfile = /keys/root.key
```

> **Security note:** The keyfile lives on the unencrypted ESP. Use TPM sealing  
> or a separate unlocked USB key for a stronger threat model.

## FAT-backed partitions

If the kernel is loaded from a FAT volume (e.g., combined ESP), LUKS unlock is skipped  
for that entry and a console note is printed. In that case, configure LUKS unlock in initramfs.
