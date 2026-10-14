#ifndef SZC_SEMA_H
#define SZC_SEMA_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "arena.h"
#include "comptime.h"
#include "diag.h"
#include "intern.h"
#include "ir.h"
#include "source.h"
#include "tir.h"

/*
 * Semantic analysis (sema.c): untyped IR -> types, comptime values and typed
 * IR for each runtime function that is reachable from the entry point.
 */

/* ---------- Types ---------- */

typedef enum TypeKind {
	TYPE_VOID,
	TYPE_BOOL,
	TYPE_NORETURN,
	TYPE_TYPE,
	TYPE_COMPTIME_INT,
	TYPE_COMPTIME_FLOAT,
	TYPE_INT,
	TYPE_FLOAT,
	TYPE_POINTER,
	TYPE_ARRAY,
	TYPE_FN,
	TYPE_STRUCT,
	TYPE_ANYOPAQUE,
	TYPE_ANYERROR,
	TYPE_NULL,
	TYPE_UNDEFINED,
	TYPE_ENUM_LITERAL,
} TypeKind;

typedef enum PointerSize {
	POINTER_ONE,
	POINTER_MANY,
	POINTER_SLICE,
	POINTER_C,
} PointerSize;

typedef struct TypePointer {
	TypeId			child;
	PointerSize	size;
	uint32_t		alignment;	/* 0 = implicit ABI alignment */
	bool				is_const;
	bool				is_volatile;
	bool				is_allowzero;
} TypePointer;

typedef struct TypeArray {
	TypeId child;
	uint32_t length;
	bool sentinel_zero;
} TypeArray;

typedef enum FunctionConvention {
	FUNCTION_AUTO,
	FUNCTION_C,
	FUNCTION_INLINE,
} FunctionConvention;

typedef struct TypeFn {
	TypeId   *params;
	bool     *noalias;
	FunctionConvention convention;
	uint32_t param_count;
	TypeId   ret;
	bool     is_var_args;
	bool     is_generic;	/* has comptime or anytype parameters: params/ret are not known */
} TypeFn;

typedef struct SemaNamespace SemaNamespace;

typedef struct Type {
	TypeKind kind;
	union {
		struct {
			bool     is_signed;
			uint16_t bits;
		} integer;
		struct {
			uint16_t bits;
		} floating;
		TypePointer   pointer;
		TypeArray     array;
		TypeFn        fn;
		SemaNamespace *structure;	/* the struct's namespace (nominal type) */
	} as;
	StringId name;	/* for named types (structs), STRING_NONE otherwise */
} Type;

/*
 * Builtin types have fixed ids: for a well-known IR type ref such as
 * IR_REF_TYPE_U8, the TypeId is the ref's number. (The type table slots of the
 * well-known *value* refs are unused.)
 */
static inline TypeId
type_id_of_well_known(IrRef ref)
{
	return (TypeId)ref;
}

/* ---------- Functions ---------- */

typedef enum FuncState {
	FUNC_DECLARED,	/* type known, body not analysed */
	FUNC_QUEUED,	/* referenced at runtime: body will be analysed */
	FUNC_IN_PROGRESS,
	FUNC_ANALYSED,
	FUNC_FAILED,
} FuncState;

typedef struct SemaFunc {
	SemaNamespace *namespace_scope;
	const IrFile  *ir;
	IrInstIndex   func_inst;
	IrFuncHeader  header;
	TypeId        type;
	StringId      name;
	const char    *symbol;	/* linker symbol */
	bool          is_extern;
	bool          is_export;
	bool          is_generic;
	FuncState     state;
	TirFunc       tir;
} SemaFunc;

typedef struct SemaGlobal {
	TypeId type;
	ValueId initializer;
	const char *symbol;
	FileId file;
	uint32_t src;
} SemaGlobal;

typedef struct Sema Sema;

Sema *sema_create(Arena *arena, Diag *diag, Interner *interner, SourceTable *sources);

/*
 * Analyses `file` (already lowered to `ir`) as the root of an executable:
 * finds `main` and analyses every function reachable from it. Returns false
 * if errors were reported.
 */
bool sema_analyze_executable(Sema *sema, FileId file, const IrFile *ir);

/* Results for codegen. */
const Type       *sema_type(const Sema *sema, TypeId type);
TypeId            sema_void_type(const Sema *sema);
uint32_t          sema_global_count(const Sema *sema);
const SemaGlobal *sema_global(const Sema *sema, GlobalId global);
uint32_t          sema_func_count(const Sema *sema);
const SemaFunc   *sema_func(const Sema *sema, FuncId func);
FuncId            sema_main_func(const Sema *sema);
const ValueTable *sema_values(const Sema *sema);
const Interner   *sema_interner(const Sema *sema);

/* Size and alignment in bytes of a runtime type. */
uint64_t sema_type_size(const Sema *sema, TypeId type);
uint32_t sema_type_align(const Sema *sema, TypeId type);

/* Human-readable type name (for diagnostics and dumps). */
const char *sema_type_name(Sema *sema, TypeId type);

/* Prints the typed IR of every analysed function (`szc tir`). */
void sema_dump(FILE *out, Sema *sema);

#endif
