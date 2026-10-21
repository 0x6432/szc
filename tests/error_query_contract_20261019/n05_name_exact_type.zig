comptime { if(@TypeOf(@errorName(error.A)) != *const [1:0]u8) { const failed_assertion: u8 = false; _ = failed_assertion; } }
pub fn main() void {}
