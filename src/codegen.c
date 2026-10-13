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
	PANIC_COUNT,
} PanicKind;

typedef struct Codegen {
	Sema					*sema;
	FILE					*out;
	Diag					*diag;
	const SemaFunc	*func;
	const TirFunc		*tir;
	bool					failed;
	bool					panic_used[PANIC_COUNT];
} Codegen;

static void
codegen_error(Codegen *cg, uint32_t src, const char *message)
{
	cg->failed = true;
	diag_report(cg->diag, DIAG_ERROR, cg->func->ir->file, src, "%s", message);
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

/* Brings %tN of integer type `type` back into canonical form. */
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
	bool is_add = inst->op == TIR_ADD;
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

static const char *
load_op(Codegen *cg, TypeId type)
{
	uint64_t size = sema_type_size(cg->sema, type);
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
	switch (sema_type_size(cg->sema, type)) {
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
	case TIR_ARG:
	case TIR_CONST:
	case TIR_ALLOC:
		/* parameters, inline immediates, and allocations hoisted to @start */
		return;
	case TIR_ADD:
	case TIR_SUB:
	case TIR_MUL:
	case TIR_ADD_WRAP:
	case TIR_SUB_WRAP:
	case TIR_MUL_WRAP: {
		bool is_mul = inst->op == TIR_MUL || inst->op == TIR_MUL_WRAP;
		bool checked = inst->op == TIR_ADD || inst->op == TIR_SUB || inst->op == TIR_MUL;
		const char *op = is_mul ? "mul" : inst->op == TIR_ADD || inst->op == TIR_ADD_WRAP ? "add" : "sub";
		if (checked && is_mul)
			emit_mul_overflow(cg, ref, cls);
		emit_binary(cg, ref, cls, op, inst->data.bin.lhs, inst->data.bin.rhs);
		normalize(cg, ref, inst->type, cls);
		if (checked) {
			if (!is_mul)
				emit_add_sub_overflow(cg, ref, cls);
			emit_guard(cg, ref, "overflow", PANIC_OVERFLOW);
		}
		return;
	}
	case TIR_SHL:
		emit_bound_check(cg, ref, inst->data.bin.rhs, "shift", "cuge", type_bits(cg, inst->type), PANIC_SHIFT);
		emit_binary(cg, ref, cls, "shl", inst->data.bin.lhs, inst->data.bin.rhs);
		normalize(cg, ref, inst->type, cls);
		return;
	case TIR_DIV_TRUNC:
		emit_bound_check(cg, ref, inst->data.bin.rhs, "zero", "ceq", 0, PANIC_DIV_ZERO);
		emit_binary(cg, ref, cls, type_is_signed(cg, inst->type) ? "div" : "udiv", inst->data.bin.lhs, inst->data.bin.rhs);
		return;
	case TIR_REM:
		emit_bound_check(cg, ref, inst->data.bin.rhs, "zero", "ceq", 0, PANIC_DIV_ZERO);
		emit_binary(cg, ref, cls, type_is_signed(cg, inst->type) ? "rem" : "urem", inst->data.bin.lhs, inst->data.bin.rhs);
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
	case TIR_LOAD:
		if (cls == QBE_NONE)
			return;
		print_dest(cg, ref, cls);
		fprintf(out, "%s ", load_op(cg, inst->type));
		print_operand(cg, inst->data.un.operand);
		fputs("\n", out);
		return;
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
		fprintf(out, "call $%s(", sema_func(cg->sema, call.callee)->symbol);
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
		return;
	}
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
	case TIR_UNREACHABLE:
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
emit_function(Codegen *cg, const SemaFunc *func)
{
	cg->func = func;
	cg->tir = &func->tir;
	FILE *out = cg->out;
	const TypeFn *fn = &sema_type(cg->sema, func->type)->as.fn;
	uint32_t src = func->ir->insts[func->func_inst].src;
	QbeClass ret_class = type_class(cg, fn->ret, src);
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
		TypeId child = sema_type(cg->sema, inst->type)->as.pointer.child;
		uint64_t size = sema_type_size(cg->sema, child);
		uint32_t align = sema_type_align(cg->sema, child);
		uint32_t slot_align = align <= 4 ? 4 : align <= 8 ? 8 : 16;
		fprintf(out, "\t%%t%u =l alloc%u %" PRIu64 "\n", (unsigned)ref, (unsigned)slot_align, size == 0 ? 1 : size);
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

bool
codegen_qbe(FILE *out, Sema *sema, Diag *diag)
{
	Codegen cg;
	memset(&cg, 0, sizeof(cg));
	cg.sema = sema;
	cg.out = out;
	cg.diag = diag;
	for (FuncId id = 1; id < sema_func_count(sema); id++) {
		const SemaFunc *func = sema_func(sema, id);
		if (func->state == FUNC_ANALYSED && !func->is_extern)
			emit_function(&cg, func);
	}
	emit_panic_stubs(&cg);
	FuncId main_id = sema_main_func(sema);
	if (main_id != 0)
		emit_entry(&cg, sema_func(sema, main_id));
	return !cg.failed;
}
