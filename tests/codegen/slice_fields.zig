var s: []const u8 = "hello";
const length = &s.len;
fn trim(p: *[]const u8) void { p.ptr += 1; p.len -= 1; }
pub fn main() u8 { trim(&s); return @intCast(length.*); }
