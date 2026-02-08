.PHONY: all kernel iso run clean

all: iso

kernel:
	$(MAKE) -C kernel

iso: kernel
	mkdir -p iso/boot
	cp kernel/kernel.elf iso/boot/
	cp limine.conf iso/
	cp limine/limine-bios.sys iso/boot/
	cp limine/limine-bios-cd.bin iso/boot/
	cp limine/limine-uefi-cd.bin iso/boot/
	mkdir -p iso/EFI/BOOT
	cp limine/BOOTX64.EFI iso/EFI/BOOT/
	xorriso -as mkisofs -b boot/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso -o lithium.iso
	./limine/limine bios-install lithium.iso

run: iso
	qemu-system-x86_64 -cdrom lithium.iso -serial stdio -m 2G

clean:
	$(MAKE) -C kernel clean
	rm -rf iso lithium.iso