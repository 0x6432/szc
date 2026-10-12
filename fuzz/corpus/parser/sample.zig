const std = @import("std");
pub fn main() !void {
    var x: u32 = 1 + 2 * 3;
    x += if (x > 2) 1 else 2;
    const s = .{ .a = 1, .b = "hi" };
    _ = s;
    blk: while (x < 10) : (x += 1) {
        if (x == 5) break :blk;
    }
    for (0..10, items) |i, *it| it.* = i;
    switch (x) {
        0...3, 7 => |v| std.debug.print("{}\n", .{v}),
        else => {},
    }
    const p: [*:0]const u8 = undefined;
    const y = foo() catch |err| return err;
    _ = .{ p, y };
}
