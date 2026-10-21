comptime { _ = @as(u8, @errorCast(@as(error{A}, undefined))); }
pub fn main() void {}
