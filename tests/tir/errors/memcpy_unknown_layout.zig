pub fn main() void { const d:[*]anyerror=@ptrFromInt(4096); const s:[*]const anyerror=@ptrFromInt(8192); @memcpy(d[0..1],s[0..1]); }
