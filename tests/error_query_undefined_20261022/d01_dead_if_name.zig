comptime { if (false) { _ = @errorName(@as(error{}, undefined)); } }
pub fn main() void {}
