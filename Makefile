DISK_IMAGE = dufay.img
ISO_IMAGE = dufay.iso
INITRAMFS = initramfs.tar
BUILD = build/system-root/servers

.PHONY: all
all: $(DISK_IMAGE)

QEMUFLAGS = \
	-m 2G \
	-smp 1 \
	-drive file=$(DISK_IMAGE),if=none,id=nvme0,format=raw \
	-device nvme,drive=nvme0,serial=nvme,bus=pcie.0 \
	-device intel-iommu,aw-bits=48 \
	-machine type=q35 \
	-cpu host,migratable=no,+invtsc

QEMUFLAGS_ISO = \
	-m 2G \
	-smp 1 \
	-cdrom $(ISO_IMAGE) \
	-boot d \
	-machine type=q35,accel=kvm \
	-drive file=disk.img,if=none,id=nvme0,format=raw \
	-device nvme,drive=nvme0,serial=nvme,bus=pcie.0 \
	-cpu host,migratable=no,+invtsc

.PHONY: run
run: $(DISK_IMAGE)
	qemu-system-x86_64 $(QEMUFLAGS) -enable-kvm -serial stdio

.PHONY: run_initrd
run_initrd: $(ISO_IMAGE)
	qemu-system-x86_64 $(QEMUFLAGS_ISO) -enable-kvm -serial stdio

.PHONY: console
console: $(ISO_IMAGE)
	qemu-system-x86_64 $(QEMUFLAGS) -enable-kvm -no-reboot -monitor stdio -d int -D qemu.log -no-shutdown

.PHONY: int
int: $(ISO_IMAGE)
	qemu-system-x86_64 $(QEMUFLAGS_ISO) -d int -M smm=off -no-reboot -no-shutdown

.PHONY:
recompile_servers:
	cd servers && make 

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v7.x-binary --depth=1
	make -C limine

.PHONY: kernel
kernel:
	$(MAKE) -C kernel

$(BUILD):
	mkdir -p build/system-root/servers
	git submodule update --init --recursive

$(INITRAMFS):
	cd build/system-root/ && tar -c --format=posix -f ../../initramfs.tar .

$(ISO_IMAGE): $(BUILD) $(INITRAMFS) limine kernel recompile_servers
	rm -rf dufay.iso
	rm -rf disk_image
	mkdir disk_image
	mkdir disk_image/boot
	mkdir disk_image/servers/
	cp servers/sched/sched disk_image/servers
	cp servers/io/nvme/controller/nvme disk_image/servers
	cp servers/io/nvme/irq/nvme_irq disk_image/servers
	cp servers/io/pci/pci disk_image/servers
	cp servers/fs/vfs/vfs disk_image/servers
	cp kernel/dufay.elf initramfs.tar limine/limine-bios-cd.bin limine/limine-uefi-cd.bin limine/limine-bios.sys limine.cfg disk_image/boot
	xorriso -as mkisofs -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot boot/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label disk_image -o dufay.iso
	./limine/limine bios-install dufay.iso
	dd if=/dev/zero bs=1M count=0 seek=512 of=disk.img
	parted -s disk.img mklabel msdos
	parted -s disk.img mkpart primary 1 100%

$(DISK_IMAGE): limine kernel recompile_servers
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
	sudo mkdir disk_image/servers/
	sudo cp servers/sched/sched disk_image/servers
	sudo cp servers/io/nvme/controller/nvme disk_image/servers
	sudo cp servers/io/nvme/irq/nvme_irq disk_image/servers
	sudo cp servers/io/pci/pci disk_image/servers
	sudo cp servers/fs/vfs/vfs disk_image/servers
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
	rm -rf $(DISK_IMAGE) $(INITRAMFS) $(ISO_IMAGE) disk_image disk.img serial.log qemu.log
	$(MAKE) -C kernel clean

.PHONY: distclean
distclean: clean
	rm -rf limine
