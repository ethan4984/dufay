#ifndef FAYT_PORTAL_H_
#define FAYT_PORTAL_H_

#include <fayt/lock.h>

#include <stdint.h>
#include <stddef.h>

constexpr uint32_t PORTAL_REQ_SHARE = 1u << 0;
constexpr uint32_t PORTAL_REQ_DIRECT = 1u << 1;
constexpr uint32_t PORTAL_REQ_ANON = 1u << 2;
constexpr uint32_t PORTAL_REQ_COW = 1u << 3;
constexpr uint32_t PORTAL_REQ_SP = 1u << 4;
constexpr uint32_t PORTAL_REQ_CONTINUOUS = 1u << 5;
constexpr uint32_t PORTAL_REQ_PEEK = 1u << 6;

constexpr uint32_t PORTAL_RESP_FAILURE = 1u << 0;
constexpr uint32_t PORTAL_RESP_SUCCESS = 1u << 1;

constexpr uint32_t PORTAL_PROT_READ = 1u << 0;
constexpr uint32_t PORTAL_PROT_WRITE = 1u << 1;
constexpr uint32_t PORTAL_PROT_EXEC = 1u << 2;

struct [[gnu::packed]] portal_link {
	int lock;

	unsigned int length;
	unsigned int header_offset;
	unsigned int header_limit;
	unsigned int data_offset;
	unsigned int data_limit;
	unsigned int magic;

	char data[];
};

constexpr uint32_t LINK_CIRCULAR = 1u << 0;
constexpr uint32_t LINK_VECTOR = 1u << 1;
constexpr uint32_t LINK_RAW = 1u << 2;

constexpr uint32_t LINK_CIRCULAR_MAGIC = 0x9E810F7F;
constexpr uint32_t LINK_VECTOR_MAGIC = 0xEF8647C0;
constexpr uint32_t LINK_RAW_MAGIC = 0xFF6C7D34;

#define LINK_META(LINK) ({ \
	(LINK)->data + header->offset; \
})

#define OPERATE_LINK(LINK, CLASS, OPERATION) ({ \
	__label__ out_ol; \
	int _ret = -1; \
	if((LINK) == NULL) goto out_ol; \
	static_assert((CLASS) == LINK_CIRCULAR || \
		(CLASS) == LINK_VECTOR || (CLASS) == LINK_RAW, "Invalid link class"); \
	if((LINK)->magic != (((CLASS) == LINK_CIRCULAR) ? \
		LINK_CIRCULAR_MAGIC : ((CLASS == LINK_VECTOR) ? \
		LINK_VECTOR_MAGIC : (((CLASS) == LINK_RAW) ? LINK_RAW_MAGIC : 0)))) goto out_ol; \
	raw_spinlock(&(LINK)->lock); \
	_ret = OPERATION; \
	raw_spinrelease(&(LINK)->lock); \
out_ol: \
	_ret; \
})

struct [[gnu::packed]] portal_req {
	int type;
	int prot;
	int length;

	struct [[gnu::packed]] {
		const char *identifier;
		int create;
		int length;
		int type;
	} share;

	struct [[gnu::packed]] {
		struct {
			int asid;
			uintptr_t base;
		} source;

		struct {
			int asid;
			uintptr_t base;
		} destination;

		size_t limit;
	} cow;

	struct [[gnu::packed]] {
		uintptr_t addr;
		size_t length;
		uint64_t paddr;
		int pcnt;
	} morphology;
};

struct [[gnu::packed]] portal_resp {
	uintptr_t base;
	uint64_t limit;
	uint64_t flags;

	struct [[gnu::packed]] {
		uint64_t paddr;
		int pcnt;
	} morphology;
};

#define DUFAY_ALLOCATE_UNBROKEN(SIZE, RESP) ({ \
	__label__ finish; \
	int ret = 0; \
	uintptr_t address; \
	ret = as_address(&address_space, &address, (SIZE)); \
	if(ret == -1) { ret = -1; goto finish; } \
	struct portal_req portal_req = { \
		.type = PORTAL_REQ_ANON | PORTAL_REQ_CONTINUOUS | PORTAL_REQ_PEEK, \
		.prot = PORTAL_PROT_READ | PORTAL_PROT_WRITE, \
		.morphology = { \
			.addr = address, \
			.length = ALIGN_UP((SIZE), PAGE_SIZE), \
			.pcnt = DIV_ROUNDUP((SIZE), PAGE_SIZE) \
		} \
	}; \
	struct syscall_response syscall_response = SYSCALL2(SYSCALL_PORTAL, &portal_req, (RESP)); \
	if(syscall_response.ret == -1 || (RESP)->base != address || (RESP)->limit != \
		ALIGN_UP((SIZE), PAGE_SIZE)) { ret = -1; goto finish; } \
finish: \
	ret; \
})

#endif
