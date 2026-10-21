/* Inspect every allocated TIR instruction, including unreachable/failed-body
 * instructions. Run sema only: never codegen or execute the Zig fixture.
 * Direct roots plus immutable array-child edges are covered; this is NOT an
 * exhaustive arbitrary pointer/struct graph walk or an ABI/layout proof. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "arena.h"
#include "diag.h"
#include "source.h"
#include "parser.h"
#include "ir.h"
#include "sema.h"

static TypeKind leaf_kind(const Sema *sema, TypeId id)
{
    const Type *type = sema_type(sema, id);
    while (type->kind == TYPE_ARRAY)
        type = sema_type(sema, type->as.array.child);
    return type->kind;
}

static bool blocked(const Sema *sema, TypeId id)
{
    TypeKind kind = leaf_kind(sema, id);
    return kind == TYPE_ANYERROR || kind == TYPE_ERROR_SET
        || kind == TYPE_ERROR_UNION || kind == TYPE_OPTIONAL;
}

static bool anyerror_signature(const Sema *sema, TypeId id)
{
    const Type *type = sema_type(sema, id);
    if (type->kind != TYPE_FN) return false;
    if (leaf_kind(sema, type->as.fn.ret) == TYPE_ANYERROR) return true;
    for (uint32_t i = 0; i < type->as.fn.param_count; i++)
        if (leaf_kind(sema, type->as.fn.params[i]) == TYPE_ANYERROR) return true;
    return false;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    Arena arena;
    arena_init(&arena);
    SourceTable sources;
    source_table_init(&sources, &arena);
    Diag diag;
    diag_init(&diag, &arena, &sources);
    FileId file;
    if (!source_load_file(&sources, argv[1], &file)) {
        arena_free(&arena);
        return 2;
    }
    AstContainerDecl *root = parse_file(&arena, &diag, &sources, file);
    Interner interner;
    intern_init(&interner, &arena);
    bool frontend_ok = !diag_has_errors(&diag);
    IrFile *ir = frontend_ok ? irgen_file(&arena, &diag, &interner,
        source_get(&sources, file), file, root) : NULL;
    frontend_ok = frontend_ok && !diag_has_errors(&diag);
    Sema *sema = sema_create(&arena, &diag, &interner, &sources);
    bool ok = frontend_ok && sema_analyze_executable(sema, file, ir);
    uint32_t any_values = 0, any_tir = 0, any_slots = 0, any_backing = 0;
    uint32_t bad_tir = 0, bad_slots = 0, bad_backing = 0, bad_globals = 0;
    uint32_t allocations = 0, stores = 0, calls = 0, sink_calls = 0;
    uint32_t invalid_args = 0, active_signatures = 0;
    const ValueTable *values = sema_values(sema);
    for (uint32_t i = 1; i < values->count; i++)
        any_values += leaf_kind(sema, value_get(values, i)->type) == TYPE_ANYERROR;
    for (uint32_t i = 1; i < values->object_count; i++) {
        TypeId type = value_get(values, values->objects[i].value)->type;
        any_backing += leaf_kind(sema, type) == TYPE_ANYERROR;
        bad_backing += blocked(sema, type);
    }
    for (uint32_t i = 1; i < sema_global_count(sema); i++)
        bad_globals += blocked(sema, sema_global(sema, i)->type);
    for (uint32_t i = 1; i < sema_func_count(sema); i++) {
        const SemaFunc *function = sema_func(sema, i);
        if (function->state != FUNC_DECLARED && function->state != FUNC_FAILED)
            active_signatures += anyerror_signature(sema, function->type);
        const TirFunc *tir = &function->tir;
        for (uint32_t j = 0; j < tir->inst_count; j++) {
            const TirInst *inst = &tir->insts[j];
            any_tir += leaf_kind(sema, inst->type) == TYPE_ANYERROR;
            bad_tir += blocked(sema, inst->type);
            if (inst->op == TIR_ALLOC) {
                allocations++;
                const Type *type = sema_type(sema, inst->type);
                if (type->kind == TYPE_POINTER) {
                    any_slots += leaf_kind(sema, type->as.pointer.child) == TYPE_ANYERROR;
                    bad_slots += blocked(sema, type->as.pointer.child);
                }
            }
            stores += inst->op == TIR_STORE;
            if (inst->op == TIR_CALL) {
                calls++;
                uint32_t payload = inst->data.payload;
                if (payload > tir->extra_count || TIR_WORDS(TirCall) > tir->extra_count - payload) {
                    invalid_args++;
                    continue;
                }
                TirCall call;
                memcpy(&call, &tir->extra[payload], sizeof call);
                if (call.callee && call.callee < sema_func_count(sema)) {
                    const char *symbol = sema_func(sema, call.callee)->symbol;
                    sink_calls += symbol != NULL && strcmp(symbol, "sink") == 0;
                }
                uint32_t args = payload + TIR_WORDS(TirCall);
                if (call.arg_count > tir->extra_count - args) {
                    invalid_args++;
                    continue;
                }
                for (uint32_t k = 0; k < call.arg_count; k++)
                    invalid_args += tir->extra[args + k] == TIR_REF_NONE
                        || tir->extra[args + k] >= tir->inst_count;
                if (call.callee == 0)
                    invalid_args += call.indirect == TIR_REF_NONE || call.indirect >= tir->inst_count;
            }
        }
    }
    diag_print(&diag, stderr);
    printf("{\"frontend_ok\":%s,\"sema_ok\":%s,\"errors\":%u,"
        "\"anyerror_comptime_values\":%u,\"anyerror_tir_values\":%u,"
        "\"anyerror_allocations\":%u,\"anyerror_backing_objects\":%u,"
        "\"blocked_tir_values\":%u,\"blocked_allocations\":%u,"
        "\"blocked_backing_objects\":%u,\"blocked_globals\":%u,"
        "\"active_anyerror_signatures\":%u,\"allocations\":%u,\"stores\":%u,"
        "\"calls\":%u,\"sink_calls\":%u,\"invalid_call_arguments\":%u,"
        "\"backing_objects\":%u,\"runtime_globals\":%u}\n",
        frontend_ok ? "true" : "false", ok ? "true" : "false", diag.error_count,
        any_values, any_tir, any_slots, any_backing, bad_tir, bad_slots,
        bad_backing, bad_globals, active_signatures, allocations, stores, calls,
        sink_calls, invalid_args, (values->object_count ? values->object_count - 1 : 0),
        (sema_global_count(sema) ? sema_global_count(sema) - 1 : 0));
    arena_free(&arena);
    return 0;
}
