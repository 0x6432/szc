extern fn sink(...) void; export fn f() void { sink(@as(anyerror, undefined)); }
pub fn main() void {}
