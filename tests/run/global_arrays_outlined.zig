// exit: 0
// outlined-data: true
var aligned: [2]u64 align(256) = .{4294967297,7};
var weak: [2]u64 align(1) = .{4294967298,9};
var pointers: [2]*align(1) u64 align(128) = .{&aligned[1],&weak[0]};
fn write(p: *align(1) u64, n: u64) void { p.* = n; }
fn read(p: *align(1) const u64) u64 { return p.*; }
pub fn main() u8 {
    write(pointers[0],13);
    write(pointers[1],4294967299);
    if (aligned[1] != 13 or read(&weak[0]) != 4294967299) return 1;
    aligned = .{4294967300,17};
    const copy = aligned;
    aligned[0] = 1;
    if (copy[0] != 4294967300 or aligned[0] != 1) return 2;
    return 0;
}
