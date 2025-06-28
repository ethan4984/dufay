#pragma once

#include <acpi/rsdp.h>
#include <aria/vector.h>

struct madt_hdr {
	struct acpi_hdr acpi_hdr;
	uint32_t lapic_addr;
	uint32_t flags;
	uint8_t entries[];
} __attribute__((packed));

struct madt_ent0 {
	uint8_t acpi_id;
	uint8_t apic_id;
	uint32_t flags;
} __attribute__((packed));

struct madt_ent1 {
	uint8_t ioapic_id;
	uint8_t reserved;
	uint32_t ioapic_addr;
	uint32_t gsi_base;
} __attribute__((packed));

struct madt_ent2 {
	uint8_t bus_src;
	uint8_t irq_src;
	uint32_t gsi;
	uint16_t flags;
} __attribute__((packed));

struct madt_ent4 {
	uint8_t acpi_id;
	uint16_t flags;
	uint8_t lint;
} __attribute__((packed));

struct madt_ent5 {
	uint16_t reserved;
	uint8_t lapic_override;
} __attribute__((packed));

extern struct madt_hdr *madt_hdr;

extern VECTOR(struct madt_ent0) madt_ent0_list;
extern VECTOR(struct madt_ent1) madt_ent1_list;
extern VECTOR(struct madt_ent2) madt_ent2_list;
extern VECTOR(struct madt_ent4) madt_ent4_list;
extern VECTOR(struct madt_ent5) madt_ent5_list;
