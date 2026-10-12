fn f() void {
    const a = 1 + 2 * 3 - 4 / 5 % 6;
    const b = a << 1 >> 2 & 3 | 4 ^ 5;
    const c = a == b and a != b or !(a < b);
    const d = x orelse y catch |err| handle(err);
    const e = -a +% b -% c *% d +| e -| f *| g <<| 1;
    const g = ~a & -%b;
    const h = &a.b.c.*.?;
    const i = arr[1][2..][3..4][0.. :0];
    const j = .{ .x = 1, .y = .{} };
    const k = [_]u8{ 1, 2, 3 };
    const l = Point{ .x = 1 };
    const m = @as(u32, @intCast(z));
    const n = try foo(bar, baz) ++ "s" ++ \\multi
        \\line
    ;
    const o = error.Oops;
    const p = .enum_literal;
    const q = 'c';
    const r = error{ A, B } || E2;
    const s = if (a) b else c;
    const t = switch (a) {
        1, 2 => b,
        3...5 => |v| v,
        inline else => |v, tag| tag,
    };
    const u = blk: {
        break :blk 1;
    };
    a, var v, const w = tuple;
    a.b += 1;
    a.c <<|= 2;
}
