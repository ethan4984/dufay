#include <arch/port.h>

#include <acpi/rsdp.h>

#include <core/debug.h>
#include <aria/base.h>

struct rsdp *rsdp;
struct rsdt *rsdt;
struct xsdt *xsdt;
struct fadt *fadt;

void *acpi_find_sdt(const char *signature)
{
	if (xsdt != NULL) {
		for (size_t i = 0;
			 i < (xsdt->acpi_hdr.length - sizeof(struct acpi_hdr)); i++) {
			struct acpi_hdr *acpi_hdr =
				(struct acpi_hdr *)(P2V(xsdt->acpi_ptr[i]));
			if (strncmp(acpi_hdr->signature, signature, 4) == 0) {
				print("ACPI: %s found\n", signature);
				return acpi_hdr;
			}
		}
	}

	if (rsdt != NULL) {
		for (size_t i = 0;
			 i < (rsdt->acpi_hdr.length - sizeof(struct acpi_hdr)); i++) {
			struct acpi_hdr *acpi_hdr =
				(struct acpi_hdr *)(P2V(rsdt->acpi_ptr[i]));
			if (strncmp(acpi_hdr->signature, signature, 4) == 0) {
				print("ACPI: %s found\n", signature);
				return acpi_hdr;
			}
		}
	}

	print("ACPI: %s not found\n", signature);

	return NULL;
}
