# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| 0.1.x   | ✅ Yes     |

Only the latest release receives security fixes. Older versions are not backported.

---

## Scope

SakuruBoot is a bootloader. Security-relevant areas include:

- **Config parser** (`common/config.c`) — buffer overflows, path traversal via `sakuru.cfg`
- **ELF loader** (`os/elf_loader.c`) — malformed ELF headers causing out-of-bounds reads/writes
- **ext4 driver** (`uefi/ext4.c`) — malformed filesystem structures
- **FAT reader** (`bios/stage2/fat.c`) — malformed directory/cluster chains
- **BIOS MBR/Stage 2** — memory corruption or arbitrary code execution from crafted disk images

**Out of scope:** issues that require physical access to replace firmware or the boot drive are generally considered outside the threat model of a bootloader. However, if you find a meaningful attack, please report it anyway.

---

## Reporting a Vulnerability

**Please do not open a public GitHub issue for security vulnerabilities.**

Report privately via one of:

- **GitHub private vulnerability reporting** — use the *Security* tab → *Report a vulnerability*
- **Email** — [hanna@hannaskairipa.com](mailto:hanna@hannaskairipa.com) with the subject line `[SakuruBoot Security]`

Please include:
1. A description of the vulnerability and its potential impact
2. Steps to reproduce (proof-of-concept config, disk image, or code snippet)
3. Affected version(s) and target(s) (UEFI x86\_64, AArch64, BIOS)

---

## Response Timeline

| Step | Target time |
|------|-------------|
| Acknowledgement | Within 3 business days |
| Initial assessment | Within 7 business days |
| Fix / advisory | Dependent on severity |

You will be credited in the release notes and advisory unless you request otherwise.
