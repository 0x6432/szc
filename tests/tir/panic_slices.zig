fn crash(c: bool, s: []const u8) noreturn { @panic(if (c) s else "longer"); }
pub fn main() void { crash(true,"hi"); }
