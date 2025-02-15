#include <arch/x86/cpu.h>

#include <core/aslr.h>

#include <fayt/compiler.h>
#include <fayt/debug.h>
#include <fayt/slab.h>
#include <fayt/string.h>

int aslr_generate_layout(struct aslr *aslr, struct aslr_layout **ret,
						 size_t length)
{
	if (unlikely(aslr == NULL || ret == NULL))
		return -1;

	struct aslr_layout *layout = alloc(sizeof(struct aslr_layout));
	if (unlikely(layout == NULL))
		return -1;

	*ret = layout;

	for (;;) {
		layout->lower_bound =
			ALIGN_UP(aslr->minimum_vaddr + (rdrand() % (aslr->maximum_vaddr -
														aslr->minimum_vaddr)),
					 PAGE_SIZE);
		layout->upper_bound = layout->lower_bound + length;

		for (struct aslr_layout *root = aslr->layout; root; root = root->next) {
			if (root->upper_bound <= layout->lower_bound ||
				layout->upper_bound <= root->lower_bound)
				continue;
			else
				goto next;
		}
		break;
next:
		continue;
	}

	layout->next = aslr->layout;
	layout->last = NULL;
	if (aslr->layout)
		aslr->layout->last = layout;
	aslr->layout = layout;

	return 0;
}
