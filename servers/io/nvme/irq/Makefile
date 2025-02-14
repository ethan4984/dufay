IRQ := nvme_irq

C ?= gcc
LD ?= ld

CFLAGS ?= -Wall -Wextra -Ofast -pipe -fsanitize=undefined -DSYSCALL_INTERRUPT
LDFLAGS ?=

INTERNALLDFLAGS := \
	-nostdlib \
	-zmax-page-size=0x1000 \
	-ztext \
	-pie

INTERNALCFLAGS := \
	-I.	\
	-Ifayt \
	-I../controller \
	-std=c23 \
	-ffreestanding \
	-fstack-protector\
	-mno-80387 \
	-mno-mmx \
	-mno-3dnow \
	-mno-sse \
	-mno-sse2 \
	-fPIC \
	-fno-stack-protector

CFILES := $(shell find ./ -type f -name '*.c')
OBJ := $(CFILES:.c=.o)
HEADER_DEPS := $(CFILES:.c=.d)

.PHONY: all
all: $(IRQ)

$(IRQ): $(OBJ)
	$(LD) $(LDFLAGS) $(INTERNALLDFLAGS) $(OBJ) -n -T linker.ld -o $@

-include $(HEADER_DEPS)

%.o: %.c
	$(C) $(CFLAGS) $(INTERNALCFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(IRQ) $(OBJ) $(HEADER_DEPS)

