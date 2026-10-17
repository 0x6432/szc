const S=struct { x:u8, };
fn store(p:*S) void { p.*=undefined; }
pub fn main() void { store(@ptrFromInt(4096)); }
