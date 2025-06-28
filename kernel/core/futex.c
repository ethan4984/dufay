#include "mm/address.h"
#include <arch/amd64/smp.h>

#include <core/sched.h>
#include <core/futex.h>
#include <mm/virtual.h>
#include <core/debug.h>
#include <core/syscall.h>
#include <core/lock.h>

#include <aria/debug.h>
#include <aria/compiler.h>
#include <aria/sched.h>

static struct dictionary futex_table;

int futex(uintptr_t uaddr, int ops, int expected, int virtual)
{
	struct thread *thread = CORE_LOCAL->current_thread;
	if (unlikely(thread == NULL))
		RETURN_ERROR;

	uint64_t futex_paddr = !virtual ? uaddr : ({
		struct page_table *page_table = thread->process->as->page_table;
		if (unlikely(page_table == NULL))
			RETURN_ERROR;

		uint64_t uaddr_vaddr = uaddr & ~(0xfff);
		struct page *page = NULL;
		int ret = dictionary_search(page_table->pages, &uaddr_vaddr,
									sizeof(uaddr_vaddr), (void **)&page);
		if (ret == -1 || page == NULL)
			RETURN_ERROR;
		(page->frame ? page->frame->paddr : page->paddr) + (uaddr & (0xfff));
	});

	struct futex *futex = NULL;
	volatile uint32_t *vuaddr;
	int ret = dictionary_search(&futex_table, &futex_paddr, sizeof(futex_paddr),
								(void **)&futex);
	if (futex == NULL && (ops == FUTEX_WAIT || ops == FUTEX_WAKE)) {
		futex = alloc(sizeof(struct futex));
		if (unlikely(futex == NULL))
			RETURN_ERROR;

		futex->paddr = futex_paddr;
		futex->expected = expected;
		futex->refcnt = 0;

		ret = equeue_add(&futex->equeue, &futex->etrigger);
		if (ret == -1)
			RETURN_ERROR;

		ret = dictionary_push(&futex_table, &futex->paddr, futex,
							  sizeof(futex->paddr));
		if (ret == -1)
			RETURN_ERROR; // TODO keep track of active locks per page
	} else if (futex == NULL)
		RETURN_ERROR;
	vuaddr = (void *)futex->paddr + HIGH_VMA;

	spinlock_irqsave(&futex->lock);

	switch (ops) {
	case FUTEX_WAIT: {
		futex->refcnt++;

		for (;;) {
			if (*vuaddr == (uint32_t)expected)
				break;

			raw_spinrelease(&futex->lock);
			int ret = equeue_block(&futex->equeue, NULL);
			if (ret == -1)
				RETURN_ERROR;
		}

		if (--futex->refcnt <= 0) {
			int ret = dictionary_delete(&futex_table, &futex_paddr,
										sizeof(futex_paddr));
			if (ret == -1)
				RETURN_ERROR;
		}

		break;
	}
	case FUTEX_WAKE: {
		*vuaddr = futex->expected;
#if 0
		struct context *context = thread->context_active;
		if (unlikely(context == NULL))
			RETURN_ERROR;

		ret = equeue_wake(&futex->etrigger, context);
		if (ret == -1)
			RETURN_ERROR;
#endif
		break;
	}
	default:
		RETURN_ERROR;
	}

	spinrelease_irqsave(&futex->lock);

	return 0;
}

SYSCALL_DEFINE4(futex, uintptr_t, uaddr, int, ops, uint32_t, expected, int,
				virtual, { return futex(uaddr, ops, expected, virtual); })
