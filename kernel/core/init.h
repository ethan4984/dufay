#ifndef INIT_H_
#define INIT_H_

#include <limine.h>

#define SERVER_DEFAULT_STACK_LOCATION 0x20000
#define SERVER_DEFAULT_STACK_SIZE CONTEXT_DEFAULT_STACK_SIZE
#define SERVER_MAX_NAME_LENGTH 64

int launch_init(void);
struct limine_file *limine_search_module(const char *);

#endif
