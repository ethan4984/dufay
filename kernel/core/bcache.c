#include <arch/x86/smp.h>

#include <core/bcache.h>
#include <core/handle.h>
#include <core/syscall.h>

#include <fayt/debug.h>
#include <fayt/string.h>
#include <fayt/compiler.h>
#include <fayt/hash.h>

static struct hash_table bcache_table;

static int bcache_register(handle_t handle)
{
	return 0;
}

static int bcache_lookup(handle_t handle, struct bcache **bcache)
{
	if (bcache == NULL)
		RETURN_ERROR;

	struct handle_binding *handle_binding =
		handle_lookup(CORE_LOCAL->current_context->handles, handle);
	if (handle_binding == NULL)
		RETURN_ERROR;

	int ret = hash_table_search(&bcache_table, handle_binding,
								sizeof(struct handle_binding), (void **)bcache);
	if (ret == -1)
		RETURN_ERROR;

	return 0;
}

static int bcache_lookup_blk(struct bcache *bcache, size_t lba_start,
							 size_t lba_cnt, struct blk **blk)
{
	if (bcache == NULL || blk == NULL)
		RETURN_ERROR;

	struct handle_blk *handle_blk = bcache->handle_binding->obj;
	if (handle_blk == NULL)
		RETURN_ERROR;

	for (size_t lba = lba_start, i = 0; lba < lba_cnt; lba++, i++) {
		struct blk *root = bcache->blk_tree;

		for (; root;) {
			if (root->lba == lba) {
				blk[i] = root;
				goto found;
			}

			if (root->lba > lba) {
				root = root->left;
			} else {
				root = root->right;
			}
		}
		blk[i] = NULL;
found:
		continue;
	}

	return 0;
}

static int bcache_consult_read(handle_t handle, size_t offset, int count,
							   void *buffer)
{
	if (buffer == NULL)
		RETURN_ERROR;

	struct bcache *bcache = NULL;
	int ret = bcache_lookup(handle, &bcache);
	if (ret == -1 || bcache == NULL)
		RETURN_ERROR;

	if ((bcache->handle_binding->access & HANDLE_ACCESS_READ) == 0)
		RETURN_ERROR;

	struct handle_blk *handle_blk = bcache->handle_binding->obj;
	if (handle_blk == NULL)
		RETURN_ERROR;

	size_t lba_start = offset / handle_blk->lba_size;
	size_t lba_end = DIV_ROUNDUP(offset + count, handle_blk->lba_size);
	size_t lba_cnt = lba_end - lba_start;

	if (!lba_cnt)
		RETURN_ERROR;

	struct blk **blks = alloc(sizeof(struct blk *) * lba_cnt);
	if (blks == NULL)
		RETURN_ERROR;

	ret = bcache_lookup_blk(bcache, lba_start, lba_cnt, blks);
	if (ret == -1)
		RETURN_ERROR;

	for (size_t lba = lba_start, location = offset, bytes_read = 0, i = 0;
		 lba < lba_cnt; lba++, i++) {
		struct blk *blk = blks[i];

		size_t index_into_blk = location % handle_blk->lba_size;
		size_t byte_cnt = handle_blk->lba_size - index_into_blk;

		bytes_read += byte_cnt;
		if (unlikely(bytes_read > count)) {
			byte_cnt = count - bytes_read - byte_cnt;
			bytes_read -= byte_cnt;
		}

		if (blk) {
			memcpy(buffer + bytes_read, blk->buffer + index_into_blk, byte_cnt);
		} else {
		}

		bytes_read += handle_blk->lba_size;
		location += byte_cnt;
	}

	free(blks);

	return count;
}

static int bcache_consult_write(handle_t handle, size_t offset, int count,
								const void *buffer)
{
	if (buffer == NULL)
		RETURN_ERROR;

	struct bcache *bcache = NULL;
	int ret = bcache_lookup(handle, &bcache);
	if (ret == -1 || bcache == NULL)
		RETURN_ERROR;

	if ((bcache->handle_binding->access & HANDLE_ACCESS_WRITE) == 0)
		RETURN_ERROR;

	struct handle_blk *handle_blk = bcache->handle_binding->obj;
	if (handle_blk == NULL)
		RETURN_ERROR;

	size_t lba_start = offset / handle_blk->lba_size;
	size_t lba_end = DIV_ROUNDUP(offset + count, handle_blk->lba_size);
	size_t lba_cnt = lba_end - lba_start;

	struct blk **blks = alloc(sizeof(struct blk *) * lba_cnt);
	if (blks == NULL)
		RETURN_ERROR;

	ret = bcache_lookup_blk(bcache, lba_start, lba_cnt, blks);
	if (ret == -1)
		RETURN_ERROR;

	for (size_t lba = lba_start, location = offset, bytes_read = 0, i = 0;
		 lba < lba_cnt; lba++, i++) {
		struct blk *blk = blks[i];

		size_t index_into_blk = location % handle_blk->lba_size;
		size_t byte_cnt = handle_blk->lba_size - index_into_blk;

		bytes_read += byte_cnt;
		if (unlikely(bytes_read > count)) {
			byte_cnt = count - bytes_read - byte_cnt;
			bytes_read -= byte_cnt;
		}

		if (blk) {
		} else {
		}

		bytes_read += handle_blk->lba_size;
		location += byte_cnt;
	}

	free(blks);

	return count;
}

SYSCALL_DEFINE4(bcache_consult_read, handle_t, handle, size_t, offset, int,
				count, void *, buffer,
				{ bcache_consult_read(handle, offset, count, buffer); })

SYSCALL_DEFINE4(bcache_consult_write, handle_t, handle, size_t, offset, int,
				count, const void *, buffer,
				{ bcache_consult_write(handle, offset, count, buffer); })
