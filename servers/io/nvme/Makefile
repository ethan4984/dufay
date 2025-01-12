SERVER := nvme

C ?= gcc
LD ?= ld

CFLAGS ?= -Wall -Wextra -Ofast -pipe -fsanitize=undefined
LDFLAGS ?=

INTERNALLDFLAGS := \
	-nostdlib \
	-zmax-page-size=0x1000 \
	-static \
	-ztext

INTERNALCFLAGS := \
	-I.	\
	-Ifayt \
	-std=c23 \
	-ffreestanding \
	-fstack-protector\
	-mno-80387 \
	-mno-mmx \
	-mno-3dnow \
	-mno-sse \
	-mno-sse2

CFILES := $(shell find ./ -type f -name '*.c')
OBJ := $(CFILES:.c=.o)
HEADER_DEPS := $(CFILES:.c=.d)

.PHONY: all
all: $(SERVER)

$(SERVER): $(OBJ)
	$(LD) $(LDFLAGS) $(INTERNALLDFLAGS) $(OBJ) -n -T linker.ld -o $@

-include $(HEADER_DEPS)

%.o: %.c
	$(C) $(CFLAGS) $(INTERNALCFLAGS) -c $< -o $@

.PHONY: clean
clean:
	rm -rf $(SERVER) $(OBJ) $(HEADER_DEPS)

