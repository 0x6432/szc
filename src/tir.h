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
	X(PTR_CAST, "ptr_cast")	/* un: compatible pointer qualifier coercion */ \
	X(INT_CAST, "int_cast")	/* un: widen/narrow integer to `type` */ \
	X(TRUNCATE, "truncate")	/* un: discard high integer bits */ \
	X(INT_FROM_BOOL, "int_from_bool")	/* un: bool -> u1 */ \
	X(ALLOC, "alloc")	/* type is the pointer type; stack slot */ \
	X(LOAD, "load")	/* un: pointer */ \
	X(STORE, "store")	/* bin: pointer, value */ \
	X(FOR_LEN_CHECK, "for_len_check")	/* bin: assert equal usize lengths */ \
	X(CALL, "call")	/* payload: callee FuncId, arg count, args */ \
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
	FuncId   callee;
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
