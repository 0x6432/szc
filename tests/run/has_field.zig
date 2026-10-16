// exit: 0
const N=struct {
	pub const len:u8=256;
	const ptr:u8=256;
	pub fn bad() u8 { return 256; }
	const Child=struct {};
	const self_field=@hasField(@This(),"self_field");
};
pub fn main() u8 {
	if(!@hasField([0]u0,"len") or !@hasField([3:0]u8,"len")) return 1;
	if(@hasField([3]u8,"ptr") or @hasField([3]u8,"unknown")) return 2;
	if(!@hasField([]u8,"len") or !@hasField([]const u8,"ptr")) return 3;
	if(!@hasField([:0]const u8,"ptr") or !@hasField([]align(4) volatile u8,"len")) return 4;
	if(@hasField(N,"len") or @hasField(N,"ptr") or @hasField(N,"bad")) return 5;
	if(@hasField(@This(),"main") or @hasField(N.Child,"") or N.self_field) return 6;
	const name:[]const u8=".len."[1..4];
	if(!@hasField([2]u8,name) or !@hasField([2]u8,&.{108,101,110})) return 7;
	if(!@hasField([]u8,"l"++"en") or !@hasField([]u8,"ptr\x00tail")) return 8;
	if(!@hasField([2]u8,"len\x00\x00") or @hasField([]u8,"\x00len")) return 9;
	const T=[if(@hasField([2]u8,"len"))1 else 2]u8;
	const a:T=.{7};
	if(a[0]!=7 or !@hasField(T,"len")) return 10;
	const answer=@hasField([]u8,"ptr");
	const pointer:*const bool=&answer;
	if(!pointer.*) return 11;
	return 0;
}
