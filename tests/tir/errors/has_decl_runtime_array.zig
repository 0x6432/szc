pub fn main() void { var a=[_]u8{120}; _=&a; _=@hasDecl(@This(),&a); }
