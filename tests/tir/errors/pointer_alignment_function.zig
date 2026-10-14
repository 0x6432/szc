fn f() void {} pub fn main() void { const p: *align(16) const fn () void = &f; p(); }
