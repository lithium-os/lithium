# ================================================
# Lithium core Makefile, this handles all the fun
# complex parts of building the kernel and building
# the ISO are all handed in this monolithic makefile.
#
# (c) 2026 LithiumOS Project
#
# SPDX-License-Identifier: GPL-3.0-Only
#
# ================================================

# ANSI colours for pretty printed compiler guff
GREEN  = \033[1;32m
BLUE   = \033[1;34m
YELLOW = \033[1;33m
RED    = \033[1;31m
RESET  = \033[0m

# Tooling / OS detection
ifeq ($(OS),Windows_NT)
	$(error Windows is not supported, using a Linux VM or WSL is advised.)
else
	CC = x86_64-linux-gnu-gcc
	AS = x86_64-linux-gnu-as
	LD = x86_64-linux-gnu-ld
endif

SRCD 	:= src/kernel
ISOD 	:= iso
OBJD 	:= obj
PREPD 	:= preproc
ASMD  	:= asm
LINK 	:= src/linker.ld
KERNEL 	:= kernel.elf

SRCS := $(shell find $(SRCD) -type f -name '*.c' 2>/dev/null)
OBJS := $(patsubst $(SRCD)/%, $(OBJD)/%, $(SRCS:.c=.o))
PREPS := $(patsubst $(SRCD)/%, $(PREPD)/%, $(SRCS:.c=.i))
ASMS := $(patsubst $(SRCD)/%, $(ASMD)/%, $(SRCS:.c=.s))

# A lot of flags, perfect for pedantic compiling!
CFLAGS = \
	-Wall -Wextra -Wpedantic -pipe -ffreestanding \
	-fno-stack-protector -fno-pic -mno-80387 -mno-mmx \
	-mno-sse2 -mno-red-zone -fno-omit-frame-pointer \
	-Wno-unused-function -Wno-unused-parameter \
	-Wno-pointer-to-int-cast -fno-unwind-tables \
	-Werror=implicit-function-declaration -Werror=return-type \
	-Werror=implicit-int -Werror=incompatible-pointer-types \
	-fno-delete-null-pointer-checks -fno-strict-overflow \
	-fno-common -fno-builtin -Wshadow -Wcast-align -Wundef \
	-mcmodel=kernel -I$(SRCD)/include -O3

LDFLAGS = \
	-nostdlib -static --no-dynamic-linker -z text \
	-z max-page-size=0x1000 -T $(LINK)

.PHONY: all clean iso run preprocess assemble

all: $(KERNEL)

$(OBJD):
	@mkdir -p $(OBJD)

$(PREPD):
	@mkdir -p $(PREPD)

$(ASMD):
	@mkdir -p $(ASMD)

# Preprocess only - stop after preprocessing
$(PREPD)/%.i: $(SRCD)/%.c | $(PREPD)
	@printf "$(YELLOW)[CPP]$(RESET) $<\n"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -E $< -o $@

# Assemble only - stop after assembly generation
$(ASMD)/%.s: $(SRCD)/%.c | $(ASMD)
	@printf "$(BLUE)[ASM]$(RESET) $<\n"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -S $< -o $@

# Compile C sources
$(OBJD)/%.o: $(SRCD)/%.c | $(OBJD)
	@printf "$(BLUE)[CC ]$(RESET) $<\n"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

# targets to build all preprocessed/assembly files
preprocess: $(PREPS)
	@printf "$(GREEN)All files preprocessed!$(RESET)\n"

assemble: $(ASMS)
	@printf "$(GREEN)All files assembled!$(RESET)\n"

$(KERNEL): $(OBJS)
	@printf "$(GREEN)[LD ]$(RESET) $@\n"
	@$(LD) $(LDFLAGS) $(OBJS) -o $@
	@printf "$(GREEN)Kernel built successfully!$(RESET)\n"

iso: $(KERNEL)
	@printf "$(YELLOW)[ISO]$(RESET) Building bootable image...\n"
	@mkdir -p $(ISOD)/boot $(ISOD)/EFI/BOOT
	@cp $(KERNEL) $(ISOD)/boot/
	@cp limine.conf $(ISOD)/
	@cp limine/limine-bios.sys $(ISOD)/boot/
	@cp limine/limine-bios-cd.bin $(ISOD)/boot/
	@cp limine/limine-uefi-cd.bin $(ISOD)/boot/
	@cp limine/BOOTX64.EFI $(ISOD)/EFI/BOOT/
	@xorriso -as mkisofs -b boot/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(ISOD) -o lithium.iso 2>&1 | grep -v "^xorriso" || true
	@./limine/limine bios-install lithium.iso 2>/dev/null
	@printf "$(GREEN)ISO built:$(RESET) lithium.iso\n"

run: iso
	@printf "$(BLUE)[QEMU]$(RESET) Launching Lithium via qemu..."
	@qemu-system-x86_64 -cdrom lithium.iso -serial stdio -m 2G

clean:
	@printf "$(RED)[CLEAN]$(RESET) Removing build artifacts...\n"
	@rm -rf $(OBJD) $(KERNEL) $(ISOD) lithium.iso $(PREPD) $(ASMD)
	@printf "$(RED)[CLEAN]$(RESET) Removing object files...\n"
	@rm -rf src/kernel/*.o
	@printf "$(GREEN)Clean complete!$(RESET)\n"