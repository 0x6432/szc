// exit: 0
fn runtime(n: usize) usize { var total: usize = 0; for (0..n, 3.., 5..5 + n) |a, b, c| total += a + b + c; return total; }
fn known(n: usize) usize { var total: usize = 0; for (0..n, 3..6) |a, b| total += a + b; return total; }
fn leading(n: usize) usize { var total: usize = 0; for (7.., 0..n) |a, b| total += a - b; return total; }
fn discard() usize { var total: usize = 0; for (0..3, 7..10) |_, i| total += i; return total; }
fn all_discard() u8 { var total: u8 = 0; for (1..4, 6..9) |_, _| total += 1; return total; }
pub fn main() u8 {
    if (runtime(3) != 33 or runtime(0) != 0 or known(3) != 15) return 1;
    if (leading(3) != 21 or discard() != 24 or all_discard() != 3) return 2;
    return 0;
}
