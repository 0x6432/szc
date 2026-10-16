// exit: 0
var global: [3]u16 = .{1,2,3};
fn fill(p: []u16, n:u16) void { @memset(p,n); }
fn set_bool(p: []bool) void { @memset(p,true); }
fn set_pointer(p: []*u8, n:*u8) void { @memset(p,n); }
fn volatile_fill(p: []volatile u16) void { @memset(p,12345); }
fn empty(p: []allowzero u8) void { @memset(p,1); }
fn zero_bits(p: []u0) void { @memset(p,undefined); }
fn next(n:*u8) u16 { n.* += 1; return 77; }
fn destination(n:*u8, p:[]u16) []u16 { n.* += 1; return p; }
pub fn main() u8 {
	var a:[5:1234]u16=.{1,2,3,4,5};
	@memset(&a,9);
	if (a[0]!=9 or a[4]!=9 or a[5]!=1234) return 1;
	fill(a[1..4],31);
	if (a[0]!=9 or a[1]!=31 or a[3]!=31 or a[4]!=9) return 2;
	@memset(&global,42);
	if (global[0]!=42 or global[2]!=42) return 3;
	var flags:[3]bool=.{false,false,false};
	set_bool(&flags);
	if (!flags[0] or !flags[2]) return 4;
	var byte:u8=1;
	var other:u8=2;
	var pointers:[2]*u8=.{&byte,&byte};
	set_pointer(&pointers,&other);
	if (pointers[0]!=&other or pointers[1].*!=2) return 5;
	var calls:u8=0;
	@memset(destination(&calls,&global), next(&calls));
	if (calls!=2 or global[0]!=77 or global[2]!=77) return 6;
	@memset(a[0..0],next(&calls));
	if (calls!=3 or a[0]!=9) return 7;
	const zero_pointer:[*]allowzero u8=@ptrFromInt(0);
	empty(zero_pointer[0..0]);
	var zeros:[3]u0=.{0,0,0};
	zero_bits(&zeros);
	if (zeros[2]!=0) return 8;
	var wide:[3]u33=.{1,2,3};
	@memset(&wide,0x1ffffffff);
	if(wide[0]!=0x1ffffffff or wide[2]!=0x1ffffffff) return 9;
	var signed:[3]i40=.{1,2,3};
	@memset(&signed,-123456789);
	if(signed[0]!=-123456789 or signed[2]!=-123456789) return 10;
	volatile_fill(&a);
	if(a[0]!=12345 or a[4]!=12345 or a[5]!=1234) return 11;
	return 0;
}
