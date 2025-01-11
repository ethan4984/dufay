DISK_IMAGE = dufay.img
ISO_IMAGE = dufay.iso
INITRAMFS = initramfs.tar

.PHONY: all
all: $(DISK_IMAGE)

QEMUFLAGS = -m 4G \
			-smp 4 \
			-drive id=disk,file=$(DISK_IMAGE),if=none \
			-device ahci,id=ahci \
			-device ide-hd,drive=disk,bus=ahci.0 \
			-device intel-iommu,aw-bits=48 \
			-machine type=q35 \

QEMUFLAGS_ISO = -m 4G \
				-smp 1 \
				-cdrom $(ISO_IMAGE) \
				-drive file=disk.img,if=none,id=nvm \
				-device nvme,serial=deadbeef,drive=nvm \
				-machine type=q35 \
				-boot d \
				-cpu host,migratable=no,+invtsc

.PHONY: run
run: $(DISK_IMAGE)
	qemu-system-x86_64 $(QEMUFLAGS) -enable-kvm -serial stdio

.PHONY: run_initrd
run_initrd: $(ISO_IMAGE)
	qemu-system-x86_64 $(QEMUFLAGS_ISO) -enable-kvm -serial stdio

.PHONY: console
console: $(ISO_IMAGE)
	qemu-system-x86_64 $(QEMUFLAGS_ISO) -enable-kvm -no-reboot -monitor stdio -d int -D qemu.log -no-shutdown

.PHONY: int
int: $(ISO_IMAGE)
	qemu-system-x86_64 $(QEMUFLAGS_ISO) -d int -M smm=off -no-reboot -no-shutdown

.PHONY:
recompile_servers:
	cd servers && make clean && make 

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v7.x-binary --depth=1
	make -C limine

.PHONY: kernel
kernel:
	$(MAKE) -C kernel

$(INITRAMFS):
	cd build/system-root/ && tar -c --format=posix -f ../../initramfs.tar .

$(ISO_IMAGE): $(INITRAMFS) limine kernel
	rm -rf dufay.iso
	rm -rf disk_image
	mkdir disk_image
	mkdir disk_image/boot
	mkdir disk_image/servers/
	cp servers/sched/sched disk_image/servers
	cp servers/io/nvme/nvme disk_image/servers
	cp servers/io/pci/pci disk_image/servers
	cp servers/fs/vfs/vfs disk_image/servers
	cp kernel/dufay.elf initramfs.tar limine/limine-bios-cd.bin limine/limine-uefi-cd.bin limine/limine-bios.sys limine.cfg disk_image/boot
	xorriso -as mkisofs -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot boot/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label disk_image -o dufay.iso
	./limine/limine bios-install dufay.iso
	dd if=/dev/zero bs=1M count=0 seek=512 of=disk.img

$(DISK_IMAGE): limine kernel
	rm -f dufay.img 
	dd if=/dev/zero bs=1M count=0 seek=1024 of=dufay.img
	parted -s dufay.img mklabel msdos
	parted -s dufay.img mkpart primary 1 100%
	rm -rf disk_image
	mkdir disk_image
	sudo losetup -Pf --show dufay.img > loopback_dev
	sudo mkfs.ext2 `cat loopback_dev`p1
	sudo mount `cat loopback_dev`p1 disk_image
	sudo mkdir disk_image/boot
	sudo cp kernel/dufay.elf limine/limine-bios-cd.bin limine/limine-uefi-cd.bin limine/limine-bios.sys limine.cfg disk_image/boot
	sync
	sudo umount disk_image/
	sudo losetup -d `cat loopback_dev`
	rm -rf disk_image loopback_dev
	./limine/limine bios-install dufay.img

rebuild_mlibc:
	cd build && xbstrap install mlibc --rebuild

rebuild_servers:
	cd servers/scheduler/ && make clean && make

.PHONY: clean
clean:
	rm -f $(DISK_IMAGE) $(INITRAMFS) $(ISO_IMAGE) serial.log qemu.log
	$(MAKE) -C kernel clean

.PHONY: distclean
distclean: clean
	rm -rf limine
