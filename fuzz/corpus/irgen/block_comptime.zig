// exit: 0
const answer = value: { const x = 3; break :value x * 7; };
const T = value: { break :value u8; };
const nested = outer: { _ = inner: { if (false) break :inner 0; break :outer 23; }; };
const selected = value: { if (true) break :value 24; break :value @divTrunc(1, 0); };
const dead = value: { if (false) break :value @divTrunc(1, 0); break :value 25; };
fn increment(x: u8) u8 { return x + 1; }
const alias = value: { break :value increment; };
fn known_type(c: bool) u8 {
    const Local = value: { if (c) return 31; break :value u8; };
    const x: Local = 32;
    return x;
}
fn label_namespace() u8 { const value: u8 = 33; return value: { break :value value; }; }
fn quoted() u8 { return @"a label": { break :@"a label" 34; }; }
pub fn main() u8 {
    const x: T = answer;
    if (x != 21 or nested != 23 or selected != 24 or dead != 25) return 1;
    if (known_type(true) != 31 or known_type(false) != 32) return 2;
    if (label_namespace() != 33 or quoted() != 34) return 3;
    if (alias(34) != 35) return 4;
    return 0;
}
