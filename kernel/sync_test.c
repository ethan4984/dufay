#include <core/mutex.h>
#include <core/semaphore.h>
#include <core/debug.h>
#include <core/scheduler/thread.h>
#include <aria/debug.h>

/*
 * ------------ Mutex Test ------------
 */

static struct mutex mtx;
static int val = 0;

#define MUTEX_TEST(N)                                        \
	static void mtx##N()                                     \
	{                                                        \
		for (;;) {                                           \
			mutex_lock(&mtx);                                \
			val = N;                                         \
			print_unlocked("thread" #N ": val = %d\n", val); \
			mutex_unlock(&mtx);                              \
		}                                                    \
	}

MUTEX_TEST(1)
MUTEX_TEST(2)
MUTEX_TEST(3)
MUTEX_TEST(4)
MUTEX_TEST(5)
MUTEX_TEST(6)
MUTEX_TEST(7)
MUTEX_TEST(8)

#define ENQUEUE_FUNCTION(fn)                                       \
	({                                                             \
		struct thread *nthread = new_kernel_thread((uintptr_t)fn); \
		if (nthread == NULL) {                                     \
			REPORT_ERROR;                                          \
			panic("Failed to create thread for " #fn);             \
		}                                                          \
		enqueue_thread(nthread);                                   \
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
	static void sem##N()                                          \
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

void do_sync_test()
{
#if 0
  	mutex_test();
#else
	sem_test();
#endif
}
