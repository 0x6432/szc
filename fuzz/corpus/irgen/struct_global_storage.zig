const Inner = struct { x: u16, tag: u8 };
const State = struct { inner: Inner, row: [2]u8 };
var g: State = .{ .inner = .{ .x = 513, .tag = 3 }, .row = .{ 4, 5 } };
noinline fn change(p: *State) State {
	const before = p.*;
	p.inner.x = 1027;
	return before;
}
pub fn main() u8 {
	const before = change(&g);
	g.row[1] = @truncate(g.inner.x);
	return @truncate(before.inner.x + g.row[1]);
}
