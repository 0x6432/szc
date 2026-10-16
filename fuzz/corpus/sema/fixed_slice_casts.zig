// exit: 0
fn bytes(p: *u32) []u8 { return @ptrCast(p); }
fn words(p: *[4]u8) []u16 { return @alignCast(@ptrCast(p)); }
fn signed(p: *const [3]u16) []const i16 { return @ptrCast(p); }
fn flags(p: *const volatile [2]u16) []u8 {
 return @volatileCast(@constCast(@ptrCast(p)));
}
fn zero(p: *[5]u0) []u0 { return @ptrCast(p); }
fn empty(p: *[0]u16) []u8 { return @ptrCast(p); }
fn marked(p: *[2:0]u8) [:0]u8 { return @ptrCast(p); }
var backing: [4]u8 align(16) = .{1,2,3,4};
pub fn main() u8 {
 var n: u32 = 0x12345678;
 const b = bytes(&n);
 if (b.len != 4 or b[0] != 0x78 or b[3] != 0x12) return 1;
 b[1] = 0xAB;
 if (n != 0x1234AB78) return 2;
 const w = words(&backing);
 if (w.len != 2 or w[1] != 0x0403) return 3;
 w[0] = 0x0908;
 if (backing[0] != 8 or backing[1] != 9) return 4;
 const a: [3]u16 = .{0,65535,32768};
 const s = signed(&a);
 if (s.len != 3 or s[1] != -1 or s[2] != -32768) return 5;
 var a2: [2]u16 = .{0x0102,0x0304};
 if (flags(&a2).len != 4 or flags(&a2)[2] != 4) return 6;
 var z: [5]u0 = .{0,0,0,0,0};
 if (zero(&z).len != 5) return 7;
 var e: [0]u16 = .{};
 if (empty(&e).len != 0) return 8;
 var m: [2:0]u8 = .{5,6};
 const ms = marked(&m);
 if (ms.len != 2 or ms[2] != 0) return 9;
 const numeric: *allowzero [4]u8 = @ptrFromInt(16);
 const ns: []allowzero u16 = @alignCast(@ptrCast(numeric));
 if (ns.len != 2 or @intFromPtr(ns.ptr) != 16) return 10;
 const cp: []const u8 = @ptrCast(&a);
 if (cp.len != 6) return 11;
 return 0;
}
