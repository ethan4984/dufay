#pragma once

#include <acpi/madt.h>
#include <stdbool.h>

#define IOAPIC_INT_MASK (1 << 16)
#define IOAPIC_TRIGGER_MODE (1 << 15)
#define IOAPIC_REMOTE_IRR (1 << 14)
#define IOAPIC_INTPOL (1 << 13)
#define IOAPIC_DELIVS (1 << 12)
#define IOAPIC_DESTMOD (1 << 11)

#define XAPIC_ID_REG_OFF 0x20
#define XAPIC_VERSION_REG_OFF 0x30
#define XAPIC_TPR_OFF 0x80
#define XAPIC_PPR_OFF 0xA0
#define XAPIC_EOI_OFF 0xB0
#define XAPIC_LDR_OFF 0xD0
#define XAPIC_SINT_OFF 0xF0
#define XAPIC_ISR0_OFF 0x100
#define XAPIC_ISR1_OFF 0x110
#define XAPIC_ISR2_OFF 0x120
#define XAPIC_ISR3_OFF 0x130
#define XAPIC_ISR4_OFF 0x140
#define XAPIC_ISR5_OFF 0x150
#define XAPIC_ISR6_OFF 0x160
#define XAPIC_ISR7_OFF 0x170
#define XAPIC_TMR0_OFF 0x180
#define XAPIC_TMR1_OFF 0x190
#define XAPIC_TMR2_OFF 0x1A0
#define XAPIC_TMR3_OFF 0x1B0
#define XAPIC_TMR4_OFF 0x1C0
#define XAPIC_TMR5_OFF 0x1D0
#define XAPIC_TMR6_OFF 0x1E0
#define XAPIC_TMR7_OFF 0x1F0
#define XAPIC_IRR0_OFF 0x200
#define XAPIC_IRR1_OFF 0x210
#define XAPIC_IRR2_OFF 0x220
#define XAPIC_IRR3_OFF 0x230
#define XAPIC_IRR4_OFF 0x240
#define XAPIC_IRR5_OFF 0x250
#define XAPIC_IRR6_OFF 0x260
#define XAPIC_IRR7_OFF 0x270
#define XAPIC_ESR_OFF 0x280
#define XAPIC_ICR_OFF 0x300
#define XAPIC_ICR_1 0x310
#define XAPIC_TIMER_LVT_OFF 0x320
#define XAPIC_THERMAL_LVT_OFF 0x330
#define XAPIC_PERFORMANCE_LVT_OFF 0x340
#define XAPIC_LINT0_OFF 0x350
#define XAPIC_LINT1_OFF 0x360
#define XAPIC_EVT_OFF 0x370
#define XAPIC_TIMER_INITAL_COUNT_OFF 0x380
#define XAPIC_TIMER_CURRENT_COUNT_OFF 0x390
#define XAPIC_TIMER_DIVIDE_CONF_OFF 0x3E0

struct ioapic {
	uint32_t ioapic_id;
	uint32_t ioapic_version;
	uint32_t maximum_redirection_entry;
	uint32_t ioapic_arbitration_id;

	volatile uint32_t *ioapic_base;

	struct madt_ent1 *madt1;
};

void apic_init();
void apic_timer_init(uint32_t ms);
uint32_t ioapic_read(struct ioapic *ioapic, uint8_t reg);
void ioapic_write(struct ioapic *ioapic, uint32_t reg, uint32_t data);
void ioapic_write_redirection_table(struct ioapic *ioapic,
									uint32_t redirection_entry, uint64_t data);
void xapic_write(uint32_t reg, uint32_t data);
uint32_t xapic_read(uint32_t reg);
uint64_t ioapic_read_redirection_table(struct ioapic *ioapic,
									   uint8_t redirection_entry);
int ioapic_set_irq_redirection(uint32_t lapic_id, uint8_t vector, uint8_t irq,
							   bool bask);

extern VECTOR(struct ioapic) ioapic_list;
