// exit: 0
fn signed(s: []u16) []i16 { return @ptrCast(s); }
fn bytes(s: []const i8) []const u8 { return @ptrCast(s); }
fn checked(s: []align(1) allowzero u16) []u16 { return @alignCast(@ptrCast(s)); }
fn promise(s: []const u8) [:0]const u8 { return @ptrCast(s); }
fn flags(s: []const volatile u8) []i8 { return @volatileCast(@constCast(@ptrCast(s))); }
fn zero(s: []u0) []u0 { return @ptrCast(s); }
var backing: [2]u16 align(16) = .{65535,32768};
pub fn main() u8 {
 const a = signed(&backing);
 if (a.len != 2 or a[0] != -1 or a[1] != -32768) return 1;
 a[0] = 7;
 if (backing[0] != 7) return 2;
 const b: [2]i8 = .{-1,2};
 if (bytes(&b).len != 2 or bytes(&b)[0] != 255) return 3;
 const p: [*]align(1) allowzero u16 = @ptrFromInt(17);
 const empty = checked(p[0..0]);
 if (empty.len != 0 or @intFromPtr(empty.ptr) != 17) return 4;
 const n: [*]align(1) allowzero u16 = @ptrFromInt(0);
 const null_empty = checked(n[0..0]);
 if (null_empty.len != 0 or @intFromPtr(null_empty.ptr) != 0) return 5;
 const q: [*]align(1) allowzero u16 = @ptrCast(&backing);
 if (checked(q[0..2])[0] != 7) return 6;
 if (promise("ab").len != 2 or promise("ab")[2] != 0) return 7;
 var x: [2]u8 = .{255,2};
 const v = flags(&x);
 if (v.len != 2 or v[0] != -1) return 8;
 var z: [3]u0 = .{0,0,0};
 if (zero(&z).len != 3) return 9;
 const c: []const u8 = "ab";
 const d: []const i8 = @ptrCast(c);
 if (d.len != 2 or @intFromPtr(d.ptr) != @intFromPtr(c.ptr)) return 10;
 return 0;
}
