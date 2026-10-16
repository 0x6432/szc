// exit: 0
var a: [3:17]u16 = .{7,9,11};
const borrowed: [:17]const u16 = &a;
const writable: [:17]u16 = @constCast(borrowed);
fn mutate(s: [:17]const u16) [:17]u16 { return @constCast(s); }
fn seven() u8 { return 7; }
pub fn main() u8 {
    writable[0] = 13;
    if (a[0] != 13 or writable[3] != 17) return 1;
    const s = mutate(borrowed);
    s[1] = 15;
    if (a[1] != 15 or s.len != 3) return 2;
    const p: *const volatile u16 = &a[2];
    const q: *u16 = @constCast(@volatileCast(p));
    q.* = 19;
    if (a[2] != 19 or @intFromPtr(q) != @intFromPtr(&a[2])) return 3;
    var local = [2]u8{1,2};
    const immutable: []const u8 = &local;
    const mutable = @constCast(immutable);
    mutable[1] = 5;
    if (local[1] != 5 or mutable.len != 2) return 4;
    if (@constCast(&seven)() != 7) return 5;
    return 0;
}
