#include <arch/port.h>
#include <core/thread.h>
#include <mm/address.h>

void thread_switch(struct thread *cur, struct thread *next)
{
	if (cur->process->as->page_table->pmap !=
		next->process->as->page_table->pmap)
		pmap_activate(cur->process->as->page_table->pmap);

	arch_context_switch(cur, next);
}

void thread_load(struct thread *td)
{
	if (td->process && td->process->as)
		pmap_activate(td->process->as->page_table->pmap);

	arch_load_context(td);
}
