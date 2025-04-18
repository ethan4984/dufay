#include <arch/x86/smp.h>

#include <core/syscall.h>
#include <core/debug.h>
#include <core/events.h>
#include <core/capability.h>
#include <core/message.h>
#include <core/object.h>

#include <fayt/debug.h>
#include <fayt/slab.h>
#include <fayt/string.h>

#include <sys/queue.h>

static struct port *lookup_port(capability_t port, struct context *ctx,
								uint8_t *rights)
{
	struct capability_binding *binding = capability_lookup(ctx->handles, port);

	if (!binding)
		return NULL;

	*rights = binding->access;

	return binding->obj;
}

static void port_constructor(void *obj)
{
	struct port *port = (struct port *)obj;

	TAILQ_INIT(&port->queue);
}

static void port_destructor(void *obj)
{
	struct port *port = (struct port *)obj;
	struct kernel_message *msg;

	TAILQ_FOREACH(msg, &port->queue, queue_hook)
	{
		free(msg->header);
		free(msg);
	}
}

int message_init()
{
	struct object_class class = { .constructor = port_constructor,
								  .destructor = port_destructor,
								  .size = sizeof(struct port) };

	return object_register_class(OBJ_CLASS_PORT, class);
}

int message_send(struct message_header *message, struct context *context)
{
	struct kernel_message *msg = alloc(sizeof(struct kernel_message));
	if (msg == NULL)
		RETURN_ERROR;
	uint8_t dest_rights = 0, reply_rights = 0;

	struct port *destination =
		lookup_port(message->destination, context, &dest_rights);

	struct port *reply = lookup_port(message->reply, context, &reply_rights);

	if (!destination || !reply)
		RETURN_ERROR;

	// Doesn't hold send rights for this port, error out
	if (!(dest_rights & PORT_RIGHT_SEND) &&
		!(dest_rights & PORT_RIGHT_SEND_ONCE))
		RETURN_ERROR;

	msg->header = alloc(message->size);
	if (msg->header == NULL)
		RETURN_ERROR;

	memcpy(msg->header, message, message->size);

	msg->reply_port = reply;

	// Add message to destination port's message queue
	TAILQ_INSERT_TAIL(&destination->queue, msg, queue_hook);

	VECTOR_PUSH(destination->trigger.equeue, &destination->equeue);

	equeue_wake(&destination->trigger,
				CORE_LOCAL->current_context->ucontext_active);

	return 0;
}

int message_receive(capability_t port, struct message_header *out,
					struct context *context)
{
	uint8_t rights;
	struct port *actual_port = lookup_port(port, context, &rights);

	// Can't receive if we don't have the right to do so
	if (!(rights & PORT_RIGHT_RECV)) {
		RETURN_ERROR;
	}

	for (;;) {
		if (!TAILQ_EMPTY(&actual_port->queue))
			break;

		int ret = equeue_block(&actual_port->equeue, NULL);

		if (ret == -1)
			RETURN_ERROR;
	}

	// Pop first element from queue and copy to buffer
	struct kernel_message *first = TAILQ_FIRST(&actual_port->queue);

	memcpy(out, first->header, first->header->size);

	TAILQ_REMOVE(&actual_port->queue, first, queue_hook);

	free(first->header);
	free(first);

	return 0;
}

SYSCALL_DEFINE2(msg_recv, capability_t, port, struct message_header *, out, {
	return message_receive(port, out, CORE_LOCAL->current_context);
});

SYSCALL_DEFINE1(msg_send, struct message_header *, msg,
				{ return message_send(msg, CORE_LOCAL->current_context); });
