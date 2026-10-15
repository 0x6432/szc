// exit: 0
const left: u8 align(256) = if (&left == &left and &right == &right) 7 else 0;
const right: i40 align(1) = if (&left == &left) -9 else 0;
const callback: *const fn () u8 align(256) = if (&callback == &callback) target else other;
const link: *align(256) const u8 align(256) = &left;
const inferred = &link;
const folded: u8 = inferred.*.*;
const yes: bool = if (&yes == &yes) true else false;
const zero: u0 align(64) = (&zero).*;
const empty: void align(64) = (&empty).*;
var mutable: u8 = if (&left == &left) 11 else 0;
const value_first: u16 = if (&value_first == &value_first) 31 else 0;
const unused_bad: u8 = 256;
fn target() u8 { return 42; }
fn other() u8 { return 1; }
fn read(p: *align(1) const u8) u8 { return p.*; }
fn signed_read(p: *align(1) const i40) i40 { return p.*; }
fn invoke(p: *align(256) const *const fn () u8) u8 { return p.*(); }
fn chain(p: *align(256) const *align(256) const u8) u8 { return p.*.*; }
fn read_bool(p: *const bool) bool { return p.*; }
fn read_u16(p: *const u16) u16 { return p.*; }
pub fn main() u8 {
    if (value_first != 31 or read_u16(&value_first) != 31) return 1;
    if (read(&left) != 7 or signed_read(&right) != -9) return 2;
    if (chain(&link) != 7 or folded != 7 or inferred.* != &left) return 3;
    if (invoke(&callback) != 42 or !read_bool(&yes)) return 4;
    if (mutable != 11 or read(&mutable) != 11) return 5;
    const again: *align(1) const u8 = &left;
    if (again != &left or (&left).* != 7 or (&right).* != -9) return 6;
    if (zero != 0) return 7;
    _ = empty;
    _ = &zero;
    _ = &empty;
    return 0;
}
