pub fn main() void { target: for (0..2) |_| { target: { break :target; } break :target; } }
