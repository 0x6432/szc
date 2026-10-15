// exit: 0
const bytes: [8]u8 align(16) = .{1,2,3,4,5,6,7,8};
const first: *align(16) const u8 = &bytes[0];
const second: *align(2) const u8 = &bytes[2];
const fourth: *align(4) const u8 = &bytes[4];
const weak: [2]u64 align(1) = .{4294967297,4294967298};
const weak_second: *align(1) const u64 = &weak[1];
fn read(ptr: *align(1) const u64) u64 { return ptr.*; }
fn qualified(ptr: *const volatile u8) u8 { return ptr.*; }
fn local(n: u8, index: usize) u8 {
    var a: [8]u8 align(16) = .{n,2,3,4,5,6,7,8};
    const p: *align(16) u8 = &a[0];
    const q: *align(4) u8 = &a[4];
    const r: *align(2) u8 = &a[2];
    const dynamic: *u8 = &a[index];
    p.* += 1;
    q.* += 1;
    r.* += 1;
    return p.* + q.* + r.* + dynamic.*;
}
pub fn main() u8 {
    if (first.* != 1 or second.* != 3 or fourth.* != 5) return 1;
    if (read(weak_second) != 4294967298 or qualified(&bytes[7]) != 8) return 2;
    if (local(1,1) != 14) return 3;
    return 0;
}
