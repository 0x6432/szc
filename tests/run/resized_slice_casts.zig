// exit: 0
fn bytes(s: []u16) []u8 { return @ptrCast(s); }
fn words(s: []u8) []u16 { return @alignCast(@ptrCast(s)); }
fn triples(s: []u16) [][3]u8 { return @ptrCast(s); }
fn from_triples(s: [][3]u8) []u16 { return @alignCast(@ptrCast(s)); }
fn empty(s: []allowzero u8) []u32 { return @alignCast(@ptrCast(s)); }
fn zero(s: []u0) []u8 { return @ptrCast(s); }
var backing: [3]u16 align(16) = .{0x0201,0x0403,0x0605};
pub fn main() u8 {
 const b = bytes(&backing);
 if (b.len != 6 or b[5] != 6) return 1;
 b[0] = 7;
 if (backing[0] != 0x0207) return 2;
 const w = words(b);
 if (w.len != 3 or w[1] != 0x0403) return 3;
 w[2] = 0x0908;
 if (b[4] != 8 or b[5] != 9) return 4;
 const t = triples(&backing);
 if (t.len != 2 or t[1][0] != 4 or t[1][2] != 9) return 5;
 const again = from_triples(t);
 if (again.len != 3 or again[2] != 0x0908) return 6;
 const p: [*]allowzero u8 = @ptrFromInt(17);
 const e = empty(p[0..0]);
 if (e.len != 0 or @intFromPtr(e.ptr) != 17) return 7;
 const n: [*]allowzero u8 = @ptrFromInt(0);
 if (empty(n[0..0]).len != 0) return 8;
 var z: [5]u0 = .{0,0,0,0,0};
 if (zero(&z).len != 0) return 9;
 const known: [*]u16 = @ptrFromInt(16);
 const ks: []u16 = known[0..3];
 const kb: []u8 = @ptrCast(ks);
 if (kb.len != 6 or @intFromPtr(kb.ptr) != 16) return 10;
	const huge: [*]u16 = @ptrFromInt(16);
	if (bytes(huge[0..9223372036854775808]).len != 0) return 11;
 return 0;
}
