// exit: 0
var global:[2][3]u16=.{.{0,0,0},.{0,0,0}};
fn arrays(d:[][3]u16,s:[]const [3]u16) void { @memcpy(d,s); }
fn descriptors(d:[][]u8,s:[]const []u8) void { @memcpy(d,s); }
fn const_descriptors(d:[][]const u8,s:[]const []u8) void { @memcpy(d,s); }
fn empty(d:[]allowzero [3]u8,s:[]allowzero const [3]u8) void { @memcpy(d,s); }
fn many(d:[*][3]u16,s:[]const [3]u16) void { @memcpy(d,s); }
pub fn main() u8 {
	const source:[2][3]u16=.{.{7,9,11},.{13,15,17}};
	var destination:[2][3]u16=undefined;
	@memcpy(&destination,&source);
	if(destination[0][0]!=7 or destination[1][2]!=17) return 1;
	arrays(&global,&source);
	if(global[0][2]!=11 or global[1][0]!=13) return 2;
	many(&global,source[0..1]);
	arrays(destination[0..1],source[1..2]);
	if(destination[0][0]!=13 or destination[1][2]!=17) return 3;
	var a:[2][2]i40=undefined;
	const b:[2][2]i40=.{.{-549755813888,549755813887},.{-123456789,123456789}};
	@memcpy(&a,&b);
	if(a[0][0]!=-549755813888 or a[0][1]!=549755813887 or a[1][0]!=-123456789) return 4;
	var tail:[2][2:17]u16=.{.{1,2},.{3,4}};
	const tail_source:[2][2:17]u16=.{.{7,9},.{11,13}};
	@memcpy(&tail,&tail_source);
	if(tail[0][0]!=7 or tail[0][2]!=17 or tail[1][1]!=13 or tail[1][2]!=17) return 5;
	var bytes:[3]u8=.{7,9,11};
	const slices:[2][]u8=.{bytes[0..2],bytes[1..3]};
	var borrowed:[2][]u8=undefined;
	descriptors(&borrowed,&slices);
	if(borrowed[0].len!=2 or borrowed[1][0]!=9) return 6;
	borrowed[0][1]=99;
	if(bytes[1]!=99 or slices[1][0]!=99) return 7;
	var readonly:[2][]const u8=undefined;
	const_descriptors(&readonly,&slices);
	if(readonly[0].ptr!=slices[0].ptr or readonly[1].len!=2 or readonly[1][0]!=99) return 8;
	const null_pointer:[*]allowzero [3]u8=@ptrFromInt(0);
	empty(null_pointer[0..0],null_pointer[0..0]);
	arrays(destination[0..0],source[0..0]);
	return 0;
}
