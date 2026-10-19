// nc-lane: accept
test "not an anonymous comptime block" {@panic("test body ran");} comptime {const x:u8=1;_=x;}
pub fn main() void {}
