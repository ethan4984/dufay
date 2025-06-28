DISK_IMAGE = dufay.img
ISO_IMAGE = dufay.iso
INITRAMFS = initramfs.tar
BUILD = build

.PHONY: all
all: $(DISK_IMAGE)

#-object memory-backend-ram,id=mem0,size=1024M -numa node,memdev=mem0,nodeid=0,cpus=0\
#-object memory-backend-ram,id=mem1,size=1024M -numa node,memdev=mem1,nodeid=1,cpus=1\
#	-numa dist,src=0,dst=1,val=15\
#	-numa dist,src=1,dst=0,val=15

QEMUFLAGS = \
	-m 2G \
	-smp 2\
	-drive file=$(DISK_IMAGE),if=none,id=nvme0,format=raw \
	-device nvme,drive=nvme0,serial=nvme,bus=pcie.0 \
	-device intel-iommu,aw-bits=48 \
	-machine type=q35 \
	-cpu host,migratable=no,+invtsc

QEMUFLAGS_ISO = \
	-m 2G \
	-smp 2 \
	-cdrom $(ISO_IMAGE) \
	-boot d \
	-machine type=q35,accel=kvm \
	-drive file=disk.img,if=none,id=nvme0,format=raw \
	-device nvme,drive=nvme0,serial=nvme,bus=pcie.0 \
	-cpu host,migratable=no,+invtsc

.PHONY: run
run: $(DISK_IMAGE)
	qemu-system-x86_64 $(QEMUFLAGS) -enable-kvm -serial stdio -no-reboot -no-shutdown

.PHONY: run_initrd
run_initrd: $(ISO_IMAGE)
	qemu-system-x86_64 $(QEMUFLAGS_ISO) -enable-kvm -serial stdio -display none

.PHONY: console
console: $(DISK_IMAGE)
	qemu-system-x86_64 $(QEMUFLAGS) -enable-kvm -no-reboot -monitor stdio -d int -D qemu.log -no-shutdown -display none

$(BUILD):
	mkdir -p $@
	mkdir -p $(BUILD)/system-root/servers
	git submodule update --init --recursive

.PHONY: build_servers
build_servers: $(BUILD)
	$(MAKE) -C servers

.PHONY: clean_servers
clean_servers:
	cd sched && make clean
	cd io/nvme/controller && make clean
	cd io/nvme/irq && make clean
	cd io/pci && make clean
	cd sys/init && make clean

limine:
	git clone https://github.com/limine-bootloader/limine.git --branch=v7.x-binary --depth=1
	make -C limine

.PHONY: kernel
kernel:
	$(MAKE) -C kernel

$(INITRAMFS):
	cd build/system-root/ && tar -c --format=posix -f ../../initramfs.tar .

$(ISO_IMAGE): $(BUILD) $(INITRAMFS) limine kernel build_servers 
	rm -rf dufay.iso
	rm -rf disk_image
	mkdir disk_image
	mkdir disk_image/boot
	mkdir disk_image/servers/
	$(MAKE) -C servers install DEST=$(CURDIR)/disk_image/servers
	cp kernel/build/dufay initramfs.tar limine/limine-bios-cd.bin limine/limine-uefi-cd.bin limine/limine-bios.sys limine.cfg disk_image/boot
	xorriso -as mkisofs -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table --efi-boot boot/limine-uefi-cd.bin -efi-boot-part --efi-boot-image --protective-msdos-label disk_image -o dufay.iso
	./limine/limine bios-install dufay.iso
	dd if=/dev/zero bs=1M count=0 seek=512 of=disk.img
	parted -s disk.img mklabel msdos
	parted -s disk.img mkpart primary 1 100%

$(DISK_IMAGE): $(BUILD) limine kernel build_servers
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
	sudo $(MAKE) -C servers install DEST=$(CURDIR)/disk_image/servers
	sudo cp kernel/build/dufay limine/limine-bios-cd.bin limine/limine-uefi-cd.bin limine/limine-bios.sys limine.cfg disk_image/boot
	sync
	sudo umount disk_image/
	sudo losetup -d `cat loopback_dev`
	rm -rf disk_image loopback_dev
	./limine/limine bios-install dufay.img

rebuild_mlibc:
	cd build && xbstrap install mlibc --rebuild

.PHONY: clean
clean:
	rm -rf $(DISK_IMAGE) $(INITRAMFS) $(ISO_IMAGE) disk_image disk.img serial.log qemu.log
	$(MAKE) -C kernel clean
	$(MAKE) -C servers clean

.PHONY: format
format:
	find kernel servers -iname '*.h' -o -iname '*.c' | xargs clang-format -i

.PHONY: kconfig
kconfig:
	$(MAKE) -C kernel menuconfig

.PHONY: distclean
distclean: clean
	rm -rf limine $(BUILD) 
