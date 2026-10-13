const message = "base";
fn fail(c: bool) noreturn { var text = message; text = if (c) "left" else "rght"; @panic(text); }
pub fn main() u8 { const text = "\x00\u{1f600}\n"; if (false) @panic(text); return 0; }
export fn abort_message(c: bool) noreturn { fail(c); }
