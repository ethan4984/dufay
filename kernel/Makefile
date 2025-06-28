-include .config

BUILD = build
<<<<<<< HEAD
ARCH ?= amd64
=======
KERNEL := $(BUILD)/fuga.elf
>>>>>>> upstream/dufay

ifeq ($(CONFIG_ARCH_AMD64), y)
ARCH = amd64
else ifeq ($(CONFIG_ARCH_AARCH64), y)
ARCH = aarch64
endif

ifeq ($(CONFIG_LLVM), y)
TOOLCHAIN = llvm
else
TOOLCHAIN = gnu
endif

.PHONY: all
all: $(BUILD)
	ninja -j$(shell nproc) -C $<

$(BUILD): .config
	meson setup $@ --cross-file=../toolchains/$(ARCH)-$(TOOLCHAIN).ini

ifeq ($(ARCH), amd64)
	nasm -fbin arch/amd64/smp.real -o build/smp.real.bin
endif

.config:
	./menuconfig

.PHONY: menuconfig
menuconfig:
	./menuconfig

.PHONY: clean
clean:
	rm -rf $(BUILD)