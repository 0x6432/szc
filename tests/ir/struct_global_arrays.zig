const Cell = struct { x: u16, bytes: [1:9]u8 };
var rows: [2][1]Cell = .{ .{.{ .x = 1, .bytes = .{2} }}, .{.{ .x = 3, .bytes = .{4} }} };
var empty: [0]Cell = .{};
noinline fn index() usize { return 1; }
pub fn main() u8 {
	const old = rows;
	rows[index()][0].x = 7;
	return @truncate(old[1][0].x + rows[1][0].x + empty.len);
}
