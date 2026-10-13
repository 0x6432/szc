// exit: 0
const message = value: { defer {} break :value "global panic"; };
fn stop() noreturn { @panic(message); }
fn branch(c: bool) u8 { if (c) stop(); return 7; }
fn choose(c: bool, fail: bool) void { const text = if (c) "left" else "rght"; if (fail) @panic(text); }
fn mutate(fail: bool) void { var text = "first"; text = "later"; if (fail) @panic(text); }
fn cleanup(c: bool) u8 { if (c) { defer @panic("cleanup"); return 1; } return 2; }
fn known(fail: bool) void { const text = if (true) "short" else "a longer message"; if (fail) @panic(text); }
fn multiline(fail: bool) void {
    const text =
        \\first line
        \\raw \u{41}
    ;
    if (fail) @panic(text);
}
fn arguments(fail: bool) void { if (fail) @panic(@panic("inner")); }
fn unreachable_argument(fail: bool) void { if (fail) @panic(unreachable); }
pub fn main() u8 {
    if (branch(false) != 7 or cleanup(false) != 2) return 1;
    choose(true, false); choose(false, false); mutate(false); known(false); multiline(false);
    arguments(false); unreachable_argument(false);
    return 0;
}
