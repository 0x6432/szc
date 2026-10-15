// exit: 0
const first = [_]u8{7, 9, 11};
const selected: u8 = first[1];
const nested: [2][2]u8 = .{ .{1, 2}, .{3, 4} };
const wide: [2]u16 = [_]u8{1} ++ [_]u16{258};
const text = "ab" ++ "cd";
const mixed = [_]u8{42} ++ "x";
const tail = "q" ++ [_]u8{43};
const plain: [2]u8 = "hi".*;
const empty = [_]u8{};
const flags = [_]bool{true, false};
const types = [_]type{u8, u16};
const undef_array: [3]u8 = undefined;
const undef_pointer: *const [5]u8 = undefined;
const partly = [_]u8{undefined, 13} ++ "z";
const huge: *const [4294967296]u0 = undefined;
const max: *const [18446744073709551615]u0 = undefined;
const callback = [_]*const fn () u8{target, other};
fn target() u8 { return 23; }
fn other() u8 { return 29; }
fn add(a: u8, b: u8) u8 { return a + b; }
pub fn main() u8 {
    if (add(selected, first[0]) != 16) return 1;
    if (nested[1][0] != 3 or wide[1] != 258) return 2;
    if (text.len != 4 or text[3] != 100 or text[4] != 0) return 3;
    if (mixed.len != 2 or mixed[0] != 42 or mixed[2] != 0) return 4;
    if (tail[0] != 113 or tail[1] != 43 or tail[2] != 0) return 5;
    if (plain[1] != 105 or (empty ++ first)[2] != 11) return 6;
    if (!flags[0] or flags[1] or types[0] != u8 or types[1] != u16) return 7;
    if (undef_array.len != 3 or undef_pointer.len != 5) return 8;
    if (partly[1] != 13 or partly[2] != 122 or partly[3] != 0) return 9;
    if (huge.len != 4294967296 or max.len != 18446744073709551615) return 10;
    if ([4294967296]u0 == [0]u0 or [2]u8 != [2]u8) return 11;
    if (callback[1]() != 29) return 12;
    const casted: [2]u8 = .{ @intCast(@as(u16, 17)), 19 };
    if (casted[0] != 17) return 13;
    return 0;
}
