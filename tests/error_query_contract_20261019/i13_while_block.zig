export fn block_probe(flag: bool) void { while (flag) { @as(u8,1); break; } }
export fn else_probe(flag:bool) void { while(flag) { break; } else @as(u8,3); }
export fn labeled_probe() void { outer: while(true) { break :outer @as(u8,7); } }
pub fn main() void {}
