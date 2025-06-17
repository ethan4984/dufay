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

	rpool->total_depth = 0;
	{
		size_t length = rpool->total;
		for (; length;) {
			rpool->total_depth++;
			length >>= 1;
		}
	}
	if (unlikely(!rpool->total_depth))
		RETURN_ERROR;

	rpool->boundary_segments =
		alloc(sizeof(struct rboundary) * rpool->total_depth);
	if (unlikely(rpool->boundary_segments == NULL))
		RETURN_ERROR;
	rpool->boundary_table = (struct dictionary){ 0 };

	struct rboundary *boundary = alloc(sizeof(struct rboundary));
	if (unlikely(boundary == NULL))
		RETURN_ERROR;

	boundary->base = base;
	boundary->length = length;
	boundary->allocated = false;
	boundary->next = NULL;
	boundary->last = NULL;

	rpool->boundary_segments[rpool->total_depth - 1] = boundary;

	return 0;
}

int ralloc(struct rpool *rpool, uintptr_t *resource, size_t length)
{
	int boundary_index = rpool_boundary_index(rpool, length);
	if (unlikely(boundary_index == -1))
		RETURN_ERROR;
	if (rpool->free - length <= 0)
		return -1;

	struct rboundary *boundary = NULL;
	for (; (size_t)boundary_index < rpool->total_depth;) {
		boundary = rpool->boundary_segments[boundary_index];
		if (boundary)
			goto found;
		boundary_index++;
	}
	RETURN_ERROR;
found:
	struct rboundary *nrboundary = alloc(sizeof(struct rboundary));
	if (unlikely(nrboundary == NULL))
		RETURN_ERROR;

	nrboundary->base = boundary->base;
	nrboundary->length = boundary->base;
	nrboundary->allocated = true;

	boundary->base += length;
	boundary->length -= length;
	boundary->allocated -= false;

	int new_boundary_index = rpool_boundary_index(rpool, boundary->length);
	if (boundary_index != new_boundary_index) {
		int ret = rpool_boundary_move(
			&rpool->boundary_segments[boundary_index],
			&rpool->boundary_segments[new_boundary_index], boundary);
		if (unlikely(ret == -1))
			RETURN_ERROR;
	}

	int ret = RB_GENERIC_INSERT(rpool->boundary_prospects, base, nrboundary);
	if (unlikely(ret == -1))
		RETURN_ERROR;

	ret = dictionary_push(&rpool->boundary_table, &nrboundary->base, nrboundary,
						  sizeof(nrboundary->base));
	if (unlikely(ret == -1))
		RETURN_ERROR;

	rpool->free -= length;
	*resource = nrboundary->base;

	return 0;
}

int rfree(struct rpool *rpool, uintptr_t resource)
{
	struct rboundary *rboundary = NULL;
	int ret = dictionary_search(&rpool->boundary_table, &resource,
								sizeof(resource), (void **)&rboundary);
	if (ret == -1 || rboundary == NULL)
		return 0;

	struct rboundary *rboundary_left = rboundary->left;
	struct rboundary *rboundary_right = rboundary->right;

	rpool->free += rboundary->length;

	if (rboundary_left || rboundary_right) {
		struct rboundary *rboundary_coalesced = NULL;
		rboundary_coalesced = alloc(sizeof(struct rboundary));
		if (unlikely(rboundary_coalesced == NULL))
			RETURN_ERROR;

		if (rboundary_left &&
			rboundary_left->base + rboundary_left->length == rboundary->base) {
			if (!rboundary_coalesced)
				rboundary_coalesced = alloc(sizeof(struct rboundary));
			if (unlikely(rboundary_coalesced == NULL))
				RETURN_ERROR;

			rboundary_coalesced->base = rboundary_left->base;
			rboundary_coalesced->length =
				rboundary_left->length + rboundary->length;
			rboundary_coalesced->allocated = false;
			rboundary->next = NULL;
			rboundary->last = NULL;

			ret = RB_GENERIC_DELETE(rpool->boundary_prospects, rboundary_left);
			if (ret == -1)
				RETURN_ERROR;

			free(rboundary_left);
		}

		if (rboundary_right &&
			rboundary->base + rboundary->length == rboundary_right->base) {
			if (!rboundary_coalesced) {
				rboundary_coalesced = alloc(sizeof(struct rboundary));
				if (unlikely(rboundary_coalesced == NULL))
					RETURN_ERROR;

				rboundary_coalesced->base = rboundary->base;
				rboundary_coalesced->length = rboundary->length;
			}

			rboundary_coalesced->length += rboundary_right->length;
			rboundary_coalesced->allocated = false;
			rboundary->next = NULL;
			rboundary->last = NULL;

			ret = RB_GENERIC_DELETE(rpool->boundary_prospects, rboundary_right);
			if (ret == -1)
				RETURN_ERROR;

			free(rboundary_right);
		}

		if (rboundary_coalesced) {
			ret = RB_GENERIC_DELETE(rpool->boundary_prospects, rboundary);
			if (ret == -1)
				RETURN_ERROR;

			free(rboundary);
			rboundary = rboundary_coalesced;
		}
	}

	int boundary_index = rpool_boundary_index(rpool, rboundary->length);
	if (unlikely(boundary_index == -1))
		RETURN_ERROR;

	struct rboundary **segment = &rpool->boundary_segments[boundary_index];
	rboundary->next = *segment;
	rboundary->last = NULL;
	rboundary->allocated = false;
	if (*segment)
		(*segment)->last = rboundary;
	*segment = rboundary;

	return 0;
}

int rdestroy(struct rpool *rpool)
{
	if (unlikely(rpool == NULL))
		RETURN_ERROR;

	for (size_t i = 0; i < rpool->total_depth; i++) {
		struct rboundary *node = rpool->boundary_segments[i];
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

	size_t ret = 0;
	for (; n;) {
		ret++;
		n >>= 1;
	}

	if (unlikely(ret >= rpool->total_depth))
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
