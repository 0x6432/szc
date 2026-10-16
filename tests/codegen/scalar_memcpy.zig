fn copy(d:[]volatile u16,s:[]const volatile u16) void { @memcpy(d,s); }
pub fn main() u8 { var a:[2]u16=undefined; const b:[2]u16=.{7,9}; copy(&a,&b); return if(a[1]==9) 0 else 1; }
