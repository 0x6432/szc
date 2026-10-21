comptime { _ = @errorCast(@as(error{A}, undefined)); }
pub fn main() void {}
