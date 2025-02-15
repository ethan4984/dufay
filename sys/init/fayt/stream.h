#ifndef FAYT_STREAM_H_
#define FAYT_STREAM_H_

#include <fayt/lock.h>
#include <stdarg.h>

struct stream_info {
	void *private;
	void (*write)(struct stream_info*, char);
	struct spinlock lock;
};

int stream_print(struct stream_info *stream, const char *str, va_list arg);

#endif
