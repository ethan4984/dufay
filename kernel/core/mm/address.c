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

	*asid = as->asid;

	return 0;
}

int address_space_allocate(struct address_space *as, uintptr_t *address,
						   size_t length)
{
	if (unlikely(as == NULL || address == NULL))
		RETURN_ERROR;
	if (!length)
		return -1;

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
						struct address_space_handle *address_space_handle = ({
							struct handle_binding *binding =
								handle_lookup(current_context->handles, handle);
							if (binding == NULL)
								RETURN_ERROR;
							binding->obj;
						});
						if (address_space_handle == NULL)
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

						int ret = address_find_as(as->asid, &as);
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
