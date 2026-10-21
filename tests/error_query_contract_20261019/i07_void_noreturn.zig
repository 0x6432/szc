fn v() void {}
export fn probe(flag: bool) void { v(); if (flag) unreachable; }
pub fn main() void {}
