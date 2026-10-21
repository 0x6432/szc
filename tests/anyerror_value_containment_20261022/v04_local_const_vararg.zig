extern fn sink(...) void; export fn f() void { const x: anyerror = undefined; sink(x); }
pub fn main() void {}
