#ifndef FAYT_PCI_H_
#define FAYT_PCI_H_

#include <stdint.h>
#include <stddef.h>

constexpr int NOT_PCI_BAR = 1;
constexpr int NOT_PCI_MSI = 2;

struct [[gnu::packed]] pci_config_device {
	uint16_t vendor_id;
	uint16_t device_id;
	uint16_t command;
	uint16_t status;
	uint8_t revision_id;
	uint8_t prog_if;
	uint8_t subclass;
	uint8_t class;
	uint8_t cache_line_size;
	uint8_t reversed0;
	uint8_t header_type;
	uint8_t reserved1;
	uint32_t bar[6];
	uint32_t reserved2;
	uint16_t subsystem_vendor_id;
	uint16_t subsystem_device_id;
	uint32_t reserved3;
	uint8_t capabilities;
	uint8_t reserved4[3];
	uint8_t interrupt_line;
	uint8_t interrupt_pin;
	uint16_t reserved5;
};

struct [[gnu::packed]] pci_config_bridge {
	uint16_t vendor_id;
	uint16_t device_id;
	uint16_t command;
	uint16_t status;
	uint8_t revision_id;
	uint8_t prog_if;
	uint8_t subclass;
	uint8_t class;
	uint8_t cache_line_size;
	uint8_t latency_timer;
	uint8_t header_type;
	uint8_t bist;
	uint32_t bar[2];
	uint8_t primary_bus;
	uint8_t secondary_bus;
	uint8_t subordinate_bus;
	uint8_t secondary_latency_timer;
	uint8_t io_base;
	uint8_t io_limit;
	uint16_t secondary_status;
	uint16_t memory_base;
	uint16_t memory_limit;
	uint16_t prefetch_memory_base;
	uint16_t prefetch_memory_limit;
	uint32_t prefetch_upper_base32;
	uint32_t prefetch_upper_limit32;
	uint16_t io_base_upper16;
	uint16_t io_limit_upper;
	uint8_t capabilities;
	uint8_t reserved0[3];
	uint32_t expansion_rom_base;
	uint8_t interrupt_line;
	uint8_t interrupt_pin;
	uint16_t bridge_control;
};

union [[gnu::packed]] pci_config {
	volatile struct pci_config_device device;
	volatile struct pci_config_bridge bridge;
};

struct [[gnu::packed]] pci_bar {
	uint64_t base;
	uint64_t limit;
	int is_mmio;
	int is_prefetchable;
};

struct [[gnu::packed]] pci_descriptor {
	int segment;
	int bus;
	int device;
	int func;
};

struct [[gnu::packed]] pci_nbar {
	struct pci_descriptor descriptor;
	struct pci_bar bar;
	int bar_index;
	int valid;
};

struct [[gnu::packed]] pci_nmsi {
	struct pci_descriptor descriptor;
	int irq_vector;
	int msix;
	int valid;
};

struct [[gnu::packed]] pci_info {
	struct pci_descriptor descriptor;
	int msi_capable;
	int msix_capable;
};

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

struct [[gnu::packed]] pci_server_meta {
	struct mcfg *mcfg;
	int logical_processor_cnt;
	int lapic_id[];
};

#endif
