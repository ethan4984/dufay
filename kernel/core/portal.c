#include <arch/x86/smp.h>
#include <arch/x86/paging.h>

#include <core/physical.h>
#include <core/syscall.h>
#include <core/portal.h>
#include <core/debug.h>
#include <core/virtual.h>

#include <fayt/compiler.h>
#include <fayt/circular_queue.h>
#include <fayt/string.h>
#include <fayt/bst.h>
#include <fayt/debug.h>

#define PAGE_FAULT_COW (1 << 9)
#define PAGE_FAULT_SHARE (1 << 10)
#define PAGE_FAULT_SP (1 << 11)

static int portal_handle_direct(struct portal *portal, struct portal_req *req, struct portal_resp*);
static int portal_handle_anon(struct portal *portal, struct portal_req *req,  struct portal_resp*);
static int portal_handle_share(struct portal *portal, struct portal_req *req, struct portal_resp*);
static int portal_handle_cow(struct portal *portal, struct portal_req *req,  struct portal_resp*);
static int portal_handle_sp(struct portal *portal, struct portal_req *req, struct portal_resp*);

static int portal_fault_anon(struct page_table *page_table, uintptr_t addr);
static int portal_fault_share(struct page_table *page_table, uintptr_t addr);
static int portal_fault_sp(struct page_table *page_table, uintptr_t addr);
static int portal_fault_cow(struct page_table *page_table, uintptr_t addr);

struct gateway_orb {
	const char *identifier;

	VECTOR(struct context) active_contexts;

	int page_cnt;
	struct page *pages;
	uint64_t **pml;
};

struct hash_table portal_gateway_map;

static uint64_t portal_translate_protections(int permission) {
	uint64_t ret = X86_FLAGS_P | X86_FLAGS_US | X86_FLAGS_NX;

	if(permission & PORTAL_PROT_WRITE) ret |= X86_FLAGS_RW;
	if(permission & PORTAL_PROT_EXEC) ret &= ~X86_FLAGS_NX;

	return ret;
}

int portal_resolve_fault(uintptr_t faulting_address, uint64_t error_code) {
	struct context *context = CORE_LOCAL->current_context;
	
	if(unlikely(context == NULL)) RETURN_ERROR;

	struct page_table *page_table = context->page_table;
	if(unlikely(page_table == NULL)) RETURN_ERROR;

	uint64_t faulting_page = faulting_address & ~(0xfff);
	uint64_t *pml_entry = page_table->page_entry(page_table, faulting_page);
	uint64_t page_entry = (pml_entry == NULL) ? 0 : *pml_entry;

	if((error_code & X86_FLAGS_P) == 0) if(portal_fault_anon(page_table,
				faulting_address) == -1) RETURN_ERROR;
	if(page_entry & PAGE_FAULT_COW) if(portal_fault_cow(page_table,
				faulting_address) == -1) RETURN_ERROR;
	if(page_entry & PAGE_FAULT_SHARE) if(portal_fault_share(page_table,
				faulting_address) == -1) RETURN_ERROR;
	if(page_entry & PAGE_FAULT_SP) if(portal_fault_sp(page_table,
				faulting_address) == -1) RETURN_ERROR;

	return 0;
}

static int portal_fault_anon(struct page_table *page_table, uintptr_t addr) {
	struct portal *root = page_table->portal_root;
	if(unlikely(root == NULL)) RETURN_ERROR;

	while(root) {
		if((root->type & PORTAL_REQ_ANON) == 0) goto next;

		if(root->base <= addr && (root->base + root->limit) >= addr) {
			uintptr_t flags = X86_FLAGS_P | X86_FLAGS_US | X86_FLAGS_NX;

			if(root->prot & PORTAL_PROT_WRITE) flags |= X86_FLAGS_RW;
			if(root->prot & PORTAL_PROT_EXEC) flags &= ~(X86_FLAGS_NX);

			uintptr_t misalignment = addr & (PAGE_SIZE - 1); 
			uintptr_t vaddr = addr - misalignment;

			invlpg(vaddr);

			struct page *page = NULL;
			if((root->type & PORTAL_REQ_SHARE) == PORTAL_REQ_SHARE) {
				struct gateway_orb *orb = root->orb;
				if(orb == NULL) {
					print("DUFAY: backing sharepoint does not exist\n");
					RETURN_ERROR;	
				}

				size_t page_index = (vaddr - root->base) / PAGE_SIZE;
				if(page_index >= orb->page_cnt) {
					print("DUFAY: page index out of bounds\n");
					RETURN_ERROR;
				}

				page = orb->pages + page_index;

				if(page->frame == NULL) {
					page->frame = alloc(sizeof(struct frame));
					page->frame->paddr = pmm_alloc(1, 1);
					page->frame->refcnt = 1;
					page->vaddr = vaddr;
				} else page->frame->refcnt++;
			} else {
				page = alloc(sizeof(struct page));

				page->frame = alloc(sizeof(struct frame));
				page->frame->paddr = pmm_alloc(1, 1);
				page->frame->refcnt = 1;
				page->vaddr = vaddr;
			}

			page_table->map_page(page_table, page->vaddr, page->frame->paddr, flags);
			int ret = hash_table_push(page_table->pages, &page->vaddr, page, sizeof(page->vaddr));
			if(ret == -1) RETURN_ERROR;

			return 0;
		}
next:
		if(root->base > addr) {
			root = root->left;
		} else {
			root = root->right;
		}
	}

	RETURN_ERROR;
}

static int portal_fault_share(struct page_table *page_table, uintptr_t addr) {
	page_table;
	addr;
	RETURN_ERROR;
}

static int portal_fault_sp(struct page_table *page_table, uintptr_t addr) {
	page_table;
	addr;
	RETURN_ERROR;
}

static int portal_fault_cow(struct page_table *page_table, uintptr_t addr) {
	page_table;
	addr;
	RETURN_ERROR;
}

static int portal_handle_direct(struct portal *portal, struct portal_req *req, struct portal_resp*) {
	if(unlikely(portal == NULL || req == NULL)) RETURN_ERROR;

	int page_cnt = req->morphology.pcnt;
	uintptr_t paddr = req->morphology.paddr;
	uintptr_t vaddr = req->morphology.addr;

	for(size_t i = 0; i < page_cnt; i++) {
		uint64_t permissions = portal_translate_protections(req->prot);

		portal->page_table->map_page(portal->page_table, vaddr, paddr, permissions);

		vaddr += PAGE_SIZE;
		paddr += PAGE_SIZE;
	}

	portal->type |= PORTAL_REQ_DIRECT;

	return 0;
}

static int portal_handle_anon(struct portal *portal, struct portal_req *req, struct portal_resp *resp) {
	if(unlikely(portal == NULL || req == NULL)) RETURN_ERROR;

	if(req->type & PORTAL_REQ_CONTINUOUS) {
		uint64_t permissions = portal_translate_protections(req->prot);

		uintptr_t vaddr = req->morphology.addr;
		uint64_t paddr = pmm_alloc(req->morphology.pcnt, 1);

		for(size_t i = 0; i < req->morphology.pcnt; i++) {
			portal->page_table->map_page(portal->page_table,
				vaddr + i * PAGE_SIZE, paddr + i * PAGE_SIZE, permissions);
		}

		portal->type |= PORTAL_REQ_CONTINUOUS;
		if(req->type & PORTAL_REQ_PEEK) {
			resp->morphology.paddr = paddr;
			resp->morphology.pcnt = req->morphology.pcnt;

			portal->type |= PORTAL_REQ_PEEK;
		}
	}

	portal->type |= PORTAL_REQ_ANON;

	return 0;
}

static int portal_handle_share(struct portal *portal, struct portal_req *req, struct portal_resp *resp) {
	if(unlikely(portal == NULL || req == NULL)) RETURN_ERROR;

	struct gateway_orb *orb = NULL;
	int ret = hash_table_search(&portal_gateway_map,
		(void*)req->share.identifier, strlen(req->share.identifier), (void**)&orb);

	if(orb == NULL && !req->share.create) RETURN_ERROR;	
	if(orb == NULL && req->share.create) {
		orb = alloc(sizeof(struct gateway_orb));

		orb->identifier = alloc(strlen(req->share.identifier) + 1);
		orb->page_cnt = DIV_ROUNDUP(req->morphology.length, PAGE_SIZE);
		orb->pages = alloc(sizeof(struct page) * orb->page_cnt);
		
		strcpy((void*)orb->identifier, req->share.identifier);

		int ret = hash_table_push(&portal_gateway_map, (void*)orb->identifier, orb,
			strlen(orb->identifier));
		if(ret == -1) RETURN_ERROR;

		if((req->type & PORTAL_REQ_DIRECT) == PORTAL_REQ_DIRECT) {
			uint64_t paddr = req->morphology.paddr;
			
			for(int i = 0; i < orb->page_cnt; i++) {
				struct page *page = orb->pages + i;

				page->vaddr = req->morphology.addr + i * PAGE_SIZE;
				page->paddr = paddr;
				page->frame = NULL;

				paddr += PAGE_SIZE;
			}

			if(unlikely((portal->type & PORTAL_REQ_DIRECT) != PORTAL_REQ_DIRECT))
				if(portal_handle_direct(portal, req, resp) == -1) RETURN_ERROR;
		}

		if((req->share.type & LINK_CIRCULAR) == LINK_CIRCULAR) {
			struct portal_link *link = (void*)req->morphology.addr;
			struct circular_queue *queue = (void*)link + sizeof(struct portal_link);

			*link = (struct portal_link) {
				.lock = 0,
				.length = req->morphology.length,
				.header_offset = 0,
				.header_limit = sizeof(struct portal_link),
				.data_offset = sizeof(struct portal_link),
				.data_limit = req->morphology.length - sizeof(struct portal_link) - sizeof(struct circular_queue),
				.magic = LINK_CIRCULAR_MAGIC
			};

			int queue_length = (req->morphology.length - sizeof(struct portal_link) -
				sizeof(struct circular_queue)) / req->share.length;

			circular_queue_init(queue, sizeof(struct portal_link)
				+ sizeof(struct circular_queue), queue_length, req->share.length);
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

	uintptr_t vaddr = req->morphology.addr;

	for(int i = 0; i < orb->page_cnt; i++) {
		struct page *page = orb->pages + i;

		uintptr_t paddr = page->paddr;
		if(page->frame == NULL) goto map;
		paddr = page->frame->paddr;
map:
		uint64_t permissions = portal_translate_protections(req->prot);

		portal->page_table->map_page(portal->page_table, vaddr, paddr, permissions);
		vaddr += PAGE_SIZE;
	}			struct portal_link *link = (void*)req->morphology.addr;

	portal->orb = orb;
	portal->type |= PORTAL_REQ_SHARE;

	return 0;
}

static int portal_handle_cow(struct portal *portal, struct portal_req *req, struct portal_resp*) {
	if(unlikely(portal == NULL || req == NULL)) RETURN_ERROR;
	portal->type |= PORTAL_REQ_COW;
	return 0;
}

static int portal_handle_sp(struct portal *portal, struct portal_req *req, struct portal_resp*) {
	if(unlikely(portal == NULL || req == NULL)) RETURN_ERROR;
	portal->type |= PORTAL_REQ_SP;
	return 0;
}

int portal(struct portal_req *req, struct portal_resp *resp) {
	if(req == NULL || resp == NULL) RETURN_ERROR;

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

	BST_GENERIC_INSERT(page_table->portal_root, base, portal);

	if(req->type & PORTAL_REQ_DIRECT) if(portal_handle_direct(portal, req, resp) == -1) goto failure;
	if(req->type & PORTAL_REQ_ANON) if(portal_handle_anon(portal, req, resp) == -1) goto failure;
	if(req->type & PORTAL_REQ_SHARE) if(portal_handle_share(portal, req, resp) == -1) goto failure;
	if(req->type & PORTAL_REQ_COW) if(portal_handle_cow(portal, req, resp) == -1) goto failure;
	if(req->type & PORTAL_REQ_SP) if(portal_handle_sp(portal, req, resp) == -1) goto failure;

	resp->base = portal->base;
	resp->limit = portal->limit;
	resp->flags = PORTAL_RESP_SUCCESS | portal->flags;

	return 0;
failure:
	resp->base = 0;
	resp->limit = 0;
	resp->flags = PORTAL_RESP_FAILURE | portal->flags;

	free(portal);

	RETURN_ERROR;
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
