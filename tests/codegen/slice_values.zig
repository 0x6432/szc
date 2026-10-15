var a: [2]u8 = .{7,9};
fn create(p: *[2]u8) []u8 { return p; }
fn read(s: []const u8, n: usize) u8 { return s[n]; }
pub fn main() u8 { return read(create(&a),1); }
