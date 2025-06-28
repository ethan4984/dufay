SERVER = fs
BUILDDIR = $(BUILD)/$(SERVER)
SERVER_BIN = $(BUILDDIR)/$(SERVER)

CC ?= gcc
LD ?= ld

CFLAGS ?= -Wall -Wextra -Ofast -pipe -fsanitize=undefined
LDFLAGS ?=

INTERNALLDFLAGS := \
	-nostdlib \
	-zmax-page-size=0x1000 \
	-ztext \
	-pie

INTERNALCFLAGS := \
	-I.	\
	-Iaria \
	-std=c23 \
	-ffreestanding \
	-mno-80387 \
	-mno-mmx \
	-mno-3dnow \
	-mno-sse \
	-mno-sse2 \
	-fPIC \
	-fno-stack-protector

CFILES := $(shell find ./ -type f -name '*.c')
OBJ := $(CFILES:%=$(BUILDDIR)/%.o)
HEADER_DEPS := $(CFILES:%=$(BUILDDIR)/%.d)

.PHONY: all
all: $(SERVER_BIN)

$(SERVER_BIN): $(OBJ)
	@mkdir -p $(dir $@)
	@echo "LD $<"
	@$(LD) $(LDFLAGS) $(INTERNALLDFLAGS) $(OBJ) -n -T linker.ld -o $@

-include $(HEADER_DEPS)

$(BUILDDIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	@echo "CC $<"
	@$(CC) $(CFLAGS) $(INTERNALCFLAGS) -c $< -o $@

.PHONY: install
install:
	cp $(SERVER_BIN) $(DEST)
