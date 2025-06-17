#include <arch/amd64/cpu.h>
#include <arch/port.h>
#include <core/debug.h>
#include <limine.h>
#include <acpi/rsdp.h>

void dufay_entry(void);

struct limine_hhdm_request limine_hhdm_request = { .id = LIMINE_HHDM_REQUEST,
												   .revision = 0 };

static volatile struct limine_rsdp_request limine_rsdp_request = {
	.id = LIMINE_RSDP_REQUEST,
	.revision = 0
};

void amd64_entry(void)
{
	if (limine_hhdm_request.response)
		HIGH_VMA = limine_hhdm_request.response->offset;

	amd64_system_init();

	print("amd64: welcome\n");

	dufay_entry();
}

void arch_devices_init()
{
	rsdp = limine_rsdp_request.response->address;

	if (rsdp->xsdt_addr) {
		xsdt = (struct xsdt *)(P2V(rsdp->xsdt_addr));
		print("ACPI: xsdt found at %x\n", (uintptr_t)xsdt);
	} else {
		rsdt = (struct rsdt *)(P2V(rsdp->rsdt_addr));
		print("ACPI: rsdt found at %x\n", (uintptr_t)rsdt);
	}

	fadt = acpi_find_sdt("FACP");

	amd64_system_tables();
}