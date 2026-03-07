# sakuru.cfg Reference

SakuruBoot reads `sakuru.cfg` from the root of the EFI System Partition at boot time.  
The file uses a simple key-value syntax with `entry { }` blocks for boot targets.

## Global keys

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `default` | string | first entry | Title of the entry to boot automatically |
| `timeout` | integer | `5` | Seconds to wait before booting the default |

## Entry block keys

| Key | Type | Required | Description |
|-----|------|----------|-------------|
| `title` | string | ✓ | Display name shown in the selection menu |
| `kernel` | string | ✓ | Path to the kernel image on the ESP |
| `initrd` | string | — | Path to the initial ramdisk image |
| `cmdline` | string | — | Kernel command-line arguments |
| `encrypted` | bool | — | Set `yes` to enable LUKS unlock before boot |
| `luks_keyfile` | string | — | Path to a keyfile on the ESP (optional) |
| `luks_tries` | integer | `3` | Number of passphrase attempts before aborting |

## Boolean values

`encrypted` accepts: `yes`, `true`, `1` (case-insensitive) as truthy;  
`no`, `false`, `0` as falsy.

## Comments

Lines starting with `;` are comments and are ignored.

## Example

```ini
; SakuruBoot configuration

default = Arch Linux
timeout = 5

entry {
    title   = Arch Linux
    kernel  = /vmlinuz-linux
    initrd  = /initramfs-linux.img
    cmdline = root=/dev/sda2 rw quiet splash
}

entry {
    title      = Arch Linux (encrypted root)
    kernel     = /vmlinuz-linux
    initrd     = /initramfs-linux.img
    cmdline    = root=/dev/mapper/cryptroot rw quiet
    encrypted  = yes
    luks_tries = 3
}

entry {
    title        = Arch Linux (keyfile unlock)
    kernel       = /vmlinuz-linux
    initrd       = /initramfs-linux.img
    cmdline      = root=/dev/mapper/cryptroot rw
    encrypted    = yes
    luks_keyfile = /keys/root.key
}
```
