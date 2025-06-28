#include <aria/debug.h>
#include <aria/portal.h>
#include <aria/address.h>
#include <aria/dictionary.h>
#include <aria/syscall.h>
#include <aria/string.h>
#include <aria/slab.h>
#include <aria/notification.h>
#include <aria/vector.h>
#include <aria/compiler.h>

#include <pci.h>

struct dictionary device_tree;
struct dictionary segment_tree;

static int pci_device_bar(volatile union pci_config *, struct pci_bar *, int);
static int pci_device_msi(struct pci_device *, int);
static int pci_device_msix(struct pci_device *, int);

struct pci_core_morphology {
	int load;
	int lapic_id;
};

static VECTOR(struct pci_core_morphology *) pci_core_morphology;
static struct pci_core_morphology *pci_fetch_optimal_core(void);

static void nbar(struct notification_info *, void *data, int)
{
	struct pci_nbar *nbar = data;
	if (nbar == NULL) {
		print("ERROR: NBAR: is null\n");
		goto finish;
	}

	struct pci_device *device = NULL;
	int ret = dictionary_search(&device_tree, &nbar->descriptor,
								sizeof(struct pci_descriptor),
								(void **)&device);
	if (ret == -1 || device == NULL) {
		print("ERROR: NBAR: can not find device\n");
		goto finish;
	}

	ret = pci_device_bar(device->config, &nbar->bar, 0);
	if (ret == -1) {
		nbar->valid = false;
		goto finish;
	} else
		nbar->valid = true;
finish:
	SYSCALL0(SYSCALL_NOTIFICATION_RETURN);
}

static void nmsi(struct notification_info *, void *data, int)
{
	struct pci_nmsi *nmsi = data;
	if (nmsi == NULL) {
		print("ERROR: NMSI: is null\n");
		goto finish;
	}

	struct pci_device *device = NULL;
	int ret = dictionary_search(&device_tree, &nmsi->descriptor,
								sizeof(struct pci_descriptor),
								(void **)&device);
	if (ret == -1 || device == NULL) {
		print("ERROR: NMSI: can not find device\n");
		goto finish;
	}

	if (nmsi->msix)
		ret = pci_device_msix(device, nmsi->irq_vector);
	else
		ret = pci_device_msi(device, nmsi->irq_vector);

	if (ret == -1) {
		nmsi->valid = false;
		goto finish;
	} else
		nmsi->valid = true;
finish:
	SYSCALL0(SYSCALL_NOTIFICATION_RETURN);
}

static int pci_device_bar(volatile union pci_config *config,
						  struct pci_bar *bar, int index)
{
	if (config == NULL || bar == NULL)
		return -1;

	uint64_t bar_low = config->device.bar[index];

	bool is_mmio = (bar_low & 1) == 0;
	bool is_prefetchable = is_mmio && (bar_low & (1 << 3)) != 0;
	bool is_64_bits = is_mmio && ((bar_low >> 1) & 0b11) == 0b10;
	uint64_t bar_high = is_64_bits ? config->device.bar[index + 1] : 0;

	uint64_t base = (bar_high << 32) | bar_low;
	base = is_mmio ? (base & ~0xf) : (base & ~0x3);

	config->device.bar[index] = 0xffffffff;
	uint64_t bar_size_low = config->device.bar[index];

	uint64_t bar_size_high = 0;
	if (is_64_bits) {
		config->device.bar[index + 1] = 0xffffffff;
		bar_size_high = config->device.bar[index + 1];
	}

	config->device.bar[index] = bar_low;
	if (is_64_bits)
		config->device.bar[index + 1] = bar_high;

	uint64_t limit = bar_size_high << 32 | bar_size_low;
	limit = is_mmio ? limit & ~0xf : limit & ~0x3;
	limit = ~limit + 1;
	limit &= ~0xFFFFFFFF00000000; // hack

	*bar = (struct pci_bar){ .base = base,
							 .limit = limit,
							 .is_mmio = is_mmio,
							 .is_prefetchable = is_prefetchable };

	return 0;
}

static int pci_device_msi(struct pci_device *device, int vector)
{
	if (device == NULL)
		return -1;

	struct pci_core_morphology *core_morphology = pci_fetch_optimal_core();
	if (core_morphology == NULL)
		RETURN_ERROR;

	uint16_t message_control =
		*(volatile uint16_t *)((uintptr_t)device->config + device->msi_offset +
							   2);

	*(volatile uint32_t *)((uintptr_t)device->config + device->msi_offset + 4) =
		(0xfeeull << 20) | (core_morphology->lapic_id << 12);
	*(volatile uint32_t *)((uintptr_t)device->config + device->msi_offset +
						   (message_control & (1 << 7) ? 8 : 12)) = vector;

	message_control |= (1 << 0);
	message_control &= ~(0b111 << 4);

	*(volatile uint16_t *)((uintptr_t)device->config + device->msi_offset + 2) =
		message_control;

	return 0;
}

static int pci_device_msix(struct pci_device *device, int vector)
{
	if (device == NULL)
		return -1;

	int msix_vector_offset;
	int ret = bitmap_alloc(&device->msix_bitmap, &msix_vector_offset);
	if (ret == -1)
		return -1;

	struct pci_core_morphology *core_morphology = pci_fetch_optimal_core();
	if (core_morphology == NULL)
		RETURN_ERROR;

	msix_vector_offset *= 16;

	*(volatile uint32_t *)(device->msix_space + device->msix_bar_offset +
						   msix_vector_offset) =
		(0xfeeull << 20) | (core_morphology->lapic_id << 12);
	*(volatile uint32_t *)(device->msix_space + device->msix_bar_offset +
						   msix_vector_offset + 4) = 0;
	*(volatile uint32_t *)(device->msix_space + device->msix_bar_offset +
						   msix_vector_offset + 8) = vector;
	*(volatile uint32_t *)(device->msix_space + device->msix_bar_offset +
						   msix_vector_offset + 12) = 0;

	uint16_t message_control =
		*(volatile uint16_t *)((uintptr_t)device->config + device->msix_offset +
							   2);
	message_control |= (1 << 15);
	message_control &= ~(1 << 14);
	*(volatile uint16_t *)((uintptr_t)device->config + device->msix_offset +
						   2) = message_control;

	return 0;
}

static struct pci_core_morphology *pci_fetch_optimal_core(void)
{
	struct pci_core_morphology *ret = NULL;

	for (size_t i = 0; i < pci_core_morphology.length; i++) {
		struct pci_core_morphology *core_morphology =
			pci_core_morphology.data[i];
		if (core_morphology == NULL)
			continue;

		if (unlikely(ret == NULL))
			ret = core_morphology;
		else if (ret->load > core_morphology->load)
			ret = core_morphology;
		if (ret->load == 0)
			break;
	}

	return ret;
}

static int pci_device_spawn(struct pci_device *pci_device)
{
	if (pci_device == NULL)
		RETURN_ERROR;

	print(
		"[%x:%x:%x] class %x: subclass %x: progif: %x: vendor_id: %x: device_id: %x\n",
		pci_device->descriptor.bus, pci_device->descriptor.device,
		pci_device->descriptor.func, pci_device->config->device.class,
		pci_device->config->device.subclass, pci_device->config->device.prog_if,
		pci_device->config->device.vendor_id,
		pci_device->config->device.device_id);

	if (pci_device->config->device.status & (1 << 4)) {
		int offset = pci_device->config->device.capabilities;

		for (; offset;) {
			int id = *((volatile uint8_t *)pci_device->config + offset);

			if (id == 0x5) {
				pci_device->msi_capable = true;
				pci_device->msi_offset = offset;
			} else if (id == 0x11) {
				pci_device->msix_capable = true;
				pci_device->msix_offset = offset;

				uint32_t table_ptr =
					*(volatile uint16_t *)((uintptr_t)pci_device->config +
										   pci_device->msix_offset + 4);

				int bar_index = table_ptr & 0b111;

				int ret = pci_device_bar(pci_device->config,
										 &pci_device->msix_bar, bar_index);
				if (ret == -1) {
					print("ERROR: failed to locate MSIX bar\n");
					continue;
				}

				ret = as_vmem_allocate(CAPABILITY_SELF_AS,
									   (uintptr_t *)&pci_device->msix_space,
									   pci_device->msix_bar.limit);
				if (ret == -1)
					RETURN_ERROR;

				struct portal_req portal_req = {
					.type = PORTAL_REQ_DIRECT,
					.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
					.length = sizeof(struct portal_req),
					.morphology = { .addr = (uintptr_t)pci_device->msix_space,
									.length = pci_device->msix_bar.limit,
									.paddr = pci_device->msix_bar.base,
									.pcnt = DIV_ROUNDUP(
										pci_device->msix_bar.limit, PAGE_SIZE) }
				};

				struct portal_resp portal_resp;
				struct syscall_response syscall_response =
					SYSCALL2(SYSCALL_PORTAL, &portal_req, &portal_resp);
				if (syscall_response.ret == -1 ||
					portal_resp.base != (uintptr_t)pci_device->msix_space ||
					portal_resp.limit != pci_device->msix_bar.limit)
					RETURN_ERROR;

				pci_device->msix_bar_offset = (table_ptr >> 3) << 3;
				pci_device->msix_bitmap = (struct bitmap){
					.data = alloc(2048 / 8), .size = 2048, .resizable = false
				};
			}

			offset = *((volatile uint8_t *)pci_device->config + offset + 1);
		}
	}

	switch (pci_device->config->device.class) {
	case 1:
		switch (pci_device->config->device.subclass) {
		case 6: { // AHCI
			break;
		}
		case 8: { // NVME
			//struct pci_info pci_info = { .descriptor = pci_device->descriptor,
			//							 .msi_capable = pci_device->msi_capable,
			//							 .msix_capable =
			//								 pci_device->msix_capable };
			//			struct syscall_response syscall_response =
			//				SYSCALL4(SYSCALL_SERVER_ACTIVATE, "IO", "nvme", &pci_info,
			//						 sizeof(struct pci_info));
			//			if (syscall_response.ret == -1)
			//				RETURN_ERROR;
			break;
		}
		}
		break;
	}

	return 0;
}

int pci(struct pci_server_meta *server_meta)
{
	if (server_meta == NULL)
		RETURN_ERROR;

	for (int i = 0; i < server_meta->logical_processor_cnt; i++) {
		struct pci_core_morphology *core_morphology =
			alloc(sizeof(struct pci_core_morphology));
		if (unlikely(core_morphology == NULL))
			RETURN_ERROR;

		core_morphology->lapic_id = server_meta->lapic_id[i];
		core_morphology->load = 0;

		VECTOR_PUSH(pci_core_morphology, core_morphology);
	}

	struct mcfg *mcfg = server_meta->mcfg;
	if (mcfg == NULL ||
		!(mcfg->signature[0] == 'M' && mcfg->signature[1] == 'C' &&
		  mcfg->signature[2] == 'F' && mcfg->signature[3] == 'G')) {
		print("ERROR: MCFG does not exist\n");
		return -1;
	}

	struct notification_action bar_action = { .handler = nbar };
	struct notification_action msi_action = { .handler = nmsi };

	struct syscall_response response =
		SYSCALL3(SYSCALL_NOTIFICATION_ACTION, NOT_PCI_BAR, &bar_action, NULL);
	if (response.ret == -1) {
		print("ERROR: failure to set notification PCI_NBAR\n");
		return -1;
	}

	response =
		SYSCALL3(SYSCALL_NOTIFICATION_ACTION, NOT_PCI_MSI, &msi_action, NULL);
	if (response.ret == -1) {
		print("ERROR: failure to set notification PCI_NMSI\n");
		return -1;
	}

	response = SYSCALL0(SYSCALL_NOTIFICATION_UNMUTE);
	if (response.ret == -1) {
		print("ERROR: failed to activate notification queue\n");
		return -1;
	}

	for (unsigned int i = 0;
		 i < (mcfg->length - sizeof(struct mcfg)) / sizeof(struct mcfg_entry);
		 i++) {
		struct mcfg_entry *mcfg_entry = &mcfg->entry[i];

		print("segment %d bus [%d -> %d] at base [%x]\n", mcfg_entry->segment,
			  mcfg_entry->bus_start, mcfg_entry->bus_end, mcfg_entry->base);

		size_t page_cnt = DIV_ROUNDUP(
			PCI_CONFIG(mcfg_entry->base, mcfg_entry->bus_end, 31, 7) -
				PCI_CONFIG(mcfg_entry->base, mcfg_entry->bus_start, 0, 0),
			PAGE_SIZE);

		uintptr_t addr;
		int ret =
			as_vmem_allocate(CAPABILITY_SELF_AS, &addr, page_cnt * PAGE_SIZE);
		if (ret == -1) {
			print("ERROR: failed to map configuration space for segment=%d\n",
				  mcfg_entry->segment);
			RETURN_ERROR;
		}

		struct portal_req portal_req = {
			.type = PORTAL_REQ_DIRECT,
			.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
			.length = sizeof(struct portal_req),
			.morphology = { .addr = addr,
							.length = page_cnt * PAGE_SIZE,
							.paddr = mcfg_entry->base,
							.pcnt = page_cnt }
		};

		struct portal_resp portal_resp;
		struct syscall_response syscall_response =
			SYSCALL2(SYSCALL_PORTAL, &portal_req, &portal_resp);
		if (syscall_response.ret == -1 || portal_resp.base != addr ||
			portal_resp.limit != page_cnt * PAGE_SIZE)
			RETURN_ERROR;

		struct pci_segment *pci_segment = alloc(sizeof(struct pci_segment));
		pci_segment->segment = mcfg_entry->segment;
		pci_segment->address_space = (void *)addr;
		pci_segment->mcfg_entry = *mcfg_entry;

		ret = dictionary_push(&segment_tree, &pci_segment->segment, pci_segment,
							  sizeof(struct pci_segment));
		if (ret == -1)
			RETURN_ERROR;

		for (int bus = mcfg_entry->bus_start; bus < mcfg_entry->bus_end;
			 bus++) {
			for (int device = 0; device < 32; device++) {
				for (int func = 0; func < 8; func++) {
					volatile union pci_config *config = PCI_CONFIG(
						pci_segment->address_space, bus, device, func);
					if (config->device.vendor_id == 0xffff)
						continue;

					struct pci_device *pci_device =
						alloc(sizeof(struct pci_device));

					pci_device->config = config;
					pci_device->descriptor =
						(struct pci_descriptor){ .segment = mcfg_entry->segment,
												 .bus = bus,
												 .device = device,
												 .func = func };

					ret = dictionary_push(&device_tree, &pci_device->descriptor,
										  pci_device,
										  sizeof(struct pci_descriptor));
					if (ret == -1)
						RETURN_ERROR;

					int ret = pci_device_spawn(pci_device);
					if (ret == -1)
						RETURN_ERROR;
				}
			}
		}
	}

	return 0;
}
