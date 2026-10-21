extern fn sink(...) void; export fn f() void { sink(@as(anyerror, undefined), @as(c_int, 1)); }
pub fn main() void {}
