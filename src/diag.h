#ifndef SZC_DIAG_H
#define SZC_DIAG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "arena.h"
#include "source.h"

typedef enum DiagLevel {
	DIAG_ERROR,
	DIAG_NOTE,
} DiagLevel;

typedef struct Diagnostic {
	DiagLevel  level;
	FileId     file;
	uint32_t   offset;
	const char *message;
	size_t message_length;
} Diagnostic;

/* Collects diagnostics; compilation keeps going after an error. */
typedef struct Diag {
	Arena       *arena;
	SourceTable *sources;
	Diagnostic  *items;
	uint32_t    count;
	uint32_t    capacity;
	uint32_t    error_count;
} Diag;

void diag_init(Diag *diag, Arena *arena, SourceTable *sources);
void diag_report(Diag *diag, DiagLevel level, FileId file, uint32_t offset, const char *format, ...);
void diag_report_bytes(Diag *diag, DiagLevel level, FileId file, uint32_t offset, const char *message, size_t length);
void diag_print(const Diag *diag, FILE *out);
bool diag_has_errors(const Diag *diag);

#endif
