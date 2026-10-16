const N=struct { pub const len:u8=256; pub fn bad() u8 { return 256; } };
pub fn main() u8 {
	if(!@hasField([3:0]u8,"len") or !@hasField([]u8,"ptr")) return 1;
	if(@hasField(N,"len") or @hasField(@This(),"main")) return 2;
	if(!@hasField([]u8,"len\x00tail")) return 3;
	return 0;
}
