#include <fayt/circular_queue.h>
#include <fayt/string.h>
#include <fayt/slab.h>
#include <fayt/debug.h>

void circular_queue_init(struct circular_queue *queue, int data_offset, size_t size, size_t obj_size) {
	queue->data_offset = data_offset;
	queue->size = size;
	queue->obj_size = obj_size;
	queue->head = -1;
	queue->tail = -1;
	queue->items = 0;
}

bool circular_queue_push(struct circular_queue *queue, const void *data) {
	if((queue->head == 0 && queue->tail == (queue->size - 1)) || (queue->head == (queue->tail + 1))) {
		return false;
	}

	if(queue->head == -1) {
		queue->head = 0;
		queue->tail = 0;
	} else {
		if(queue->tail == (queue->size - 1)) {
			queue->tail = 0;
		} else {
			queue->tail++;
		}
	}

	memcpy((void*)queue + queue->data_offset + (queue->tail * queue->obj_size), data, queue->obj_size);
	__atomic_add_fetch(&queue->items, 1, __ATOMIC_RELAXED);

	return true;
}

bool circular_queue_pop(struct circular_queue *queue, void *data) {
	if(queue->head == -1) {
		return false;
	}

	memcpy(data, (void*)queue + queue->data_offset + (queue->head * queue->obj_size), queue->obj_size);
	__atomic_sub_fetch(&queue->items, 1, __ATOMIC_RELAXED);

	if(queue->head == queue->tail) {
		queue->head = -1;
		queue->tail = -1;
	} else {
		if(queue->head == (queue->size - 1)) {
			queue->head = 0;
		} else {
			queue->head++;
		}
	}

	return true;
}

bool circular_queue_pop_tail(struct circular_queue *queue, void *data) {
	if(queue->head == queue->tail) {
		return false;
	}

	if(queue->tail == 0) {
		queue->tail = queue->size - 1;
	} else {
		queue->tail--;
	}

	memcpy(data, (void*)queue + queue->data_offset + (queue->tail * queue->obj_size), queue->obj_size);
	__atomic_sub_fetch(&queue->items, 1, __ATOMIC_RELAXED);

	return true;
}

bool circular_queue_peek(struct circular_queue *queue, void *data) {
	if(queue->head == -1) {
		return false;
	}

	memcpy(data, (void*)queue + queue->data_offset + (queue->head * queue->obj_size), queue->obj_size);

	return true;
}

void circular_queue_flush(struct circular_queue *queue) {
	queue->head = -1;
	queue->tail = -1;
	queue->items = 0;
}

void circular_queue_destroy(struct circular_queue *queue) {
	free(queue);
}
