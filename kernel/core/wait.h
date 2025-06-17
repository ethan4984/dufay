#ifndef CORE_WAIT_H_
#define CORE_WAIT_H_
#include <sys/queue.h>
#include <fayt/lock.h>
#include <stdatomic.h>

#define INTERNAL_WAITBLOCKS_N 4

/*
 * This is loosely based on Arun Kishan's work on Windows 7:
 * https://www.youtube.com/watch?v=OAAiOEQhsK0
*/

enum waitblock_status {
	WAITBLOCK_ACTIVE, /* Waitblock is linked to an object as part of a thread that is waiting */
	WAITBLOCK_INACTIVE, /* The wait associated with this waitblock has been satisfied (or timed out) */
	WAITBLOCK_SIGNALED, /* A signal was delivered to the wait (this could be before it was committed) */
};

enum wait_status {
	WAIT_IN_PROGRESS, /* Wait is currently being processed */
	WAIT_COMMITTED, /* Wait has been committed and is waiting for the object to be signaled */
	WAIT_SATISFIED, /* Wait has been satisfied (could have been satisfied by a timeout) */
};

struct waitblock {
	/* Linkage into dispatch_header::waitblocks */
	TAILQ_ENTRY(waitblock) queue_hook;

	/* Object being waited on */
	struct dispatch_header *object;

	/* Thread that the waitblock is part of */
	struct thread *thread;

	/* Status of the waitblock */
	enum waitblock_status status;
};

enum dispatch_object_type {
	DISPATCH_NOTIFICATION, /* When signaled, signaled is kept high and all waiters are woken up */
	DISPATCH_SYNCHRONIZATION, /* When signaled, signal count is decreased until 0 and a single waiter is woken up */
};

struct dispatch_header {
	/* Waitblocks waiting on this object */
	TAILQ_HEAD(, waitblock) waitblocks;

	enum dispatch_object_type type;

	struct spinlock lock;

	/* A count of >= 1 means the object is currently signaled */
	int signaled_count;

	/* Name of this event for debugging purposes */
	const char *name;
};

void dispatch_object_init(struct dispatch_header *hdr,
						  enum dispatch_object_type type, const char *name);

/* Tries to satisfy a wait on an object
 * Returns the thread that was satisfied, or NULL if no thread was satisfied.
 * If the object is a notification object, it will satisfy all waiting threads.
 * If the object is a synchronization object, it will satisfy only one thread.
 */
struct thread *try_satisfy_dispatch_object(struct dispatch_header *hdr);

/* Waits for any of the provided objects to be signaled.
 * Returns the index of the object that was signaled, or -1 on error.
 * If timeout is -1, it will wait indefinitely.
 * If timeout is 0, it will return immediately.
 */
int wait_any(int count, void *objects[], long timeout);

/* * Waits for a single object to be signaled.
 * Returns 0 on success, or -1 on error.
 * If timeout is -1, it will wait indefinitely.
 * If timeout is 0, it will return immediately.
 */
int wait_one(struct dispatch_header *hdr, long timeout);

#endif
