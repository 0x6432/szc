// exit: 0
var a: [3]u16 align(64) = .{7,9,11};
const p: [*]u16 = &a;
const element = p + 2;
fn aligned() align(16) u8 { return 7; }
fn address(q: [*]const u16) usize { return @intFromPtr(q); }
fn function_address(f: *const fn () u8) usize { return @intFromPtr(f); }
pub fn main() u8 {
    const base = @intFromPtr(&a);
    if (base == 0 or base % 64 != 0 or address(p) != base) return 1;
    if (@intFromPtr(element) != base+4 or @intFromPtr(&a[1]) != base+2) return 2;
    if (function_address(&aligned) == 0 or @intFromPtr(&aligned) % 16 != 0) return 3;
    var n: u8 = 7;
    const q = &n;
    q.* = 9;
    if (@intFromPtr(q) != @intFromPtr(&n) or n != 9) return 4;
    const text = "ab";
    if (@intFromPtr(&text[1]) != @intFromPtr(text)+1) return 5;
    const zero: u0 = 0;
    if (@intFromPtr(&zero) == 0) return 6;
    return 0;
}
