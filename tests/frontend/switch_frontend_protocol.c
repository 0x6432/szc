/* Standalone frontend-only checks. No sema or backend is linked. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"
#include "parser.h"

_Static_assert(sizeof(IrSwitch) == 8, "frozen switch header");
_Static_assert(sizeof(IrSwitchProng) == 16, "frozen switch prong");
_Static_assert(sizeof(IrSwitchItem) == 16, "frozen switch item");
_Static_assert(IR_SWITCH_ELSE == 1, "frozen else flag");
_Static_assert(IR_SWITCH_INDEX_CAPTURE == 2, "index capture flag");

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { \
 fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

typedef struct Fixture {
 Arena arena;
 SourceTable sources;
 Diag diag;
 Interner interner;
 IrFile *ir;
} Fixture;

static void init(Fixture *f, const char *text)
{
 arena_init(&f->arena);
 source_table_init(&f->sources, &f->arena);
 diag_init(&f->diag, &f->arena, &f->sources);
 intern_init(&f->interner, &f->arena);
 FileId id = source_add_memory(&f->sources, "switch_frontend.zig", text, (uint32_t)strlen(text));
 AstContainerDecl *root = parse_file(&f->arena, &f->diag, &f->sources, id);
 if (diag_has_errors(&f->diag)) diag_print(&f->diag, stderr);
 CHECK(!diag_has_errors(&f->diag));
 f->ir = irgen_file(&f->arena, &f->diag, &f->interner, source_get(&f->sources, id), id, root);
}

static const IrInst *last(const IrFile *ir, IrBody body)
{
 CHECK(body != 0 && body < ir->extra_count);
 uint32_t n = ir_body_count(ir, body);
 CHECK(n > 0 && n <= ir->extra_count - body - 1);
 IrInstIndex idx = ir_body_insts(ir, body)[n - 1];
 CHECK(idx < ir->inst_count);
 return &ir->insts[idx];
}

static unsigned count_op(const IrFile *ir, IrOp op)
{
 unsigned n = 0;
 for (uint32_t i = 0; i < ir->inst_count; i++) n += ir->insts[i].op == op;
 return n;
}

static void endpoint(const IrFile *ir, IrBody body, IrRef owner, uint32_t src)
{
 const IrInst *end = last(ir, body);
 CHECK(end->op == IR_BREAK_INLINE && end->data.brk.block == IR_BODY_RESULT);
 const IrInst *first = &ir->insts[ir_body_insts(ir, body)[0]];
 CHECK(first->op == IR_SWITCH_TYPE || first->op == IR_SWITCH_UNDERSCORE);
 if (first->op == IR_SWITCH_UNDERSCORE) {
  CHECK(ir_body_count(ir, body) == 2);
  CHECK(end->data.brk.operand == ir_ref_of_inst(ir_body_insts(ir, body)[0]));
 }
 CHECK(first->data.un.operand == owner && first->src == src);
 for (uint32_t i = 0; i < ir_body_count(ir, body); i++)
  CHECK(ir->insts[ir_body_insts(ir, body)[i]].op != IR_SWITCH_CAPTURE);
}

/* Validate exact record walk, owner-qualified pseudo ops, sources, and body order. */
static void protocol(const Fixture *f)
{
 const IrFile *ir = f->ir;
 for (uint32_t i = 0; i < ir->inst_count; i++) {
  const IrInst *inst = &ir->insts[i];
  if (inst->op != IR_SWITCH) continue;
  IrRef owner = ir_ref_of_inst(i);
  IrSwitch h;
  IR_READ(ir, inst->data.payload, &h);
  if (ir_ref_is_inst(h.operand)) CHECK(ir_ref_inst(h.operand) < i);
  uint32_t cursor = inst->data.payload + IR_WORDS(IrSwitch);
  for (uint32_t p = 0; p < h.prong_count; p++) {
   CHECK(cursor + IR_WORDS(IrSwitchProng) <= ir->extra_count);
   IrSwitchProng prong;
   IR_READ(ir, cursor, &prong);
   cursor += IR_WORDS(IrSwitchProng);
   CHECK((prong.flags & ~(IR_SWITCH_ELSE | IR_SWITCH_INDEX_CAPTURE)) == 0);
   CHECK((prong.item_count == 0) == ((prong.flags & IR_SWITCH_ELSE) != 0));
   for (uint32_t j = 0; j < prong.item_count; j++) {
    CHECK(cursor + IR_WORDS(IrSwitchItem) <= ir->extra_count);
    IrSwitchItem item;
    IR_READ(ir, cursor, &item);
    cursor += IR_WORDS(IrSwitchItem);
    endpoint(ir, item.low_body, owner, item.src);
    if (item.high_body) endpoint(ir, item.high_body, owner, item.end_src);
    else CHECK(item.src == item.end_src);
   }
   const IrInst *end = last(ir, prong.body);
   CHECK(end->op == IR_BREAK_INLINE || end->op == IR_RET || end->op == IR_UNREACHABLE || end->op == IR_BREAK);
   if (end->op == IR_BREAK_INLINE) CHECK(end->data.brk.block == IR_BODY_RESULT);
   for (uint32_t j = 0; j < ir_body_count(ir, prong.body); j++) {
    IrInstIndex idx = ir_body_insts(ir, prong.body)[j];
    CHECK(idx > i);
    if (ir->insts[idx].op == IR_SWITCH_INDEX_VALUE) {
     CHECK(prong.flags & IR_SWITCH_INDEX_CAPTURE);
     CHECK(j > 0 && ir->insts[idx].src == prong.src);
     CHECK(ir->insts[idx].data.un.operand == owner);
    }
    if (ir->insts[idx].op == IR_SWITCH_CAPTURE) {
     CHECK(j == 0);
     CHECK(ir->insts[idx].data.un.operand == owner);
    }
   }
  }
 }
 /* Every pseudo-operation has a real switch owner, including nested switches. */
 for (uint32_t i = 0; i < ir->inst_count; i++) {
  const IrInst *inst = &ir->insts[i];
  if (inst->op == IR_BREAK) {
   CHECK(inst->data.brk.block < ir->inst_count);
   IrOp target = ir->insts[inst->data.brk.block].op;
   CHECK(target == IR_BLOCK || target == IR_LOOP);
  }
  if (inst->op == IR_INT_CAST) {
   CHECK(ir_ref_is_inst(inst->data.bin.lhs));
   IrOp context = ir->insts[ir_ref_inst(inst->data.bin.lhs)].op;
   CHECK(context == IR_RET_TYPE || context == IR_ELEM_TYPE);
  }
  if (inst->op != IR_SWITCH_TYPE && inst->op != IR_SWITCH_CAPTURE && inst->op != IR_SWITCH_UNDERSCORE && inst->op != IR_SWITCH_INDEX_VALUE) continue;
  CHECK(ir_ref_is_inst(inst->data.un.operand));
  IrInstIndex owner = ir_ref_inst(inst->data.un.operand);
  CHECK(owner < i && ir->insts[owner].op == IR_SWITCH);
 }
 /* All new ops must be visible to the actual dumper (which lives in irgen.c). */
 FILE *out = tmpfile();
 CHECK(out != NULL);
 ir_dump(out, ir, &f->interner);
 CHECK(ftell(out) > 0);
 fclose(out);
}

int main(void)
{
 const struct { const char *source; unsigned switches, captures, types; } valid[] = {
  {"fn f(x: u8) u8 { return switch (x) { 1, 2, => |v| v, 3...7 => |v| v, else => |v| v }; }", 1, 3, 4},
  {"const E = enum { a, b }; fn f(x: E) E { return switch (x) { .a => |v| v, .b => .b }; }", 1, 1, 2},
  {"fn f(x: u8) u8 { return switch (x) { @enumFromInt(1) => @intCast(x), else => @intCast(x) }; }", 1, 0, 1},
  {"fn f(x: u8) u8 { return switch (x) { 1 => |v| switch (v) { 1 => |inner| inner, else => v }, else => 0 }; }", 2, 2, 2},
  {"fn f(x: u8) u8 { return switch (x) { 1 => |v| blk: { defer _ = v; break :blk v; }, else => return 0 }; }", 1, 1, 1},
  {"fn f(x: u8) void { while (true) { defer _ = x; switch (x) { 1 => continue, else => break } } }", 1, 0, 1},
  {"fn f(x: u8) u8 { return switch (x) { 1 => |@\"_\"| @\"_\", else => 0 }; }", 1, 1, 1},
  {"fn f(x: u8) u8 { return switch (x) {}; }", 1, 0, 0},
  {"fn f(x: u8) u8 { return switch (x) { 1 => |v| (&v).*, else => 0 }; }", 1, 1, 1},
  {"fn f(x: u8) u8 { return switch (x) { 1 => |v| blk: { defer _ = v; break :blk 1; }, else => 0 }; }", 1, 1, 1},
  {"const E = enum { @\"_\", a }; fn f(x: E) u8 { return switch (x) { .@\"_\" => 1, .a => 2 }; }", 1, 0, 2},
  {"fn f(x: u128) u8 { return switch (x) { 0...0xffffffffffffffffffffffffffffffff => 1 }; }", 1, 0, 2},
  /* Semantic errors are deliberately not consumed by the frontend. */
  {"fn f(x: u8) u8 { return switch (x) { 9...1 => @as(u8, false), 9 => 1, 9 => 2, else => 3, else => 4 }; }", 1, 0, 4},
  {"fn f(x: bool) u8 { return switch (x) { true => 1, else => 2 }; }", 1, 0, 1},
  {"fn get() u8 { return 1; } fn f() u8 { return switch (get()) { 1 => 2, else => 3 }; }", 1, 0, 1},
  {"fn f(x: u8) void { var y: u8 = 0; y = switch (x) { 1 => @intCast(x), else => 2 }; }", 1, 0, 1},
  {"fn f(x: u8) [1]u8 { return switch (x) { 1 => .{x}, else => .{0} }; }", 1, 0, 1},
 };
 for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
  Fixture f; init(&f, valid[i].source);
  if (diag_has_errors(&f.diag)) diag_print(&f.diag, stderr);
  CHECK(!diag_has_errors(&f.diag));
  CHECK(count_op(f.ir, IR_SWITCH) == valid[i].switches);
  CHECK(count_op(f.ir, IR_SWITCH_CAPTURE) == valid[i].captures);
  CHECK(count_op(f.ir, IR_SWITCH_TYPE) == valid[i].types);
  if (strstr(valid[i].source, "fn get()") != NULL) CHECK(count_op(f.ir, IR_CALL) == 1);
  if (strstr(valid[i].source, "while (true)") != NULL) {
   CHECK(count_op(f.ir, IR_DEFER) == 3); /* continue, break, lexical fallthrough */
   for (uint32_t k = 0; k < f.ir->inst_count; k++) {
    if (f.ir->insts[k].op != IR_SWITCH) continue;
    uint32_t cursor = f.ir->insts[k].data.payload + IR_WORDS(IrSwitch);
    for (uint32_t p = 0; p < 2; p++) {
     IrSwitchProng prong; IR_READ(f.ir, cursor, &prong);
     cursor += IR_WORDS(IrSwitchProng) + prong.item_count * IR_WORDS(IrSwitchItem);
     uint32_t n = ir_body_count(f.ir, prong.body);
     CHECK(n == 2);
     CHECK(f.ir->insts[ir_body_insts(f.ir, prong.body)[n - 2]].op == IR_DEFER);
     CHECK(last(f.ir, prong.body)->op == IR_BREAK);
    }
   }
  }
  protocol(&f);
  arena_free(&f.arena);
 }
 /* Force repeated instruction/extra reallocation; preserve record offsets and owners. */
 char *many = malloc(65536);
 CHECK(many != NULL);
 size_t used = (size_t)snprintf(many, 65536, "fn f(x: u32) u32 { return switch (x) { ");
 for (unsigned i = 0; i < 512; i++) {
  int n = snprintf(many + used, 65536 - used, "%u, %u...%u => |v| v,", i, i + 1000, i + 2000);
  CHECK(n > 0 && (size_t)n < 65536 - used);
  used += (size_t)n;
 }
 CHECK(snprintf(many + used, 65536 - used, "else => 0 }; }") > 0);
 Fixture stress; init(&stress, many);
 CHECK(!diag_has_errors(&stress.diag));
 CHECK(count_op(stress.ir, IR_SWITCH) == 1);
 CHECK(count_op(stress.ir, IR_SWITCH_TYPE) == 1536);
 CHECK(count_op(stress.ir, IR_SWITCH_CAPTURE) == 512);
 protocol(&stress);
 arena_free(&stress.arena);
 free(many);
 const struct { const char *source, *message, *at; } invalid[] = {
  {"fn f(x: u8) u8 { return switch (x) { 1 => |_| 4, else => 0 }; }", "discard of capture; omit it instead", "_|"},
  {"fn f(x: u8) u8 { return switch (x) { 1...3 => |_| 4, else => 0 }; }", "discard of capture; omit it instead", "_|"},
  {"fn f(x: u8) u8 { return switch (x) { else => |_| 4 }; }", "discard of capture; omit it instead", "_|"},
  {"fn f() u8 { return switch (@as(u8, 0)) { 1 => |_| 4, else => 0 }; }", "discard of capture; omit it instead", "_|"},
  {"fn f(x: u8) u8 { return switch (x) { 1 => |unused| 4, else => 0 }; }", "unused capture", "unused"},
  {"fn f() u8 { return switch (@as(u8, 0)) { 1 => |unused| 4, else => 0 }; }", "unused capture", "unused"},
  {"fn f(x: u8) u8 { return label: switch (x) { else => 1 }; }", "not supported by szc yet: labeled switch", "label"},
  {"fn f(x: u8) u8 { return switch (x) { inline 1 => 2, else => 1 }; }", "not supported by szc yet: inline switch prong", "inline"},
  {"fn f(x: u8) u8 { return switch (x) { 1 => |*ptr| ptr.*, else => 0 }; }", "not supported by szc yet: switch pointer capture", "ptr"},
  {"fn f(x: u8) u8 { return switch (x) { 1 => |v, idx| v, else => 0 }; }", "unused switch tag capture", "idx"},
  {"fn f(x: u8) u8 { return switch (x) { 1 => |x| x, else => 0 }; }", "capture 'x' shadows function parameter from outer scope", "|x"},
  {"fn f(x: u8) u8 { return switch (x) { 1 => |v| { v = 2; }, else => 0 }; }", "cannot assign to constant", "v ="},
  {"fn f(x: u8) u8 { return switch (x) { v => |v| v, else => 0 }; }", "use of undeclared identifier 'v'", "v =>"},
  {"fn f(x: u8) u8 { return switch (x) { 1 => |v| v, else => v }; }", "use of undeclared identifier 'v'", "v };"},
  {"fn f(x: u8) void { switch (x) { else => break } }", "break expression outside loop", "break"},
  {"fn f(x: u8) void { switch (x) { else => continue } }", "continue expression outside loop", "continue"},
  {"fn f(x: u8) u8 { return switch (x) { @inComptime() => 1, else => 0 }; }", "redundant", "@inComptime"},
  {"fn f(x: u8) u8 { return switch (x) { 1...@inComptime() => 1, else => 0 }; }", "redundant", "@inComptime"},
  {"fn f(x: u8) u8 { return switch (x) { _ => 1, else => 0 }; }", "'_' prong only allowed when switching on non-exhaustive enums", "_ =>"},
  {"fn f(x: u8) u8 { return switch (x) { 1 => 1, else => missing }; }", "use of undeclared identifier 'missing'", "missing"},
 };
 for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
  Fixture f; init(&f, invalid[i].source);
  const char *at = strstr(invalid[i].source, invalid[i].at);
  CHECK(at != NULL);
  uint32_t offset = (uint32_t)(at - invalid[i].source);
  if (strcmp(invalid[i].at, "|x") == 0) offset++;
  bool underscore = strcmp(invalid[i].at, "_ =>") == 0;
  if (underscore) {
   /* These remain language-invalid: sema now owns the exact primary above.
    * Preserve each original source and assert its complete deferred protocol. */
   CHECK(!diag_has_errors(&f.diag));
   CHECK(count_op(f.ir, IR_SWITCH) == 1);
   CHECK(count_op(f.ir, IR_SWITCH_UNDERSCORE) == (unsigned)underscore);
   for (uint32_t k = 0; k < f.ir->inst_count; k++) {
    const IrInst *inst = &f.ir->insts[k];
    if (inst->op == IR_SWITCH_UNDERSCORE) CHECK(inst->src == offset);
    if (inst->op != IR_SWITCH) continue;
    IrSwitchProng first;
    IR_READ(f.ir, inst->data.payload + IR_WORDS(IrSwitch), &first);
    CHECK(first.flags == 0);
   }
   protocol(&f);
  } else {
   CHECK(diag_has_errors(&f.diag));
   if (strstr(f.diag.items[0].message, invalid[i].message) == NULL) {
    fprintf(stderr, "diagnostic fixture %zu: expected %s\n", i, invalid[i].message);
    diag_print(&f.diag, stderr);
   }
   CHECK(strstr(f.diag.items[0].message, invalid[i].message) != NULL);
   CHECK(f.diag.items[0].offset == offset);
  }
  arena_free(&f.arena);
 }
 printf("switch frontend: %u checks, %zu valid and %zu negative fixtures (19 lexical, 1 deferred semantic) + 512-prong stress passed (no sema linked)\n", checks,
  sizeof(valid) / sizeof(valid[0]), sizeof(invalid) / sizeof(invalid[0]));
 return 0;
}
