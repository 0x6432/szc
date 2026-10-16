fn copy(d:[][3]u16,s:[]const [3]u16) void { @memcpy(d,s); }
pub fn main() u8 { var a:[1][3]u16=undefined; const b:[1][3]u16=.{.{7,9,11}}; copy(&a,&b); return if(a[0][2]==11) 0 else 1; }
