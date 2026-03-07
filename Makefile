# SakuruBoot — Top-level Makefile
.DELETE_ON_ERROR:
#
# Targets:
#   make uefi-x86_64      Build UEFI x86_64 bootloader (BOOTX64.EFI)
#   make uefi-aarch64     Build UEFI AArch64 bootloader (BOOTAA64.EFI)
#   make bios             Build BIOS bootloader (mbr.bin + stage2.bin)
#   make all              Build all targets
#   make clean            Remove build artifacts

# ----------------------------------------------------------------
# Tools
# ----------------------------------------------------------------
NASM        = nasm
LD          = ld
OBJCOPY     = objcopy

# UEFI x86_64: mingw-w64 produces PE32+ directly
CC_X86_UEFI  = x86_64-w64-mingw32-gcc

# UEFI AArch64: bare-metal ELF cross-compiler; we convert to PE via objcopy
CC_ARM_UEFI  = aarch64-elf-gcc
LD_ARM_UEFI  = aarch64-elf-ld
OC_ARM_UEFI  = aarch64-elf-objcopy

# BIOS (native x86_64 freestanding)
CC_BIOS     = gcc

# ----------------------------------------------------------------
# Directories
# ----------------------------------------------------------------
BUILD       = build

# ----------------------------------------------------------------
# Common flags
# ----------------------------------------------------------------
COMMON_CFLAGS = \
	-std=c11 \
	-ffreestanding \
	-fno-stack-protector \
	-nostdlib \
	-I. \
	-O2 \
	-Wall -Wextra

LUKS_SRCS = \
	crypto/sha.c \
	crypto/hmac.c \
	crypto/pbkdf2.c \
	crypto/blake2b.c \
	crypto/argon2.c \
	crypto/aes.c \
	crypto/cipher.c \
	luks/luks1.c \
	luks/luks2.c \
	luks/luks_vol.c \
	common/passphrase.c

# ----------------------------------------------------------------
# UEFI x86_64
# ----------------------------------------------------------------
UEFI_X86_SRCS = \
	uefi/x86_64/entry.c \
	uefi/uefi_loader.c \
	uefi/ext4.c \
	common/config.c \
	common/menu.c \
	os/elf_loader.c \
	os/vios.c \
	os/linux.c \
	os/windows.c \
	$(LUKS_SRCS)

UEFI_X86_CFLAGS = $(COMMON_CFLAGS) -fshort-wchar -mno-red-zone -DSAKURU_UEFI

UEFI_X86_LDFLAGS = \
	-Wl,--subsystem,10 \
	-Wl,-e,efi_main \
	-Wl,--strip-all

$(BUILD)/BOOTX64.EFI: $(UEFI_X86_SRCS) | $(BUILD)
	$(CC_X86_UEFI) $(UEFI_X86_CFLAGS) $(UEFI_X86_LDFLAGS) \
		-o $@ $(UEFI_X86_SRCS)

.PHONY: uefi-x86_64
uefi-x86_64: $(BUILD)/BOOTX64.EFI

# ----------------------------------------------------------------
# UEFI AArch64
# ----------------------------------------------------------------
UEFI_ARM_SRCS = \
	uefi/aarch64/entry.c \
	uefi/uefi_loader.c \
	uefi/ext4.c \
	common/config.c \
	common/menu.c \
	os/elf_loader.c \
	os/vios.c \
	os/linux.c \
	os/windows.c \
	$(LUKS_SRCS)

UEFI_ARM_CFLAGS = $(COMMON_CFLAGS) -fshort-wchar -DSAKURU_UEFI

# Compile ELF, then convert to flat PE32+ binary (QEMU accepts flat for aarch64)
$(BUILD)/BOOTAA64.elf: $(UEFI_ARM_SRCS) | $(BUILD)
	$(CC_ARM_UEFI) $(UEFI_ARM_CFLAGS) \
		-Wl,-e,efi_main -Wl,-Ttext=0 \
		-o $@ $(UEFI_ARM_SRCS)

# Convert to raw binary (QEMU virt aarch64 can load flat images)
$(BUILD)/BOOTAA64.EFI: $(BUILD)/BOOTAA64.elf
	$(OC_ARM_UEFI) -O binary $< $@

.PHONY: uefi-aarch64
uefi-aarch64: $(BUILD)/BOOTAA64.EFI

# ----------------------------------------------------------------
# BIOS — Stage 1 (MBR) and Stage 2
# ----------------------------------------------------------------
BIOS_STAGE2_SRCS = \
	bios/stage2/main.c \
	bios/stage2/disk.c \
	bios/stage2/fat.c \
	common/config.c \
	common/menu.c \
	os/elf_loader.c \
	os/vios.c \
	os/linux.c \
	os/windows.c

BIOS_CFLAGS = \
	-std=c11 -m64 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pic \
	-mno-red-zone \
	-nostdlib \
	-I. \
	-O2 \
	-Wall -Wextra

BIOS_STAGE2_OBJS = \
	$(BUILD)/bios/entry.o \
	$(patsubst %.c,$(BUILD)/bios/%.o,$(BIOS_STAGE2_SRCS))

$(BUILD)/bios/entry.o: bios/stage2/entry.asm | $(BUILD)/bios
	$(NASM) -f elf64 -o $@ $<

$(BUILD)/bios/%.o: %.c | $(BUILD)/bios
	@mkdir -p $(dir $@)
	$(CC_BIOS) $(BIOS_CFLAGS) -c -o $@ $<

$(BUILD)/bios/stage2.elf: $(BIOS_STAGE2_OBJS) bios/stage2/linker.ld
	$(LD) -T bios/stage2/linker.ld -o $@ $(BIOS_STAGE2_OBJS) --nostdlib

$(BUILD)/bios/stage2.bin: $(BUILD)/bios/stage2.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/bios/mbr.bin: bios/stage1/mbr.asm | $(BUILD)/bios
	$(NASM) -f bin -o $@ $<

.PHONY: bios
bios: $(BUILD)/bios/mbr.bin $(BUILD)/bios/stage2.bin

# ----------------------------------------------------------------
# Raw BIOS disk image (MBR + stage2 in first 64 sectors)
# ----------------------------------------------------------------
$(BUILD)/sakuruboot.img: bios | $(BUILD)
	dd if=/dev/zero bs=512 count=2880 status=none > $@
	dd if=$(BUILD)/bios/mbr.bin    of=$@ bs=512 seek=0  conv=notrunc status=none
	dd if=$(BUILD)/bios/stage2.bin of=$@ bs=512 seek=1  conv=notrunc status=none

.PHONY: img
img: $(BUILD)/sakuruboot.img

# ----------------------------------------------------------------
# ViOS kernel (built here for convenience / run.sh)
# ----------------------------------------------------------------
VIOS_X86_SRCS = \
	../ViOS/arch/x86_64/boot.S \
	../ViOS/kernel/main.c \
	../ViOS/drivers/uart.c \
	../ViOS/drivers/fb.c \
	../ViOS/drivers/amd_gpu.c

VIOS_CFLAGS = \
	-std=c11 -m64 \
	-ffreestanding \
	-fno-stack-protector \
	-fno-pic \
	-no-pie \
	-mno-red-zone \
	-nostdlib \
	-I../ViOS/include \
	-O2

$(BUILD)/vios-x86_64.elf: $(VIOS_X86_SRCS) | $(BUILD)
	$(CC_BIOS) $(VIOS_CFLAGS) \
		-T ../ViOS/kernel/linker.ld \
		-o $@ $(VIOS_X86_SRCS)

.PHONY: vios
vios: $(BUILD)/vios-x86_64.elf

# ----------------------------------------------------------------
# Shell.efi — included in the ESP so the "UEFI Shell" menu entry works.
# Checks common system install paths first; downloads if none found.
# ----------------------------------------------------------------
SHELL_EFI = $(BUILD)/Shell_x64.efi

SHELL_EFI_SYSTEM_PATHS = \
    /usr/share/edk2/x64/Shell_Full.efi \
    /usr/share/edk2/x64/Shell.efi \
    /usr/share/edk2-shell/x64/Shell_Full.efi \
    /usr/share/ovmf/x64/Shell.efi \
    /usr/share/OVMF/Shell.efi

$(SHELL_EFI): | $(BUILD)
	@found=; \
	for p in $(SHELL_EFI_SYSTEM_PATHS); do \
	    if [ -f "$$p" ]; then found="$$p"; break; fi; \
	done; \
	if [ -n "$$found" ]; then \
	    echo "[shell] Using $$found"; cp "$$found" $@; \
	else \
	    echo "[shell] Downloading shellx64.efi from EDK2 releases..."; \
	    curl -fsSL -o $@ \
	        https://github.com/pbatard/UEFI-Shell/releases/download/26H1/shellx64.efi \
	    || { echo "ERROR: Failed to download Shell.efi"; rm -f $@; exit 1; }; \
	fi


$(BUILD)/efi_disk.img: uefi-x86_64 vios $(SHELL_EFI) | $(BUILD)
	@# Create a FAT32 ESP image
	dd if=/dev/zero bs=1M count=64 status=none > $@
	mformat -i $@ -F ::
	mmd -i $@ ::/EFI
	mmd -i $@ ::/EFI/BOOT
	mmd -i $@ ::/EFI/Shell
	mmd -i $@ ::/boot
	mmd -i $@ ::/boot/vios
	mcopy -i $@ $(BUILD)/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
	mcopy -i $@ $(SHELL_EFI) ::/EFI/Shell/Shell.efi
	mcopy -i $@ sakuru.cfg.example ::/sakuru.cfg
	mcopy -i $@ $(BUILD)/vios-x86_64.elf ::/boot/vios/kernel.elf

.PHONY: efi-disk
efi-disk: $(BUILD)/efi_disk.img

# ----------------------------------------------------------------
.PHONY: all clean $(BUILD) $(BUILD)/bios

all: uefi-x86_64 uefi-aarch64 bios vios

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/bios:
	mkdir -p $(BUILD)/bios

clean:
	rm -rf $(BUILD)
