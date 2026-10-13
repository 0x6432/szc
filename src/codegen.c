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

typedef struct Codegen {
	Sema					*sema;
	FILE					*out;
	Diag					*diag;
	const SemaFunc	*func;
	const TirFunc		*tir;
	bool					failed;
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
	if (cls == QBE_NONE && sema_type(cg->sema, inst->type)->kind == TYPE_INT && inst->op != TIR_CALL)
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
	case TIR_SHL: {
		const char *op = inst->op == TIR_ADD ? "add" : inst->op == TIR_SUB ? "sub" : inst->op == TIR_MUL ? "mul" : "shl";
		emit_binary(cg, ref, cls, op, inst->data.bin.lhs, inst->data.bin.rhs);
		normalize(cg, ref, inst->type, cls);
		return;
	}
	case TIR_DIV_TRUNC:
		emit_binary(cg, ref, cls, type_is_signed(cg, inst->type) ? "div" : "udiv", inst->data.bin.lhs, inst->data.bin.rhs);
		return;
	case TIR_REM:
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
	FuncId main_id = sema_main_func(sema);
	if (main_id != 0)
		emit_entry(&cg, sema_func(sema, main_id));
	return !cg.failed;
}
