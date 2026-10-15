fn crash(s: []volatile const u8) noreturn { @panic(s); } pub fn main() void { crash(undefined); }
