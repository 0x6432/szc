// exit: 0
const a = [5]u16{7,9,17,21,17};
const known: [:17]const u16 = a[0..2:17];
fn cut(p: []u16, first: usize, last: usize) [:17]u16 { return p[first..last:17]; }
fn array_cut(p: *[5]u16) *[2:17]u16 { return p[0..2:17]; }
fn many_cut(p: [*]u16, last: usize) [:17]u16 { return p[0..last:17]; }
fn run(n: usize) u8 {
    if (known.len != 2 or known[2] != 17) return 1;
    var backing = [5]u16{7,9,17,21,17};
    const s = cut(&backing,1,n);
    if (s.len != 1 or s[0] != 9 or s[1] != 17) return 2;
    s[0] = 13;
    if (backing[1] != 13) return 3;
    const p = array_cut(&backing);
    if (p[2] != 17) return 4;
    const tail = many_cut(&backing,4);
    if (tail.len != 4 or tail[4] != 17) return 5;
    const typed = backing[1..2:@intCast(17)];
    if (typed[0] != 13 or typed[1] != 17) return 6;
    const flags = [3]bool{false,true,true};
    const bs = flags[0..1:true];
    if (bs[0] or !bs[1]) return 7;
    const sent = [2:17]u16{7,9};
    const mp: [*:17]const u16 = &sent;
    const shifted = mp[1.. :17];
    if (shifted[0] != 9 or shifted[1] != 17) return 8;
    return 0;
}
pub fn main() u8 { return run(2); }
