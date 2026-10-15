// exit: 0
// outlined-data: true
const a: [2][2]u8 align(128) = .{.{1,2},.{3,4}};
const p = &a[1][1];
const table: [2]*const u8 align(128) = .{p,&"ab"[1]};
fn read(ptr: *const u8) u8 { return ptr.*; }
fn indirect(ptr: *const [2]*const u8, index: usize) u8 { return read(ptr[index]); }
pub fn main() u8 { return if (read(p) == 4 and indirect(&table,0) == 4 and indirect(&table,1) == 98) 0 else 1; }
