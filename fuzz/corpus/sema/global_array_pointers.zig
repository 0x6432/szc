// exit: 0
var pointers: [2]*u8 = .{&later[0],&later[1]};
var later: [2]u8 = if (&later[0] == &later[0]) .{7,9} else .{1,2};
var saved: *u8 = &later[1];
const first_slot = &pointers[0];
const second = &later[1];
fn plus(n: u8) u8 { return n + 1; }
fn minus(n: u8) u8 { return n - 1; }
var functions = [_]*const fn (u8) u8{&plus,&minus};
const callback_slot = &functions[0];
var flags = [_]bool{true,false};
const volatile_flag: *volatile bool = &flags[1];
fn choose(i: usize) u8 { pointers[i].* += 1; return pointers[i].*; }
fn invoke(i: usize, n: u8) u8 { return functions[i](n); }
pub fn main() u8 {
    if (choose(0) != 8 or choose(1) != 10) return 1;
    first_slot.* = second;
    first_slot.*.* = 13;
    if (later[1] != 13 or saved.* != 13) return 2;
    saved = &later[0];
    saved.* = 17;
    if (later[0] != 17 or invoke(0,7) != 8 or invoke(1,7) != 6) return 3;
    callback_slot.* = &minus;
    if (invoke(0,7) != 6) return 4;
    volatile_flag.* = true;
    if (!volatile_flag.* or !flags[1]) return 5;
    return 0;
}
