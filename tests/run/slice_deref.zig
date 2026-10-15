// exit: 0
var a: [4]u16 align(32) = .{2,3,5,7};
const whole: []align(32) u16 = &a;
const middle: []u16 = a[1..3];
const text: []const u8 = "hello";
fn snapshot() [2]u16 { return (&middle.*).*; }
fn first(p: *[2]u16) u16 { return p[0]; }
pub fn main() u8 {
    const owned = snapshot();
    const borrowed = &middle.*;
    if (first(borrowed) != 3 or &borrowed[1] != &a[2]) return 1;
    borrowed[0] = 11;
    if (a[1] != 11 or owned[0] != 3) return 2;
    const aligned: *align(32) [4]u16 = &whole.*;
    if (aligned[3] != 7) return 3;
    const empty_view: []u16 = a[1..1];
    const empty: [0]u16 = (&empty_view.*).*;
    if (empty.len != 0) return 8;
    const word = text.*;
    if (word.len != 5 or word[1] != 101) return 4;
    const types = [2]type{u8,u16};
    const ts: []const type = &types;
    const T = ts.*[1];
    if (@as(T,257) != 257) return 5;
    const zeros = [2]u0{0,0};
    const zs: []const u0 = &zeros;
    const zero: u0 = zs.*;
    if (zero != 0 or (&zs.*).len != 2) return 6;
    const tiny = [1][2]u0{.{0,0}};
    const tiny_slice: []const [2]u0 = &tiny;
    const singleton: [2]u0 = tiny_slice.*;
    if (singleton[0] != 0 or singleton[1] != 0) return 7;
    const nothing: []void = undefined;
    const unit: void = nothing.*;
    _ = unit;
    return 0;
}
