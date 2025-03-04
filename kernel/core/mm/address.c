#include <arch/x86/smp.h>

#include <core/mm/address.h>
#include <core/syscall.h>
#include <core/handle.h>

#include <fayt/compiler.h>
#include <fayt/debug.h>
#include <fayt/address.h>

static struct hash_table as_table;
static int asid_bump;

int address_find_as(int asid, struct address_space **as)
{
	if (as == NULL)
		RETURN_ERROR;

	int ret = hash_table_search(&as_table, &asid, sizeof(asid), (void **)as);
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

int address_push_as(struct address_space *as)
{
	if (as == NULL)
		RETURN_ERROR;

	as->asid = asid_bump++;

	int ret = hash_table_push(&as_table, &as->asid, as, sizeof(as->asid));
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

int address_space_construct(int *asid)
{
	struct address_space *as = alloc(sizeof(struct address_space));
	if (unlikely(as == NULL))
		RETURN_ERROR;

	as->page_table = alloc(sizeof(struct page_table));
	if (unlikely(as->page_table == NULL))
		RETURN_ERROR;

	int ret = vmm_default_table(as->page_table);
	if (ret == -1)
		RETURN_ERROR;

	ret = address_push_as(as);
	if (ret == -1)
		RETURN_ERROR;

	as->current = 0xa0000000;
	as->base = 0xa0000000;
	as->limit = 0x0000fffffffff0ff;
	as->aslr = (struct aslr){ .layout = NULL,
							  .minimum_vaddr = 0x100000000000,
							  .maximum_vaddr = 0x7fffffffffff };

	*asid = as->asid;

	return 0;
}

int as_address(struct address_space *as, uintptr_t *ret, size_t size)
{
	if (as == NULL || ret == NULL || size == 0 ||
		(as->current + size) > (as->base + as->limit))
		RETURN_ERROR;

	uintptr_t address = as->current;

	for (struct address_hole *hole = as->hole_root; hole; hole = hole->next) {
		if ((address < hole->base + hole->limit) &&
			(hole->base < address + size))
			address += hole->limit;
		hole = hole->next;
	}

	as->current = address + size;
	*ret = address;

	return 0;
}

int as_insert_hole(struct address_space *as, struct address_hole *hole)
{
	if (as == NULL || hole == NULL)
		RETURN_ERROR;

	if (as->hole_root == NULL) {
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

int address_space_allocate(struct address_space *as, uintptr_t *address,
						   size_t length)
{
	if (unlikely(as == NULL || address == NULL))
		RETURN_ERROR;
	if (!length)
		return -1;

	int ret = as_address(as, address, length);
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

int address_space_free(struct address_space *as, uintptr_t address,
					   size_t length)
{
	if (unlikely(as == NULL))
		RETURN_ERROR;
	if (!length)
		return 0;

	return 0;
}

SYSCALL_DEFINE4(as_action, handle_t, handle, int, ops, uintptr_t *, address,
				size_t, length, ({
					struct context *current_context =
						CORE_LOCAL->current_context;
					if (unlikely(current_context == NULL))
						RETURN_ERROR;

					struct address_space_handle *as_handle = NULL;
					if (ops != AS_ACTION_CONSTRUCT) {
						as_handle = ({
							struct handle_binding *binding =
								handle_lookup(current_context->handles, handle);
							if (binding == NULL)
								RETURN_ERROR;
							binding->obj;
						});
						if (as_handle == NULL)
							RETURN_ERROR;
					}

					switch (ops) {
					case AS_ACTION_CONSTRUCT: {
						int asid;
						int ret = address_space_construct(&asid);
						if (ret == -1)
							RETURN_ERROR;

						as_handle = alloc(sizeof(struct address_space_handle));
						if (unlikely(as_handle == NULL))
							RETURN_ERROR;

						as_handle->asid = asid;

						handle_t handle;
						ret = handle_create(
							current_context->handles, as_handle,
							HANDLE_ACCESS_READ | HANDLE_ACCESS_WRITE, &handle);
						if (ret == -1)
							RETURN_ERROR;

						return handle;
					}
					case AS_ACTION_ALLOCATE: {
						struct address_space *as = NULL;
						int ret = address_find_as(as_handle->asid,
												  (struct address_space **)&as);
						if (ret == -1 || as == NULL)
							RETURN_ERROR;

						return address_space_allocate(as, address, length);
					}
					case AS_ACTION_FREE: {
						struct address_space *as = NULL;
						int ret = address_find_as(as_handle->asid, &as);
						if (ret == -1 || as == NULL)
							RETURN_ERROR;

						return address_space_free(as, *address, length);
					}
					default:
						RETURN_ERROR;
					}
				}));

struct address_space kernel_mappings;
