#define _GNU_SOURCE
#include <threads.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdio.h>
#include <unistd.h>
#include "ule.h"
#include <stdlib.h>
#include <string.h>
#include "cpu.h"

void sched_setup();

void sched_add_cpu(struct cpu_local *cpu);

struct thread *new_thread(const char *name, int prio_class, int nice)
{
	struct thread *td = calloc(1, sizeof(struct thread));

	strcpy(td->name, name);
	td->priority = (PRIO_DEFAULT - nice);
	td->priority_class = prio_class;
	td->nice = nice;

	sched_enqueue(td);

	return td;
}

thread_local struct cpu_local *CORE_LOCAL;

void *cpu1_entry(void *_)
{
	sched_setup();
	sched_add_cpu(CORE_LOCAL);

	CORE_LOCAL->core_id = 1;

	while (true) {
		sched_clock();
	}

	return NULL;
}

void *cpu2_entry(void *_)
{
	sched_setup();
	sched_add_cpu(CORE_LOCAL);

	CORE_LOCAL->core_id = 2;

	while (true) {
		sched_clock();
	}

	return NULL;
}

/* void *cpu3_entry(void *_) */
/* { */
/* 	sched_setup(); */
/* 	sched_add_cpu(CORE_LOCAL); */

/* 	CORE_LOCAL->core_id = 3; */

/* 	new_thread("david", PRIO_LOW_BATCH, 0); */
/* 	new_thread("roger", PRIO_LOW_BATCH, 0); */
/* 	new_thread("nick", PRIO_LOW_BATCH, 0); */
/* 	new_thread("richard", PRIO_LOW_BATCH, 0); */

/* 	while (true) { */
/* 		sched_clock(); */
/* 	} */

/* 	return NULL; */
/* } */

void set_thread_affinity(pthread_t thread, int cpu_id)
{
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(cpu_id, &cpuset);

	int s = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
	if (s != 0)
		perror("pthread_setaffinity_np");
}

int main(int argc, char **argv)
{
	pthread_t cpu1, cpu2;

	sched_setup();

	CORE_LOCAL->core_id = 0;

	sched_add_cpu(CORE_LOCAL);

	int r = pthread_create(&cpu1, NULL, cpu1_entry, NULL);

	if (r < 0)
		return r;

	set_thread_affinity(cpu1, 1);

	r = pthread_create(&cpu2, NULL, cpu2_entry, NULL);

	if (r < 0)
		return r;

	set_thread_affinity(cpu2, 2);

	/* r = pthread_create(&cpu3, NULL, cpu3_entry, NULL); */

	/* if (r < 0) */
	/* 	return r; */

	/* new_thread("john", PRIO_REALTIME, PRIO_REALTIME); */
	new_thread("paul", PRIO_LOW_BATCH, 0);
	new_thread("john", PRIO_LOW_BATCH, 0);
	new_thread("george", PRIO_LOW_BATCH, 0);
	new_thread("ringo", PRIO_LOW_BATCH, 0);
	new_thread("bob", PRIO_LOW_BATCH, 0);
	new_thread("alice", PRIO_LOW_BATCH, 0);

	while (true) {
		sched_clock();
	}

	pthread_join(cpu1, NULL);
	pthread_join(cpu2, NULL);

	return 0;
}
