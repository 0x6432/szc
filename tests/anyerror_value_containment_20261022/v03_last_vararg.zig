extern fn sink(...) void; export fn f() void { sink(@as(c_int, 1), @as(anyerror, undefined)); }
pub fn main() void {}
