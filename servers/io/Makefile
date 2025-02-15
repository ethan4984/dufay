.PHONY: all
all:
	$(MAKE) -C pci BUILD=$(BUILD)
	$(MAKE) -C nvme/irq BUILD=$(BUILD)
	$(MAKE) -C nvme/controller BUILD=$(BUILD)

.PHONY: install
install:
	cp $(BUILD)/pci/pci $(DEST)
	cp $(BUILD)/nvme_irq/nvme_irq $(DEST)
	cp $(BUILD)/nvme/nvme $(DEST)