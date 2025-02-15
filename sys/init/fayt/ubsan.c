#include <fayt/debug.h>
#include <stdint.h>

struct ub_source_location {
	const char *file;
	uint32_t line;
	uint32_t column;
};

struct ub_type_descriptor {
	uint16_t kind;
	uint16_t info;
	char name[];
};

struct ub_overflow_data {
	struct ub_source_location location;
	struct ub_type_descriptor *type;
};

struct ub_shift_out_of_bounds_data {
	struct ub_source_location location;
	struct ub_type_descriptor *left_type;
	struct ub_type_descriptor *right_type;
};

struct ub_invalid_value_data {
	struct ub_source_location location;
	struct ub_type_descriptor *type;
};

struct ub_array_out_of_bounds_data {
	struct ub_source_location location;
	struct ub_type_descriptor *array_type;
	struct ub_type_descriptor *index_type;
};

struct ub_type_mismatch_v1_data {
	struct ub_source_location location;
	struct ub_type_descriptor *type;
	unsigned char log_alignment;
	unsigned char type_check_kind;
};

struct ub_negative_vla_data {
	struct ub_source_location location;
	struct ub_type_descriptor *type;
};

struct ub_nonnull_return_data {
	struct ub_source_location location;
};

struct ub_nonnull_arg_data {
	struct ub_source_location location;
};

struct ub_unreachable_data {
	struct ub_source_location location;
};

struct ub_invalid_builtin_data {
	struct ub_source_location location;
	unsigned char kind;
};

void __ubsan_handle_add_overflow(struct ub_overflow_data *data) {
	panic("ubsan: [%s]: \"%s\": on [%s] at %d:%d", data->location.file,
		"addition overflow", data->type->name, data->location.line, data->location.column);
}

void __ubsan_handle_sub_overflow(struct ub_overflow_data *data) {
	panic("ubsan: [%s]: \"%s\": on [%s] at %d:%d", data->location.file,
		"subtraction overflow", data->type->name, data->location.line, data->location.column);
}

void __ubsan_handle_mul_overflow(struct ub_overflow_data *data) {
	panic("ubsan: [%s]: \"%s\": on [%s] at %d:%d", data->location.file,
		"multiplication overflow", data->type->name, data->location.line, data->location.column);
}

void __ubsan_handle_divrem_overflow(struct ub_overflow_data *data) {
	panic("ubsan: [%s]: \"%s\": on [%s] at %d:%d", data->location.file,
		"division overflow", data->type->name, data->location.line, data->location.column);
}

void __ubsan_handle_negate_overflow(struct ub_overflow_data *data) {
	panic("ubsan: [%s]: \"%s\": on [%s] at %d:%d", data->location.file,
		"negation overflow", data->type->name, data->location.line, data->location.column);
}

void __ubsan_handle_pointer_overflow(struct ub_overflow_data *data) {
	panic("ubsan: [%s]: \"%s\": on [%s] at %d:%d", data->location.file,
		"pointer overflow", data->type->name, data->location.line, data->location.column);
}

void __ubsan_handle_shift_out_of_bounds(struct ub_shift_out_of_bounds_data *data) {
	panic("ubsan: [%s]: \"%s\": between [%s] and [%s] at %d:%d", data->location.file,
		"shift out of bounds", data->left_type->name, data->right_type->name,
		data->location.line, data->location.column);
}

void __ubsan_handle_load_invalid_value(struct ub_invalid_value_data *data) {
	panic("ubsan: [%s]: \"%s\": on [%s] at %d:%d", data->location.file,
		"invalid load value", data->type->name, data->location.line, data->location.column);
}

void __ubsan_handle_out_of_bounds(struct ub_array_out_of_bounds_data *data) {
	panic("ubsan: [%s]: \"%s\": between array [%s] and index [%s] at %d:%d", data->location.file,
		"array out of bounds", data->array_type->name, data->index_type->name,
		data->location.line, data->location.column);
}

void __ubsan_handle_type_mismatch_v1(struct ub_type_mismatch_v1_data *data, uintptr_t ptr) {	
	panic("ubsan: [%s]: \"%s\": ptr [%x] on [%s] at %d:%d | NULL {%s}, MISALIGNED {%s}", data->location.file,
		"ptr mismatch", ptr, data->type->name, data->location.line, data->location.column, !ptr ? "TRUE" : "FALSE",
		(ptr & ((1 << data->log_alignment) - 1)) ? "TRUE" : "FALSE");
}

void __ubsan_handle_vla_bound_not_positive(struct ub_negative_vla_data *data) {
	panic("ubsan: [%s]: \"%s\": on [%s] at %d:%d", data->location.file,
		"variable length array bound is negative", data->type->name, data->location.line, data->location.column);
}

void __ubsan_handle_nonnull_return(struct ub_nonnull_return_data *data) {
	panic("ubsan: [%s]: \"%s\": at %d:%d", data->location.file,
		"non-null return is null", data->location.line, data->location.column);
}

void __ubsan_handle_nonnull_arg(struct ub_nonnull_arg_data *data) {
	panic("ubsan: [%s]: \"%s\": at %d:%d", data->location.file,
		"non-null argument is null", data->location.line, data->location.column);
}

void __ubsan_handle_builtin_unreachable(struct ub_unreachable_data *data) {
	panic("ubsan: [%s]: \"%s\": at %d:%d", data->location.file,
		"unreachable code is reached", data->location.line, data->location.column);
}

void __ubsan_handle_invalid_builtin(struct ub_invalid_builtin_data *data) {
	panic("ubsan: [%s]: \"%s\": at %d:%d", data->location.file,
		"invalid builtin", data->location.line, data->location.column);
}
