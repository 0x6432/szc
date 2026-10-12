#ifndef SZC_PARSER_H
#define SZC_PARSER_H

#include "arena.h"
#include "ast.h"
#include "diag.h"
#include "source.h"

/*
 * Parses one source file into an AST. The root is a struct container holding the
 * file's members. Never returns NULL: syntax errors are reported to `diag`
 * and the parser recovers and keeps going.
 */
AstContainerDecl *parse_file(Arena *arena, Diag *diag, SourceTable *sources, FileId file);

#endif
