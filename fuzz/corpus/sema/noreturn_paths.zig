// exit: 0
const constant: u8 = value: { if (false) unreachable; break :value 5; };
fn stop() noreturn { unreachable; }
fn infinite() noreturn { while (true) {} }
fn both(c: bool) noreturn { if (c) stop() else stop(); }
fn choose(c: bool, x: u8) u8 { return if (c) x else stop(); }
fn branch(c: bool) u8 { if (c) both(c); return 7; }
fn loop_branch(c: bool) u8 { if (c) infinite(); return 8; }
fn cleanup(c: bool) u8 { if (c) { defer stop(); return 1; } return 2; }
fn implicit_after_stop() bool { stop(); }
fn block(c: bool, x: u8) u8 { return value: { if (c) break :value x; stop(); }; }
pub fn main() u8 {
    if (constant != 5 or choose(true, 9) != 9 or branch(false) != 7) return 1;
    if (loop_branch(false) != 8 or cleanup(false) != 2 or block(true, 11) != 11) return 2;
    if (false and implicit_after_stop()) return 3;
    if (!(true or implicit_after_stop())) return 4;
    return 0;
}
