// exit: 0
var global:[3][3]u16=undefined;
var effects:u8=0;
fn destination() *[3][3]u16 { effects=effects*10+1; return &global; }
fn value() [3]u16 { effects=effects*10+2; return .{7,9,11}; }
fn arrays(d:[][3]u16,v:[3]u16) void { @memset(d,v); }
fn empty(d:[]allowzero [3]u8,v:[3]u8) void { @memset(d,v); }
fn descriptors(d:[][]u8,v:[]u8) void { @memset(d,v); }
pub fn main() u8 {
	@memset(destination(),value());
	if(effects!=12 or global[0][0]!=7 or global[2][2]!=11) return 1;
	var a:[3][3]u16=.{.{1,2,3},.{4,5,6},.{7,8,9}};
	@memset(&a,a[1]);
	if(a[0][0]!=4 or a[1][1]!=5 or a[2][2]!=6) return 2;
	arrays(a[1..3],.{17,19,23});
	if(a[0][0]!=4 or a[1][1]!=19 or a[2][2]!=23) return 3;
	var odd:[3][2]i40=undefined;
	@memset(&odd,.{-549755813888,549755813887});
	if(odd[0][0]!=-549755813888 or odd[2][1]!=549755813887) return 4;
	var tail:[2][2:17]u16=undefined;
	@memset(&tail,.{31,37});
	if(tail[0][0]!=31 or tail[0][2]!=17 or tail[1][1]!=37 or tail[1][2]!=17) return 5;
	var bytes:[3]u8=.{41,43,47};
	var slices:[3][]u8=undefined;
	descriptors(&slices,bytes[1..3]);
	if(slices[0].len!=2 or slices[2][1]!=47) return 6;
	slices[0][0]=53;
	if(bytes[1]!=53 or slices[2][0]!=53) return 7;
	@memset(&slices,slices[1][1..2]);
	if(slices[0].len!=1 or slices[2][0]!=47) return 8;
	var readonly:[2][]const u8=undefined;
	@memset(&readonly,"hello");
	if(readonly[0].len!=5 or readonly[1][4]!='o') return 9;
	const null_pointer:[*]allowzero [3]u8=@ptrFromInt(0);
	empty(null_pointer[0..0],.{1,2,3});
	@memset(@as(*[0][3]u8,undefined),.{1,2,3});
	@memset(@as([][2]u0,undefined),.{0,0});
	var nested:[2][2][3]bool=undefined;
	@memset(&nested,.{.{true,false,true},.{false,true,false}});
	if(!nested[0][0][2] or !nested[1][1][1] or nested[1][1][2]) return 10;
	var raw:[6]u8=.{1,2,3,4,5,6};
	const overlap:*[3][2]u8=@ptrCast(&raw);
	const source:*const [2]u8=@ptrCast(&raw[1]);
	@memset(overlap,source.*);
	if(raw[0]!=2 or raw[1]!=3 or raw[2]!=2 or raw[3]!=3 or raw[4]!=2 or raw[5]!=3) return 11;
	effects=0;
	@memset(destination()[0..0],value());
	if(effects!=12) return 12;
	var pointees:[2]u8=.{73,79};
	var pointers:[3][2]*u8=undefined;
	@memset(&pointers,.{&pointees[0],&pointees[1]});
	if(pointers[0][0]!=&pointees[0] or pointers[2][1].*!=79) return 13;
	pointers[1][0].*=83;
	if(pointees[0]!=83 or pointers[2][0].*!=83) return 14;
	const unaligned:*align(1) [2][2]u33=@ptrCast(@alignCast(&odd));
	@memset(unaligned,.{8589934591,1});
	if(unaligned[0][0]!=8589934591 or unaligned[1][1]!=1) return 15;
	var sentinel_slices:[2][:0]const u8=undefined;
	@memset(&sentinel_slices,"ok");
	if(sentinel_slices[0].len!=2 or sentinel_slices[1][2]!=0) return 16;
	return 0;
}
