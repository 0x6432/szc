fn fill(p: []u16, n: u16) void { @memset(p,n); }
pub fn main() u8 {
	var data:[2]u16=.{1,2};
	fill(&data,1234);
	return if(data[1]==1234) 0 else 1;
}
