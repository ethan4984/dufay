BUILD = build
KERNEL := $(BUILD)/dufay.elf

CC ?= gcc
LD ?= ld

CFLAGS ?= -Wall -Wextra -O0 -pipe -fsanitize=undefined -Wno-unused -DDUFAY
LDFLAGS ?=

INTERNALLDFLAGS :=		 \
	-Tlinker.ld			\
	-nostdlib				\
	-zmax-page-size=0x1000 \
	-static				\
	--no-dynamic-linker	\
	-ztext

INTERNALCFLAGS := \
	-I. \
	-std=c23 \
	-ffreestanding \
	-fno-stack-protector \
	-fpie \
	-mno-80387 \
	-mno-mmx \
	-mno-3dnow \
	-mno-sse \
	-mno-sse2 \
	-mno-red-zone \
	-MMD \
	-Wno-sign-compare

CFILES := $(shell find ./ -type f -name '*.c')
ASMFILES := $(shell find ./ -type f -name '*.asm')
REALFILES := $(shell find ./ -type f -name '*.real')
OBJ := $(CFILES:%=$(BUILD)/%.o) $(ASMFILES:%=$(BUILD)/%.o)
BINS := $(REALFILES:%=$(BUILD)/%.bin)
HEADER_DEPS := $(CFILES:%=$(BUILD)/%.d)

.PHONY: all
all: $(KERNEL)

limine.h:
	curl -Lo $@ https://github.com/limine-bootloader/limine/raw/trunk/limine.h

$(KERNEL): $(BINS) $(OBJ)
	@echo "LD $@"
	@$(LD) $(OBJ) $(LDFLAGS) $(INTERNALLDFLAGS) -o $@

-include $(HEADER_DEPS)

$(BUILD)/%.real.bin: %.real
	mkdir -p $(dir $@)
	@echo "NASM $<"
	nasm -fbin $< -o $@

$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) $(INTERNALCFLAGS) -c $< -o $@

$(BUILD)/%.asm.o: %.asm
	mkdir -p $(dir $@)
	@echo "NASM $<"
	nasm -felf64 $< -o $@

.PHONY: clean
clean:
	rm -rf $(BUILD)
