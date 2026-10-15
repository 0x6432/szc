const values: [*]const [2]u8 = &.{.{1,2},.{3,4}};
pub fn main() u8 { return values[1][0]; }
