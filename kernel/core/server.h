#ifndef SERVER_H_
#define SERVER_H_

#include <fayt/hash.h>
#include <fayt/lock.h>
#include <fayt/bitmap.h>

#include <core/scheduler.h>

extern struct hash_table context_table;
extern struct bitmap cid_bitmap;

#define SEARCH_CONTEXT(CID, CONTEXT) ({ \
	__label__ finish; \
	int ret = -1; \
	if((CONTEXT) == NULL) goto finish; \
	ret = hash_table_search(&context_table, &(CID), sizeof((CID)), (void**)CONTEXT); \
finish: \
	ret; \
})

#define NEW_CONTEXT(CONTEXT) ({ \
	__label__ finish; \
	int ret = -1; \
	if((CONTEXT) == NULL) goto finish; \
	int cid; \
	ret = bitmap_alloc(&cid_bitmap, &cid); \
	if(ret == -1) goto finish; \
	(CONTEXT)->comms.cid = cid; \
	ret = hash_table_push(&context_table, &(CONTEXT)->comms.cid, \
		(CONTEXT), sizeof((CONTEXT)->comms.cid)); \
finish: \
	ret; \
})


#define SERVER_DEFAULT_STACK_LOCATION 0x20000
#define SERVER_DEFAULT_STACK_SIZE CONTEXT_DEFAULT_STACK_SIZE
#define SERVER_MAX_NAME_LENGTH 64

struct server_id {
	int nid;
	int sid;
};

struct namespace {
	const char *name;

	int nid;

	struct bitmap sid_bitmap;
	struct hash_table server_table;
};

struct server {
	const char *name;

	struct server_id id;
	struct context *context;

	struct limine_file *file;

	struct spinlock lock;
};

struct server_env { 
	const char *name;
};

struct sched_descriptor {
	struct timer timer;
	int processor_id;
	int queue_default_refill;
	int load;
	int cid;
};

struct sched_queue_config {
	int cid;
	int cgroup;
	int nice;
	int offload;
};

extern struct server *master_scheduler;

int launch_servers(void);
int create_server(const char*, const char*, struct server*);
int create_namespace(const char*);

struct server *find_server(const char*, const char*);

#endif
