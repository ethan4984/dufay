#include <fayt/debug.h>
#include <fayt/slab.h>
#include <fayt/string.h>

#include <nvme.h>

struct nvme_controller {
	struct nvme_regs *regs;
	struct nvme_controller_id *id;

	struct {
		int major;
		int minor; 
		int tertiary;
	} version;

	unsigned int page_size_max;
	unsigned int page_size_min;
};

int nvme(struct nvme_regs *regs) {
	if(regs == NULL) return -1;

	struct nvme_controller *controller = alloc(sizeof(struct nvme_controller));

	controller->version.major = (regs->vs >> 16) & 0xffff;
	controller->version.minor = (regs->vs >> 8) & 0xff;
	controller->version.tertiary = (regs->vs >> 0) & 0xff;

	print("dufay: nvme: version detected %d:%d:%d\n", controller->version.major,
		controller->version.minor, controller->version.tertiary);

	return 0;
}
