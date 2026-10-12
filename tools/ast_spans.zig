//! Reference span dumper for differential testing of the szc parser.
//!
//! For each file argument prints "== <path>" and then "start end" (byte
//! offsets of the node's first token start and last token end) for every
//! node of std.zig.Ast except the root and nodes szc does not represent as
//! nodes (switch prongs, switch/for ranges, asm outputs/inputs).
//!
//! Build: zig build-exe tools/ast_spans.zig -OReleaseFast
const std = @import("std");
const Ast = std.zig.Ast;

pub fn main(init: std.process.Init) !void {
    const io = init.io;
    const arena = init.arena.allocator();
    const args = try init.minimal.args.toSlice(arena);
    for (args[1..]) |path| {
        const source = try std.Io.Dir.cwd().readFileAllocOptions(io, path, arena, .unlimited, .of(u8), 0);
        var tree = try Ast.parse(arena, source, .{});
        std.debug.print("== {s}\n", .{path});
        if (tree.errors.len != 0) {
            std.debug.print("parse error\n", .{});
            continue;
        }
        var i: u32 = 1;
        while (i < tree.nodes.len) : (i += 1) {
            const node: Ast.Node.Index = @enumFromInt(i);
            switch (tree.nodeTag(node)) {
                .root, .switch_case, .switch_case_one, .switch_case_inline, .switch_case_inline_one, .switch_range, .for_range, .asm_output, .asm_input => continue,
                else => {},
            }
            const first = tree.firstToken(node);
            const last = tree.lastToken(node);
            const start = tree.tokenStart(first);
            const end = tree.tokenStart(last) + tree.tokenSlice(last).len;
            std.debug.print("{d} {d}\n", .{ start, end });
        }
    }
}
