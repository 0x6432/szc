pub fn main() void { outer: while (false) { defer continue :outer; } }
