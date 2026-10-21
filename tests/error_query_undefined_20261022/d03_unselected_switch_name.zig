comptime { const y = switch (@as(u8, 0)) { 0 => "ok", else => @errorName(@as(error{}, undefined)) }; _ = y; }
pub fn main() void {}
