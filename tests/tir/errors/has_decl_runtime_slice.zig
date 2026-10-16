fn f(name:[]const u8) bool { return @hasDecl(@This(),name); }
pub fn main() void { _=f("x"); }
