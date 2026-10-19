const Tag = enum(u8) { a = 1, b = 7 };
fn choose(x: Tag) Tag {
    return switch (x) {
        .a => |v| v,
        .b => .b,
    };
}
fn nested(x: u8) u8 {
    return switch (x) {
        0, 1, => |v| switch (v) { 0 => 3, else => v },
        2...9 => |v| blk: { defer _ = v; break :blk v; },
        else => return 0,
    };
}
fn effects(x: u8) void {
    var result: u8 = 0;
    outer: while (true) {
        defer _ = result;
        result = switch (x) { 0 => @intCast(x), else => 2 };
        switch (x) { 0 => continue :outer, else => break :outer }
    }
}
pub fn main() u8 { effects(0); return nested(@intFromEnum(choose(.a))); }
