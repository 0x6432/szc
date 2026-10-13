// exit: 0
fn block_exit(c: bool) usize {
    var history: usize = 0;
    const result: usize = done: {
        defer history = history * 10 + 1;
        { defer history = history * 10 + 2; if (c) break :done 7; }
        break :done 8;
    };
    return history * 10 + result;
}
fn operand() usize {
    var value: usize = 2;
    const saved: usize = done: { defer value += 10; break :done value; };
    return value * 10 + saved;
}
fn branches(c: bool, d: bool) usize {
    var history: usize = 0;
    done: {
        defer { const digit: usize = if (d) 3 else 4; history = history * 10 + digit; }
        if (c) break :done;
        history = 1;
        break :done;
    }
    return history;
}
fn internal_break() usize {
    var total: usize = 0;
    { defer { total += result: { break :result 7; }; defer total += 2; } }
    return total;
}
fn branch_registration(c: bool) usize {
    var total: usize = 0;
    if (c) { defer total += 7; total += 1; } else { defer total += 9; total += 2; }
    return total;
}
fn map_growth(c: bool, x: usize) usize {
    var total: usize = 0;
    done: {
        defer {
            const v0: usize = x + 0;
            const v1: usize = x + 1;
            const v2: usize = x + 2;
            const v3: usize = x + 3;
            const v4: usize = x + 4;
            const v5: usize = x + 5;
            const v6: usize = x + 6;
            const v7: usize = x + 7;
            const v8: usize = x + 8;
            const v9: usize = x + 9;
            const v10: usize = x + 10;
            const v11: usize = x + 11;
            const v12: usize = x + 12;
            const v13: usize = x + 13;
            const v14: usize = x + 14;
            const v15: usize = x + 15;
            const v16: usize = x + 16;
            const v17: usize = x + 17;
            const v18: usize = x + 18;
            const v19: usize = x + 19;
            const v20: usize = x + 20;
            const v21: usize = x + 21;
            const v22: usize = x + 22;
            const v23: usize = x + 23;
            const v24: usize = x + 24;
            const v25: usize = x + 25;
            const v26: usize = x + 26;
            const v27: usize = x + 27;
            const v28: usize = x + 28;
            const v29: usize = x + 29;
            const v30: usize = x + 30;
            const v31: usize = x + 31;
            const v32: usize = x + 32;
            const v33: usize = x + 33;
            const v34: usize = x + 34;
            const v35: usize = x + 35;
            const v36: usize = x + 36;
            const v37: usize = x + 37;
            const v38: usize = x + 38;
            const v39: usize = x + 39;
            const v40: usize = x + 40;
            const v41: usize = x + 41;
            const v42: usize = x + 42;
            const v43: usize = x + 43;
            const v44: usize = x + 44;
            const v45: usize = x + 45;
            const v46: usize = x + 46;
            const v47: usize = x + 47;
            total = v0 + v1 + v2 + v3 + v4 + v5 + v6 + v7 + v8 + v9 + v10 + v11 + v12 + v13 + v14 + v15 + v16 + v17 + v18 + v19 + v20 + v21 + v22 + v23 + v24 + v25 + v26 + v27 + v28 + v29 + v30 + v31 + v32 + v33 + v34 + v35 + v36 + v37 + v38 + v39 + v40 + v41 + v42 + v43 + v44 + v45 + v46 + v47;
        }
        if (c) break :done;
        break :done;
    }
    return total;
}
pub fn main() u8 {
    if (block_exit(true) != 217 or block_exit(false) != 218 or operand() != 122) return 1;
    if (branches(true, true) != 3 or branches(true, false) != 4 or branches(false, true) != 13 or branches(false, false) != 14) return 2;
    if (internal_break() != 9 or branch_registration(true) != 8 or branch_registration(false) != 11) return 3;
    if (map_growth(true, 1) != 1176 or map_growth(false, 2) != 1224) return 4;
    return 0;
}
