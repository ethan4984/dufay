#include <arch/x86/smp.h>

#include <core/futex.h>
#include <core/virtual.h>
#include <core/debug.h>
#include <core/scheduler.h>
#include <core/syscall.h>

#include <fayt/debug.h>
#include <fayt/compiler.h>
#include <fayt/sched.h>

static struct hash_table futex_table;

int futex(uintptr_t uaddr, int ops, int expected) {
	struct context *context = CORE_LOCAL->current_context;
	if(unlikely(context == NULL)) RETURN_ERROR;

	struct page_table *page_table = context->page_table;
	if(unlikely(page_table == NULL)) RETURN_ERROR;

	uint64_t uaddr_vaddr = uaddr & ~(0xfff);
	struct page *page = NULL;
	int ret = hash_table_search(page_table->pages, &uaddr_vaddr,
		sizeof(uaddr_vaddr), (void**)&page);
	if(ret == -1 || page == NULL) RETURN_ERROR;

	uint64_t futex_paddr =
		(page->frame ? page->frame->paddr : page->paddr) + (uaddr & (0xfff));
	struct futex *futex = NULL;
	ret = hash_table_search(&futex_table, &futex_paddr,	
		sizeof(futex_paddr), (void**)&futex);
	if(futex == NULL && ops == FUTEX_WAKE) return 0;
	if(futex == NULL && ops == FUTEX_WAIT) {
		futex = alloc(sizeof(struct futex));
		if(unlikely(futex == NULL)) RETURN_ERROR;

		futex->paddr = futex_paddr;

		ret = hash_table_push(&futex_table, &futex->paddr, futex, sizeof(futex->paddr));
		if(ret == -1) RETURN_ERROR; // TODO keep track of active locks per page
	}

	switch(ops) {
		FUTEX_WAIT: {
			futex->expected = expected;
			futex->operation = ops;
			futex->locked = 1;

			for(;;) {
				if(!futex->locked) break;

				int ret = equeue_block(&futex->equeue, NULL);
				if(ret == -1) RETURN_ERROR;
			}
			break;
		}
		FUTEX_WAKE: {
			if(!futex->locked) break;

			int ret = hash_table_delete(&futex_table, &futex_paddr, sizeof(futex_paddr));
			if(ret == -1) RETURN_ERROR;

			struct ucontext *ucontext = context->ucontext_active;
			if(unlikely(ucontext == NULL)) RETURN_ERROR;

			ret = equeue_wake(&futex->etrigger, ucontext);
			if(ret == -1) RETURN_ERROR;

			break;
		}
		default: RETURN_ERROR;
	}

	return 0;
}

SYSCALL_DEFINE3(futex, uintptr_t, uaddr, int, ops, uint32_t, expected, {
	return futex(uaddr, ops, expected);
})
