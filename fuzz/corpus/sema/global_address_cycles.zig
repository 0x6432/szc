// exit: 0
// Resolving inferred first must not force typed's initializer before its own type.
var inferred = &typed;
var typed: u8 = if (&inferred == &inferred) 7 else 0;
var first: u8 = if (&second == &second) 11 else 0;
var second: u8 = if (&first == &first) 13 else 0;
var zero: u0 = (&zero).*;
var distinct: u8 = 7;
const different = &typed != &distinct;
pub fn main() u8 {
    if (inferred.* != 7) return 1;
    if (first != 11 or second != 13) return 2;
    if (!different) return 3;
    zero = 0;
    return @as(u8, zero);
}
