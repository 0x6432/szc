pub fn main() void { outer: { defer { break :outer; } } }
