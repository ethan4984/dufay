#ifndef CORE_RESOURCE_H_
#define CORE_RESOURCE_H_

#include <fayt/rb_tree.h>
#include <fayt/dictionary.h>

#include <stdint.h>
#include <stddef.h>

struct rboundary {
	uintptr_t base;
	size_t length;
	int allocated;

	RB_META(struct rboundary);

	struct rboundary *next;
	struct rboundary *last;
};

//	MAINTIAN A RB TREE ORDERED WITH RESPECT TO THE BASE OF THE BOUNDARY WHICH REPRESENTS
//	ALL OBJECT IN PROSPECT TO BE COALESCED. REPRESENT ALL OBJECTS CURRENTLY ALLOCATED IN
//	THE FORM OF A HASH TABLE FOR CONSTANT TIME LOOKUP, BUT REPRESENT OBJECTS IN PROSPECT
//	TO BE COALESCED SO THAT WHEN WE ATTEMPT TO FREE THE OBJECT WE CAN QUICKLY COMPARE TO
//	SURROUNDING OBJECTS TO CHECK WHETHER OR NOT IT IS POSSIBLE TO UNITE.

//	A SEGMENT IS DEFINED AS A FREELIST CARRYING OBJECTS GUARANTEED WITH A LENGTH BETWEEN
//	[2^(N-1), 2^N). THIS ALLOWS US EFFECTIVELY MAKE USE OF THE RESOURCE SPACE WHILE ALSO
//	ALLOWING FOR CONSTANT TIME ALLOCATION.

struct rpool {
	uintptr_t base;
	size_t total;
	size_t total_depth;
	size_t free;

	struct dictionary boundary_table;
	struct rboundary *boundary_prospects;
	struct rboundary **boundary_segments;
};

int rinit(struct rpool *, uintptr_t, size_t);
int rdestroy(struct rpool *);
int ralloc(struct rpool *, uintptr_t *, size_t);
int rfree(struct rpool *, uintptr_t);

#endif
