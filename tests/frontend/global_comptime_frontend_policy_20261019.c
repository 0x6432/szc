/* Namespace-only ignored-result generation; no sema/backend linked. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir.h"
#include "parser.h"

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

static void init(Fixture *f, const char *source)
{
 arena_init(&f->arena);
 source_table_init(&f->sources, &f->arena);
 diag_init(&f->diag, &f->arena, &f->sources);
 intern_init(&f->interner, &f->arena);
 FileId file = source_add_memory(&f->sources, "namespace_policy.zig", source, (uint32_t)strlen(source));
 AstContainerDecl *root = parse_file(&f->arena, &f->diag, &f->sources, file);
 if (diag_has_errors(&f->diag)) diag_print(&f->diag, stderr);
 CHECK(!diag_has_errors(&f->diag));
 f->ir = irgen_file(&f->arena, &f->diag, &f->interner, source_get(&f->sources, file), file, root);
 if (diag_has_errors(&f->diag)) diag_print(&f->diag, stderr);
 CHECK(!diag_has_errors(&f->diag));
}

static unsigned ensure_count(const IrFile *ir, uint32_t source_length)
{
 unsigned count = 0;
 for (uint32_t i = 0; i < ir->inst_count; i++) {
  const IrInst *inst = &ir->insts[i];
  if (inst->op != IR_ENSURE_RESULT) continue;
  count++;
  CHECK(inst->src < source_length);
  IrRef operand = inst->data.un.operand;
  if (ir_ref_is_inst(operand)) CHECK(ir_ref_inst(operand) < i);
 }
 return count;
}

int main(void)
{
 const struct { const char *name, *source; unsigned ensures; } cases[] = {
  {"direct", "comptime{@as(u8,3);}",1},
  {"nested block", "comptime{{@as(u8,3);}}",1},
  {"nested if", "comptime{if(true){@as(u8,3);}}",2},
  {"unselected if still generated", "comptime{if(false){@as(u8,3);}}",2},
  {"if consumed result", "comptime{const x:u8=if(true) 3 else 4;_=x;}",0},
  {"if explicit discard", "comptime{_=if(true) @as(u8,3) else @as(u8,4);}",0},
  {"switch nested block", "comptime{switch(@as(u8,0)){0=>{@as(u8,3);},else=>{}}}",2},
  {"switch consumed result", "comptime{const x:u8=switch(@as(u8,0)){0=>3,else=>4};_=x;}",0},
  {"switch explicit discard", "comptime{_=switch(@as(u8,0)){0=>@as(u8,3),else=>@as(u8,4)};}",0},
  {"labeled consumed result", "comptime{const x:u8=blk:{break :blk @as(u8,3);};_=x;}",0},
  {"labeled internal statement", "comptime{const x:u8=blk:{@as(u8,3);break :blk 4;};_=x;}",1},
  {"labeled ignored result", "comptime{blk:{break :blk @as(u8,3);}}",1},
  {"explicit discard", "comptime{_= @as(u8,3);}",0},
  {"defer direct", "comptime{defer @as(u8,3);}",1},
  {"defer nested", "comptime{defer {@as(u8,3);}}",2},
  {"defer selected if", "comptime{defer {if(true){@as(u8,3);}}}",3},
  {"defer explicit discard retains old void check", "comptime{defer {_= @as(u8,3);}}",1},
  {"break cleanup", "comptime{blk:{defer {@as(u8,3);} break :blk;}}",3},
  {"void", "comptime{{}}",0},
  {"fresh function", "comptime{const N=struct{fn f()void{@as(u8,3);}};_=N;}",1},
  {"fresh function return", "comptime{const N=struct{fn f()u8{return @as(u8,3);}};_=N;}",0},
  {"fresh test body", "comptime{const N=struct{test{ @as(u8,3); }};_=N;}",1},
  {"fresh named initializer", "comptime{const N=struct{const x=blk:{@as(u8,3);break :blk 4;};};_=N;}",1},
  {"nested namespace seed", "comptime{const N=struct{comptime{@as(u8,3);}};_=N;}",1},
  {"nested namespace with fresh function", "comptime{const N=struct{comptime{@as(u8,3);}fn f()void{@as(u8,4);}};_=N;}",2},
  {"runtime ignored statement now checked", "fn f()void{@as(u8,3);}",1},
  {"named initializer ignored statement now checked", "const x=blk:{@as(u8,3);break :blk 4;};",1},
  {"runtime defer old behavior unchanged", "fn f()void{defer {@as(u8,3);}}",2},
 };
 for (size_t c = 0; c < sizeof(cases)/sizeof(cases[0]); c++) {
  Fixture f; init(&f,cases[c].source);
  unsigned count = ensure_count(f.ir, (uint32_t)strlen(cases[c].source));
  if (count != cases[c].ensures)
   fprintf(stderr,"%s: ensures %u != %u\n",cases[c].name,count,cases[c].ensures);
  CHECK(count == cases[c].ensures);
  if (c == 0) {
   for (uint32_t i = 0; i < f.ir->inst_count; i++) {
    if (f.ir->insts[i].op != IR_ENSURE_RESULT) continue;
    CHECK(f.ir->insts[i].src == (uint32_t)(strstr(cases[c].source,"@as")-cases[c].source));
    CHECK(f.ir->insts[ir_ref_inst(f.ir->insts[i].data.un.operand)].op == IR_AS);
   }
  }
  FILE *out=tmpfile(); CHECK(out != NULL);
  ir_dump(out,f.ir,&f.interner); CHECK(ftell(out)>0); fclose(out);
  arena_free(&f.arena);
 }
 printf("namespace result policy: %u checks, %zu structural cases passed (frontend only)\n",checks,sizeof(cases)/sizeof(cases[0]));
 return 0;
}
