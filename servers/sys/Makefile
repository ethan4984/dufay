.PHONY: all
all:
	$(MAKE) -C init BUILD=$(BUILD)
	$(MAKE) -C sched BUILD=$(BUILD)

.PHONY: install
install:
	cp $(BUILD)/init/init $(DEST)
	cp $(BUILD)/sched/sched $(DEST)