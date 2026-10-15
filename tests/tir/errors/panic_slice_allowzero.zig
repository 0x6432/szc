fn crash(s: []allowzero const u8) noreturn { @panic(s); } pub fn main() void { crash(undefined); }
