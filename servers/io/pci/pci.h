#ifndef PCI_H_
#define PCI_H_

#include <fayt/pci.h>
#include <fayt/bitmap.h>

#include <stdint.h>
#include <stddef.h>

struct pci_segment {
	int segment;
	struct mcfg_entry mcfg_entry;
	void *address_space;
};

struct pci_device {
	struct pci_descriptor descriptor;

	bool msi_capable;
	int msi_offset;

	bool msix_capable;
	int msix_offset;
	struct pci_bar msix_bar;
	int msix_bar_offset;
	struct bitmap msix_bitmap;
	void *msix_space;

	volatile union pci_config *config;
};

#define PCI_CONFIG(BASE, BUS, DEVICE, FUNC)                     \
	({                                                          \
		(void *)((uintptr_t)(BASE) +                            \
				 ((BUS) * 256 + (DEVICE) * 8 + (FUNC)) * 4096); \
	})

int pci(struct pci_server_meta *);

#endif
