/*
 * codegen: typed IR -> QBE IL.
 *
 * Value representation: `bool` and integers of up to 32 bits live in `w`
 * temporaries, wider integers (up to 64 bits) and pointers in `l`
 * temporaries. An integer narrower than its temporary is kept in canonical
 * form: zero-extended if unsigned, sign-extended if signed. Operations whose
 * result may leave that form (add, sub, mul, shl, negate, not, truncation)
 * re-normalize it.
 *
 * Every typed-IR instruction `%N` becomes the QBE temporary `%tN`; QBE does
 * not require SSA input, so `br` simply assigns the block's result temporary.
 * Constants are printed inline as immediate operands.
 */
#include "codegen.h"

#include <inttypes.h>
#include <string.h>

typedef enum QbeClass {
	QBE_NONE,	/* zero-sized: no temporary */
	QBE_W,
	QBE_L,
} QbeClass;

typedef enum PanicKind {
	PANIC_OVERFLOW,
	PANIC_INT_CAST,
	PANIC_DIV_ZERO,
	PANIC_SHIFT,
	PANIC_EXACT,
	PANIC_FOR_LEN,
	PANIC_UNREACHABLE,
	PANIC_INDEX,
	PANIC_SLICE_ORDER,
	PANIC_SENTINEL,
	PANIC_NULL_CAST,
	PANIC_ALIGNMENT,
	PANIC_COUNT,
} PanicKind;

typedef enum MemoryWidth {
	MEMORY_BYTE,
	MEMORY_HALF,
	MEMORY_WORD,
	MEMORY_LONG,
	MEMORY_WIDTH_COUNT,
} MemoryWidth;

typedef struct Codegen {
	Sema					*sema;
	FILE					*out;
	FILE					*assembly;
	Diag					*diag;
	FileId file;
	const SemaFunc	*func;
	const TirFunc		*tir;
	bool					failed;
	bool					panic_used[PANIC_COUNT];
	bool *bytes_used;
	bool *constants_used;
	bool *dependencies_seen;
	uint64_t array_storage_bytes;
	bool explicit_panic_used;
	bool volatile_loads[2][MEMORY_WIDTH_COUNT];
	bool volatile_stores[MEMORY_WIDTH_COUNT];
} Codegen;

static void
codegen_error(Codegen *cg, uint32_t src, const char *message)
{
	cg->failed = true;
	diag_report(cg->diag, DIAG_ERROR, cg->file, src, "%s", message);
}

/* ---------- Types ---------- */

static QbeClass
type_class(Codegen *cg, TypeId type_id, uint32_t src)
{
	const Type *type = sema_type(cg->sema, type_id);
	switch (type->kind) {
	case TYPE_BOOL:
		return QBE_W;
	case TYPE_INT:
		if (type->as.integer.bits == 0)
			return QBE_NONE;
		if (type->as.integer.bits <= 32)
			return QBE_W;
		if (type->as.integer.bits <= 64)
			return QBE_L;
		codegen_error(cg, src, "not supported by szc yet: integers wider than 64 bits");
		return QBE_L;
	case TYPE_POINTER:
		if (type->as.pointer.size == POINTER_SLICE) {
			codegen_error(cg, src, "slice value cannot enter a scalar QBE temporary");
			return QBE_NONE;
		}
		return QBE_L;
	case TYPE_VOID:
	case TYPE_NORETURN:
		return QBE_NONE;
	default:
		codegen_error(cg, src, "not supported by szc yet: runtime values of this type");
		return QBE_NONE;
	}
}

static char
class_char(QbeClass cls)
{
	return cls == QBE_L ? 'l' : 'w';
}

static uint32_t
class_bits(QbeClass cls)
{
	return cls == QBE_L ? 64 : 32;
}

static bool
type_is_signed(Codegen *cg, TypeId type_id)
{
	const Type *type = sema_type(cg->sema, type_id);
	return type->kind == TYPE_INT && type->as.integer.is_signed;
}

static uint32_t
type_bits(Codegen *cg, TypeId type_id)
{
	const Type *type = sema_type(cg->sema, type_id);
	if (type->kind == TYPE_BOOL)
		return 1;
	if (type->kind == TYPE_INT)
		return type->as.integer.bits;
	return 64;
}

/* ---------- Operands ---------- */

static const TirInst *
inst_at(Codegen *cg, TirRef ref)
{
	return &cg->tir->insts[ref];
}

/* Constant subobjects are relocations into their root, not separate data objects. */
static void
print_pointer_address(Codegen *cg, FILE *out, ValueId id, bool outlined, bool with_offset)
{
	PointerAddress address = value_pointer_address(sema_values(cg->sema), id);
	const Value *root = value_get(sema_values(cg->sema), address.root);
	const char *prefix = outlined ? "" : "$";
	if (address.logical) {
		codegen_error(cg, 0, "not supported by szc yet: runtime address of comptime-only array element");
		fputs("0", out);
		return;
	}
	switch (root->kind) {
	case VALUE_REF:
		cg->constants_used[root->as.object] = true;
		fprintf(out, "%sszc.const.%u", prefix, (unsigned)root->as.object);
		break;
	case VALUE_BYTES:
		cg->bytes_used[address.root] = true;
		fprintf(out, "%sszc.bytes.%u", prefix, (unsigned)address.root);
		break;
	case VALUE_GLOBAL:
		fprintf(out, "%s%s", prefix, sema_global(cg->sema, root->as.global)->symbol);
		break;
	default:
		codegen_error(cg, 0, "not supported by szc yet: this array element pointer backing");
		fputs("0", out);
		return;
	}
	if (with_offset && address.offset != 0)
		fprintf(out, " + %" PRIu64, address.offset);
}

static void
print_operand(Codegen *cg, TirRef ref)
{
	const TirInst *inst = inst_at(cg, ref);
	const Type *type = sema_type(cg->sema, inst->type);
	if (type->kind == TYPE_INT && type->as.integer.bits == 0) {
		fputs("0", cg->out);
		return;
	}
	if (inst->op != TIR_CONST) {
		fprintf(cg->out, "%%t%u", (unsigned)ref);
		return;
	}
	const Value *value = value_get(sema_values(cg->sema), inst->data.value);
	switch (value->kind) {
	case VALUE_BOOL:
		fputs(value->as.boolean ? "1" : "0", cg->out);
		return;
	case VALUE_INT: {
		uint64_t bits = bigint_to_u64_bits(&value->as.integer);
		if (type_class(cg, inst->type, inst->src) == QBE_W)
			fprintf(cg->out, "%" PRId32, (int32_t)(uint32_t)bits);
		else
			fprintf(cg->out, "%" PRId64, (int64_t)bits);
		return;
	}
	case VALUE_INT_PTR:
		fprintf(cg->out, "%" PRIu64, value->as.address);
		return;
	case VALUE_GLOBAL:
		fprintf(cg->out, "$%s", sema_global(cg->sema, value->as.global)->symbol);
		return;
	case VALUE_REF:
		cg->constants_used[value->as.object] = true;
		fprintf(cg->out, "$szc.const.%u", (unsigned)value->as.object);
		return;
	case VALUE_PTR_CAST:
	case VALUE_SLICE_FIELD_PTR:
	case VALUE_ELEMENT_PTR:
		if (value_pointer_address(sema_values(cg->sema), inst->data.value).offset == 0)
			print_pointer_address(cg, cg->out, inst->data.value, false, false);
		else
			fprintf(cg->out, "%%t%u", (unsigned)ref);
		return;
	case VALUE_BYTES:
		cg->bytes_used[inst->data.value] = true;
		fprintf(cg->out, "$szc.bytes.%u", (unsigned)inst->data.value);
		return;
	case VALUE_FUNCTION:
		fprintf(cg->out, "$%s", sema_func(cg->sema, value->as.function)->symbol);
		return;
	default:
		/* undefined and zero-sized values */
		fputs("0", cg->out);
		return;
	}
}

/* Starts `\t%tN =c ` for instruction `ref`. */
static void
print_dest(Codegen *cg, TirRef ref, QbeClass cls)
{
	fprintf(cg->out, "\t%%t%u =%c ", (unsigned)ref, class_char(cls));
}

static void
emit_binary(Codegen *cg, TirRef ref, QbeClass cls, const char *op, TirRef lhs, TirRef rhs)
{
	print_dest(cg, ref, cls);
	fprintf(cg->out, "%s ", op);
	print_operand(cg, lhs);
	fputs(", ", cg->out);
	print_operand(cg, rhs);
	fputs("\n", cg->out);
}

/* Brings an integer/bool %tN back into canonical form. */
static void
normalize(Codegen *cg, TirRef ref, TypeId type, QbeClass cls)
{
	uint32_t bits = type_bits(cg, type);
	uint32_t width = class_bits(cls);
	if (bits >= width)
		return;
	char c = class_char(cls);
	if (!type_is_signed(cg, type)) {
		uint64_t mask = bits == 64 ? UINT64_MAX : (((uint64_t)1 << bits) - 1);
		fprintf(cg->out, "\t%%t%u =%c and %%t%u, %" PRIu64 "\n", (unsigned)ref, c, (unsigned)ref, mask);
		return;
	}
	if (bits == 8 || bits == 16) {
		fprintf(cg->out, "\t%%t%u =%c exts%c %%t%u\n", (unsigned)ref, c, bits == 8 ? 'b' : 'h', (unsigned)ref);
		return;
	}
	if (bits == 32) {
		fprintf(cg->out, "\t%%t%u =l extsw %%t%u\n", (unsigned)ref, (unsigned)ref);
		return;
	}
	uint32_t shift = width - bits;
	fprintf(cg->out, "\t%%t%u =%c shl %%t%u, %u\n", (unsigned)ref, c, (unsigned)ref, (unsigned)shift);
	fprintf(cg->out, "\t%%t%u =%c sar %%t%u, %u\n", (unsigned)ref, c, (unsigned)ref, (unsigned)shift);
}

/* ---------- Runtime safety ---------- */

static const char *const panic_messages[PANIC_COUNT] = {
	"integer overflow",
	"integer does not fit in destination type",
	"division by zero",
	"shift amount is greater than the type size",
	"exact division produced remainder",
	"for loop over objects with non-equal lengths",
	"reached unreachable code",
	"index out of bounds",
	"start index is larger than end index",
	"sentinel mismatch",
	"cast causes pointer to be null",
	"incorrect alignment",
};

/* Bounds as canonical 64-bit bit patterns; wider types have already errored. */
static uint64_t
int_max_bits(uint32_t bits, bool is_signed)
{
	if (bits == 0)
		return 0;
	if (is_signed)
		return bits >= 64 ? INT64_MAX : ((uint64_t)1 << (bits - 1)) - 1;
	return bits >= 64 ? UINT64_MAX : ((uint64_t)1 << bits) - 1;
}

static uint64_t
int_min_bits(uint32_t bits)
{
	return bits == 0 ? 0 : ~int_max_bits(bits, true);
}

/* %gN_suffix is a bool: true branches to a minimal, non-returning panic. */
static void
emit_guard(Codegen *cg, TirRef ref, const char *suffix, PanicKind kind)
{
	cg->panic_used[kind] = true;
	fprintf(cg->out, "\tjnz %%g%u_%s, @g%u_%s_panic, @g%u_%s_ok\n", ref, suffix, ref, suffix, ref, suffix);
	fprintf(cg->out, "@g%u_%s_panic\n\tcall $szc.panic.%u()\n\thlt\n@g%u_%s_ok\n", ref, suffix, (unsigned)kind, ref, suffix);
}

static void
emit_bound_check(Codegen *cg, TirRef ref, TirRef operand, const char *suffix, const char *cmp, uint64_t bound, PanicKind kind)
{
	QbeClass cls = type_class(cg, inst_at(cg, operand)->type, inst_at(cg, ref)->src);
	fprintf(cg->out, "\t%%g%u_%s =w %s%c ", ref, suffix, cmp, class_char(cls));
	print_operand(cg, operand);
	fprintf(cg->out, ", %" PRId64 "\n", (int64_t)bound);
	emit_guard(cg, ref, suffix, kind);
}

static void
emit_cast_checks(Codegen *cg, TirRef ref)
{
	const TirInst *inst = inst_at(cg, ref);
	TirRef operand = inst->data.un.operand;
	TypeId source = inst_at(cg, operand)->type;
	uint32_t from_bits = type_bits(cg, source);
	uint32_t to_bits = type_bits(cg, inst->type);
	bool from_signed = type_is_signed(cg, source);
	bool to_signed = type_is_signed(cg, inst->type);
	if (from_bits == 0)
		return;
	if (from_signed && !to_signed)
		emit_bound_check(cg, ref, operand, "negative", "cslt", 0, PANIC_INT_CAST);
	if (from_signed && to_signed && to_bits < from_bits)
		emit_bound_check(cg, ref, operand, "low", "cslt", int_min_bits(to_bits), PANIC_INT_CAST);
	uint32_t source_positive_bits = from_bits - (from_signed ? 1 : 0);
	uint32_t dest_positive_bits = to_bits - (to_signed && to_bits != 0 ? 1 : 0);
	if (dest_positive_bits < source_positive_bits)
		emit_bound_check(cg, ref, operand, "high", from_signed ? "csgt" : "cugt", int_max_bits(to_bits, to_signed), PANIC_INT_CAST);
}

/* Multiplication: compare magnitudes against limit / |rhs| before multiplying.
 * This also handles i64.min without ever doing a signed negation or division.
 */
static void
emit_mul_overflow(Codegen *cg, TirRef ref, QbeClass cls)
{
	const TirInst *inst = inst_at(cg, ref);
	FILE *out = cg->out;
	char c = class_char(cls);
	uint32_t bits = type_bits(cg, inst->type);
	bool is_signed = type_is_signed(cg, inst->type);
	TirRef operands[] = {inst->data.bin.lhs, inst->data.bin.rhs};
	for (uint32_t i = 0; i < 2; i++) {
		char name = i == 0 ? 'a' : 'b';
		fprintf(out, "\t%%g%u_%c =%c copy ", ref, name, c);
		print_operand(cg, operands[i]);
		fputs("\n", out);
		if (is_signed) {
			fprintf(out, "\t%%g%u_%c_sign =%c sar %%g%u_%c, %u\n", ref, name, c, ref, name, class_bits(cls) - 1);
			fprintf(out, "\t%%g%u_%c =%c xor %%g%u_%c, %%g%u_%c_sign\n", ref, name, c, ref, name, ref, name);
			fprintf(out, "\t%%g%u_%c =%c sub %%g%u_%c, %%g%u_%c_sign\n", ref, name, c, ref, name, ref, name);
		}
	}
	if (is_signed) {
		fprintf(out, "\t%%g%u_sign =%c xor %%g%u_a_sign, %%g%u_b_sign\n", ref, c, ref, ref);
		fprintf(out, "\t%%g%u_sign =%c and %%g%u_sign, 1\n", ref, c, ref);
		fprintf(out, "\t%%g%u_limit =%c add %" PRId64 ", %%g%u_sign\n", ref, c, (int64_t)int_max_bits(bits, true), ref);
	} else {
		fprintf(out, "\t%%g%u_limit =%c copy %" PRId64 "\n", ref, c, (int64_t)int_max_bits(bits, false));
	}
	fprintf(out, "\t%%g%u_nonzero =w cne%c %%g%u_b, 0\n", ref, c, ref);
	fprintf(out, "\tjnz %%g%u_nonzero, @g%u_mul_check, @g%u_mul_zero\n", ref, ref, ref);
	fprintf(out, "@g%u_mul_check\n\t%%g%u_limit =%c udiv %%g%u_limit, %%g%u_b\n", ref, ref, c, ref, ref);
	fprintf(out, "\t%%g%u_overflow =w cugt%c %%g%u_a, %%g%u_limit\n\tjmp @g%u_mul_done\n", ref, c, ref, ref, ref);
	fprintf(out, "@g%u_mul_zero\n\t%%g%u_overflow =w copy 0\n\tjmp @g%u_mul_done\n@g%u_mul_done\n", ref, ref, ref, ref);
}

static void
emit_add_sub_overflow(Codegen *cg, TirRef ref, QbeClass cls)
{
	const TirInst *inst = inst_at(cg, ref);
	FILE *out = cg->out;
	char c = class_char(cls);
	bool is_add = inst->op == TIR_ADD || inst->op == TIR_ADD_SAT;
	if (!type_is_signed(cg, inst->type)) {
		fprintf(out, "\t%%g%u_overflow =w cult%c ", ref, c);
		if (is_add)
			fprintf(out, "%%t%u", ref);
		else
			print_operand(cg, inst->data.bin.lhs);
		fputs(", ", out);
		print_operand(cg, is_add ? inst->data.bin.lhs : inst->data.bin.rhs);
		fputs("\n", out);
		return;
	}
	fprintf(out, "\t%%g%u_same =%c xor ", ref, c);
	print_operand(cg, inst->data.bin.lhs);
	fputs(", ", out);
	print_operand(cg, inst->data.bin.rhs);
	fputs("\n", out);
	if (is_add)
		fprintf(out, "\t%%g%u_same =%c xor %%g%u_same, -1\n", ref, c, ref);
	fprintf(out, "\t%%g%u_change =%c xor ", ref, c);
	print_operand(cg, inst->data.bin.lhs);
	fprintf(out, ", %%t%u\n", ref);
	fprintf(out, "\t%%g%u_change =%c and %%g%u_same, %%g%u_change\n", ref, c, ref, ref);
	fprintf(out, "\t%%g%u_overflow =w cslt%c %%g%u_change, 0\n", ref, c, ref);
}

/* Clamp a normalized arithmetic result when %gN_overflow is true. */
static void
emit_saturate(Codegen *cg, TirRef ref, QbeClass cls, bool is_mul, bool is_sub)
{
	const TirInst *inst = inst_at(cg, ref);
	FILE *out = cg->out;
	char c = class_char(cls);
	fprintf(out, "\tjnz %%g%u_overflow, @g%u_saturate, @g%u_saturated\n@g%u_saturate\n", ref, ref, ref, ref);
	if (type_is_signed(cg, inst->type)) {
		if (is_mul)
			fprintf(out, "\t%%g%u_clamp =%c neg %%g%u_sign\n", ref, c, ref);
		else {
			fprintf(out, "\t%%g%u_clamp =%c sar ", ref, c);
			print_operand(cg, inst->data.bin.lhs);
			fprintf(out, ", %u\n", class_bits(cls) - 1);
		}
		fprintf(out, "\t%%t%u =%c xor %%g%u_clamp, %" PRId64 "\n", ref, c, ref, (int64_t)int_max_bits(type_bits(cg, inst->type), true));
	} else {
		fprintf(out, "\t%%t%u =%c copy %" PRId64 "\n", ref, c, is_sub ? 0 : (int64_t)int_max_bits(type_bits(cg, inst->type), false));
	}
	fprintf(out, "\tjmp @g%u_saturated\n@g%u_saturated\n", ref, ref);
}

static void
emit_saturating_shift(Codegen *cg, TirRef ref, QbeClass cls)
{
	const TirInst *inst = inst_at(cg, ref);
	FILE *out = cg->out;
	char c = class_char(cls);
	TirRef lhs = inst->data.bin.lhs;
	TirRef rhs = inst->data.bin.rhs;
	QbeClass amount_class = type_class(cg, inst_at(cg, rhs)->type, inst->src);
	fprintf(out, "\t%%g%u_large =w cuge%c ", ref, class_char(amount_class));
	print_operand(cg, rhs);
	fprintf(out, ", %u\n\tjnz %%g%u_large, @g%u_shift_large, @g%u_shift_small\n", type_bits(cg, inst->type), ref, ref, ref);
	fprintf(out, "@g%u_shift_large\n\t%%t%u =%c copy 0\n\t%%g%u_overflow =w cne%c ", ref, ref, c, ref, c);
	print_operand(cg, lhs);
	fprintf(out, ", 0\n\tjmp @g%u_shift_done\n@g%u_shift_small\n", ref, ref);
	fprintf(out, "\t%%g%u_amount =w copy ", ref);
	print_operand(cg, rhs);
	fputs("\n", out);
	print_dest(cg, ref, cls);
	fputs("shl ", out);
	print_operand(cg, lhs);
	fprintf(out, ", %%g%u_amount\n", ref);
	normalize(cg, ref, inst->type, cls);
	fprintf(out, "\t%%g%u_back =%c %s %%t%u, %%g%u_amount\n", ref, c, type_is_signed(cg, inst->type) ? "sar" : "shr", ref, ref);
	fprintf(out, "\t%%g%u_overflow =w cne%c %%g%u_back, ", ref, c, ref);
	print_operand(cg, lhs);
	fprintf(out, "\n\tjmp @g%u_shift_done\n@g%u_shift_done\n", ref, ref);
	emit_saturate(cg, ref, cls, false, false);
}

static void
emit_division(Codegen *cg, TirRef ref, QbeClass cls)
{
	const TirInst *inst = inst_at(cg, ref);
	FILE *out = cg->out;
	char c = class_char(cls);
	TirRef lhs = inst->data.bin.lhs;
	TirRef rhs = inst->data.bin.rhs;
	bool is_signed = type_is_signed(cg, inst->type);
	bool is_rem = inst->op == TIR_REM || inst->op == TIR_MOD;
	emit_bound_check(cg, ref, rhs, "zero", "ceq", 0, PANIC_DIV_ZERO);
	if (is_signed) {
		fprintf(out, "\t%%g%u_minus_one =w ceq%c ", ref, c);
		print_operand(cg, rhs);
		fputs(", -1\n", out);
		if (is_rem) {
			/* Remainders are zero for divisor -1, including min/-1.
			 * Avoid the hardware division exception without a false panic.
			 */
			fprintf(out, "\tjnz %%g%u_minus_one, @g%u_rem_zero, @g%u_rem_compute\n", ref, ref, ref);
			fprintf(out, "@g%u_rem_zero\n\t%%t%u =%c copy 0\n\tjmp @g%u_div_done\n@g%u_rem_compute\n", ref, ref, c, ref, ref);
		} else {
			fprintf(out, "\t%%g%u_min =w ceq%c ", ref, c);
			print_operand(cg, lhs);
			fprintf(out, ", %" PRId64 "\n", (int64_t)int_min_bits(type_bits(cg, inst->type)));
			fprintf(out, "\t%%g%u_overflow =w and %%g%u_min, %%g%u_minus_one\n", ref, ref, ref);
			emit_guard(cg, ref, "overflow", PANIC_OVERFLOW);
		}
	}
	emit_binary(cg, ref, cls, is_rem ? (is_signed ? "rem" : "urem") : (is_signed ? "div" : "udiv"), lhs, rhs);
	bool needs_remainder = inst->op == TIR_DIV_EXACT || inst->op == TIR_DIV_CEIL || (is_signed && inst->op == TIR_DIV_FLOOR);
	bool needs_adjust = inst->op == TIR_DIV_CEIL || (is_signed && (inst->op == TIR_DIV_FLOOR || inst->op == TIR_MOD));
	if (needs_remainder) {
		fprintf(out, "\t%%g%u_remainder =%c %s ", ref, c, is_signed ? "rem" : "urem");
		print_operand(cg, lhs);
		fputs(", ", out);
		print_operand(cg, rhs);
		fputs("\n", out);
	}
	if (needs_adjust || inst->op == TIR_DIV_EXACT) {
		fprintf(out, "\t%%g%u_nonzero =w cne%c %%%c%u%s, 0\n", ref, c, is_rem ? 't' : 'g', ref, is_rem ? "" : "_remainder");
		if (inst->op == TIR_DIV_EXACT) {
			emit_guard(cg, ref, "nonzero", PANIC_EXACT);
		} else {
			if (is_signed) {
				fprintf(out, "\t%%g%u_sign =%c xor ", ref, c);
				print_operand(cg, lhs);
				fputs(", ", out);
				print_operand(cg, rhs);
				fputs("\n", out);
				fprintf(out, "\t%%g%u_adjust =w cslt%c %%g%u_sign, 0\n", ref, c, ref);
				if (inst->op == TIR_DIV_CEIL)
					fprintf(out, "\t%%g%u_adjust =w xor %%g%u_adjust, 1\n", ref, ref);
				fprintf(out, "\t%%g%u_adjust =w and %%g%u_adjust, %%g%u_nonzero\n", ref, ref, ref);
			} else {
				fprintf(out, "\t%%g%u_adjust =w copy %%g%u_nonzero\n", ref, ref);
			}
			fprintf(out, "\tjnz %%g%u_adjust, @g%u_div_adjust, @g%u_div_done\n@g%u_div_adjust\n", ref, ref, ref, ref);
			fprintf(out, "\t%%t%u =%c %s %%t%u, ", ref, c, inst->op == TIR_DIV_FLOOR ? "sub" : "add", ref);
			if (is_rem)
				print_operand(cg, rhs);
			else
				fputs("1", out);
			fprintf(out, "\n\tjmp @g%u_div_done\n", ref);
		}
	}
	if (needs_adjust || (is_signed && is_rem)) {
		if (!needs_adjust)
			fprintf(out, "\tjmp @g%u_div_done\n", ref);
		fprintf(out, "@g%u_div_done\n", ref);
	}
}

static void
emit_panic_stubs(Codegen *cg)
{
	for (PanicKind kind = PANIC_OVERFLOW; kind < PANIC_COUNT; kind++) {
		if (!cg->panic_used[kind])
			continue;
		fprintf(cg->out, "data $szc.panic.msg.%u = { b \"%s\\n\", b 0 }\n", (unsigned)kind, panic_messages[kind]);
		fprintf(cg->out, "function $szc.panic.%u() {\n@start\n", (unsigned)kind);
		fprintf(cg->out, "\tcall $write(w 2, l $szc.panic.msg.%u, l %zu)\n", (unsigned)kind, strlen(panic_messages[kind]) + 1);
		fputs("\tcall $abort()\n\thlt\n}\n\n", cg->out);
	}
}

/* ---------- Memory ---------- */

/* All physical storage callers must handle layout overflow explicitly. */
static uint64_t
storage_size(Codegen *cg, TypeId type, uint32_t src)
{
	uint64_t size;
	if (!sema_type_size(cg->sema, type, &size)) {
		codegen_error(cg, src, "not supported by szc yet: array layout size exceeds 64-bit address space");
		return 0;
	}
	return size;
}

static const char *
load_op(Codegen *cg, TypeId type)
{
	uint64_t size = storage_size(cg, type, 0);
	bool is_signed = type_is_signed(cg, type);
	switch (size) {
	case 1:
		return is_signed ? "loadsb" : "loadub";
	case 2:
		return is_signed ? "loadsh" : "loaduh";
	case 4:
		return "loadw";
	default:
		return "loadl";
	}
}

static const char *
store_op(Codegen *cg, TypeId type)
{
	switch (storage_size(cg, type, 0)) {
	case 1:
		return "storeb";
	case 2:
		return "storeh";
	case 4:
		return "storew";
	default:
		return "storel";
	}
}

/* Calls are opaque to QBE; each outlined helper performs exactly one access. */
static MemoryWidth
memory_width(Codegen *cg, TypeId type)
{
	switch (storage_size(cg, type, 0)) {
	case 1: return MEMORY_BYTE;
	case 2: return MEMORY_HALF;
	case 4: return MEMORY_WORD;
	default: return MEMORY_LONG;
	}
}

static void
emit_volatile_helpers(Codegen *cg)
{
	static const char *const loads[2][MEMORY_WIDTH_COUNT] = {
		{"loadub", "loaduh", "loadw", "loadl"},
		{"loadsb", "loadsh", "loadw", "loadl"},
	};
	static const char *const stores[MEMORY_WIDTH_COUNT] = {"storeb", "storeh", "storew", "storel"};
	for (uint32_t index = 0; index < MEMORY_WIDTH_COUNT; index++) {
		MemoryWidth width = (MemoryWidth)index;
		uint32_t bits = 8u << index;
		char cls = width == MEMORY_LONG ? 'l' : 'w';
		for (uint32_t sign = 0; sign < 2; sign++) {
			if (!cg->volatile_loads[sign][width])
				continue;
			fprintf(cg->out, "function %c $szc.volatile.load.%c%u(l %%ptr) {\n@start\n", cls, sign ? 'i' : 'u', (unsigned)bits);
			fprintf(cg->out, "\t%%value =%c %s %%ptr\n\tret %%value\n}\n\n", cls, loads[sign][width]);
		}
		if (cg->volatile_stores[width]) {
			fprintf(cg->out, "function $szc.volatile.store.%u(l %%ptr, %c %%value) {\n@start\n", (unsigned)bits, cls);
			fprintf(cg->out, "\t%s %%value, %%ptr\n\tret\n}\n\n", stores[width]);
		}
	}
}

/* ---------- Instructions ---------- */

static void emit_body(Codegen *cg, TirBody body);

static const char *
compare_op(TirOp op, bool is_signed)
{
	switch (op) {
	case TIR_CMP_EQ:
		return "ceq";
	case TIR_CMP_NEQ:
		return "cne";
	case TIR_CMP_LT:
		return is_signed ? "cslt" : "cult";
	case TIR_CMP_GT:
		return is_signed ? "csgt" : "cugt";
	case TIR_CMP_LTE:
		return is_signed ? "csle" : "cule";
	default:
		return is_signed ? "csge" : "cuge";
	}
}

static void
emit_inst(Codegen *cg, TirRef ref)
{
	const TirInst *inst = inst_at(cg, ref);
	FILE *out = cg->out;
	QbeClass cls = type_class(cg, inst->type, inst->src);
	if (cls == QBE_NONE && sema_type(cg->sema, inst->type)->kind == TYPE_INT && inst->op != TIR_CALL && inst->op != TIR_INT_CAST)
		return;
	switch (inst->op) {
	case TIR_CONST: {
		const Value *value = value_get(sema_values(cg->sema), inst->data.value);
		PointerAddress address = value_pointer_address(sema_values(cg->sema), inst->data.value);
		if ((value->kind == VALUE_ELEMENT_PTR || value->kind == VALUE_PTR_CAST || value->kind == VALUE_SLICE_FIELD_PTR) && address.offset != 0) {
			print_dest(cg, ref, QBE_L);
			fputs("add ", out);
			print_pointer_address(cg, cg->out, inst->data.value, false, false);
			fprintf(out, ", %" PRIu64 "\n", address.offset);
		}
		return;
	}
	case TIR_ARG:
	case TIR_ALLOC:
		/* parameters, inline immediates, and allocations hoisted to @start */
		return;
	case TIR_ADD:
	case TIR_SUB:
	case TIR_MUL:
	case TIR_ADD_WRAP:
	case TIR_SUB_WRAP:
	case TIR_ADD_SAT:
	case TIR_SUB_SAT:
	case TIR_MUL_SAT:
	case TIR_MUL_WRAP: {
		bool is_mul = inst->op == TIR_MUL || inst->op == TIR_MUL_WRAP || inst->op == TIR_MUL_SAT;
		bool checked = inst->op == TIR_ADD || inst->op == TIR_SUB || inst->op == TIR_MUL;
		bool saturating = inst->op == TIR_ADD_SAT || inst->op == TIR_SUB_SAT || inst->op == TIR_MUL_SAT;
		const char *op = is_mul ? "mul" : inst->op == TIR_ADD || inst->op == TIR_ADD_WRAP || inst->op == TIR_ADD_SAT ? "add" : "sub";
		if ((checked || saturating) && is_mul)
			emit_mul_overflow(cg, ref, cls);
		emit_binary(cg, ref, cls, op, inst->data.bin.lhs, inst->data.bin.rhs);
		normalize(cg, ref, inst->type, cls);
		if (checked || saturating) {
			if (!is_mul)
				emit_add_sub_overflow(cg, ref, cls);
			if (saturating)
				emit_saturate(cg, ref, cls, is_mul, inst->op == TIR_SUB_SAT);
			else
				emit_guard(cg, ref, "overflow", PANIC_OVERFLOW);
		}
		return;
	}
	case TIR_SHL:
		emit_bound_check(cg, ref, inst->data.bin.rhs, "shift", "cuge", type_bits(cg, inst->type), PANIC_SHIFT);
		emit_binary(cg, ref, cls, "shl", inst->data.bin.lhs, inst->data.bin.rhs);
		normalize(cg, ref, inst->type, cls);
		return;
	case TIR_SHL_SAT:
		emit_saturating_shift(cg, ref, cls);
		return;
	case TIR_DIV_TRUNC:
	case TIR_DIV_FLOOR:
	case TIR_DIV_CEIL:
	case TIR_DIV_EXACT:
	case TIR_REM:
	case TIR_MOD:
		emit_division(cg, ref, cls);
		return;
	case TIR_BIT_AND:
		emit_binary(cg, ref, cls, "and", inst->data.bin.lhs, inst->data.bin.rhs);
		return;
	case TIR_BIT_OR:
		emit_binary(cg, ref, cls, "or", inst->data.bin.lhs, inst->data.bin.rhs);
		return;
	case TIR_BIT_XOR:
		emit_binary(cg, ref, cls, "xor", inst->data.bin.lhs, inst->data.bin.rhs);
		return;
	case TIR_SHR:
		emit_bound_check(cg, ref, inst->data.bin.rhs, "shift", "cuge", type_bits(cg, inst->type), PANIC_SHIFT);
		emit_binary(cg, ref, cls, type_is_signed(cg, inst->type) ? "sar" : "shr", inst->data.bin.lhs, inst->data.bin.rhs);
		return;
	case TIR_CMP_EQ:
	case TIR_CMP_NEQ:
	case TIR_CMP_LT:
	case TIR_CMP_GT:
	case TIR_CMP_LTE:
	case TIR_CMP_GTE: {
		TypeId operand_type = inst_at(cg, inst->data.bin.lhs)->type;
		QbeClass operand_class = type_class(cg, operand_type, inst->src);
		char op[8];
		snprintf(op, sizeof(op), "%s%c", compare_op(inst->op, type_is_signed(cg, operand_type)), class_char(operand_class));
		emit_binary(cg, ref, QBE_W, op, inst->data.bin.lhs, inst->data.bin.rhs);
		return;
	}
	case TIR_NOT:
		print_dest(cg, ref, cls);
		fputs("xor ", out);
		print_operand(cg, inst->data.un.operand);
		fputs(sema_type(cg->sema, inst->type)->kind == TYPE_BOOL ? ", 1\n" : ", -1\n", out);
		normalize(cg, ref, inst->type, cls);
		return;
	case TIR_NEGATE:
	case TIR_NEGATE_WRAP:
		if (inst->op == TIR_NEGATE)
			emit_bound_check(cg, ref, inst->data.un.operand, "overflow", "ceq", int_min_bits(type_bits(cg, inst->type)), PANIC_OVERFLOW);
		print_dest(cg, ref, cls);
		fputs("neg ", out);
		print_operand(cg, inst->data.un.operand);
		fputs("\n", out);
		normalize(cg, ref, inst->type, cls);
		return;
	case TIR_ALIGN_CHECK: {
		TypePointer pointer = sema_type(cg->sema, inst->type)->as.pointer;
		uint32_t alignment = pointer.alignment != 0 ? pointer.alignment : sema_type_align(cg->sema, pointer.child);
		fprintf(out, "\t%%g%u_align_bits =l and ", (unsigned)ref);
		print_operand(cg, inst->data.bin.lhs);
		fprintf(out, ", %u\n\t%%g%u_misaligned =w cnel %%g%u_align_bits, 0\n",
			(unsigned)(alignment - 1), (unsigned)ref, (unsigned)ref);
		fprintf(out, "\t%%g%u_nonempty =w ceqw ", (unsigned)ref);
		print_operand(cg, inst->data.bin.rhs);
		fprintf(out, ", 0\n\t%%g%u_alignment =w and %%g%u_misaligned, %%g%u_nonempty\n",
			(unsigned)ref, (unsigned)ref, (unsigned)ref);
		emit_guard(cg, ref, "alignment", PANIC_ALIGNMENT);
		return;
	}
	case TIR_PTR_FROM_INT: {
		TypePointer pointer = sema_type(cg->sema, inst->type)->as.pointer;
		TirRef operand = inst->data.un.operand;
		if (!pointer.is_allowzero)
			emit_bound_check(cg, ref, operand, "null", "ceq", 0, PANIC_NULL_CAST);
		uint32_t alignment = pointer.alignment != 0 ? pointer.alignment : sema_type_align(cg->sema, pointer.child);
		if (alignment > 1) {
			fprintf(out, "\t%%g%u_align_bits =l and ", (unsigned)ref);
			print_operand(cg, operand);
			fprintf(out, ", %u\n\t%%g%u_alignment =w cnel %%g%u_align_bits, 0\n",
				(unsigned)(alignment - 1), (unsigned)ref, (unsigned)ref);
			emit_guard(cg, ref, "alignment", PANIC_ALIGNMENT);
		}
		print_dest(cg, ref, QBE_L);
		fputs("copy ", out);
		print_operand(cg, operand);
		fputc('\n', out);
		return;
	}
	case TIR_PTR_TO_INT:
	case TIR_PTR_CAST:
		print_dest(cg, ref, QBE_L);
		fputs("copy ", out);
		print_operand(cg, inst->data.un.operand);
		fputs("\n", out);
		return;
	case TIR_INT_CAST:
	case TIR_TRUNCATE:
	case TIR_INT_FROM_BOOL: {
		const TirInst *operand = inst_at(cg, inst->data.un.operand);
		QbeClass from = type_class(cg, operand->type, inst->src);
		if (inst->op == TIR_INT_CAST)
			emit_cast_checks(cg, ref);
		if (cls == QBE_NONE)
			return;
		print_dest(cg, ref, cls);
		if (from == QBE_NONE) {
			fputs("copy 0\n", out);
			return;
		}
		if (from == QBE_W && cls == QBE_L)
			fputs(type_is_signed(cg, operand->type) ? "extsw " : "extuw ", out);
		else
			fputs("copy ", out);
		print_operand(cg, inst->data.un.operand);
		fputs("\n", out);
		if (type_bits(cg, inst->type) < type_bits(cg, operand->type) || type_is_signed(cg, inst->type) != type_is_signed(cg, operand->type))
			normalize(cg, ref, inst->type, cls);
		return;
	}
	case TIR_PTR_ADD:
	case TIR_PTR_SUB:
	case TIR_PTR_ELEM_PTR: {
		TypePointer pointer = sema_type(cg->sema, inst_at(cg, inst->data.bin.lhs)->type)->as.pointer;
		uint64_t stride = storage_size(cg, pointer.child, inst->src);
		fprintf(out, "\t%%e%u_offset =l mul ", (unsigned)ref);
		print_operand(cg, inst->data.bin.rhs);
		fprintf(out, ", %" PRIu64 "\n", stride);
		print_dest(cg, ref, QBE_L);
		fputs(inst->op == TIR_PTR_SUB ? "sub " : "add ", out);
		print_operand(cg, inst->data.bin.lhs);
		fprintf(out, ", %%e%u_offset\n", (unsigned)ref);
		return;
	}
	case TIR_SLICE_LEN_PTR:
		print_dest(cg, ref, QBE_L);
		fputs("add ", out);
		print_operand(cg, inst->data.un.operand);
		fputs(", 8\n", out);
		return;
	case TIR_SLICE_START_CHECK:
	case TIR_SLICE_END_CHECK:
		fprintf(out, "\t%%g%u_slice =w cugtl ", (unsigned)ref);
		print_operand(cg, inst->data.bin.lhs);
		fputs(", ", out);
		print_operand(cg, inst->data.bin.rhs);
		fputc('\n', out);
		emit_guard(cg, ref, "slice", inst->op == TIR_SLICE_START_CHECK ? PANIC_SLICE_ORDER : PANIC_INDEX);
		return;
	case TIR_SENTINEL_CHECK: {
		TypeId operand_type = inst_at(cg, inst->data.bin.lhs)->type;
		QbeClass operand_class = type_class(cg, operand_type, inst->src);
		fprintf(out, "\t%%g%u_sentinel =w cne%c ", (unsigned)ref, class_char(operand_class));
		print_operand(cg, inst->data.bin.lhs);
		fputs(", ", out);
		print_operand(cg, inst->data.bin.rhs);
		fputc('\n', out);
		emit_guard(cg, ref, "sentinel", PANIC_SENTINEL);
		return;
	}
	case TIR_INDEX_CHECK:
		fprintf(out, "\t%%g%u_index =w cugel ", (unsigned)ref);
		print_operand(cg, inst->data.bin.lhs);
		fputs(", ", out);
		print_operand(cg, inst->data.bin.rhs);
		fputc('\n', out);
		emit_guard(cg, ref, "index", PANIC_INDEX);
		return;
	case TIR_ARRAY_ELEM_PTR: {
		TypePointer pointer = sema_type(cg->sema, inst_at(cg, inst->data.bin.lhs)->type)->as.pointer;
		TypeArray array = sema_type(cg->sema, pointer.child)->as.array;
		uint64_t stride = storage_size(cg, array.child, inst->src);
		if (inst_at(cg, inst->data.bin.rhs)->op != TIR_CONST)
			emit_bound_check(cg, ref, inst->data.bin.rhs, "index",
				array.sentinel != VALUE_NONE ? "cugt" : "cuge", array.length, PANIC_INDEX);
		fprintf(out, "\t%%e%u_offset =l mul ", (unsigned)ref);
		print_operand(cg, inst->data.bin.rhs);
		fprintf(out, ", %" PRIu64 "\n", stride);
		print_dest(cg, ref, QBE_L);
		fputs("add ", out);
		print_operand(cg, inst->data.bin.lhs);
		fprintf(out, ", %%e%u_offset\n", (unsigned)ref);
		return;
	}
	case TIR_LOAD_VOLATILE: {
		if (cls == QBE_NONE)
			return;
		MemoryWidth width = memory_width(cg, inst->type);
		bool is_signed = type_is_signed(cg, inst->type);
		cg->volatile_loads[is_signed ? 1 : 0][width] = true;
		print_dest(cg, ref, cls);
		fprintf(out, "call $szc.volatile.load.%c%u(l ", is_signed ? 'i' : 'u', (unsigned)(8u << width));
		print_operand(cg, inst->data.un.operand);
		fputs(")\n", out);
		normalize(cg, ref, inst->type, cls);
		return;
	}
	case TIR_STORE_VOLATILE: {
		const TirInst *value = inst_at(cg, inst->data.bin.rhs);
		QbeClass value_class = type_class(cg, value->type, inst->src);
		if (value_class == QBE_NONE)
			return;
		MemoryWidth width = memory_width(cg, value->type);
		cg->volatile_stores[width] = true;
		fprintf(out, "\tcall $szc.volatile.store.%u(l ", (unsigned)(8u << width));
		print_operand(cg, inst->data.bin.lhs);
		fprintf(out, ", %c ", class_char(value_class));
		print_operand(cg, inst->data.bin.rhs);
		fputs(")\n", out);
		return;
	}
	case TIR_LOAD:
		if (cls == QBE_NONE)
			return;
		print_dest(cg, ref, cls);
		fprintf(out, "%s ", load_op(cg, inst->type));
		print_operand(cg, inst->data.un.operand);
		fputs("\n", out);
		/* Unused high storage bits are not part of a narrow Zig value. */
		if (type_bits(cg, inst->type) < storage_size(cg, inst->type, inst->src) * 8)
			normalize(cg, ref, inst->type, cls);
		return;
	case TIR_ARRAY_COPY: {
		TypePointer pointer = sema_type(cg->sema, inst_at(cg, inst->data.bin.lhs)->type)->as.pointer;
		uint64_t size = storage_size(cg, pointer.child, inst->src);
		if (size == 0)
			return;
		fputs("\tcall $memmove(l ", out);
		print_operand(cg, inst->data.bin.lhs);
		fputs(", l ", out);
		print_operand(cg, inst->data.bin.rhs);
		fprintf(out, ", l %" PRIu64 ")\n", size);
		return;
	}
	case TIR_STORE: {
		const TirInst *value = inst_at(cg, inst->data.bin.rhs);
		if (type_class(cg, value->type, inst->src) == QBE_NONE)
			return;
		fprintf(out, "\t%s ", store_op(cg, value->type));
		print_operand(cg, inst->data.bin.rhs);
		fputs(", ", out);
		print_operand(cg, inst->data.bin.lhs);
		fputs("\n", out);
		return;
	}
	case TIR_CALL: {
		TirCall call;
		memcpy(&call, &cg->tir->extra[inst->data.payload], sizeof(call));
		const uint32_t *args = &cg->tir->extra[inst->data.payload + TIR_WORDS(TirCall)];
		if (cls == QBE_NONE)
			fputs("\t", out);
		else
			print_dest(cg, ref, cls);
		fputs("call ", out);
		if (call.callee != 0)
			fprintf(out, "$%s", sema_func(cg->sema, call.callee)->symbol);
		else
			print_operand(cg, call.indirect);
		fputs("(", out);
		bool first = true;
		for (uint32_t i = 0; i < call.arg_count; i++) {
			const TirInst *arg = inst_at(cg, args[i]);
			QbeClass arg_class = type_class(cg, arg->type, inst->src);
			if (arg_class == QBE_NONE)
				continue;
			fprintf(out, "%s%c ", first ? "" : ", ", class_char(arg_class));
			print_operand(cg, args[i]);
			first = false;
		}
		fputs(")\n", out);
		TypeKind kind = sema_type(cg->sema, inst->type)->kind;
		if (cls != QBE_NONE && (kind == TYPE_INT || kind == TYPE_BOOL))
			normalize(cg, ref, inst->type, cls);
		return;
	}
	case TIR_FOR_LEN_CHECK:
		fprintf(out, "\t%%g%u_len =w cnel ", (unsigned)ref);
		print_operand(cg, inst->data.bin.lhs);
		fputs(", ", out);
		print_operand(cg, inst->data.bin.rhs);
		fputs("\n", out);
		emit_guard(cg, ref, "len", PANIC_FOR_LEN);
		return;
	case TIR_RET:
		if (inst->data.un.operand == TIR_REF_NONE || type_class(cg, inst_at(cg, inst->data.un.operand)->type, inst->src) == QBE_NONE) {
			fputs("\tret\n", out);
			return;
		}
		fputs("\tret ", out);
		print_operand(cg, inst->data.un.operand);
		fputs("\n", out);
		return;
	case TIR_BLOCK:
		emit_body(cg, inst->data.payload);
		if (sema_type(cg->sema, inst->type)->kind != TYPE_NORETURN)
			fprintf(out, "@b%u\n", (unsigned)ref);
		return;
	case TIR_BR: {
		TirRef target = inst->data.bin.lhs;
		QbeClass result_class = type_class(cg, inst_at(cg, target)->type, inst->src);
		if (result_class != QBE_NONE) {
			print_dest(cg, target, result_class);
			fputs("copy ", out);
			print_operand(cg, inst->data.bin.rhs);
			fputs("\n", out);
		}
		fprintf(out, "\tjmp @b%u\n", (unsigned)target);
		return;
	}
	case TIR_CONDBR: {
		TirCondbr condbr;
		memcpy(&condbr, &cg->tir->extra[inst->data.payload], sizeof(condbr));
		fputs("\tjnz ", out);
		print_operand(cg, condbr.condition);
		fprintf(out, ", @c%u_then, @c%u_else\n", (unsigned)ref, (unsigned)ref);
		fprintf(out, "@c%u_then\n", (unsigned)ref);
		emit_body(cg, condbr.then_body);
		fprintf(out, "@c%u_else\n", (unsigned)ref);
		emit_body(cg, condbr.else_body);
		return;
	}
	case TIR_LOOP:
		fprintf(out, "@l%u\n", (unsigned)ref);
		emit_body(cg, inst->data.payload);
		return;
	case TIR_REPEAT:
		fprintf(out, "\tjmp @l%u\n", (unsigned)inst->data.un.operand);
		return;
	case TIR_PANIC:
		cg->explicit_panic_used = true;
		fputs("\tcall $szc.panic.bytes(l ", out);
		print_operand(cg, inst->data.bin.lhs);
		fputs(", l ", out);
		print_operand(cg, inst->data.bin.rhs);
		fputs(")\n\thlt\n", out);
		return;
	case TIR_UNREACHABLE:
		cg->panic_used[PANIC_UNREACHABLE] = true;
		fprintf(out, "\tcall $szc.panic.%u()\n\thlt\n", (unsigned)PANIC_UNREACHABLE);
		return;
	case TIR_TRAP:
		fputs("\thlt\n", out);
		return;
	case TIR_OP_COUNT:
		return;
	}
}

static void
emit_body(Codegen *cg, TirBody body)
{
	uint32_t count = cg->tir->extra[body];
	for (uint32_t i = 0; i < count; i++)
		emit_inst(cg, cg->tir->extra[body + 1 + i]);
}

/* ---------- Functions ---------- */

static void
emit_function(Codegen *cg, const SemaFunc *func, FuncId id)
{
	cg->func = func;
	cg->file = func->ir->file;
	cg->tir = &func->tir;
	FILE *out = cg->out;
	const TypeFn *fn = &sema_type(cg->sema, func->type)->as.fn;
	uint32_t src = func->ir->insts[func->func_inst].src;
	/* Internal array results use a trailing destination pointer and void return. */
	QbeClass ret_class = sema_type_is_aggregate(cg->sema, fn->ret) ? QBE_NONE : type_class(cg, fn->ret, src);
	if (func->alignment > 16) {
		if (cg->assembly == NULL) {
			codegen_error(cg, src, "function alignment above 16 requires assembly output; use build-exe");
			return;
		}
		/* One function at offset zero in a privately aligned executable section. */
		fprintf(cg->assembly, ".section .text.szc.fn.%u,\"ax\",@progbits\n.balign %u\n", (unsigned)id, (unsigned)func->alignment);
		fprintf(out, "section \".text.szc.fn.%u\"\n", (unsigned)id);
	}
	fputs(func->is_export ? "export function " : "function ", out);
	if (ret_class != QBE_NONE)
		fprintf(out, "%c ", class_char(ret_class));
	fprintf(out, "$%s(", func->symbol);

	/* Parameters are the ARG instructions, in order. */
	bool first = true;
	for (TirRef ref = 0; ref < cg->tir->inst_count; ref++) {
		const TirInst *inst = inst_at(cg, ref);
		if (inst->op != TIR_ARG)
			continue;
		QbeClass cls = type_class(cg, inst->type, inst->src);
		if (cls == QBE_NONE)
			continue;
		fprintf(out, "%s%c %%t%u", first ? "" : ", ", class_char(cls), (unsigned)ref);
		first = false;
	}
	fputs(") {\n@start\n", out);

	/* Stack slots, allocated once at function entry. */
	for (TirRef ref = 0; ref < cg->tir->inst_count; ref++) {
		const TirInst *inst = inst_at(cg, ref);
		if (inst->op != TIR_ALLOC)
			continue;
		TypePointer pointer = sema_type(cg->sema, inst->type)->as.pointer;
		uint64_t size = storage_size(cg, pointer.child, inst->src);
		if (size == 0)
			size = 1;
		uint32_t align = pointer.alignment != 0 ? pointer.alignment : sema_type_align(cg->sema, pointer.child);
		if (align <= 16) {
			uint32_t slot_align = align <= 4 ? 4 : align <= 8 ? 8 : 16;
			fprintf(out, "\t%%t%u =l alloc%u %" PRIu64 "\n", (unsigned)ref, (unsigned)slot_align, size);
		} else {
			/* Round a private over-allocation up; the complete object remains in its slot. */
			if (size > UINT64_MAX - (align - 1)) {
				codegen_error(cg, inst->src, "stack allocation size is too large");
				continue;
			}
			fprintf(out, "\t%%a%u_raw =l alloc16 %" PRIu64 "\n", (unsigned)ref, size + align - 1);
			fprintf(out, "\t%%a%u_round =l add %%a%u_raw, %u\n", (unsigned)ref, (unsigned)ref, (unsigned)(align - 1));
			fprintf(out, "\t%%t%u =l and %%a%u_round, -%u\n", (unsigned)ref, (unsigned)ref, (unsigned)align);
		}
	}
	emit_body(cg, cg->tir->body);
	fputs("}\n\n", out);
}

/* The process entry point: C `main` calling Zig's `main`. */
static void
emit_entry(Codegen *cg, const SemaFunc *main_func)
{
	FILE *out = cg->out;
	const TypeFn *fn = &sema_type(cg->sema, main_func->type)->as.fn;
	fputs("export function w $main() {\n@start\n", out);
	if (fn->ret == type_id_of_well_known(IR_REF_TYPE_U8)) {
		fprintf(out, "\t%%status =w call $%s()\n\tret %%status\n", main_func->symbol);
	} else if (fn->ret == type_id_of_well_known(IR_REF_TYPE_NORETURN)) {
		fprintf(out, "\tcall $%s()\n\thlt\n", main_func->symbol);
	} else {
		fprintf(out, "\tcall $%s()\n\tret 0\n", main_func->symbol);
	}
	fputs("}\n", out);
}

/* Array leaves may contain relocations to other immutable objects or byte backing. */
static void
mark_value_dependencies(Codegen *cg, ValueId id)
{
	if (cg->dependencies_seen[id])
		return;
	cg->dependencies_seen[id] = true;
	const Value *value = value_get(sema_values(cg->sema), id);
	if (value->kind == VALUE_ELEMENT_PTR || value->kind == VALUE_PTR_CAST || value->kind == VALUE_SLICE_FIELD_PTR)
		mark_value_dependencies(cg, value_pointer_address(sema_values(cg->sema), id).root);
	else if (value->kind == VALUE_REF)
		cg->constants_used[value->as.object] = true;
	else if (value->kind == VALUE_BYTES)
		cg->bytes_used[id] = true;
	else if (value->kind == VALUE_SLICE) {
		mark_value_dependencies(cg, value->as.slice.pointer);
		mark_value_dependencies(cg, value->as.slice.length);
	}
	else if (value->kind == VALUE_ARRAY) {
		ArrayValue array = value->as.array;
		for (uint32_t i = 0; i < array.count; i++)
			mark_value_dependencies(cg, array.items[i]);
	}
}

/* Nested objects predate their parents, so descending IDs close all dependencies. */
static void
mark_constant_dependencies(Codegen *cg)
{
	const ValueTable *values = sema_values(cg->sema);
	for (ConstObjectId id = values->object_count; id > 1;) {
		id--;
		if (cg->constants_used[id])
			mark_value_dependencies(cg, values->objects[id].value);
	}
}

static bool
data_type_allowed(Codegen *cg, TypeId type_id, uint32_t src)
{
	const Type *type = sema_type(cg->sema, type_id);
	if (type->kind == TYPE_ARRAY)
		return data_type_allowed(cg, type->as.array.child, src);
	if (type->kind != TYPE_VOID && type->kind != TYPE_BOOL && type->kind != TYPE_POINTER && type->kind != TYPE_INT) {
		codegen_error(cg, src, "not supported by szc yet: runtime storage of this constant type");
		return false;
	}
	if (type->kind == TYPE_INT && type->as.integer.bits > 64) {
		codegen_error(cg, src, "not supported by szc yet: integers wider than 64 bits");
		return false;
	}
	return true;
}

static bool
data_type_has_relocations(Codegen *cg, TypeId type_id)
{
	const Type *type = sema_type(cg->sema, type_id);
	if (type->kind == TYPE_ARRAY)
		return data_type_has_relocations(cg, type->as.array.child);
	return type->kind == TYPE_POINTER;
}

/* Emit one layout-checked value, not an address standing in for an aggregate. */
static void
emit_data_value(Codegen *cg, FILE *out, ValueId stored, bool outlined, bool *first, uint32_t src)
{
	const Value *value = value_get(sema_values(cg->sema), stored);
	const Type *type = sema_type(cg->sema, value->type);
	uint64_t size = storage_size(cg, value->type, src);
	if (size == 0)
		return;
	if (type->kind == TYPE_ARRAY && value->kind == VALUE_ARRAY) {
		ArrayValue array = value->as.array;
		uint64_t count = type->as.array.length + (type->as.array.sentinel != VALUE_NONE ? 1u : 0u);
		if (array.count != count) {
			codegen_error(cg, src, "invalid constant array backing for runtime storage");
			return;
		}
		for (uint32_t i = 0; i < array.count; i++)
			emit_data_value(cg, out, array.items[i], outlined, first, src);
		return;
	}
	if (value->kind == VALUE_SLICE) {
		emit_data_value(cg, out, value->as.slice.pointer, outlined, first, src);
		emit_data_value(cg, out, value->as.slice.length, outlined, first, src);
		return;
	}
	if (!outlined && !*first)
		fputs(", ", out);
	*first = false;
	if (value->kind == VALUE_UNDEFINED || value->kind == VALUE_VOID) {
		fprintf(out, outlined ? ".zero %" PRIu64 "\n" : "z %" PRIu64, size);
		return;
	}
	if (type->kind == TYPE_ARRAY) {
		codegen_error(cg, src, "not supported by szc yet: this array constant backing");
		return;
	}
	char cls = size == 1 ? 'b' : size == 2 ? 'h' : size == 4 ? 'w' : 'l';
	if (outlined)
		fprintf(out, "%s ", size == 1 ? ".byte" : size == 2 ? ".short" : size == 4 ? ".long" : ".quad");
	else
		fprintf(out, "%c ", cls);
	switch (value->kind) {
	case VALUE_INT:
		fprintf(out, "%" PRId64, (int64_t)bigint_to_u64_bits(&value->as.integer));
		break;
	case VALUE_BOOL:
		fputs(value->as.boolean ? "1" : "0", out);
		break;
	case VALUE_INT_PTR:
		fprintf(out, "%" PRIu64, value->as.address);
		break;
	case VALUE_FUNCTION:
		fprintf(out, "%s%s", outlined ? "" : "$", sema_func(cg->sema, value->as.function)->symbol);
		break;
	case VALUE_GLOBAL:
		fprintf(out, "%s%s", outlined ? "" : "$", sema_global(cg->sema, value->as.global)->symbol);
		break;
	case VALUE_REF:
		fprintf(out, "%sszc.const.%u", outlined ? "" : "$", (unsigned)value->as.object);
		break;
	case VALUE_PTR_CAST:
	case VALUE_SLICE_FIELD_PTR:
	case VALUE_ELEMENT_PTR:
		print_pointer_address(cg, out, stored, outlined, true);
		break;
	case VALUE_BYTES:
		fprintf(out, "%sszc.bytes.%u", outlined ? "" : "$", (unsigned)stored);
		break;
	default:
		codegen_error(cg, src, "not supported by szc yet: constant object initializer");
		fputs("0", out);
		break;
	}
	if (outlined)
		fputc('\n', out);
}

static void
emit_data_object(Codegen *cg, ValueId stored, const char *symbol, bool immutable, uint32_t src, uint32_t alignment)
{
	const Value *value = value_get(sema_values(cg->sema), stored);
	if (!data_type_allowed(cg, value->type, src))
		return;
	uint64_t size;
	if (!sema_type_size(cg->sema, value->type, &size)) {
		codegen_error(cg, src, "not supported by szc yet: array layout size exceeds 64-bit address space");
		return;
	}
	/* Dense static emission is bounded until compact/repeated aggregate backing lands. */
	if (sema_type(cg->sema, value->type)->kind == TYPE_ARRAY && size > 16777216) {
		codegen_error(cg, src, "not supported by szc yet: array static storage exceeds 16777216 bytes");
		return;
	}
	if (sema_type(cg->sema, value->type)->kind == TYPE_ARRAY) {
		if (size > UINT64_C(16777216) - cg->array_storage_bytes) {
			codegen_error(cg, src, "not supported by szc yet: array static storage budget exceeds 16777216 bytes");
			return;
		}
		cg->array_storage_bytes += size;
	}
	uint32_t align = alignment != 0 ? alignment : sema_type_align(cg->sema, value->type);
	bool outlined = align > 64;
	if (outlined && cg->assembly == NULL) {
		codegen_error(cg, src, "static alignment above 64 requires assembly output; use build-exe");
		return;
	}
	bool relocations = data_type_has_relocations(cg, value->type);
	FILE *out = outlined ? cg->assembly : cg->out;
	if (outlined) {
		if (immutable)
			fputs(relocations ? ".section .data.rel.ro,\"aw\",@progbits\n" : ".section .rodata,\"a\",@progbits\n", out);
		else
			fputs(".data\n", out);
		fprintf(out, ".balign %u\n%s:\n", (unsigned)align, symbol);
	} else {
		if (immutable)
			fputs(relocations ? "section \".data.rel.ro\"\n" : "section \".rodata\"\n", out);
		fprintf(out, "data $%s = align %u { ", symbol, (unsigned)align);
	}
	bool zero = value->kind == VALUE_UNDEFINED || value->kind == VALUE_VOID
		|| (value->kind == VALUE_BOOL && !value->as.boolean)
		|| (value->kind == VALUE_INT && value->as.integer.limb_count == 0);
	if (!immutable && zero) {
		fprintf(out, outlined ? ".zero %" PRIu64 "\n" : "z %" PRIu64 " }\n", size == 0 ? UINT64_C(1) : size);
		return;
	}
	if (size == 0) {
		fputs(outlined ? ".byte 0\n" : "b 0 }\n", out);
		return;
	}
	bool first = true;
	emit_data_value(cg, out, stored, outlined, &first, src);
	if (!outlined)
		fputs(" }\n", out);
}

bool
codegen_qbe(FILE *out, FILE *assembly, Sema *sema, Diag *diag)
{
	Codegen cg;
	memset(&cg, 0, sizeof(cg));
	cg.sema = sema;
	cg.out = out;
	cg.assembly = assembly;
	cg.diag = diag;
	Arena arena;
	arena_init(&arena);
	cg.bytes_used = ARENA_ARRAY(&arena, bool, sema_values(sema)->count);
	cg.constants_used = ARENA_ARRAY(&arena, bool, sema_values(sema)->object_count);
	cg.dependencies_seen = ARENA_ARRAY(&arena, bool, sema_values(sema)->count);
	for (FuncId id = 1; id < sema_func_count(sema); id++) {
		const SemaFunc *func = sema_func(sema, id);
		if (func->state == FUNC_ANALYSED && !func->is_extern)
			emit_function(&cg, func, id);
	}
	for (GlobalId id = 1; id < sema_global_count(sema); id++) {
		ValueId stored = sema_global(sema, id)->initializer;
		mark_value_dependencies(&cg, stored);
	}
	mark_constant_dependencies(&cg);
	const ValueTable *values = sema_values(sema);
	for (ConstObjectId id = 1; id < values->object_count; id++)
		if (cg.constants_used[id]) {
			char symbol[64];
			snprintf(symbol, sizeof(symbol), "szc.const.%u", (unsigned)id);
			emit_data_object(&cg, values->objects[id].value, symbol, true, 0, values->objects[id].alignment);
		}
	for (GlobalId id = 1; id < sema_global_count(sema); id++) {
		const SemaGlobal *global = sema_global(sema, id);
		cg.file = global->file;
		emit_data_object(&cg, global->initializer, global->symbol, global->is_const, global->src, global->alignment);
	}
	for (ValueId id = 1; id < values->count; id++) {
		if (!cg.bytes_used[id])
			continue;
		ByteString bytes = value_get(values, id)->as.bytes;
		fprintf(out, "section \".rodata\"\ndata $szc.bytes.%u = { ", (unsigned)id);
		for (uint32_t i = 0; i < bytes.length; i++)
			fprintf(out, "b %u, ", (unsigned)bytes.data[i]);
		fputs("b 0 }\n", out);
	}
	if (cg.explicit_panic_used) {
		fputs("section \".rodata\"\ndata $szc.panic.newline = { b 10 }\n", out);
		fputs("function $szc.panic.bytes(l %ptr, l %len) {\n@start\n", out);
		fputs("\tcall $write(w 2, l %ptr, l %len)\n", out);
		fputs("\tcall $write(w 2, l $szc.panic.newline, l 1)\n\tcall $abort()\n\thlt\n}\n\n", out);
	}
	emit_volatile_helpers(&cg);
	emit_panic_stubs(&cg);
	FuncId main_id = sema_main_func(sema);
	if (main_id != 0)
		emit_entry(&cg, sema_func(sema, main_id));
	arena_free(&arena);
	return !cg.failed;
}
