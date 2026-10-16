// exit: 0
var calls:u8=0;
var global:u8=9;
const RuntimeType=@TypeOf(value());
const ArrayType=@TypeOf(array());
fn value() u8 { calls+=1; return 7; }
fn array() [2]u8 { calls+=1; return .{1,2}; }
fn conditional(b:bool) u8 {
	_=@TypeOf(if(b) return 99 else @as(u8,3));
	return 4;
}
fn controls(b:bool) u8 {
	outer: {
		_=@TypeOf(if(b) break :outer else @as(u8,3));
		if(b) return 5;
	}
	return 6;
}
fn continue_query(b:bool) u8 {
	var i:u8=0;
	while(i<3):(i+=1) {
		_=@TypeOf(if(b) continue else @as(u8,1));
		global+=1;
	}
	return i;
}
fn aggregate(a:[2]u8,b:bool) [2]u8 {
	_=@TypeOf(if(b) return .{99,100} else a);
	return a;
}
fn captured(a:u8,b:u16,s:[]const u8,p:*volatile u8) u16 {
	if(@TypeOf(a+1)!=u8 or @TypeOf(a,b)!=u16) return 100;
	if(@TypeOf(s.ptr)!=[*]const u8 or @TypeOf(s[0])!=u8) return 101;
	if(@TypeOf(p.*)!=u8 or @TypeOf(@field(s,"len"))!=usize) return 102;
	_=@TypeOf({ p.*=77; });
	_=@TypeOf(@memset(@as(*[0]u8,@ptrFromInt(16)),0));
	return a+b;
}
pub fn main() u8 {
	if(RuntimeType!=u8 or ArrayType!=[2]u8 or calls!=0) return 1;
	if(@TypeOf(value())!=u8 or @TypeOf(array())!=[2]u8) return 2;
	if(@TypeOf(@TypeOf(value()))!=type or calls!=0) return 3;
	if(@TypeOf(@as(u8,undefined),@as(i8,undefined))!=u8) return 4;
	if(@TypeOf(@as(i8,undefined),@as(u8,undefined))!=i8) return 5;
	if(@TypeOf(@as(u8,undefined),@as(i8,undefined),@as(i16,undefined))!=i16) return 6;
	if(@TypeOf(@as(u8,1),256)!=u8 or @TypeOf(1,2,3)!=comptime_int) return 7;
	if(@TypeOf("a","abc")!=[:0]const u8) return 8;
	if(@TypeOf(@as(f32,undefined),@as(f64,undefined))!=f64) return 9;
	var data=[_]u8{11,12,13};
	const p:*volatile u8=&data[0];
	_=@TypeOf({ p.*=90; });
	_=@TypeOf(@memset(&data,55));
	_=@TypeOf(@memcpy(data[0..1],data[1..2]));
	_=@TypeOf({ global=0; });
	_=@TypeOf(blk: { global=0; break :blk @as(u8,0); });
	if(data[0]!=11 or data[1]!=12 or data[2]!=13 or global!=9) return 10;
	if(captured(1,2,&data,p)!=3 or p.*!=11) return 11;
	if(conditional(true)!=4 or conditional(false)!=4) return 12;
	if(controls(true)!=5 or controls(false)!=6) return 13;
	const T=@TypeOf(@as(*align(32) const volatile u8,undefined));
	if(T!=*align(32) const volatile u8) return 14;
	const answer:RuntimeType=7;
	const pair:ArrayType=.{answer,8};
	if(pair[0]!=7 or pair[1]!=8 or calls!=0) return 15;
	if(continue_query(true)!=3 or global!=12) return 16;
	const result=aggregate(.{21,22},true);
	if(result[0]!=21 or result[1]!=22) return 17;
	return 0;
}
