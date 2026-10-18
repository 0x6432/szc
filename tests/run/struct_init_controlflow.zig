// exit: 0
// Seven successful initializer control-flow regressions combined.
// Known aggregate values fold; field blocks still run exactly once in source order.
var trace: u32 = 0;
var calls: u8 = 0;
const Pair = struct { a: u8, b: u64 };
const Triple = struct { a: u8, b: u64, c: u16 };

fn reset() void {
    trace = 0;
    calls = 0;
}
fn mark(n: u32) void {
    trace = trace * 10 + n;
    calls += 1;
}

fn earlyReturn() u8 {
    _ = Pair{
        .a = {
            mark(1);
            return 7;
        },
        .b = blk: {
            mark(2);
            break :blk @as(u64, 22);
        },
    };
    return 9;
}
fn missingAfterReturn() u8 {
    _ = Pair{ .a = return 7 };
    return 9;
}
fn missingAfterUnreachable(b: bool) u8 {
    if (b) {
        _ = Pair{ .a = unreachable };
    }
    return 7;
}
fn fieldNoreturnBranch(b: bool) u8 {
    const s = Pair{ .a = if (b) unreachable else @as(u8, 7), .b = 9 };
    return s.a;
}

pub fn main() u8 {
    reset();
    const typed = Triple{
        .c = blk: {
            mark(3);
            break :blk @as(u16, 33);
        },
        .a = blk: {
            mark(1);
            break :blk @as(u8, 11);
        },
        .b = blk: {
            mark(2);
            break :blk @as(u64, 22);
        },
    };
    if (calls != 3 or trace != 312) return 1;
    if (typed.a != 11 or typed.b != 22 or typed.c != 33) return 2;
    reset();
    _ = Pair{
        .b = blk: {
            mark(2);
            break :blk @as(u64, 22);
        },
        .a = blk: {
            mark(1);
            break :blk @as(u8, 11);
        },
    };
    if (calls != 2 or trace != 21) return 3;
    reset();
    const contextual: Pair = .{
        .b = blk: {
            mark(2);
            break :blk @as(u64, 22);
        },
        .a = blk: {
            mark(1);
            break :blk @as(u8, 11);
        },
    };
    if (calls != 2 or trace != 21 or contextual.a != 11 or contextual.b != 22) return 4;
    reset();
    if (earlyReturn() != 7 or calls != 1 or trace != 1) return 5;
    if (missingAfterReturn() != 7) return 6;
    // Trap branches are semantically analyzed but intentionally not executed.
    if (missingAfterUnreachable(false) != 7) return 7;
    if (fieldNoreturnBranch(false) != 7) return 8;
    return 0;
}
