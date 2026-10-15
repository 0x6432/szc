#!/usr/bin/env python3
"""Runtime panic regressions, with optional checks against the official Zig binary."""
import os
import resource
import signal
import subprocess
import sys
import tempfile
from pathlib import Path


# Share the exact timeout lifecycle used by the compiler comparison tools.
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compiler_process import run


def binary_case(name, integer_type, operator, lhs, rhs, message="integer overflow"):
    source = (f"fn operation(a: {integer_type}, b: {integer_type}) {integer_type} {{\n"
              f"    return a {operator} b;\n}}\n"
              f"pub fn main() void {{ _ = operation({lhs}, {rhs}); }}\n")
    return name, source, message


def cases():
    for label, operation in (("read", "_ = a[n];"), ("store", "a[n] = 3;"),
                             ("address", "_ = &a[n];"), ("nested", "_ = a[0][n];")):
        ty = "[2][2]u8" if label == "nested" else "[2]u8"
        value = ".{.{1,2},.{3,4}}" if label == "nested" else ".{1,2}"
        source = (f"var a: {ty} = {value}; fn operation(n: usize) void {{ {operation} }} "
                  "pub fn main() void { operation(2); }\n")
        yield "global_array_" + label, source, "index out of bounds"
    yield "array_callback_noreturn", 'fn stop(a: u8, b: u8, c: u8, d: u8, e: u8, f: u8, g: u8, h: u8) noreturn {\n    if (a != 1 or b != 2 or c != 3 or d != 4 or e != 5 or f != 6 or g != 7 or h != 8) @panic("wrong array callback arguments");\n    @panic("array callback terminated");\n}\nfn invoke(p: *const fn (u8, u8, u8, u8, u8, u8, u8, u8) [2]u8) void { _ = p(1,2,3,4,5,6,7,8); }\npub fn main() void { invoke(&stop); }\n', "array callback terminated"
    for name, operation in (
        ("store", "a[n] = 3;"),
        ("address", "const p = &a[n]; p.* = 3;"),
        ("zero_store", "var z: [2]u0 = undefined; z[n] = 0; _ = &a;"),
        ("nested", "var z: [2][2]u8 = .{.{1,2},.{3,4}}; z[0][n] = 3; _ = &a;"),
    ):
        source = ("fn operation(n: usize) void { var a = [_]u8{1,2}; " + operation + " }\n"
                  "pub fn main() void { operation(2); }\n")
        yield "array_mutable_" + name, source, "index out of bounds"
    for child, value in (("u8", "7"), ("i33", "-7"), ("u0", "0"), ("void", "{}")):
        for index in ("2", "18446744073709551615"):
            source = (f"fn read(p: *const [2]{child}, n: usize) {child} {{ return p[n]; }}\n"
                      f"pub fn main() void {{ const a = [_]{child}{{{value}, {value}}}; _ = read(&a, {index}); }}\n")
            yield f"array_pointer_{child}_{index}", source, "index out of bounds"
    yield "array_value_index", ("fn read(n: usize) u8 { const a = [_]u8{7,9}; return a[n]; }\n"
                                "pub fn main() void { _ = read(2); }\n"), "index out of bounds"
    yield "array_string_index", ("fn read(n: usize) u8 { return \"hi\"[n]; }\n"
                                 "pub fn main() void { _ = read(3); }\n"), "index out of bounds"
    yield "function_pointer_noreturn_coercion", ('fn stop() noreturn { @panic("coerced callback"); }\n'
          'fn invoke(p: *const fn () void) void { p(); }\n'
          'pub fn main() void { invoke(&stop); }\n'), "coerced callback"
    yield "function_pointer_noreturn", ('fn stop() noreturn { @panic("callback"); }\n'
          'fn invoke(p: *const fn () noreturn) noreturn { p(); }\n'
          'pub fn main() void { invoke(&stop); }\n'), "callback"
    yield "function_pointer_overflow", ('fn operation(n: u8) u8 { return n + 1; }\n'
          'fn invoke(p: *const fn (u8) u8) void { _ = p(255); }\n'
          'pub fn main() void { invoke(&operation); }\n'), "integer overflow"
    for label, body, message in (
        ("overflow", "n += 1;", "integer overflow"),
        ("cast", "const x: u4 = @intCast(n); _ = x;", "integer does not fit in destination type"),
        ("cleanup", "defer n += 1;", "integer overflow"),
    ):
        yield f"global_{label}", f"var n: u8 = 255;\npub fn main() void {{ {body} }}\n", message
    yield "panic_global", 'var text = "first";\npub fn main() void { text = "later"; @panic(text); }\n', "later"
    for label, body, message in (
        ("overflow", "p.* += 1;", "integer overflow"),
        ("cast", "const n: u4 = @intCast(p.*); _ = n;", "integer does not fit in destination type"),
        ("cleanup", "defer p.* += 1;", "integer overflow"),
    ):
        source = (f"fn operation(p: *u8) void {{ {body} }}\n"
                  "pub fn main() void { var n: u8 = 255; operation(&n); }\n")
        yield f"pointer_{label}", source, message
    yield "panic_reference", 'pub fn main() void { @panic((&"reference").*); }\n', "reference"
    source = ('fn operation(p: *const u8) u4 { return @intCast(p.*); }\n'
              'pub fn main() void { const n: u8 = 255; _ = operation(&n); }\n')
    yield "constant_pointer_cast", source, "integer does not fit in destination type"
    for label, literal, message in (
        ("plain", '"explicit panic"', "explicit panic"),
        ("empty", '""', ""),
        ("escapes", r'"quote \" slash \\ tab \t CR \r LF \n"', 'quote " slash \\ tab \t CR \r LF \n'),
        ("utf8", '"héllo 😀"', "héllo 😀"),
        ("unicode", r'"\u{0}\u{7f}\u{80}\u{7ff}\u{800}\u{d7ff}\u{e000}\u{ffff}\u{10000}\u{10ffff}"',
         "".join(chr(c) for c in (0, 0x7f, 0x80, 0x7ff, 0x800, 0xd7ff, 0xe000, 0xffff, 0x10000, 0x10ffff))),
        ("binary", r'"a\x00b\xff"', b"a\x00b\xff"),
    ):
        yield f"panic_{label}", f"pub fn main() void {{ @panic({literal}); }}\n", message
    literal = '"' + "".join(f"\\x{c:02x}" for c in range(256)) + '"'
    yield "panic_all_bytes", f"pub fn main() void {{ @panic({literal}); }}\n", bytes(range(256))
    yield "panic_nested", 'pub fn main() void { @panic(@panic("inner")); }\n', "inner"
    yield "panic_unreachable_argument", "pub fn main() void { @panic(unreachable); }\n", "reached unreachable code"
    yield "panic_alias", 'const text = value: { break :value "alias"; };\npub fn main() void { const local = text; @panic(local); }\n', "alias"
    for c, message in (("true", "left"), ("false", "rght")):
        source = f'fn operation(c: bool) noreturn {{ @panic(if (c) "left" else "rght"); }}\npub fn main() void {{ operation({c}); }}\n'
        yield f"panic_choice_{c}", source, message
    yield "panic_variable", 'fn operation() noreturn { var text = "first"; text = "later"; @panic(text); }\npub fn main() void { operation(); }\n', "later"
    slashes = chr(92) * 2
    source = "\n".join(("pub fn main() void { @panic(", "    " + slashes + "first line", "    " + slashes + "raw " + chr(92) + "u{41}", "); }", ""))
    yield "panic_multiline", source, "first line\nraw " + chr(92) + "u{41}"
    yield "panic_cleanup", 'fn operation(x: u8) void { var a = x; defer a += 1; defer @panic("cleanup"); }\npub fn main() void { operation(255); }\n', "cleanup"
    yield "panic_before_cleanup", 'fn operation(x: u8) void { var a = x; defer a += 1; @panic("direct"); }\npub fn main() void { operation(255); }\n', "direct"
    for label, source in (
        ("direct", "pub fn main() void { unreachable; }"),
        ("noreturn_main", "pub fn main() noreturn { unreachable; }"),
        ("noreturn_call", "fn stop() noreturn { unreachable; }\npub fn main() void { stop(); }"),
        ("runtime_branch", "fn operation(c: bool) void { if (c) unreachable; }\npub fn main() void { operation(true); }"),
        ("loop", "fn operation(n: usize) void { for (0..n) |_| { unreachable; } }\npub fn main() void { operation(1); }"),
        ("skip_cleanup", "fn operation(x: u8) void { var a = x; defer a += 1; unreachable; }\npub fn main() void { operation(255); }"),
        ("cleanup", "fn operation(x: u8) void { var a = x; defer a += 1; defer unreachable; }\npub fn main() void { operation(255); }"),
        ("cleanup_call", "fn stop() noreturn { unreachable; }\nfn operation(x: u8) void { var a = x; defer a += 1; defer stop(); }\npub fn main() void { operation(255); }"),
    ):
        yield f"unreachable_{label}", source + "\n", "reached unreachable code"
    for label, exit_stmt in (("return", "return a;"), ("fallthrough", ""), ("break", "break;"), ("continue", "continue;")):
        if label == "return":
            operation = f"fn operation(x: u8) u8 {{ var a = x; defer a += 1; {exit_stmt} }}"
        elif label == "fallthrough":
            operation = f"fn operation(x: u8) void {{ var a = x; defer a += 1; {exit_stmt} }}"
        else:
            operation = f"fn operation(x: u8) void {{ for (0..1) |_| {{ var a = x; defer a += 1; {exit_stmt} }} }}"
        source = operation + "\npub fn main() void { _ = operation(255); }\n"
        yield f"defer_{label}_overflow", source, "integer overflow"
    source = ("fn operation(x: u8, n: u8, d: u8) void { var a = x; defer a += 1; defer _ = @divTrunc(n, d); }\n"
              "pub fn main() void { operation(255, 3, 0); }\n")
    yield "defer_panic_order", source, "division by zero"
    source = ("fn operation(x: u8, n: u8, d: u8) u8 { var a = x; defer a += 1; return @divTrunc(n, d); }\n"
              "pub fn main() void { _ = operation(255, 3, 0); }\n")
    yield "defer_return_operand_panic", source, "division by zero"
    source = ("fn operation(start: usize, end: usize) void { for (start..end) |_| {} }\n"
              "pub fn main() void { operation(2, 1); }\n")
    yield "for_reversed", source, "integer overflow"
    source = ("fn operation(n: usize) void { for (0..n, 3..5) |_, _| {} }\n"
              "pub fn main() void { operation(1); }\n")
    yield "for_known_mismatch", source, "for loop over objects with non-equal lengths"
    for n, m, label in ((1, 2, "runtime"), (0, 1, "zero")):
        source = ("fn operation(n: usize, m: usize) void { for (0..n, 0..m) |_, _| {} }\n"
                  f"pub fn main() void {{ operation({n}, {m}); }}\n")
        yield f"for_{label}_mismatch", source, "for loop over objects with non-equal lengths"
    source = ("fn operation(start: usize) void { for (0..2, start..) |_, i| { _ = i; } }\n"
              "pub fn main() void { operation(0xffffffffffffffff); }\n")
    yield "for_counter_overflow", source, "integer overflow"
    source = ("fn operation(c: bool, a: u8) u8 { return value: { if (c) break :value a + 1; break :value 0; }; }\n"
              "pub fn main() void { _ = operation(true, 255); }\n")
    yield "block_break_overflow", source, "integer overflow"
    source = ("fn operation(c: bool, a: u16) u8 { return value: { if (c) break :value @intCast(a); break :value 0; }; }\n"
              "pub fn main() void { _ = operation(true, 256); }\n")
    yield "block_break_cast", source, "integer does not fit in destination type"
    source = ("fn operation(a: u8) u8 { var x = a; while (true) { x += 1; if (x == 0) break; } return x; }\n"
              "pub fn main() void { _ = operation(255); }\n")
    yield "loop_body_overflow", source, "integer overflow"
    source = ("fn operation(a: u8) u8 { var x = a; var i: u8 = 0; while (i < 2) : (x += 1) { i += 1; } return x; }\n"
              "pub fn main() void { _ = operation(255); }\n")
    yield "loop_continue_overflow", source, "integer overflow"
    for bits in (1, 5, 8, 32, 33, 64):
        unsigned = f"u{bits}"
        signed = f"i{bits}"
        maximum = (1 << bits) - 1
        minimum = -(1 << (bits - 1))
        signed_max = (1 << (bits - 1)) - 1
        yield binary_case(f"add_{unsigned}", unsigned, "+", maximum, 1)
        yield binary_case(f"sub_{unsigned}", unsigned, "-", 0, 1)
        if bits > 1:
            yield binary_case(f"mul_{unsigned}", unsigned, "*", maximum, 2)
            yield binary_case(f"add_positive_{signed}", signed, "+", signed_max, 1)
            yield binary_case(f"sub_negative_{signed}", signed, "-", minimum, 1)
            yield binary_case(f"mul_positive_{signed}", signed, "*", signed_max, 2)
            yield binary_case(f"mul_below_min_{signed}", signed, "*", minimum, 2)
        yield binary_case(f"add_negative_{signed}", signed, "+", minimum, -1)
        yield binary_case(f"sub_positive_{signed}", signed, "-", signed_max, -1)
        yield binary_case(f"mul_negative_{signed}", signed, "*", minimum, -1)
        source = (f"fn operation(a: {signed}) {signed} {{ return -a; }}\n"
                  f"pub fn main() void {{ _ = operation({minimum}); }}\n")
        yield f"negate_{signed}", source, "integer overflow"
    for name, from_type, to_type, value in (
        ("cast_negative", "i64", "u64", -1),
        ("cast_unsigned_narrow", "u64", "u8", 256),
        ("cast_signed_low", "i64", "i8", -129),
        ("cast_signed_high", "i64", "i8", 128),
        ("cast_sign_change", "u64", "i64", 1 << 63),
        ("cast_small_sign_change", "u8", "i8", 128),
        ("cast_unsigned_zero", "u8", "u0", 1),
        ("cast_signed_zero", "i1", "u0", -1),
    ):
        source = (f"fn operation(a: {from_type}) {to_type} {{ return @intCast(a); }}\n"
                  f"pub fn main() void {{ _ = operation({value}); }}\n")
        yield name, source, "integer does not fit in destination type"
    for integer_type in ("u32", "u64"):
        for op, label in (("/", "div"), ("%", "rem")):
            yield binary_case(f"{label}_zero_{integer_type}", integer_type, op, 123, 0, "division by zero")

    for builtin in ("divTrunc", "divFloor", "divCeil", "divExact", "rem", "mod"):
        for integer_type in ("i8", "i64", "u64"):
            source = (f"fn operation(a: {integer_type}, b: {integer_type}) {integer_type} {{ return @{builtin}(a, b); }}\n"
                      "pub fn main() void { _ = operation(3, 0); }\n")
            yield f"{builtin}_zero_{integer_type}", source, "division by zero"
    for builtin in ("divTrunc", "divFloor", "divCeil", "divExact"):
        for bits in (1, 8, 33, 64):
            source = (f"fn operation(a: i{bits}, b: i{bits}) i{bits} {{ return @{builtin}(a, b); }}\n"
                      f"pub fn main() void {{ _ = operation({-(1 << (bits - 1))}, -1); }}\n")
            yield f"{builtin}_overflow_i{bits}", source, "integer overflow"
    for integer_type in ("u8", "u64", "i8", "i64"):
        source = (f"fn operation(a: {integer_type}, b: {integer_type}) {integer_type} {{ return @divExact(a, b); }}\n"
                  "pub fn main() void { _ = operation(5, 2); }\n")
        yield f"exact_remainder_{integer_type}", source, "exact division produced remainder"
    for bits in (5, 33):
        for op, label in (("<<", "shl"), (">>", "shr")):
            source = (f"fn operation(a: u{bits}, b: u{(bits - 1).bit_length()}) u{bits} {{\n"
                      f"    return a {op} b;\n}}\n"
                      f"pub fn main() void {{ _ = operation(1, {bits}); }}\n")
            yield f"{label}_size_{bits}", source, "shift amount is greater than the type size"


def main():
    szc = sys.argv[1] if len(sys.argv) > 1 else "build/asan/szc"
    zig = os.environ.get("ZIG")
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    passed = failed = 0
    with tempfile.TemporaryDirectory(prefix="szc-safety-") as directory:
        root = Path(directory)
        for name, source, message in cases():
            src = root / f"{name}.zig"
            src.write_text(source)
            try:
                message_bytes = message.encode() if isinstance(message, str) else message
                for compiler in ([szc, zig] if zig else [szc]):
                    if os.environ.get("GITHUB_ACTIONS") == "true":
                        print(f"checking safety {name}: {compiler}", flush=True)
                    exe = root / ("reference" if compiler == zig else "native")
                    command = ([compiler, "build-exe", "-j2", str(src), f"-femit-bin={exe}"]
                               if compiler == zig else
                               [compiler, "build-exe", str(src), "-o", str(exe)])
                    compiled = run(command, timeout=120)
                    if compiled.returncode != 0:
                        raise AssertionError("compilation failed: " + compiled.stderr.decode(errors="replace"))
                    result = run([str(exe)])
                    if result.returncode != -signal.SIGABRT:
                        raise AssertionError(f"{compiler} exit {result.returncode}; expected SIGABRT")
                    if result.stdout:
                        raise AssertionError("unexpected stdout")
                    if compiler == zig:
                        if message_bytes not in result.stderr:
                            raise AssertionError("reference panic message mismatch: " + result.stderr.decode(errors="replace"))
                    elif result.stderr != message_bytes + b"\n":
                        raise AssertionError("panic message mismatch: " + result.stderr.decode(errors="replace"))
                passed += 1
            except (AssertionError, subprocess.TimeoutExpired) as error:
                print(f"FAIL {name}: {error}", flush=True)
                failed += 1
    print(f"safety: {passed} passed, {failed} failed", flush=True)
    return int(failed != 0)


if __name__ == "__main__":
    sys.exit(main())
