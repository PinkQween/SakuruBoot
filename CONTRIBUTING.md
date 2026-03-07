# Contributing to SakuruBoot

Thank you for your interest in contributing! Please read this guide before opening issues or pull requests.

---

## Code of Conduct

This project follows a simple rule: be respectful. Harassment or hostile behavior will not be tolerated.

---

## Getting Started

1. **Fork** the repository and clone your fork locally.
2. Create a **feature branch** from `main`:
   ```sh
   git checkout -b feat/my-feature
   ```
3. Make your changes, following the style guidelines below.
4. **Test your changes** by building the affected targets and booting in QEMU.
5. Open a **Pull Request** against `main`.

---

## Development Setup

See the [README](README.md#building) for the full dependency list and build instructions.

A quick sanity-check cycle:

```sh
make clean && make all        # rebuild everything
make efi-disk && ./run.sh     # boot UEFI image in QEMU
```

---

## Code Style

- **C standard:** C11 (`-std=c11`)
- **Indentation:** 4 spaces — no tabs
- **Braces:** same-line opening brace (`if (x) {`)
- **Comments:** brief, only where the code is non-obvious
- **Headers:** keep `#pragma once` guards
- Keep functions small and single-purpose
- No dynamic memory allocation — this is a bootloader

---

## Commit Messages

Use the conventional format:

```
<type>: <short summary>

<optional body>
```

Types: `feat`, `fix`, `refactor`, `docs`, `build`, `test`, `chore`

Examples:
```
feat: add Multiboot2 kernel loader
fix: correct ext4 block group offset calculation
docs: document sakuru.cfg color options
```

---

## Pull Request Guidelines

- One logical change per PR — keep diffs focused and reviewable
- Describe **what** changed and **why** in the PR description
- If your PR fixes an issue, reference it: `Closes #42`
- All targets in `make all` must build without errors or new warnings before merging

---

## Reporting Bugs

Use the **Bug Report** issue template. Include:
- Host OS and toolchain versions
- The exact `make` target that fails (or the QEMU command used)
- Full error output
- Expected vs. actual behavior

---

## Feature Requests

Use the **Feature Request** issue template. Describe the use case, not just the solution.

---

## Security Issues

Please **do not** open a public issue for security vulnerabilities. See [SECURITY.md](SECURITY.md) for the responsible disclosure process.
