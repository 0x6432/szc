// exit: 0
var global: [3]u16 = .{1,2,3};
fn copy(d: []u16, s: []const u16) void { @memcpy(d,s); }
fn to_many(d: [*]u16, s: []const u16) void { @memcpy(d,s); }
fn from_many(d: []u16, s: [*]const u16) void { @memcpy(d,s); }
fn volatile_copy(d: []volatile u16, s: []const volatile u16) void { @memcpy(d,s); }
fn empty(d: []allowzero u8, s: []allowzero const u8) void { @memcpy(d,s); }
fn zero_bits(d: []u0, s: []const u0) void { @memcpy(d,s); }
fn destination(n:*u8, p:[]u16) []u16 { n.* = n.* * 10 + 1; return p; }
fn source(n:*u8, p:[]const u16) []const u16 { n.* = n.* * 10 + 2; return p; }
pub fn main() u8 {
	var a:[3:1234]u16=.{0,0,0};
	const b:[3:5678]u16=.{7,9,11};
	@memcpy(&a,&b);
	if(a[0]!=7 or a[2]!=11 or a[3]!=1234) return 1;
	copy(a[1..3],b[0..2]);
	if(a[0]!=7 or a[1]!=7 or a[2]!=9) return 2;
	to_many(&global,&b);
	if(global[0]!=7 or global[2]!=11) return 3;
	from_many(&a,&global);
	if(a[2]!=11 or a[3]!=1234) return 4;
	@memcpy(a[0..1],a[2..3]);
	if(a[0]!=11) return 5;
	@memcpy(a[2..3],a[0..1]);
	if(a[2]!=11) return 6;
	var calls:u8=0;
	@memcpy(destination(&calls,&a),source(&calls,&b));
	if(calls!=12 or a[1]!=9) return 7;
	calls=0;
	@memcpy(destination(&calls,a[0..0]),source(&calls,b[0..0]));
	if(calls!=12 or a[1]!=9) return 8;
	const null_pointer:[*]allowzero u8=@ptrFromInt(0);
	empty(null_pointer[0..0],null_pointer[0..0]);
	var zeros:[3]u0=.{0,0,0};
	zero_bits(&zeros,&zeros);
	@memcpy(@as(*[3]u0,undefined),@as(*const [3]u0,undefined));
	var wide:[3]u33=.{0,0,0};
	const wide_source:[3]u33=.{0x1ffffffff,0x123456789,0x1abcdef01};
	@memcpy(&wide,&wide_source);
	if(wide[0]!=0x1ffffffff or wide[1]!=0x123456789 or wide[2]!=0x1abcdef01) return 9;
	var signed:[3]i40=.{0,0,0};
	const signed_source:[3]i40=.{-549755813888,-123456789,549755813887};
	@memcpy(&signed,&signed_source);
	if(signed[0]!=-549755813888 or signed[1]!=-123456789 or signed[2]!=549755813887) return 10;
	var flags:[3]bool=.{false,false,false};
	const flag_source:[3]bool=.{true,false,true};
	@memcpy(&flags,&flag_source);
	if(!flags[0] or flags[1] or !flags[2]) return 11;
	var byte:u8=1;
	var other:u8=2;
	var pointers:[2]*const u8=.{&byte,&byte};
	const pointer_source:[2]*u8=.{&other,&byte};
	@memcpy(&pointers,&pointer_source);
	if(pointers[0]!=&other or pointers[1].*!=1) return 12;
	var text:[3]u8=undefined;
	@memcpy(&text,"abc");
	if(text[0]!=97 or text[2]!=99) return 13;
	volatile_copy(&a,&b);
	if(a[0]!=7 or a[2]!=11 or a[3]!=1234) return 14;
	return 0;
}
