// IR-only policy lock. Do not imply runtime struct backing/ABI acceptance.
const S = struct { a: u8 };
const Empty = struct {};
pub fn main() void {
    const named: *const S = &.{ .a = 1 };
    const explicit = &S{ .a = 2 };
    const empty: *const Empty = &.{};
    const array: *const [1]u8 = &.{3};
    const slice: [:7]const u8 = &.{};
    const many: [*:7]const u8 = &.{};
    _ = named;
    _ = explicit;
    _ = empty;
    _ = array;
    _ = slice;
    _ = many;
}
