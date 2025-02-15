#include <fayt/address_space.h>
#include <fayt/syscall.h>
#include <fayt/portal.h>
#include <fayt/debug.h>
#include <fayt/string.h>

#ifndef DUFAY

int as_address(struct address_space *as, uintptr_t *ret, size_t size) {
	if(as == NULL || ret == NULL || size == 0 ||
		(as->current + size) > (as->base + as->limit)) RETURN_ERROR;

	size = DIV_ROUNDUP(size, PAGE_SIZE) * PAGE_SIZE;

	uintptr_t address = as->current;

	for(struct address_hole *hole = as->hole_root; hole; hole = hole->next) {
		if((address < hole->base + hole->limit) && (hole->base < address + size)) address += hole->limit;
		hole = hole->next;
	}

	as->current = address + size;
	*ret = address;

	return 0;
}

int as_allocate(struct address_space *as, uintptr_t *address, size_t cnt) {
	if(as == NULL || address == NULL) RETURN_ERROR;

	int ret = as_address(as, address, cnt);
	if(ret == -1) RETURN_ERROR;

	struct portal_req portal_req = {
		.type = PORTAL_REQ_ANON,
		.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE,
		.length = sizeof(struct portal_req),
		.share = {
			.identifier = NULL, 
			.type = 0,
			.create = 0 
		},
		.morphology = {
			.addr = *address,
			.length = cnt * PAGE_SIZE 
		}
	};

	struct portal_resp portal_resp;

	struct syscall_response syscall_response = SYSCALL2(SYSCALL_PORTAL, &portal_req, &portal_resp);
	if(syscall_response.ret == -1 || portal_resp.base != *address ||
		portal_resp.limit != (cnt * PAGE_SIZE)) RETURN_ERROR;

	return 0;
}

int as_insert_hole(struct address_space *as, struct address_hole *hole) {
	if(as == NULL || hole == NULL) RETURN_ERROR;

	if(as->hole_root == NULL) {
		as->hole_root = hole;
		as->hole_tail = as->hole_root;
		return 0;
	}	

	hole->next = NULL;
	hole->last = as->hole_tail;

	as->hole_tail->next = hole;
	as->hole_tail = hole;

	return 0;
}

int as_delete_hole(struct address_space *as, uintptr_t base, size_t limit) {
	if(as == NULL) RETURN_ERROR;

	struct address_hole *hole = as->hole_root;

	for(;hole;) {
		if(base >= (hole->base + hole->limit) || hole->base >= (base + limit)) continue;

		if(base == hole->base && limit == hole->limit) {
			hole->last->next = hole->next;
			hole->next->last = hole->last;
			break;
		}

		// handle fractional splits

		hole = hole->next;
	}

	return 0;
}

#endif
