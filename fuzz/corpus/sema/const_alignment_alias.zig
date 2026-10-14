fn f(n: u32) void { const value: u32 align(64) = n; const alias = value; const p: *align(64) const u32 = &alias; _ = p; }
pub fn main() void { f(7); }
