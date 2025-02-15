#ifndef SERVER_H_
#define SERVER_H_

#include <fayt/hash.h>
#include <fayt/lock.h>
#include <fayt/bitmap.h>

#include <core/scheduler.h>

#include <limine.h>

#define SERVER_DEFAULT_STACK_LOCATION 0x20000
#define SERVER_DEFAULT_STACK_SIZE CONTEXT_DEFAULT_STACK_SIZE
#define SERVER_MAX_NAME_LENGTH 64

struct server_id {
	int nid;
	int sid;
};

struct namespace
{
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

extern struct server *master_scheduler;

int launch_servers(void);
int create_server(const char *, const char *, struct server *);
int create_namespace(const char *);

extern struct sched_cgroup cgroup_system;

struct server *find_server(const char *, const char *);
struct limine_file *limine_search_module(const char *);

#endif
