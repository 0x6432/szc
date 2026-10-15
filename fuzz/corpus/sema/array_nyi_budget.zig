const huge: [1048577]u8 = undefined;
pub fn main() void { _ = huge ++ [_]u8{}; }
