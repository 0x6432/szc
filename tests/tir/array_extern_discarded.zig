extern fn accept(a: [2]u8) void;
extern fn make() [2]u8;
pub fn main() void { _ = &accept; _ = &make; }
