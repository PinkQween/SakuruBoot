#!/usr/bin/env bash
# scripts/test-qemu.sh — Headless QEMU boot smoke test for SakuruBoot
#
# Usage: bash scripts/test-qemu.sh <path/to/BOOTX64.EFI> [timeout_secs]
#
# Exits 0 if "SakuruBoot" banner appears in serial output within timeout.
# Exits 1 on timeout or unexpected failure.
#
# Requires: qemu-system-x86_64, OVMF (tianocore)
# OVMF is looked for in common distro paths; set OVMF_CODE to override.

set -euo pipefail

EFI_IMAGE="${1:?Usage: $0 <BOOTX64.EFI>}"
TIMEOUT="${2:-30}"

# ── Locate OVMF firmware ─────────────────────────────────────────────────
if [[ -n "${OVMF_CODE:-}" ]]; then
    OVMF="${OVMF_CODE}"
else
    for candidate in \
        /usr/share/OVMF/OVMF_CODE.fd \
        /usr/share/ovmf/OVMF.fd \
        /usr/share/edk2/ovmf/OVMF_CODE.fd \
        /usr/share/qemu/OVMF.fd; do
        if [[ -f "${candidate}" ]]; then
            OVMF="${candidate}"
            break
        fi
    done
fi

if [[ -z "${OVMF:-}" ]]; then
    echo "ERROR: OVMF firmware not found. Install ovmf package or set OVMF_CODE."
    exit 1
fi

echo "OVMF : ${OVMF}"
echo "Image: ${EFI_IMAGE}"

# ── Build a minimal ESP disk image ───────────────────────────────────────
WORKDIR="$(mktemp -d)"
trap 'rm -rf "${WORKDIR}"' EXIT

ESP="${WORKDIR}/esp.img"
SERIAL_LOG="${WORKDIR}/serial.log"

# 64 MiB FAT32 ESP image
dd if=/dev/zero of="${ESP}" bs=1M count=64 status=none
/sbin/mkfs.vfat -F 32 "${ESP}" >/dev/null

# Install EFI binary at EFI/BOOT/BOOTX64.EFI
mmd -i "${ESP}" ::EFI ::EFI/BOOT
mcopy -i "${ESP}" "${EFI_IMAGE}" ::EFI/BOOT/BOOTX64.EFI

# Minimal sakuru.cfg so the menu has at least one entry
printf '[entry:TestOS]\ntype=linux\nkernel=/vmlinuz\n' > "${WORKDIR}/sakuru.cfg"
mcopy -i "${ESP}" "${WORKDIR}/sakuru.cfg" ::sakuru.cfg

# ── Launch QEMU ──────────────────────────────────────────────────────────
qemu-system-x86_64 \
    -nodefaults \
    -nographic \
    -no-reboot \
    -machine q35,accel=tcg \
    -m 256M \
    -drive if=pflash,format=raw,readonly=on,file="${OVMF}" \
    -drive format=raw,file="${ESP}" \
    -serial file:"${SERIAL_LOG}" \
    -monitor none \
    &
QEMU_PID=$!

# ── Poll for banner ───────────────────────────────────────────────────────
echo "Waiting up to ${TIMEOUT}s for SakuruBoot banner..."
DEADLINE=$(( $(date +%s) + TIMEOUT ))
SUCCESS=0

while [[ $(date +%s) -lt ${DEADLINE} ]]; do
    if [[ -f "${SERIAL_LOG}" ]] && grep -q "SakuruBoot" "${SERIAL_LOG}" 2>/dev/null; then
        SUCCESS=1
        break
    fi
    sleep 1
done

kill "${QEMU_PID}" 2>/dev/null || true
wait "${QEMU_PID}" 2>/dev/null || true

if [[ ${SUCCESS} -eq 1 ]]; then
    echo "PASS: SakuruBoot banner detected in serial output."
    exit 0
else
    echo "FAIL: Timeout (${TIMEOUT}s) waiting for SakuruBoot banner."
    echo "--- Serial log (last 20 lines) ---"
    tail -20 "${SERIAL_LOG}" 2>/dev/null || true
    exit 1
fi
