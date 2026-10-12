#ifndef SZC_AST_DUMP_H
#define SZC_AST_DUMP_H

#include <stdio.h>

#include "ast.h"
#include "source.h"

/* Prints an indented, deterministic textual form of the tree (used by `szc ast` and golden tests). */
void ast_dump(FILE *out, const SourceFile *file, const AstNode *root);

#endif
