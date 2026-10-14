#ifndef SZC_CODEGEN_H
#define SZC_CODEGEN_H

#include <stdbool.h>
#include <stdio.h>

#include "diag.h"
#include "sema.h"

/*
 * QBE code generation (codegen.c): writes the typed IR of every analysed
 * function as QBE IL, plus a C `main` that calls Zig's `main`. Returns false
 * if something could not be lowered (reported to `diag`).
 */
/* assembly may be NULL for standalone IL; larger static alignments need it. */
bool codegen_qbe(FILE *out, FILE *assembly, Sema *sema, Diag *diag);

#endif
