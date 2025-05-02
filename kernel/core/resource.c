#include <core/resource.h>

#include <fayt/compiler.h>
#include <fayt/slab.h>
#include <fayt/debug.h>

static int rpool_boundary_index(struct rpool *, size_t);
static int rpool_boundary_move(struct rboundary **, struct rboundary **,
							   struct rboundary *);

int rinit(struct rpool *rpool, uintptr_t base, size_t length)
{
	if (unlikely(rpool == NULL))
		RETURN_ERROR;

	rpool->base = base;
	rpool->total = length;
	rpool->free = length;

	rpool->segment_cnt = 0;
	{
		size_t length = rpool->total;
		for (; length >>= 1;) {
			rpool->segment_cnt++;
		}
	}
	if (unlikely(!rpool->segment_cnt))
		RETURN_ERROR;

	rpool->segments = alloc(sizeof(struct rboundary) * rpool->segment_cnt);
	if (unlikely(rpool->segments == NULL))
		RETURN_ERROR;

	struct rboundary *boundary = alloc(sizeof(struct rboundary));
	if (unlikely(boundary == NULL))
		RETURN_ERROR;

	boundary->base = base;
	boundary->length = length;
	boundary->allocated = false;
	boundary->next = NULL;
	boundary->last = NULL;

	rpool->segments[rpool->segment_cnt - 1] = boundary;

	return 0;
}

int ralloc(struct rpool *, uintptr_t *, size_t)
{
	return 0;
}

int rfree(struct rpool *, uintptr_t)
{
	return 0;
}

int rdestroy(struct rpool *rpool)
{
	if (unlikely(rpool == NULL))
		RETURN_ERROR;

	for (int i = 0; i < rpool->segment_cnt; i++) {
		struct rboundary *node = rpool->segments[i];
		for (; node;) {
			struct rboundary *tmp = node->next;
			free(node);
			node = tmp;
		}
	}

	return 0;
}

static int rpool_boundary_index(struct rpool *rpool, size_t n)
{
	if (unlikely(rpool == NULL))
		return -1;
	if (!n)
		return 0;

	int ret = 0;
	while (n >>= 1) {
		ret++;
	}

	if (unlikely(ret >= rpool->segment_cnt))
		return -1;
	return ret;
}

static int rpool_boundary_move(struct rboundary **src, struct rboundary **dest,
							   struct rboundary *node)
{
	if (node == NULL || *src == NULL)
		RETURN_ERROR;
	if (node->next)
		node->next->last = node->last;
	if (node->last)
		node->last->next = node->next;
	if (*src == node)
		*src = node->next;

	if (*dest == NULL) {
		node->last = NULL;
		node->next = NULL;
		*dest = node;
		return 0;
	}

	node->next = *dest;
	node->last = NULL;

	(*dest)->last = node;
	*dest = node;

	return 0;
}
