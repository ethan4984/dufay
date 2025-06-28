#include <arch/x86/paging.h>
#include <arch/x86/hpet.h>
#include <arch/x86/apic.h>
#include <arch/x86/cpu.h>

#include <core/memory/virtual.h>
#include <core/memory/address.h>

#include <core/debug.h>
#include <aria/string.h>

typeof(madt_ent0_list) madt_ent0_list;
typeof(madt_ent1_list) madt_ent1_list;
typeof(madt_ent2_list) madt_ent2_list;
typeof(madt_ent4_list) madt_ent4_list;
typeof(madt_ent5_list) madt_ent5_list;
typeof(ioapic_list) ioapic_list;

struct madt_hdr *madt_hdr;

uint32_t ioapic_read(struct ioapic *ioapic, uint8_t reg)
{
	*ioapic->ioapic_base = reg;
	return *(ioapic->ioapic_base + 4);
}

void ioapic_write(struct ioapic *ioapic, uint32_t reg, uint32_t data)
{
	*ioapic->ioapic_base = reg;
	*(ioapic->ioapic_base + 4) = data;
}

void xapic_write(uint32_t reg, uint32_t data)
{
	*(volatile uint32_t *)((rdmsr(MSR_LAPIC_BASE) & 0xfffff000) + HIGH_VMA +
						   reg) = data;
}

uint32_t xapic_read(uint32_t reg)
{
	return *(volatile uint32_t *)((rdmsr(MSR_LAPIC_BASE) & 0xfffff000) +
								  HIGH_VMA + reg);
}

void ioapic_write_redirection_table(struct ioapic *ioapic,
									uint32_t redirection_entry, uint64_t data)
{
	ioapic_write(ioapic, redirection_entry + 0x10, data & 0xffffffff);
	ioapic_write(ioapic, redirection_entry + 0x10 + 1, data >> 32 & 0xffffffff);
}

uint64_t ioapic_read_redirection_table(struct ioapic *ioapic,
									   uint8_t redirection_entry)
{
	uint64_t data =
		ioapic_read(ioapic, redirection_entry + 0x10) |
		((uint64_t)ioapic_read(ioapic, redirection_entry + 0x10 + 1) << 32);
	return data;
}

void apic_timer_init(uint32_t ms)
{
	xapic_write(XAPIC_TIMER_DIVIDE_CONF_OFF, 0x3); // divide by 16
	xapic_write(XAPIC_TIMER_INITAL_COUNT_OFF, ~0);

	hpet_msleep(ms);

	uint32_t ticks = ~0 - xapic_read(XAPIC_TIMER_CURRENT_COUNT_OFF);

	xapic_write(XAPIC_TIMER_LVT_OFF, 0x20 | (1 << 17));
	xapic_write(XAPIC_TIMER_DIVIDE_CONF_OFF, 0x3); // divide by 16
	xapic_write(XAPIC_TIMER_INITAL_COUNT_OFF, ticks);
}

int ioapic_set_irq_redirection(uint32_t lapic_id, uint8_t vector, uint8_t irq,
							   bool mask)
{
	uint64_t flags = 0;

	for (size_t i = 0; i < madt_ent2_list.length; i++) {
		struct madt_ent2 *madt2 = &madt_ent2_list.data[i];

		if (madt2->irq_src != irq) {
			continue;
		}

		if (madt2->flags & (1 << 1)) { // edge triggered
			flags |= IOAPIC_INTPOL;
		} else if (madt2->flags & (1 << 3)) { // level triggered
			flags |= IOAPIC_TRIGGER_MODE;
		}

		irq = madt2->gsi;

		break;
	}

	if (mask) {
		flags |= IOAPIC_INT_MASK;
	}

	uint64_t entry = vector | flags | ((uint64_t)lapic_id << 56);

	for (size_t i = 0; i < ioapic_list.length; i++) {
		struct ioapic *ioapic = &ioapic_list.data[i];

		if (irq <= ioapic->maximum_redirection_entry &&
			irq >= ioapic->madt1->gsi_base) {
			ioapic_write_redirection_table(
				ioapic, (irq - ioapic->madt1->gsi_base) * 2, entry);
			return irq;
		}
	}

	return irq;
}

void apic_init()
{
	madt_hdr = acpi_find_sdt("APIC");

	if (madt_hdr == NULL) {
		print("APIC: unable to locate APIC SDT\n");
		return;
	}

	for (size_t i = 0; i < madt_hdr->acpi_hdr.length - sizeof(struct madt_hdr);
		 i++) {
		uint8_t entry_type = madt_hdr->entries[i++];
		uint8_t entry_size = madt_hdr->entries[i++];

		switch (entry_type) {
		case 0:
			VECTOR_PUSH(madt_ent0_list,
						*(struct madt_ent0 *)(&madt_hdr->entries[i]));
			break;
		case 1:
			VECTOR_PUSH(madt_ent1_list,
						*(struct madt_ent1 *)(&madt_hdr->entries[i]));
			break;
		case 2:
			VECTOR_PUSH(madt_ent2_list,
						*(struct madt_ent2 *)(&madt_hdr->entries[i]));
			break;
		case 4:
			VECTOR_PUSH(madt_ent4_list,
						*(struct madt_ent4 *)(&madt_hdr->entries[i]));
			break;
		case 5:
			VECTOR_PUSH(madt_ent5_list,
						*(struct madt_ent5 *)(&madt_hdr->entries[i]));
		}
		i += entry_size - 3;
	}

	print("APIC: processor count %d\n", madt_ent0_list.length);

	for (size_t i = 0; i < madt_ent1_list.length; i++) {
		struct madt_ent1 *madt1 = &madt_ent1_list.data[i];

		struct ioapic ioapic = {
			.ioapic_base =
				(volatile uint32_t *)((uintptr_t)madt1->ioapic_addr + HIGH_VMA),
			.madt1 = madt1
		};

		kernel_mappings.page_table->map_page(
			kernel_mappings.page_table, (uintptr_t)ioapic.ioapic_base,
			((uintptr_t)ioapic.ioapic_base - HIGH_VMA),
			X86_FLAGS_P | X86_FLAGS_RW | X86_FLAGS_G | X86_FLAGS_PS);

		ioapic.ioapic_id = ioapic_read(&ioapic, 0);
		ioapic.ioapic_version = ioapic_read(&ioapic, 1) & 0xff;
		ioapic.maximum_redirection_entry = ioapic_read(&ioapic, 1) >> 16 & 0xff;
		ioapic.ioapic_arbitration_id = ioapic_read(&ioapic, 2) >> 23 & 0xf;

		print("IOAPIC: id %x\n", ioapic.ioapic_id);
		print("IOAPIC: version %x\n", ioapic.ioapic_version);
		print("IOAPIC: maximum redirection entry %x\n",
			  ioapic.maximum_redirection_entry);
		print("IOAPIC: arbitration id %x\n", ioapic.ioapic_arbitration_id);
		print("IOAPIC: base %x\n", (uintptr_t)ioapic.ioapic_base);

		VECTOR_PUSH(ioapic_list, ioapic);
	}

	outb(0x20, 0x11);
	outb(0xa0, 0x11);
	outb(0x21, 0x20);
	outb(0xa1, 0x28);
	outb(0x21, 0x4);
	outb(0xa1, 0x2);
	outb(0x21, 0x1);
	outb(0xa1, 0x1);
	outb(0x21, 0x0);
	outb(0xa1, 0x0);

	outb(0xa1, 0xff);
	outb(0x21, 0xff);

	uint64_t irq_bitmap = 0;
	for (size_t i = 0; i < 16; i++) {
		if (!BIT_TEST((uint8_t *)&irq_bitmap, i)) {
			int irq = ioapic_set_irq_redirection(xapic_read(XAPIC_ID_REG_OFF),
												 i + 32, i, true);
			BIT_SET((uint8_t *)&irq_bitmap, irq);
		}
	}

	kernel_mappings.page_table->map_page(
		kernel_mappings.page_table,
		(rdmsr(MSR_LAPIC_BASE) & 0xfffff000) + HIGH_VMA,
		(rdmsr(MSR_LAPIC_BASE) & 0xfffff000),
		X86_FLAGS_P | X86_FLAGS_RW | X86_FLAGS_G | X86_FLAGS_PS);

	xapic_write(XAPIC_TPR_OFF, 0);
	xapic_write(XAPIC_SINT_OFF, xapic_read(XAPIC_SINT_OFF) | 0x1ff);

	__asm__ volatile("mov %0, %%cr8" ::"r"(0ull));
}
