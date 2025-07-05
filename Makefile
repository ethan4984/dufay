DISK_IMAGE = fuga.img
BUILD = build

ARCH ?= x86_64

.PHONY: all
all: kernel build_servers

#-object memory-backend-ram,id=mem0,size=1024M -numa node,memdev=mem0,nodeid=0,cpus=0\
#-object memory-backend-ram,id=mem1,size=1024M -numa node,memdev=mem1,nodeid=1,cpus=1\
#	-numa dist,src=0,dst=1,val=15\
#	-numa dist,src=1,dst=0,val=15

QEMUFLAGS_BASE = -drive if=pflash,unit=0,format=raw,file=ovmf/ovmf-code-$(ARCH).fd,readonly=on \
-no-reboot \
-no-shutdown 

QEMUFLAGS-x86_64 = \
	-m 8G \
	-smp 2\
	-drive file=$(DISK_IMAGE),if=none,id=nvme0,format=raw \
	-device nvme,drive=nvme0,serial=nvme,bus=pcie.0 \
	-device intel-iommu,aw-bits=48 \
	-machine type=q35 \
	-enable-kvm \
	-cpu host,migratable=no,+invtsc

QEMUFLAGS-aarch64 = \
		-M virt \
		-cpu cortex-a72 \
		-device ramfb \
		-device qemu-xhci \
		-device usb-kbd \
		-device usb-mouse \
		-serial stdio\
		-hda $(DISK_IMAGE)

.PHONY: run
run: ovmf/ovmf-code-$(ARCH).fd $(DISK_IMAGE)
	qemu-system-$(ARCH) $(QEMUFLAGS-$(ARCH)) $(QEMUFLAGS_BASE) -serial stdio

.PHONY: run-gdb
run-gdb: ovmf/ovmf-code-$(ARCH).fd $(DISK_IMAGE)
	qemu-system-$(ARCH) $(QEMUFLAGS-$(ARCH)) $(QEMUFLAGS_BASE) -serial stdio -s -S

.PHONY: console
console: $(DISK_IMAGE)
	qemu-system-$(ARCH) $(QEMUFLAGS-$(ARCH)) $(QEMUFLAGS_BASE) -monitor stdio -d int -D qemu.log -no-shutdown -display none

ovmf/ovmf-code-$(ARCH).fd:
	mkdir -p ovmf
	curl -Lo $@ https://github.com/osdev0/edk2-ovmf-nightly/releases/latest/download/ovmf-code-$(ARCH).fd
	case "$(ARCH)" in \
		aarch64) dd if=/dev/zero of=$@ bs=1 count=0 seek=67108864 2>/dev/null;; \
		riscv64) dd if=/dev/zero of=$@ bs=1 count=0 seek=33554432 2>/dev/null;; \
	esac

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
	git clone https://github.com/limine-bootloader/limine.git --branch=v9.x-binary --depth=1
	make -C limine

.PHONY: kernel
kernel:
	$(MAKE) -C kernel

$(DISK_IMAGE): $(BUILD) limine kernel build_servers
	rm -f $(DISK_IMAGE)
	dd if=/dev/zero bs=1M count=0 seek=64 of=$(DISK_IMAGE)
ifeq ($(ARCH),x86_64)
	PATH=$$PATH:/usr/sbin:/sbin sgdisk $(DISK_IMAGE) -n 1:2048 -t 1:ef00 -m 1
	./limine/limine bios-install $(DISK_IMAGE)
else
	PATH=$$PATH:/usr/sbin:/sbin sgdisk $(DISK_IMAGE) -n 1:2048 -t 1:ef00
endif
	mformat -i $(DISK_IMAGE)@@1M
	mmd -i $(DISK_IMAGE)@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
	mcopy -i $(DISK_IMAGE)@@1M kernel/build/fuga ::/boot
	mcopy -i $(DISK_IMAGE)@@1M limine.conf ::/boot/limine
ifeq ($(ARCH),x86_64)
	mcopy -i $(DISK_IMAGE)@@1M limine/limine-bios.sys ::/boot/limine
	mcopy -i $(DISK_IMAGE)@@1M limine/BOOTX64.EFI ::/EFI/BOOT
	mcopy -i $(DISK_IMAGE)@@1M limine/BOOTIA32.EFI ::/EFI/BOOT
endif
ifeq ($(ARCH),aarch64)
	mcopy -i $(DISK_IMAGE)@@1M limine/BOOTAA64.EFI ::/EFI/BOOT
endif


.PHONY: clean
clean:
	rm -rf $(DISK_IMAGE) serial.log qemu.log
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
