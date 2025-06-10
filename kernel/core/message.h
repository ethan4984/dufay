#ifndef CORE_MESSAGE_H_
#define CORE_MESSAGE_H_

#include <core/capability.h>
#include <core/events.h>

#include <stddef.h>

#include <sys/queue.h>

#define OBJ_CLASS_PORT 0

#define PORT_RIGHT_RECV (1 << 0)
#define PORT_RIGHT_SEND (1 << 1)
#define PORT_RIGHT_SEND_ONCE (1 << 2)

// Messages are used for small in-line data, or simply sending rights.
// They are a more lightweight alternatives to notifications intended for
// simpler message-passing
struct message_header {
	capability_t destination; // Whom to send the message to
	capability_t reply; // A SEND or SEND-ONCE right that is sent along with the
	// message, to allow for reply

	size_t size;

	// `size` bytes of data...
};

struct kernel_message {
	struct message_header *header;

	void *reply_port;

	TAILQ_ENTRY(kernel_message) queue_hook;
};

struct port {
	TAILQ_HEAD(, kernel_message) queue;

	struct etrigger trigger;
	struct equeue equeue;
};

int message_init();

int message_send(struct message_header *message, struct thread *thread);
int message_receive(capability_t port, struct message_header *out,
					struct thread *thread);

#endif
