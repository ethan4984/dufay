#ifndef PCI_H_
#define PCI_H_ 

#include <fayt/pci.h>

#include <stdint.h>
#include <stddef.h>

struct [[gnu::packed]] mcfg_entry {
	uint64_t base;
	uint16_t segment;
	uint8_t bus_start;
	uint8_t bus_end;
	uint32_t reserved;
};

struct [[gnu::packed]] mcfg {
	char signature[4];
	uint32_t length;
	uint8_t revision;
	uint8_t checksum;
	char OEMID[6];
	char OEM_table_id[8];
	uint32_t OEM_revision;
	uint32_t creator_ID;
	uint32_t creator_revision;

	uint64_t reserved;

	struct mcfg_entry entry[];
};

struct pci_segment {
	int segment;
	struct mcfg_entry mcfg_entry;
	void *address_space;
};

struct pci_device {
	struct pci_descriptor descriptor;
	volatile union pci_config *config;
};

#define PCI_CONFIG(BASE, BUS, DEVICE, FUNC) ({ \
	(void*)((uintptr_t)(BASE) + ((BUS) * 256 + (DEVICE) * 8 + (FUNC)) * 4096); \
})

int pci(struct mcfg *mcfg);

#endif
