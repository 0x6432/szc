fn copy(d: []u16, s: []const u16) void { @memcpy(d,s); }
pub fn main() u8 {
	var data:[2]u16=.{1,2};
	const source:[2]u16=.{7,9};
	copy(&data,&source);
	return if(data[1]==9) 0 else 1;
}
