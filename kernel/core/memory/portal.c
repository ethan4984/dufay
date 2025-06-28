#include <arch/x86/smp.h>
#include <arch/x86/paging.h>

#include <core/memory/physical.h>
#include <core/syscall.h>
#include <core/memory/portal.h>
#include <core/debug.h>
#include <core/memory/virtual.h>
#include <core/memory/address.h>

#include <aria/compiler.h>
#include <aria/circular_queue.h>
#include <aria/string.h>
#include <aria/bst.h>
#include <aria/debug.h>

#define PORTAL_MAP_COW (1 << 9)
#define PORTAL_MAP_SHARE (1 << 10)
#define PORTAL_MAP_SP (1 << 11)

static int portal_handle_direct(struct portal *, struct portal_req *,
								struct portal_resp *);
static int portal_handle_anon(struct portal *, struct portal_req *,
							  struct portal_resp *);
static int portal_handle_share(struct portal *, struct portal_req *,
							   struct portal_resp *);
static int portal_handle_cow(struct portal *, struct portal_req *,
							 struct portal_resp *);
static int portal_handle_sp(struct portal *, struct portal_req *,
							struct portal_resp *);

static int portal_fault_anon(struct page_table *, uint64_t *, uintptr_t);
static int portal_fault_share(struct page_table *, uint64_t *, uintptr_t);
static int portal_fault_sp(struct page_table *, uint64_t *, uintptr_t);
static int portal_fault_cow(struct page_table *, uint64_t *, uintptr_t);

struct gateway_orb {
	const char *identifier;

	VECTOR(struct thread) active_threads;

	int page_cnt;
	struct page *pages;
	uint64_t **pml;
};

struct dictionary portal_gateway_map;

static uint64_t portal_translate_protections(int permission)
{
	uint64_t ret = X86_FLAGS_P | X86_FLAGS_US | X86_FLAGS_NX;

	if (permission & PORTAL_PROT_WRITE)
		ret |= X86_FLAGS_RW;
	if (permission & PORTAL_PROT_EXEC)
		ret &= ~X86_FLAGS_NX;

	return ret;
}

int portal_resolve_fault(uintptr_t faulting_address, uint64_t error_code)
{
	struct thread *thread = CORE_LOCAL->current_thread;

	if (unlikely(thread == NULL))
		RETURN_ERROR;

	struct page_table *page_table = thread->address_space->page_table;
	if (unlikely(page_table == NULL))
		RETURN_ERROR;

	uint64_t faulting_page = faulting_address & ~(0xfff);
	uint64_t *pmle = page_table->page_entry(page_table, faulting_page);
	uint64_t page_entry = (pmle == NULL) ? 0 : *pmle;

	if ((error_code & X86_FLAGS_P) == 0)
		if (portal_fault_anon(page_table, pmle, faulting_address) == -1)
			RETURN_ERROR;
	if (page_entry & PORTAL_MAP_COW)
		if (portal_fault_cow(page_table, pmle, faulting_address) == -1)
			RETURN_ERROR;
	if (page_entry & PORTAL_MAP_SHARE)
		if (portal_fault_share(page_table, pmle, faulting_address) == -1)
			RETURN_ERROR;
	if (page_entry & PORTAL_MAP_SP)
		if (portal_fault_sp(page_table, pmle, faulting_address) == -1)
			RETURN_ERROR;

	return 0;
}

static int portal_fault_anon(struct page_table *page_table, uint64_t *,
							 uintptr_t addr)
{
	struct portal *root = page_table->portal_root;
	if (unlikely(root == NULL))
		RETURN_ERROR;

	while (root) {
		if ((root->type & PORTAL_REQ_ANON) == 0)
			goto next;

		if (root->base <= addr && (root->base + root->limit) >= addr) {
			uintptr_t flags = X86_FLAGS_P | X86_FLAGS_US | X86_FLAGS_NX;

			if (root->prot & PORTAL_PROT_WRITE)
				flags |= X86_FLAGS_RW;
			if (root->prot & PORTAL_PROT_EXEC)
				flags &= ~(X86_FLAGS_NX);

			uintptr_t misalignment = addr & (PAGE_SIZE - 1);
			uintptr_t vaddr = addr - misalignment;

			invlpg(vaddr);

			struct page *page = NULL;
			if ((root->type & PORTAL_REQ_SHARE) == PORTAL_REQ_SHARE) {
				struct gateway_orb *orb = root->orb;
				if (orb == NULL) {
					print("DUFAY: backing sharepoint does not exist\n");
					RETURN_ERROR;
				}

				size_t page_index = (vaddr - root->base) / PAGE_SIZE;
				if (page_index >= orb->page_cnt) {
					print("DUFAY: page index out of bounds\n");
					RETURN_ERROR;
				}

				page = orb->pages + page_index;

				if (page->frame == NULL) {
					page->frame = alloc(sizeof(struct frame));
					if (page->frame == NULL)
						RETURN_ERROR;

					page->frame->paddr = pmm_alloc(1, 1);
					if (!page->frame->paddr)
						RETURN_ERROR;
					page->frame->refcnt = 1;
					page->vaddr = vaddr;
				} else
					page->frame->refcnt++;
			} else {
				page = alloc(sizeof(struct page));
				if (page == NULL)
					RETURN_ERROR;

				page->frame = alloc(sizeof(struct frame));
				if (page->frame == NULL)
					RETURN_ERROR;
				page->frame->paddr = pmm_alloc(1, 1);
				if (!page->frame->paddr)
					RETURN_ERROR;
				page->frame->refcnt = 1;
				page->vaddr = vaddr;
			}

			page_table->map_page(page_table, page->vaddr, page->frame->paddr,
								 flags);
			int ret = dictionary_push(page_table->pages, &page->vaddr, page,
									  sizeof(page->vaddr));
			if (ret == -1)
				RETURN_ERROR;

			return 0;
		}
next:
		if (root->base > addr) {
			root = root->left;
		} else {
			root = root->right;
		}
	}

	RETURN_ERROR;
}

static int portal_fault_share(struct page_table *page_table, uint64_t *pmle,
							  uintptr_t addr)
{
	page_table;
	pmle;
	addr;
	RETURN_ERROR;
}

static int portal_fault_sp(struct page_table *page_table, uint64_t *pmle,
						   uintptr_t addr)
{
	page_table;
	pmle;
	addr;
	RETURN_ERROR;
}

static int portal_fault_cow(struct page_table *page_table, uint64_t *pmle,
							uintptr_t addr)
{
	if (page_table == NULL || pmle == NULL)
		RETURN_ERROR;

	struct page *page = NULL;
	int ret = dictionary_search(page_table->pages, &addr, sizeof(addr),
								(void **)page);
	if (unlikely(ret == -1 || page == NULL))
		RETURN_ERROR;

	uint64_t original_frame = *pmle & ~(0xfff);
	uint64_t new_frame = ({
		uint64_t frame;
		if (*page->refcnt <= 1)
			frame = original_frame;
		else {
			page->frame = alloc(sizeof(struct frame));
			if (page->frame == NULL)
				RETURN_ERROR;
			new_frame = pmm_alloc(1, 1);
			if (!new_frame)
				RETURN_ERROR;
			memcpy((void *)new_frame + HIGH_VMA,
				   (void *)original_frame + HIGH_VMA, PAGE_SIZE);
		}
		frame;
	});

	(*page->refcnt)--;

	*pmle = original_frame | (*pmle & 0x1ff) | X86_FLAGS_RW;
	invlpg(addr);

	page->frame->paddr = new_frame;
	page->refcnt = alloc(sizeof(page->refcnt));
	if (page->refcnt == NULL)
		RETURN_ERROR;
	*page->refcnt = 1;

	return 0;
}

static int portal_handle_direct(struct portal *portal, struct portal_req *req,
								struct portal_resp *)
{
	if (unlikely(portal == NULL || req == NULL))
		RETURN_ERROR;

	int page_cnt = req->morphology.pcnt;
	uintptr_t paddr = req->morphology.paddr;
	uintptr_t vaddr = req->morphology.addr;

	for (size_t i = 0; i < page_cnt; i++) {
		uint64_t permissions = portal_translate_protections(req->prot);

		struct page *page = alloc(sizeof(struct page));
		if (page == NULL)
			RETURN_ERROR;

		portal->page_table->map_page(portal->page_table, vaddr, paddr,
									 permissions);

		vaddr += PAGE_SIZE;
		paddr += PAGE_SIZE;
	}

	portal->type |= PORTAL_REQ_DIRECT;

	return 0;
}

static int portal_handle_anon(struct portal *portal, struct portal_req *req,
							  struct portal_resp *resp)
{
	if (unlikely(portal == NULL || req == NULL))
		RETURN_ERROR;

	uint64_t permissions = portal_translate_protections(req->prot) &
						   ~(X86_FLAGS_P);
	uintptr_t vaddr = req->morphology.addr;
	uint64_t paddr = req->type & PORTAL_REQ_CONTINUOUS ?
						 pmm_alloc(req->morphology.pcnt, 1) :
						 0;

	if (paddr) {
		permissions |= X86_FLAGS_P;

		for (size_t i = 0; i < req->morphology.pcnt; i++) {
			portal->page_table->map_page(portal->page_table,
										 vaddr + i * PAGE_SIZE,
										 paddr + i * PAGE_SIZE, permissions);
		}

		portal->type |= PORTAL_REQ_CONTINUOUS;
		if (req->type & PORTAL_REQ_PEEK) {
			resp->morphology.paddr = paddr;
			resp->morphology.pcnt = req->morphology.pcnt;
			portal->type |= PORTAL_REQ_PEEK;
		}
	}

	for (size_t i = 0; i < req->morphology.pcnt; i++) {
		struct page *page = alloc(sizeof(struct page));
		if (page == NULL)
			RETURN_ERROR;

		page->vaddr = vaddr;
		if (paddr)
			page->paddr = paddr + i * PAGE_SIZE;
		page->flags = permissions;
		page->frame = NULL;
		page->pmle = portal->page_table->page_entry(portal->page_table,
													vaddr + i * PAGE_SIZE);
		page->refcnt = alloc(sizeof(page->refcnt));
		if (page->refcnt == NULL)
			RETURN_ERROR;

		int ret = dictionary_push(portal->page_table->pages, &page->vaddr, page,
								  sizeof(page->vaddr));
		if (ret == -1)
			RETURN_ERROR;
	}

	portal->type |= PORTAL_REQ_ANON;

	return 0;
}

static int portal_handle_share(struct portal *portal, struct portal_req *req,
							   struct portal_resp *resp)
{
	if (unlikely(portal == NULL || req == NULL))
		RETURN_ERROR;

	struct gateway_orb *orb = NULL;
	int ret = dictionary_search(&portal_gateway_map,
								(void *)req->share.identifier,
								strlen(req->share.identifier), (void **)&orb);

	if (orb == NULL && !req->share.create)
		RETURN_ERROR;
	if (orb == NULL && req->share.create) {
		orb = alloc(sizeof(struct gateway_orb));
		if (orb == NULL)
			RETURN_ERROR;

		orb->identifier = alloc(strlen(req->share.identifier) + 1);
		if (orb->identifier == NULL)
			RETURN_ERROR;
		orb->page_cnt = DIV_ROUNDUP(req->morphology.length, PAGE_SIZE);
		orb->pages = alloc(sizeof(struct page) * orb->page_cnt);
		if (orb->pages == NULL)
			RETURN_ERROR;

		strcpy((void *)orb->identifier, req->share.identifier);

		int ret = dictionary_push(&portal_gateway_map, (void *)orb->identifier,
								  orb, strlen(orb->identifier));
		if (ret == -1)
			RETURN_ERROR;

		if ((req->type & PORTAL_REQ_DIRECT) == PORTAL_REQ_DIRECT) {
			uint64_t paddr = req->morphology.paddr;

			for (int i = 0; i < orb->page_cnt; i++) {
				struct page *page = orb->pages + i;

				page->vaddr = req->morphology.addr + i * PAGE_SIZE;
				page->paddr = paddr;
				page->frame = NULL;

				paddr += PAGE_SIZE;
			}

			if (unlikely((portal->type & PORTAL_REQ_DIRECT) !=
						 PORTAL_REQ_DIRECT))
				if (portal_handle_direct(portal, req, resp) == -1)
					RETURN_ERROR;
		}

		if ((req->share.type & LINK_CIRCULAR) == LINK_CIRCULAR) {
			struct portal_link *link = (void *)req->morphology.addr;
			struct circular_queue *queue =
				(void *)link + sizeof(struct portal_link);

			*link = (struct portal_link){
				.lock = 0,
				.length = req->morphology.length,
				.header_offset = 0,
				.header_limit = sizeof(struct portal_link),
				.data_offset = sizeof(struct portal_link),
				.data_limit = req->morphology.length -
							  sizeof(struct portal_link) -
							  sizeof(struct circular_queue),
				.magic = LINK_CIRCULAR_MAGIC
			};

			int queue_length =
				(req->morphology.length - sizeof(struct portal_link) -
				 sizeof(struct circular_queue)) /
				req->share.length;

			circular_queue_init(queue,
								sizeof(struct portal_link) +
									sizeof(struct circular_queue),
								queue_length, req->share.length);
		} else if ((req->share.type & LINK_RAW) == LINK_RAW) {
			struct portal_link *link = (void *)req->morphology.addr;

			*link =
				(struct portal_link){ .lock = 0,
									  .length = req->morphology.length,
									  .header_offset = 0,
									  .header_limit = 0,
									  .data_offset = sizeof(struct portal_link),
									  .data_limit = req->morphology.length -
													sizeof(struct portal_link),
									  .magic = LINK_RAW_MAGIC };
		}
	}

	uintptr_t vaddr = req->morphology.addr;

	for (int i = 0; i < orb->page_cnt; i++) {
		struct page *page = orb->pages + i;

		uintptr_t paddr = page->paddr;
		if (page->frame == NULL)
			goto map;
		paddr = page->frame->paddr;
map:
		uint64_t permissions = portal_translate_protections(req->prot);

		portal->page_table->map_page(portal->page_table, vaddr, paddr,
									 permissions);
		vaddr += PAGE_SIZE;
	}
	struct portal_link *link = (void *)req->morphology.addr;

	portal->orb = orb;
	portal->type |= PORTAL_REQ_SHARE;

	return 0;
}

static struct portal *portal_copy_tree(struct page_table *source_table,
									   struct portal *root, uintptr_t base,
									   size_t limit)
{
	if (source_table == NULL || root == NULL)
		return NULL;

	if (base == -1 && limit == -1) {
		struct portal *region = alloc(sizeof(struct portal));
		if (unlikely(region == NULL))
			return NULL;

		*region = *root;

		region->left = portal_copy_tree(source_table, root->left, base, limit);
		region->right =
			portal_copy_tree(source_table, root->right, base, limit);

		return region;
	} else if ((base == -1 && limit != -1) || (base != -1 && limit == -1))
		return NULL;

	struct portal *src_portal = ({
		struct portal *root = source_table->portal_root;
		for (; root;) {
			if (root->base <= base && (root->base + root->limit) >= base)
				break;
		}
		root;
	});

	struct portal *region = alloc(sizeof(struct portal));
	if (unlikely(region == NULL))
		return NULL;
	*region = *src_portal;

	region->base = base;
	region->limit = src_portal->limit - abs(src_portal->limit - limit);
	if (region->limit == src_portal->limit || region->limit == limit)
		return region;

	base += abs(src_portal->limit - limit);
	limit -= abs(src_portal->limit - limit);
	region->left = portal_copy_tree(source_table, root->left, base, limit);
	region->right = portal_copy_tree(source_table, root->right, base, limit);

	return region;
}

static int portal_handle_cow(struct portal *portal, struct portal_req *req,
							 struct portal_resp *)
{
	if (unlikely(portal == NULL || req == NULL))
		RETURN_ERROR;

	struct thread *current_thread = CORE_LOCAL->current_thread;
	if (unlikely(current_thread == NULL))
		panic("DUFAY: core local corrupt");

	struct address_space *source_address_space = ({
		struct capability_binding *capability_binding = capability_lookup(
			current_thread->capability_table, req->cow.source.capability);
		capability_binding->obj;
	});

	struct address_space *destination_address_space = ({
		struct capability_binding *capability_binding = capability_lookup(
			current_thread->capability_table, req->cow.source.capability);
		capability_binding->obj;
	});

	struct page_table *source_table = source_address_space->page_table;
	struct page_table *destination_table =
		destination_address_space->page_table;

	if ((req->cow.source.base == -1 && req->cow.destination.base != -1) ||
		(req->cow.source.base != -1 && req->cow.destination.base == -1))
		return -1;

	if (req->cow.source.base == -1 && req->cow.destination.base == -1) {
		for (int i = 0; i < source_table->pages->capacity; i++) {
			__label__ skip;
			struct page *src_page = source_table->pages->data[i];
			if (src_page == NULL)
				continue;
			if (src_page->frame == NULL)
				goto skip;
			if ((*src_page->pmle & PORTAL_MAP_SHARE) != PORTAL_MAP_SHARE) {
				*src_page->pmle &= ~(X86_FLAGS_RW);
				*src_page->pmle |= PORTAL_MAP_COW;
				src_page->flags = (src_page->flags & ~(X86_FLAGS_RW)) |
								  PORTAL_MAP_COW;
			}
skip:
			src_page->refcnt++;

			struct page *dest_page = alloc(sizeof(struct page));
			if (dest_page == NULL)
				RETURN_ERROR;
			*dest_page = *src_page;

			dest_page->pmle = destination_table->map_page(
				destination_table, dest_page->vaddr, dest_page->frame->paddr,
				dest_page->flags);

			int ret = dictionary_push(destination_table->pages,
									  &dest_page->vaddr, dest_page,
									  sizeof(dest_page->vaddr));
			if (ret == -1)
				RETURN_ERROR;
		}

		destination_table->portal_root =
			portal_copy_tree(source_table, source_table->portal_root, -1, -1);
		if (destination_table->portal_root == NULL)
			RETURN_ERROR;
	} else {
		uintptr_t dest_vaddr = req->cow.destination.base;
		uintptr_t src_vaddr = req->cow.source.base;

		for (int i = 0; i < DIV_ROUNDUP(req->cow.limit, PAGE_SIZE); i++) {
			__label__ skip;
			struct page *src_page;
			int ret = dictionary_search(source_table->pages, &src_vaddr,
										sizeof(src_vaddr), (void **)&src_page);
			if (src_page == NULL)
				continue;
			if (src_page->frame == NULL)
				goto skip;
			if ((*src_page->pmle & PORTAL_MAP_SHARE) != PORTAL_MAP_SHARE) {
				*src_page->pmle &= ~(X86_FLAGS_RW);
				*src_page->pmle |= PORTAL_MAP_COW;
				src_page->flags = (src_page->flags & ~(X86_FLAGS_RW)) |
								  PORTAL_MAP_COW;
			}

			src_page->refcnt++;
skip:
			struct page *dest_page = alloc(sizeof(struct page));
			if (dest_page == NULL)
				RETURN_ERROR;
			*dest_page = *src_page;

			dest_page->vaddr = dest_vaddr;
			dest_page->pmle = destination_table->map_page(
				destination_table, dest_page->vaddr, dest_page->frame->paddr,
				dest_page->flags);

			ret = dictionary_push(destination_table->pages, &dest_page->vaddr,
								  dest_page, sizeof(dest_page->vaddr));
			if (ret == -1)
				RETURN_ERROR;

			dest_vaddr += PAGE_SIZE;
			src_vaddr += PAGE_SIZE;
		}

		destination_table->portal_root =
			portal_copy_tree(source_table, source_table->portal_root,
							 req->cow.source.base, req->cow.limit);
		if (destination_table->portal_root == NULL)
			RETURN_ERROR;
	}

	return 0;
}

static int portal_handle_sp(struct portal *portal, struct portal_req *req,
							struct portal_resp *)
{
	if (unlikely(portal == NULL || req == NULL))
		RETURN_ERROR;
	portal->type |= PORTAL_REQ_SP;
	return 0;
}

int portal(struct portal_req *req, struct portal_resp *resp)
{
	if (req == NULL || resp == NULL)
		RETURN_ERROR;

	struct portal *portal = alloc(sizeof(struct portal));
	if (portal == NULL)
		RETURN_ERROR;

	portal->base = req->morphology.addr;
	portal->limit = req->morphology.length;

	struct thread *thread = CORE_LOCAL->current_thread;
	struct page_table *page_table = NULL;

	if (likely(thread))
		page_table = thread->address_space->page_table;
	if (unlikely(thread == NULL))
		page_table = kernel_mappings.page_table;
	if (unlikely(page_table == NULL))
		goto failure;

	portal->prot = req->prot;
	portal->thread = thread;
	portal->page_table = page_table;

	if (unlikely(req == NULL))
		goto failure;

	BST_GENERIC_INSERT(page_table->portal_root, base, portal);

	if (req->type & PORTAL_REQ_DIRECT)
		if (portal_handle_direct(portal, req, resp) == -1)
			goto failure;
	if (req->type & PORTAL_REQ_ANON)
		if (portal_handle_anon(portal, req, resp) == -1)
			goto failure;
	if (req->type & PORTAL_REQ_SHARE)
		if (portal_handle_share(portal, req, resp) == -1)
			goto failure;
	if (req->type & PORTAL_REQ_COW)
		if (portal_handle_cow(portal, req, resp) == -1)
			goto failure;
	if (req->type & PORTAL_REQ_SP)
		if (portal_handle_sp(portal, req, resp) == -1)
			goto failure;

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

SYSCALL_DEFINE2(portal, struct portal_req *, req, struct portal_resp *, resp, {
	int ret = portal(req, resp);
	if (ret != 0)
		return ret;
})

void portal_destroy(struct portal *portal)
{
	if (portal == NULL)
		return;

	portal_destroy(portal->left);
	portal_destroy(portal->right);

	for (int i = 0; i < portal->pages->capacity; i++) {
		struct page *page = portal->pages->data[i];

		if (page) {
			if (--page->frame->refcnt == 0)
				pmm_free(page->frame->paddr, 1);
			if (--(*page->refcnt) == 0)
				free(page);
		}
	}

	dictionary_destroy(portal->pages);

	free(portal);
}
