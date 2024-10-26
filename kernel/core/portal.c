#include <arch/x86/smp.h>
#include <arch/x86/paging.h>

#include <core/physical.h>
#include <core/syscall.h>
#include <core/portal.h>
#include <core/debug.h>

#include <fayt/compiler.h>
#include <fayt/circular_queue.h>
#include <fayt/string.h>
#include <fayt/bst.h>

#define PAGE_FAULT_COW (1 << 9)
#define PAGE_FAULT_SHARE (1 << 10)
#define PAGE_FAULT_SP (1 << 11)

static int portal_handle_direct(struct portal *portal, struct portal_req *req);
static int portal_handle_anon(struct portal *portal, struct portal_req *req); 
static int portal_handle_share(struct portal *portal, struct portal_req *req);
static int portal_handle_cow(struct portal *portal, struct portal_req *req);
static int portal_handle_sp(struct portal *portal, struct portal_req *req);

static int portal_fault_anon(struct page_table *page_table, uintptr_t addr);
static int portal_fault_share(struct page_table *page_table, uintptr_t addr);
static int portal_fault_sp(struct page_table *page_table, uintptr_t addr);
static int portal_fault_cow(struct page_table *page_table, uintptr_t addr);

struct portal_share_point {
	const char *identifier;
	VECTOR(struct context) active_contexts;
	size_t length;
	uint64_t paddr[];
};

struct hash_table portal_share_map;

static uint64_t portal_translate_protections(int permission) {
	uint64_t ret = X86_FLAGS_P | X86_FLAGS_US | X86_FLAGS_NX;

	if(permission & PORTAL_PROT_WRITE) ret |= X86_FLAGS_RW;
	if(permission & PORTAL_PROT_EXEC) ret &= ~X86_FLAGS_NX;

	return ret;
}

int portal_resolve_fault(uintptr_t faulting_address, uint64_t error_code) {
	struct context *context = CORE_LOCAL->current_context;
	if(unlikely(context == NULL)) return -1;

	struct page_table *page_table = context->page_table;
	if(unlikely(page_table == NULL)) return -1;

	uint64_t faulting_page = faulting_address & ~(0xfff);
	uint64_t *pml_entry = page_table->page_entry(page_table, faulting_page);
	uint64_t page_entry = (pml_entry == NULL) ? 0 : *pml_entry;

	if((error_code & X86_FLAGS_P) == 0) if(portal_fault_anon(page_table,
				faulting_address) == -1) return -1;
	if(page_entry & PAGE_FAULT_COW) if(portal_fault_cow(page_table,
				faulting_address) == -1) return -1;
	if(page_entry & PAGE_FAULT_SHARE) if(portal_fault_share(page_table,
				faulting_address) == -1) return -1;
	if(page_entry & PAGE_FAULT_SP) if(portal_fault_sp(page_table,
				faulting_address) == -1) return -1;

	return 0;
}

static int portal_handle_direct(struct portal *portal, struct portal_req *req) {
	if(unlikely(portal == NULL || req == NULL)) return -1;

	size_t page_cnt = DIV_ROUNDUP(req->morphology.length, PAGE_SIZE);

	uintptr_t vaddr = req->morphology.addr;

	for(size_t i = 0; i < page_cnt; i++) {
		uintptr_t paddr = req->morphology.paddr[i];

		uint64_t permissions = portal_translate_protections(req->prot);

		portal->page_table->map_page(portal->page_table, vaddr, paddr, permissions);

		vaddr += PAGE_SIZE;
	}

	portal->type |= PORTAL_REQ_DIRECT;

	return 0;
}

static int portal_fault_anon(struct page_table *page_table, uintptr_t addr) {
	struct portal *root = page_table->portal_root;
	if(unlikely(root == NULL)) return -1;

	while(root) {
		if((root->type & PORTAL_REQ_ANON) == 0) goto next;

		if(root->base <= addr && (root->base + root->limit) >= addr) {
			uintptr_t flags = X86_FLAGS_P | X86_FLAGS_US | X86_FLAGS_NX;

			if(root->prot & PORTAL_PROT_WRITE) flags |= X86_FLAGS_RW;
			if(root->prot & PORTAL_PROT_EXEC) flags &= ~(X86_FLAGS_NX);

			uintptr_t misalignment = addr & (PAGE_SIZE - 1); 
			uintptr_t vaddr = addr - misalignment;

			invlpg(vaddr);

			uint64_t paddr;

			struct frame *shared_frame = NULL;
			if(root->type & PORTAL_REQ_SHARE) {
				struct portal_share_point *share_point;
				int ret = hash_table_search(&portal_share_map,
					(void*)root->share.identifier, strlen(root->share.identifier), (void**)&share_point);
				if(share_point == NULL) {
					print("DUFAY: backing sharepoint does not exist\n");
					return -1;	
				}

				size_t page_index = (vaddr - root->base) / PAGE_SIZE;
				if(page_index > (share_point->length - sizeof(struct portal_share_point)) / sizeof(uint64_t)) {
					print("DUFAY: page index out of bounds\n");
					return -1;
				}

				if(!share_point->paddr[page_index]) share_point->paddr[page_index] = pmm_alloc(1, 1);
				paddr = share_point->paddr[page_index];
			} else paddr = pmm_alloc(1, 1);

			struct page *page = alloc(sizeof(struct page));
			*page = (struct page) {
				.vaddr = vaddr,
				.frame = (shared_frame == NULL) ? alloc(sizeof(struct frame)) : shared_frame
			};

			if(shared_frame == NULL) {
				*page->frame = (struct frame) {
					.paddr = pmm_alloc(1, 1),
					.refcnt = 1
				};
			} else page->frame->refcnt++;

			page_table->map_page(page_table, page->vaddr, page->frame->paddr, flags);
			int ret = hash_table_push(page_table->pages, &page->vaddr, page, sizeof(page->vaddr));
			if(ret == -1) return -1;

			return 0;
		}
next:
		if(root->base > addr) {
			root = root->left;
		} else {
			root = root->right;
		}
	}

	return -1;
}

static int portal_fault_share(struct page_table *page_table, uintptr_t addr) {
	page_table;
	addr;

	return -1;
}

static int portal_fault_sp(struct page_table *page_table, uintptr_t addr) { 
	page_table;
	addr;

	return -1;
}

static int portal_fault_cow(struct page_table *page_table, uintptr_t addr) { 
	page_table;
	addr;

	return -1;
}

static int portal_handle_anon(struct portal *portal, struct portal_req *req) {
	if(unlikely(portal == NULL || req == NULL)) return -1;

	portal->type |= PORTAL_REQ_ANON;

	return 0;
}

static int portal_handle_share(struct portal *portal, struct portal_req *req) {
	if(unlikely(portal == NULL || req == NULL)) return -1;

	portal->type |= PORTAL_REQ_SHARE;

	struct portal_share_point *share_point;
	int ret = hash_table_search(&portal_share_map,
		(void*)req->share.identifier, strlen(req->share.identifier), (void**)&share_point);

	if(share_point == NULL && !req->share.create) return -1;	
	if(share_point == NULL && req->share.create) {
		share_point = alloc(sizeof(struct portal_share_point));
		share_point->identifier = alloc(strlen(req->share.identifier) + 1);
		share_point->length = sizeof(struct portal_share_point) + DIV_ROUNDUP(req->morphology.length, PAGE_SIZE);

		strcpy((void*)share_point->identifier, req->share.identifier);
		portal->share.identifier = share_point->identifier;

		int ret = hash_table_push(&portal_share_map, (void*)share_point->identifier, share_point,
			strlen(share_point->identifier));
		if(ret == -1) return -1;

		if((req->share.type & LINK_CIRCULAR) == LINK_CIRCULAR) {
			struct portal_link *link = (void*)req->morphology.addr;
			struct circular_queue *queue = (void*)link + sizeof(struct portal_link);

			*link = (struct portal_link) {
				.lock = 0,
				.length = req->morphology.length,
				.header_offset = 0,
				.header_limit = sizeof(struct portal_link),
				.data_offset = sizeof(struct portal_link) + sizeof(struct circular_queue),
				.data_limit = req->morphology.length - sizeof(struct portal_link) - sizeof(struct circular_queue),
				.magic = LINK_CIRCULAR_MAGIC
			};

			int queue_length = (req->morphology.length - sizeof(struct portal_link) -
				sizeof(struct circular_queue)) / 4;
			circular_queue_init(queue, (void*)req->morphology.addr + sizeof(struct portal_link)
				+ sizeof(struct circular_queue), queue_length, 4);
		} else if((req->share.type & LINK_RAW) == LINK_RAW) {
			struct portal_link *link = (void*)req->morphology.addr;

			*link = (struct portal_link) {
				.lock = 0,
				.length = req->morphology.length,
				.header_offset = 0,
				.header_limit = 0, 
				.data_offset = sizeof(struct portal_link),
				.data_limit = req->morphology.length - sizeof(struct portal_link),
				.magic = LINK_RAW_MAGIC
			};
		}
	}

	portal->share.identifier = share_point->identifier;

	return 0;
}

static int portal_handle_cow(struct portal *portal, struct portal_req *req) {
	if(unlikely(portal == NULL || req == NULL)) return -1;
	portal->type |= PORTAL_REQ_COW;
	return 0;
}

static int portal_handle_sp(struct portal *portal, struct portal_req *req) {
	if(unlikely(portal == NULL || req == NULL)) return -1;
	portal->type |= PORTAL_REQ_SP;
	return 0;
}

int portal(struct portal_req *req, struct portal_resp *resp) {
	if(req == NULL || resp == NULL) return -1;

	struct portal *portal = alloc(sizeof(struct portal));

	portal->base = req->morphology.addr;
	portal->limit = req->morphology.length;

	struct context *context = CORE_LOCAL->current_context;
	struct page_table *page_table = NULL; 

	if(likely(context)) page_table = context->page_table;
	if(unlikely(context == NULL)) page_table = &kernel_mappings;
	if(unlikely(page_table == NULL)) goto failure;

	portal->prot = req->prot;
	portal->context = context;
	portal->page_table = page_table;

	if(unlikely(req == NULL)) goto failure;

	if(req->type & PORTAL_REQ_DIRECT) if(portal_handle_direct(portal, req) == -1) goto failure;
	if(req->type & PORTAL_REQ_ANON) if(portal_handle_anon(portal, req) == -1) goto failure;
	if(req->type & PORTAL_REQ_SHARE) if(portal_handle_share(portal, req) == -1) goto failure;
	if(req->type & PORTAL_REQ_COW) if(portal_handle_cow(portal, req) == -1) goto failure;
	if(req->type & PORTAL_REQ_SP) if(portal_handle_sp(portal, req) == -1) goto failure;

	BST_GENERIC_INSERT(page_table->portal_root, base, portal);

	resp->base = portal->base;
	resp->limit = portal->limit;
	resp->flags = PORTAL_RESP_SUCCESS | portal->flags;

	return 0;
failure:
	resp->base = 0;
	resp->limit = 0;
	resp->flags = PORTAL_RESP_FAILURE | portal->flags;

	free(portal);

	return -1;
}

SYSCALL_DEFINE2(portal, struct portal_req*, req, struct portal_resp*, resp, {
	int ret = portal(req, resp); 
	if(ret != 0) return ret;
})

void portal_destroy(struct portal *portal) {
	if(portal == NULL) return;

	portal_destroy(portal->left);
	portal_destroy(portal->right);

	for(int i = 0; i < portal->pages->capacity; i++) {
		struct page *page = portal->pages->data[i];

		if(page) {
			if(--page->frame->refcnt == 0) {
				pmm_free(page->frame->paddr, 1);
			}

			free(page);
		}
	}

	hash_table_destroy(portal->pages);

	free(portal);
}
