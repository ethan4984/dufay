#include <fayt/time.h>

struct time invariant_tsc_read(struct timer *timer) {
	struct time ret = { .sec = 0, .nsec = 0 };
	if(timer == NULL) return ret;

	uint64_t tsc = ({
		uint64_t rax, rdx;
		__asm__ volatile ("rdtsc" : "=a"(rax), "=d"(rdx));
		(uint64_t)rax | ((uint64_t)rdx << 32);
	});

	uint64_t ns = (tsc * NANO_PER_SECOND) / timer->freq;

	ret = (struct time) {
		.nsec = ns % NANO_PER_SECOND,
		.sec = ns / NANO_PER_SECOND
	};

	return ret;
}

struct time time_add(struct time a, struct time b) {
	struct time ret = {
		.sec = a.sec + b.sec,
		.nsec = a.nsec + b.nsec
	};

	if(ret.nsec > NANO_PER_SECOND) {
		ret.nsec -= NANO_PER_SECOND;
		ret.sec++;
	}

	return ret;
}

struct time time_sub(struct time a, struct time b) {
	struct time ret = {
		.nsec = a.nsec - b.nsec,
		.sec = a.sec - b.sec
	};

	if(ret.nsec < 0) {
		ret.nsec = NANO_PER_SECOND - b.nsec - a.nsec;
		ret.sec--;
	}

	if(ret.sec < 0) {
		ret.nsec = 0;
		ret.sec = 0;
	}

	return ret;
}

time_t time_to_ns(struct time a) {
	return S_TO_NS(a.sec) + a.nsec;
}
