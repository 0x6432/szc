// exit: 0
var global:[3][0]type=.{.{},.{},.{}};
var aligned_global:[2][0]u128=undefined;
var effects:u8=0;
fn identity(a:[2][0]type) [2][0]type { effects+=1; return a; }
fn wide(a:[2][0]u128) [2][0]u128 { effects+=1; return a; }
fn fill(d:[][0]type) void { @memset(d,.{}); }
fn copy(d:[][0]type,s:[]const [0]type) void { @memcpy(d,s); }
fn floats(a:[2][0]f64) [2][0]f64 { effects+=1; return a; }
pub fn main() u8 {
	var a:[2][0]type=.{.{},.{}};
	const b=identity(a);
	if(effects!=1 or b.len!=2 or b[0].len!=0) return 1;
	fill(&a);
	copy(&a,global[1..3]);
	global[0]=.{};
	var integers:[2][0]u128=undefined;
	integers=wide(integers);
	aligned_global=integers;
	if(effects!=2 or integers.len!=2 or aligned_global[1].len!=0) return 2;
	if(@sizeOf([2][0]u128)!=0 or @alignOf([2][0]u128)!=16) return 3;
	if(@intFromPtr(&integers)&15!=0 or @intFromPtr(&aligned_global)&15!=0) return 4;
	var floating:[2][0]f64=undefined;
	floating=floats(floating);
	if(effects!=3 or floating[1].len!=0 or @alignOf([2][0]f64)!=8) return 5;
	var functions:[2][0]fn() void=undefined;
	@memset(&functions,.{});
	if(functions[0].len!=0) return 6;
	var types:[0][2]type=undefined;
	_=&types;
	if(types.len!=0) return 7;
	for(&a) |*element| element.*=.{};
	const pointer:[*]allowzero [0]type=@ptrFromInt(0);
	const range=pointer[0..3];
	@memset(range,.{});
	@memcpy(range,range);
	if(range.len!=3) return 8;
	return 0;
}
