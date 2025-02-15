BUILD = $(CURDIR)/build

.PHONY: all
all:
	$(MAKE) -C sys BUILD=$(BUILD)
	$(MAKE) -C fs BUILD=$(BUILD)
	$(MAKE) -C io BUILD=$(BUILD)

.PHONY: install
install:
	$(MAKE) -C sys install BUILD=$(BUILD) DEST=$(DEST)
	$(MAKE) -C fs install BUILD=$(BUILD) DEST=$(DEST)
	$(MAKE) -C io install BUILD=$(BUILD) DEST=$(DEST)

.PHONY: clean
clean:
	rm -rf $(BUILD)
