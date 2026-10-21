extern fn sink(...) void; export fn f() void { sink(@as(c_int, 1)); }
pub fn main() void {}
