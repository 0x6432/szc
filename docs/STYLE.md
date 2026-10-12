# Coding style

- C99 (C11/C23 only when it clearly makes code simpler). Compiles warning-free with
  `-Wall -Wextra -Wpedantic`.
- `snake_case` for functions, variables, struct fields and enum constants'
  prefixes; `TitleCase` for typedefs (`Token`, `Lexer`, `AstNode`).
  Enum constants are `UPPER_CASE` with a prefix (`TK_PUB`, `AST_FN_DECL`).
- Tabs for indentation **and** alignment. Tab width is 2 (see `.editorconfig`).
- Single-statement bodies of `if`/`else`/`for`/`while` have no braces:

  ```c
  if (token.kind == TK_PUB)
  	is_pub = true;
  ```

  Use braces when the body has more than one statement or line.
- Function definitions: return type on its own line, opening brace on its own line.
- Type safety: pass enums as their enum type, use `bool`, and use fixed-width
  integers (`uint32_t` offsets, `size_t` sizes). No `int` flags.
- No clever shortcuts: simple, readable code, one thing per line.
- Never compare token text to identify syntax; compare `TokenKind`.
- Every parser function begins with a comment giving the grammar rule it implements.
  The Pratt binary-expression function lists every rule it covers (BoolOrExpr … MultiplyExpr).
