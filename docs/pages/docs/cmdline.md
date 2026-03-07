# Kernel Command Line

The `cmdline` value in each `entry` block is passed verbatim to the Linux kernel as its command line.

## Common parameters

| Parameter | Example | Description |
|-----------|---------|-------------|
| `root=` | `root=/dev/sda2` | Root filesystem device |
| `root=` (encrypted) | `root=/dev/mapper/cryptroot` | dm-crypt device name |
| `rw` / `ro` | `rw` | Mount root read-write or read-only |
| `quiet` | `quiet` | Suppress kernel boot messages |
| `splash` | `splash` | Show Plymouth splash screen |
| `loglevel=` | `loglevel=3` | Kernel log verbosity (0–7) |
| `init=` | `init=/bin/bash` | Override init process |
| `single` | `single` | Boot into single-user / rescue mode |

## Encrypted root

When `encrypted = yes` is set, SakuruBoot unlocks the LUKS volume and exposes it via  
`/dev/mapper/cryptroot` (the device mapper name is derived from the volume UUID).  
Pass this device as `root=` so the kernel mounts it directly:

```ini
cmdline = root=/dev/mapper/cryptroot rw quiet
```

> The initramfs does **not** need to perform LUKS unlock — SakuruBoot already did it.

## Kernel parameters documentation

For a full reference of Linux kernel parameters, see the  
[kernel.org documentation](https://www.kernel.org/doc/html/latest/admin-guide/kernel-parameters.html).
