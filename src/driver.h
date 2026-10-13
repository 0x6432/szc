#ifndef SZC_DRIVER_H
#define SZC_DRIVER_H

#include <stdbool.h>
#include <stdio.h>

/*
 * Compilation driver (driver.c): source file -> executable, by way of
 * parse, irgen, sema, codegen, `qbe` and the system C compiler.
 */
typedef struct BuildOptions {
	const char *input;	/* root source file */
	const char *output;	/* executable path */
	bool			keep_temporaries;	/* keep the .ssa and .s files next to the output */
} BuildOptions;

/* Compiles `input` and writes QBE IL to `out`. Returns 0 on success, 1 on compile errors, 2 on output errors. */
int driver_emit_qbe(const char *input, FILE *out);

/* Returns 0 on success, 1 on compile errors, 2 on tool failures. */
int driver_build_exe(const BuildOptions *options);

/* Builds `input` into a temporary executable and runs it; returns its exit status. */
int driver_run(const char *input, char **args, int arg_count);

#endif
