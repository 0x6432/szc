fn f(c: bool) usize { const s = if (c) "hi" else "longer"; return s.len; } pub fn main() void { _ = f(true); }
