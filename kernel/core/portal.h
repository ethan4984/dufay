#ifndef PORTAL_H_
#define PORTAL_H_

#include <fayt/hash.h>
#include <fayt/bst.h>
#include <fayt/portal.h>

#include <stdint.h>
#include <stddef.h>

#define SHARE_MAX_NAME_LENGTH 256

struct portal {
	int type;
	int prot;

	struct context *context;
	struct page_table *page_table;

	struct gateway_orb *orb;

	uintptr_t base;
	uintptr_t limit;
	uint64_t flags;

	struct hash_table *pages;

	struct portal *left;
	struct portal *right;
	struct portal *parent;
};

int portal(struct portal_req *req, struct portal_resp *resp);
int portal_resolve_fault(uintptr_t faulting_address, uint64_t error_code);
void portal_destroy(struct portal *portal);

#endif
