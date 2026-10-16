#ifndef SZC_TIR_H
#define SZC_TIR_H

#include <stdint.h>

#include "comptime.h"
#include "intern.h"

/*
 * Typed IR: the output of sema for one runtime function, input of codegen.
 * Every instruction has a type. Comptime-known operands are TIR_CONST
 * instructions holding a ValueId. Bodies are stored like in the untyped IR:
 * `count, inst...` in `extra`.
 */

typedef uint32_t TirRef;	/* instruction index in TirFunc.insts */
typedef uint32_t TirBody;	/* extra index of a body, 0 = none */

#define TIR_REF_NONE ((TirRef)0xffffffffu)	/* e.g. the operand of `ret` in a void function */

#define TIR_OP_LIST(X) \
	X(ARG, "arg")	/* data.index: parameter number */ \
	X(CONST, "const")	/* data.value */ \
	X(ADD, "add")	/* bin: checked integer arithmetic */ \
	X(ADD_WRAP, "add_wrap") \
	X(ADD_SAT, "add_sat") \
	X(SUB, "sub") \
	X(SUB_WRAP, "sub_wrap") \
	X(SUB_SAT, "sub_sat") \
	X(MUL, "mul") \
	X(MUL_WRAP, "mul_wrap") \
	X(MUL_SAT, "mul_sat") \
	X(DIV_TRUNC, "div_trunc") \
	X(DIV_FLOOR, "div_floor") \
	X(DIV_CEIL, "div_ceil") \
	X(DIV_EXACT, "div_exact") \
	X(REM, "rem") \
	X(MOD, "mod") \
	X(BIT_AND, "bit_and") \
	X(BIT_OR, "bit_or") \
	X(BIT_XOR, "bit_xor") \
	X(SHL, "shl") \
	X(SHL_SAT, "shl_sat") \
	X(SHR, "shr") \
	X(CMP_EQ, "cmp_eq")	/* bin, type bool; operand type in the lhs instruction */ \
	X(CMP_NEQ, "cmp_neq") \
	X(CMP_LT, "cmp_lt") \
	X(CMP_GT, "cmp_gt") \
	X(CMP_LTE, "cmp_lte") \
	X(CMP_GTE, "cmp_gte") \
	X(NOT, "not")	/* un: bool not / bitwise not */ \
	X(NEGATE, "negate")	/* un: checked negation */ \
	X(NEGATE_WRAP, "negate_wrap") \
	X(PTR_CAST, "ptr_cast")	/* un: representation-preserving thin pointer cast */ \
	X(BIT_CAST, "bit_cast")	/* un: equal-width scalar bit representation */ \
	X(INT_CAST, "int_cast")	/* un: widen/narrow integer to `type` */ \
	X(TRUNCATE, "truncate")	/* un: discard high integer bits */ \
	X(BIT_REVERSE, "bit_reverse") /* un: reverse operand logical bits */ \
	X(BYTE_SWAP, "byte_swap") /* un: reverse operand bytes; width divisible by 8 */ \
	X(CLZ, "clz") /* un: unsigned count of leading zeros in operand width */ \
	X(CTZ, "ctz") /* un: unsigned count of trailing zeros in operand width */ \
	X(POP_COUNT, "pop_count") /* un: unsigned population count */ \
	X(INT_FROM_BOOL, "int_from_bool")	/* un: bool -> u1 */ \
	X(ALIGN_CHECK, "align_check") /* bin: pointer and zero-length exemption; type carries alignment */ \
	X(NULL_CHECK, "null_check") /* un: assert a thin pointer is non-null */ \
	X(SLICE_NULL_CHECK, "slice_null_check") /* bin: pointer and zero-length exemption */ \
	X(SLICE_CAST_LEN_CHECK, "slice_cast_len_check") /* bin: byte extent, divisor; assert no remainder */ \
	X(PTR_FROM_INT, "ptr_from_int")	/* un: checked usize -> pointer type */ \
	X(PTR_TO_INT, "ptr_to_int")	/* un: pointer address as usize */ \
	X(PTR_ADD, "ptr_add")	/* bin: many pointer, usize element count */ \
	X(PTR_SUB, "ptr_sub")	/* bin: many pointer, usize element count */ \
	X(PTR_ELEM_PTR, "ptr_elem_ptr")	/* bin: many pointer, usize index; no length bound */ \
	X(ARRAY_ELEM_PTR, "array_elem_ptr")	/* bin: single array pointer, checked usize index */ \
	X(SLICE_LEN_PTR, "slice_len_ptr")	/* un: pair address -> length field address */ \
	X(SLICE_START_CHECK, "slice_start_check")	/* bin: start <= end */ \
	X(SLICE_END_CHECK, "slice_end_check")	/* bin: end <= available length */ \
	X(SENTINEL_CHECK, "sentinel_check")	/* bin: actual element equals sentinel */ \
	X(INDEX_CHECK, "index_check")	/* bin: usize index < dynamic length */ \
	X(ARRAY_COPY, "array_copy")	/* bin: destination and source array pointers; overlap-safe */ \
	X(ALLOC, "alloc")	/* type is the pointer type; stack slot */ \
	X(LOAD, "load")	/* un: pointer */ \
	X(STORE, "store")	/* bin: pointer, value */ \
	X(LOAD_VOLATILE, "load_volatile")	/* un: observable runtime load */ \
	X(STORE_VOLATILE, "store_volatile")	/* bin: observable runtime store */ \
	X(FOR_LEN_CHECK, "for_len_check")	/* bin: assert equal usize lengths */ \
	X(CALL, "call")	/* payload: direct FuncId or indirect TirRef, arg count, args */ \
	X(RET, "ret")	/* un */ \
	X(BLOCK, "block")	/* payload: body; breaks target it */ \
	X(BR, "br")	/* bin: lhs = block instruction, rhs = value */ \
	X(CONDBR, "condbr")	/* payload: condition, then body, else body */ \
	X(LOOP, "loop")	/* payload: body; repeat jumps to its start */ \
	X(REPEAT, "repeat")	/* un: the loop instruction */ \
	X(PANIC, "panic")	/* bin: byte pointer, length */ \
	X(TRAP, "trap")	/* backend-only terminator after a known noreturn call */ \
	X(UNREACHABLE, "unreachable")	/* source unreachable: safety panic */

typedef enum TirOp {
#define X(name, text) TIR_##name,
	TIR_OP_LIST(X)
#undef X
	TIR_OP_COUNT
} TirOp;

typedef struct TirInst {
	TirOp    op;
	TypeId   type;
	uint32_t src;	/* byte offset in the function's file */
	union {
		struct {
			TirRef operand;
		} un;
		struct {
			TirRef lhs;
			TirRef rhs;
		} bin;
		ValueId  value;
		uint32_t index;
		uint32_t payload;
	} data;
} TirInst;

typedef struct TirCall {
	FuncId   callee;	/* 0 for indirect call */
	TirRef   indirect;	/* TIR_REF_NONE for direct call */
	uint32_t arg_count;	/* followed by arg_count TirRefs */
} TirCall;

typedef struct TirCondbr {
	TirRef  condition;
	TirBody then_body;
	TirBody else_body;
} TirCondbr;

typedef struct TirFunc {
	FuncId   func;
	TirInst  *insts;
	uint32_t inst_count;
	uint32_t inst_capacity;
	uint32_t *extra;
	uint32_t extra_count;
	uint32_t extra_capacity;
	TirBody  body;
} TirFunc;

/* Number of extra words a payload struct occupies (all payload fields are 32 bits). */
#define TIR_WORDS(Type) ((uint32_t)(sizeof(Type) / sizeof(uint32_t)))

const char *tir_op_name(TirOp op);

#endif
