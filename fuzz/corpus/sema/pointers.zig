fn update(p: *u8) *const u8 {
    p.* += 1;
    return &p.*;
}
pub fn main() u8 {
    var n: u8 = 40;
    return update(&n).*;
}
