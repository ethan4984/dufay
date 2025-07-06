#include "core/clock.h"
#include "core/rcu.h"
#include <core/ipl.h>
#include <core/mutex.h>
#include <core/semaphore.h>
#include <core/debug.h>
#include <core/sched.h>
#include <aria/base.h>
#include <aria/debug.h>
#include <core/timer.h>
#include <mm/slab.h>
#include <core/cpu.h>

/*
 * ------------ Mutex Test ------------
 */

struct thread *make_kernel_thread(void (*fn)(void *));
struct thread *make_kernel_thread_arg(void (*fn)(void *), void *);

static struct mutex mtx;
static int val = 0;

#define MUTEX_TEST(N)                                         \
	static void mtx##N(void *)                                \
	{                                                         \
		for (;;) {                                            \
			mutex_lock(&mtx, -1);                             \
			val = N;                                          \
			print_unlocked("cpu%d: thread" #N ": val = %d\n", \
						   CORE_LOCAL->core_id, val);         \
			mutex_unlock(&mtx);                               \
		}                                                     \
	}

MUTEX_TEST(1)
MUTEX_TEST(2)
MUTEX_TEST(3)
MUTEX_TEST(4)
MUTEX_TEST(5)
MUTEX_TEST(6)
MUTEX_TEST(7)
MUTEX_TEST(8)

#define ENQUEUE_FUNCTION(fn)                             \
	({                                                   \
		struct thread *nthread = make_kernel_thread(fn); \
		if (nthread == NULL) {                           \
			REPORT_ERROR;                                \
			panic("Failed to create thread for " #fn);   \
		}                                                \
		memcpy(nthread->name, #fn, sizeof(#fn));         \
		sched_ready(nthread);                            \
	})

#define ENQUEUE_FUNCTION_PINNED(fn, cpu)                 \
	({                                                   \
		struct thread *nthread = make_kernel_thread(fn); \
		if (nthread == NULL) {                           \
			REPORT_ERROR;                                \
			panic("Failed to create thread for " #fn);   \
		}                                                \
		memcpy(nthread->name, #fn, sizeof(#fn));         \
		sched_pin(nthread, cpu);                         \
		sched_ready(nthread);                            \
	})

#define ENQUEUE_FUNCTION_ARG(fn, arg)                             \
	({                                                            \
		struct thread *nthread = make_kernel_thread_arg(fn, arg); \
		if (nthread == NULL) {                                    \
			REPORT_ERROR;                                         \
			panic("Failed to create thread for " #fn);            \
		}                                                         \
		memcpy(nthread->name, #fn, sizeof(#fn));                  \
		sched_ready(nthread);                                     \
	})

struct thread *new_kernel_thread(uintptr_t entry);

static void mutex_test()
{
	mutex_init(&mtx, "sync_test_mutex");
	ENQUEUE_FUNCTION(mtx1);
	ENQUEUE_FUNCTION(mtx2);
	ENQUEUE_FUNCTION(mtx3);
	ENQUEUE_FUNCTION(mtx4);
	ENQUEUE_FUNCTION(mtx5);
	ENQUEUE_FUNCTION(mtx6);
	ENQUEUE_FUNCTION(mtx7);
	ENQUEUE_FUNCTION(mtx8);
}

/*
 * ------------ Semaphore Test ------------
 */
static struct semaphore sem;

#define SEM_TEST(N)                                               \
	static void sem##N(void *)                                    \
	{                                                             \
		for (;;) {                                                \
			semaphore_acquire(&sem);                              \
			print_unlocked("thread" #N ": semaphore acquired\n"); \
			semaphore_release(&sem);                              \
		}                                                         \
	}

SEM_TEST(1);
SEM_TEST(2);
SEM_TEST(3);
SEM_TEST(4);

static void sem_test()
{
	semaphore_init(&sem, "sync_test_sem", 1);
	ENQUEUE_FUNCTION(sem1);
	ENQUEUE_FUNCTION(sem2);
	ENQUEUE_FUNCTION(sem3);
	ENQUEUE_FUNCTION(sem4);
}

static struct ktimer timer;
static struct ktimer other_timer;

static void timer2(void *)
{
	print("2: Waiting 1 second...\n");

	timer_init(&other_timer, "l");
	timer_start(&other_timer, NANOSECONDS_PER_SECOND);
	wait_one(&other_timer.hdr, -1);

	print("2: I'm back! (cpu%d)\n", CORE_LOCAL->core_id);

	for (;;) {
	}
}

static void timer1(void *)
{
	print("%d: Waiting 4 seconds...\n", CORE_LOCAL->current_thread->id);

	timer_init(&timer, "");
	timer_start(&timer, NANOSECONDS_PER_SECOND * 4);
	wait_one(&timer.hdr, -1);

	print("1: I'm back (cpu%d)\n", CORE_LOCAL->core_id);

	for (;;) {
	}
}

static void perform_delay(int ms)
{
	struct ktimer timer;
	timer_init(&timer, "");
	timer_start(&timer, (NANOSECONDS_PER_SECOND / 1000) * ms);
	wait_one(&timer.hdr, -1);
}

static void timer3(void *)
{
	//print("td%d: I'm back\n", CORE_LOCAL->current_thread->id);

	for (;;) {
		//		print("td%d: running on cpu%d (ipl=%d)\n",
		//	  CORE_LOCAL->current_thread->id, CORE_LOCAL->core_id,
		// CORE_LOCAL->ipl);

		perform_delay(16);

		//print("td%d: im back\n", CORE_LOCAL->current_thread->id);
	}
}

static void timer4(void *)
{
	for (;;) {
		for (int i = 0; i < 5000; i++) {
			ENQUEUE_FUNCTION(timer3);
		}

		print("\033[1;31mMAIN THREAD!!\033[0m\n");
		perform_delay(2000);
	}
}

static void timer_test()
{
	timer_init(&timer, "lol");
	timer_init(&other_timer, "lol");

	//ENQUEUE_FUNCTION(timer1);
	//ENQUEUE_FUNCTION(timer2);

	ENQUEUE_FUNCTION(timer4);
}

#define THREAD_TEST(N)                                                \
	static void td##N(void *)                                         \
	{                                                                 \
		print("thread%d: on cpu%d\n", CORE_LOCAL->current_thread->id, \
			  CORE_LOCAL->core_id);                                   \
                                                                      \
		for (;;) {                                                    \
			perform_delay(1000);                                      \
		}                                                             \
	}

THREAD_TEST(1);
THREAD_TEST(2);
THREAD_TEST(3);
THREAD_TEST(4);
THREAD_TEST(5);
THREAD_TEST(6);
THREAD_TEST(7);
THREAD_TEST(8);

static void thread_starter(void *)
{
	ENQUEUE_FUNCTION(td1);
	ENQUEUE_FUNCTION(td2);
	ENQUEUE_FUNCTION(td3);
	ENQUEUE_FUNCTION(td4);
	ENQUEUE_FUNCTION(td5);
	ENQUEUE_FUNCTION(td6);
	ENQUEUE_FUNCTION(td7);
	ENQUEUE_FUNCTION(td8);

	ENQUEUE_FUNCTION(thread_starter);

	sched_wait();
}

static void thread_test()
{
	ENQUEUE_FUNCTION(thread_starter);
}

struct my_data {
	int value;
	struct rcu_head rcu;
};

static struct my_data *global_data;

static void rcu_reader(void *)
{
	struct ktimer timer;

	for (;;) {
		timer_init(&timer, "a");
		ipl_t ipl = rcu_read_lock();

		print("Reader: read %d\n", global_data->value);

		rcu_read_unlock(ipl);

		timer_start(&timer, 200000 * 1000);

		wait_one(&timer.hdr, -1);
	}
}

static int lol = 0;

static void rcu_writer(void *)
{
	struct ktimer timer;
	for (;;) {
		timer_init(&timer, "a");
		struct my_data *new_data = kmem_malloc(sizeof(struct my_data));

		new_data->value = ++lol;

		rcu_assign_pointer(global_data, new_data);

		print("Writer: updated to %d\n", new_data->value);

		synchronize_rcu();

		kmem_free(new_data);

		print("Writer: freed old data\n");

		timer_start(&timer, 200000 * 1000);

		wait_one(&timer.hdr, -1);
	}
}

static void rcu_test()
{
	global_data = kmem_malloc(sizeof(struct my_data));

	global_data->value = 69;

	for (int i = 0; i < 2; i++) {
		ENQUEUE_FUNCTION(rcu_writer);
	}

	for (int i = 0; i < 2; i++) {
		ENQUEUE_FUNCTION(rcu_reader);
	}
}

#define DO_TIMER_TEST 1

void do_sync_test()
{
	(void)mutex_test;
	(void)thread_test;
	(void)sem_test;
	(void)timer_test;
	(void)rcu_test;

	ipl_t ipl = ipldispatch();
#ifdef DO_MUTEX_TEST
	mutex_test();
#elif defined(DO_SEM_TEST)
	sem_test();
#elif defined(DO_TIMER_TEST)
	timer_test();
#elif defined(DO_THREAD_TEST)
	thread_test();
#elif defined(DO_RCU_TEST)
	rcu_test();
#elif defined(DO_FW_TEST)
	//ENQUEUE_FUNCTION_ARG(myfn, (void*)0x69);
	void PerformFireworksTest();
	PerformFireworksTest();
#endif
	ipl_lower(ipl);
}
