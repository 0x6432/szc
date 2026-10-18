const S = struct { a: u8, b: u8 };
const Outer = struct { inner: S };
const global_value: S = .{ .b = 2, .a = 1 };
fn returned() S {
    return .{ .b = 4, .a = 3 };
}
fn returned_typed() S {
    return S{ .b = 6, .a = 5 };
}
pub fn main() void {
    const local: S = .{ .b = 8, .a = 7 };
    const explicit = S{ .b = 10, .a = 9 };
    const cast = @as(S, .{ .b = 12, .a = 11 });
    const nested: Outer = .{ .inner = .{ .b = 14, .a = 13 } };
    const nested_cast = @as(Outer, .{ .inner = .{ .b = 16, .a = 15 } });
    const array: [1]S = .{.{ .b = 18, .a = 17 }};
    const typed_array = [1]S{.{ .b = 20, .a = 19 }};
    _ = local;
    _ = explicit;
    _ = cast;
    _ = nested;
    _ = nested_cast;
    _ = array;
    _ = typed_array;
}
