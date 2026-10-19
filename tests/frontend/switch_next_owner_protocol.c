/* Extends, never filters, the complete existing frontend-only protocol suite. */
int switch_original_protocol_main(void);
#define main switch_original_protocol_main
#include "switch_frontend_protocol.c"
#undef main

int main(void)
{
 CHECK(switch_original_protocol_main() == 0);
 const struct { const char *source; unsigned flagged, underscore; } deferred[] = {
  {"fn f(x:u8)u8{return switch(x){1=>|v,i| v+i,else=>0};}",1,0},
  {"fn f(x:u8)u8{return switch(x){else=>|v,i| v+i};}",1,0},
  {"fn f()u8{return switch(@as(u8,1)){1=>|v,i| v+i,else=>0};}",1,0},
  {"fn f()u8{return switch(@as(u8,0)){1=>|v,i| v+i,else=>0};}",1,0},
  {"fn f()u8{return switch(@as(u8,0)){0=>0,else=>|v,i| v+i};}",1,0},
  {"fn f(x:u8)u8{return switch(x){1=>|v,i| switch(v){_=>i,else=>0},else=>0};}",1,1},
  {"fn f(x:u8)u8{return switch(x){1=>|v,i| switch(i){1=>v,else=>0},else=>0};}",1,0},
  {"fn f()u8{return switch(@as(u8,0)){1=>|v,@\"_\"| v+@\"_\",else=>0};}",1,0},
  {"const E=enum(u8){a,b};fn f(x:E)u8{return switch(x){_=>1,else=>0};}",0,1},
  {"const E=enum(u8){@\"_\",a};fn f(x:E)u8{return switch(x){.@\"_\"=>1,.a=>0};}",0,0},
  {"fn f(x:u8)u8{const @\"_\":u8=1;return switch(x){@\"_\"=>1,else=>0};}",0,0},
 };
 for (size_t c = 0; c < sizeof(deferred)/sizeof(deferred[0]); c++) {
  Fixture f; init(&f, deferred[c].source);
  if (diag_has_errors(&f.diag)) diag_print(&f.diag, stderr);
  CHECK(!diag_has_errors(&f.diag));
  CHECK(count_op(f.ir, IR_SWITCH_INDEX_VALUE) == deferred[c].flagged);
  CHECK(count_op(f.ir, IR_SWITCH_UNDERSCORE) == deferred[c].underscore);
  unsigned flags = 0;
  for (uint32_t i = 0; i < f.ir->inst_count; i++) {
   const IrInst *inst = &f.ir->insts[i];
   if (inst->op == IR_SWITCH_UNDERSCORE) CHECK(deferred[c].source[inst->src] == '_');
   if (inst->op != IR_SWITCH) continue;
   IrSwitch header; IR_READ(f.ir, inst->data.payload, &header);
   uint32_t cursor = inst->data.payload + IR_WORDS(IrSwitch);
   for (uint32_t p = 0; p < header.prong_count; p++) {
    IrSwitchProng prong; IR_READ(f.ir, cursor, &prong);
    cursor += IR_WORDS(IrSwitchProng) + prong.item_count * IR_WORDS(IrSwitchItem);
    CHECK(prong.body != 0); /* even known-unselected invalid-index arms */
    if (!(prong.flags & IR_SWITCH_INDEX_CAPTURE)) continue;
    flags++;
    unsigned values = 0;
    for (uint32_t j = 0; j < ir_body_count(f.ir, prong.body); j++) {
     const IrInst *value = &f.ir->insts[ir_body_insts(f.ir, prong.body)[j]];
     if (value->op != IR_SWITCH_INDEX_VALUE) continue;
     values++;
     CHECK(value->src == prong.src);
     CHECK(value->data.un.operand == ir_ref_of_inst(i));
    }
    CHECK(values == 1);
   }
  }
  CHECK(flags == deferred[c].flagged);
  protocol(&f);
  arena_free(&f.arena);
 }
 /* Scope controls: do not change unrelated generic shadow diagnostics. */
 const char *controls[] = {
  "fn f(x:u8)u8{const x:u8=1;return x;}",
  "fn f(x:u8)u8{const y:u8=1;return switch(x){1=>|y| y,else=>0};}",
 };
 const char *messages[] = {"local 'x' shadows an earlier local", "local 'y' shadows an earlier local"};
 for (unsigned c = 0; c < 2; c++) {
  Fixture f; init(&f, controls[c]);
  CHECK(diag_has_errors(&f.diag));
  CHECK(strcmp(f.diag.items[0].message, messages[c]) == 0);
  arena_free(&f.arena);
 }
 printf("switch next frontend: %u total checks; all original cases + 11 owner cases + 2 unrelated-diagnostic controls\n", checks);
 return 0;
}
