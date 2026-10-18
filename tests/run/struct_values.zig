// exit: 0
// Immutable known aggregates only. No struct ABI, field addresses or runtime materialization.
const Global = struct {
    x: u8,
};
const global = Global{ .x = 9 };
const NativeInner = struct {
    x: u8,
};
const NativeEmpty = struct {};
pub fn main() u8 {
    if (global.x != 9) return 1;
    { // scalar/u8/as
        const S = struct {
            x: u8,
        };
        const s = @as(S, .{ .x = 7 });
        if (!(s.x == 7 and @field(s, "x") == 7 and @TypeOf(s) == S and @TypeOf(s.x) == u8)) return 2;
    }
    { // scalar/u128/typed
        const S = struct {
            x: u128,
        };
        const s = S{ .x = 0x10000000000000000 };
        if (!(s.x == 0x10000000000000000 and @field(s, "x") == 0x10000000000000000 and @TypeOf(s) == S and @TypeOf(s.x) == u128)) return 3;
    }
    { // order/0/typed
        const S = struct {
            a: u8,
            b: u64,
            c: u16,
        };
        const s = S{ .c = 33, .b = 22, .a = 11 };
        if (!(s.a == 11 and s.b == 22 and s.c == 33)) return 4;
    }
    { // order/5/context
        const S = struct {
            c: u16,
            b: u64,
            a: u8,
        };
        const s: S = .{ .a = 11, .b = 22, .c = 33 };
        if (!(s.a == 11 and s.b == 22 and s.c == 33)) return 5;
    }
    { // copy
        const S = struct {
            x: u8,
        };
        const s = S{ .x = 7 };
        const copy: S = s;
        if (!(copy.x == 7 and @TypeOf(copy) == S)) return 6;
    }
    { // temporary
        const S = struct {
            x: u8,
        };

        if (!((S{ .x = 13 }).x == 13 and @field(S{ .x = 14 }, "x") == 14)) return 7;
    }
    { // nested-typed
        const S = struct {
            inner: NativeInner,
            tag: u16,
        };
        const s = S{ .tag = 3, .inner = NativeInner{ .x = 7 } };
        if (!(s.inner.x == 7 and @field(@field(s, "inner"), "x") == 7 and s.tag == 3)) return 8;
    }
    { // nested-context
        const S = struct {
            inner: NativeInner,
        };
        const s: S = .{ .inner = .{ .x = 8 } };
        if (!(s.inner.x == 8 and @TypeOf(s.inner) == NativeInner)) return 9;
    }
    { // array-field
        const S = struct {
            a: [3]u8,
        };
        const s = S{ .a = .{ 3, 5, 7 } };
        if (!(s.a[0] == 3 and s.a[2] == 7 and @field(s, "a")[1] == 5)) return 10;
    }
    { // array-of-structs
        const S = struct {
            a: [2]NativeInner,
        };
        const s: S = .{ .a = .{ .{ .x = 4 }, .{ .x = 9 } } };
        if (!(s.a[0].x == 4 and s.a[1].x == 9)) return 11;
    }
    { // struct-array
        const S = struct {
            x: u8,
        };
        const a = [_]S{ S{ .x = 2 }, S{ .x = 5 } };
        if (!(a[0].x == 2 and a[1].x == 5)) return 12;
    }
    { // matrix-field
        const S = struct {
            a: [2][2]u16,
        };
        const s: S = .{ .a = .{ .{ 1, 2 }, .{ 3, 4 } } };
        if (!(s.a[1][0] == 3 and s.a[0][1] == 2)) return 13;
    }
    { // pointer-field
        const S = struct {
            p: *const u8,
        };
        const s = S{ .p = @ptrFromInt(4096) };
        if (!(@intFromPtr(s.p) == 4096 and @TypeOf(s.p) == *const u8)) return 14;
    }
    { // many-pointer-field
        const S = struct {
            p: [*]const u16,
        };
        const s: S = .{ .p = @ptrFromInt(4096) };
        if (!(@intFromPtr(s.p + 1) == 4098)) return 15;
    }
    { // slice-field
        const S = struct {
            text: []const u8,
        };
        const s = S{ .text = "abc" };
        if (!(s.text.len == 3 and s.text[1] == 98 and @field(s, "text").len == 3)) return 16;
    }
    { // sentinel-slice-field
        const S = struct {
            text: [:0]const u8,
        };
        const s: S = .{ .text = "abc" };
        if (!(s.text.len == 3 and s.text[3] == 0)) return 17;
    }
    { // synthetic-slice-field
        const S = struct {
            data: []const u16,
        };
        const p: [*]const u16 = @ptrFromInt(4096);
        const s = S{ .data = p[0..3] };
        if (!(s.data.len == 3 and @intFromPtr(s.data.ptr) == 4096)) return 18;
    }
    { // empty-typed
        const S = struct {};
        const s = S{};
        if (!(@TypeOf(s) == S and @sizeOf(@TypeOf(s)) == 0)) return 19;
    }
    { // zero-fields
        const S = struct {
            v: void,
            u: u0,
            a: [0]u64,
        };
        const s = S{ .a = .{}, .u = 0, .v = {} };
        if (!(s.u == 0 and @TypeOf(s.v) == void and s.a.len == 0)) return 20;
    }
    { // zero-array-fields
        const S = struct {
            a: [3]u0,
        };
        const s: S = .{ .a = .{ 0, 0, 0 } };
        if (!(s.a[2] == 0)) return 21;
    }
    { // partial-undefined
        const S = struct {
            a: u8,
            b: u16,
        };
        const s = S{ .a = undefined, .b = 7 };
        if (!(s.b == 7 and @TypeOf(s.a) == u8)) return 22;
    }
    { // whole-undefined-one-value
        const S = struct {
            v: void,
            u: u0,
            a: [0]u64,
        };
        const s: S = undefined;
        if (!(s.u == 0 and @TypeOf(s.v) == void and s.a.len == 0)) return 23;
    }
    { // quoted/quote
        const S = struct {
            @"a\"b": u8,
        };
        const s = S{ .@"a\"b" = 7 };
        if (!(s.@"a\"b" == 7 and @field(s, "a\"b") == 7)) return 24;
    }
    { // quoted/space
        const S = struct {
            @"two words": u8,
        };
        const s = S{ .@"two words" = 7 };
        if (!(s.@"two words" == 7 and @field(s, "two words") == 7)) return 25;
    }
    { // many-fields/64
        const S = struct {
            f0: u8,
            f1: u64,
            f2: u8,
            f3: u64,
            f4: u8,
            f5: u64,
            f6: u8,
            f7: u64,
            f8: u8,
            f9: u64,
            f10: u8,
            f11: u64,
            f12: u8,
            f13: u64,
            f14: u8,
            f15: u64,
            f16: u8,
            f17: u64,
            f18: u8,
            f19: u64,
            f20: u8,
            f21: u64,
            f22: u8,
            f23: u64,
            f24: u8,
            f25: u64,
            f26: u8,
            f27: u64,
            f28: u8,
            f29: u64,
            f30: u8,
            f31: u64,
            f32: u8,
            f33: u64,
            f34: u8,
            f35: u64,
            f36: u8,
            f37: u64,
            f38: u8,
            f39: u64,
            f40: u8,
            f41: u64,
            f42: u8,
            f43: u64,
            f44: u8,
            f45: u64,
            f46: u8,
            f47: u64,
            f48: u8,
            f49: u64,
            f50: u8,
            f51: u64,
            f52: u8,
            f53: u64,
            f54: u8,
            f55: u64,
            f56: u8,
            f57: u64,
            f58: u8,
            f59: u64,
            f60: u8,
            f61: u64,
            f62: u8,
            f63: u64,
        };
        const s = S{ .f63 = 64, .f62 = 63, .f61 = 62, .f60 = 61, .f59 = 60, .f58 = 59, .f57 = 58, .f56 = 57, .f55 = 56, .f54 = 55, .f53 = 54, .f52 = 53, .f51 = 52, .f50 = 51, .f49 = 50, .f48 = 49, .f47 = 48, .f46 = 47, .f45 = 46, .f44 = 45, .f43 = 44, .f42 = 43, .f41 = 42, .f40 = 41, .f39 = 40, .f38 = 39, .f37 = 38, .f36 = 37, .f35 = 36, .f34 = 35, .f33 = 34, .f32 = 33, .f31 = 32, .f30 = 31, .f29 = 30, .f28 = 29, .f27 = 28, .f26 = 27, .f25 = 26, .f24 = 25, .f23 = 24, .f22 = 23, .f21 = 22, .f20 = 21, .f19 = 20, .f18 = 19, .f17 = 18, .f16 = 17, .f15 = 16, .f14 = 15, .f13 = 14, .f12 = 13, .f11 = 12, .f10 = 11, .f9 = 10, .f8 = 9, .f7 = 8, .f6 = 7, .f5 = 6, .f4 = 5, .f3 = 4, .f2 = 3, .f1 = 2, .f0 = 1 };
        if (!(s.f0 == 1 and s.f1 == 2 and s.f2 == 3 and s.f3 == 4 and s.f4 == 5 and s.f5 == 6 and s.f6 == 7 and s.f7 == 8 and s.f8 == 9 and s.f9 == 10 and s.f10 == 11 and s.f11 == 12 and s.f12 == 13 and s.f13 == 14 and s.f14 == 15 and s.f15 == 16 and s.f16 == 17 and s.f17 == 18 and s.f18 == 19 and s.f19 == 20 and s.f20 == 21 and s.f21 == 22 and s.f22 == 23 and s.f23 == 24 and s.f24 == 25 and s.f25 == 26 and s.f26 == 27 and s.f27 == 28 and s.f28 == 29 and s.f29 == 30 and s.f30 == 31 and s.f31 == 32 and s.f32 == 33 and s.f33 == 34 and s.f34 == 35 and s.f35 == 36 and s.f36 == 37 and s.f37 == 38 and s.f38 == 39 and s.f39 == 40 and s.f40 == 41 and s.f41 == 42 and s.f42 == 43 and s.f43 == 44 and s.f44 == 45 and s.f45 == 46 and s.f46 == 47 and s.f47 == 48 and s.f48 == 49 and s.f49 == 50 and s.f50 == 51 and s.f51 == 52 and s.f52 == 53 and s.f53 == 54 and s.f54 == 55 and s.f55 == 56 and s.f56 == 57 and s.f57 == 58 and s.f58 == 59 and s.f59 == 60 and s.f60 == 61 and s.f61 == 62 and s.f62 == 63 and s.f63 == 64)) return 26;
    }
    { // anonymous-nested-field
        const S = struct {
            inner: struct {
                x: u8,
            },
        };
        const s: S = .{ .inner = .{ .x = 7 } };
        if (!(s.inner.x == 7)) return 27;
    }
    { // whole-undefined-builtin
        const S = struct {
            x: u8,
        };
        const s: S = undefined;
        const x = @field(s, "x");
        _ = x;
        if (!(@TypeOf(@field(s, "x")) == u8)) return 28;
    }
    { // logical-reference/known-pointer
        const S = struct {
            a: u8,
            b: u8,
        };
        const s = S{ .a = 1, .b = 2 };
        const p = &s;
        if (!(p.a == 1 and p.b == 2)) return 29;
    }
    { // logical-reference/known-builtin-pointer
        const S = struct {
            a: u8,
            b: u8,
        };
        const s = S{ .a = 1, .b = 2 };
        const p = &s;
        if (!(@field(p, "a") == 1 and @field(p, "b") == 2)) return 30;
    }
    { // logical-reference/undefined-pointer-target
        const S = struct {
            a: u8,
            b: u8,
        };
        const s: S = undefined;
        const p = &s;
        const x = p.a;
        _ = x;
        if (!(@TypeOf(p.a) == u8)) return 31;
    }
    { // logical-reference/known-struct-array-slice
        const S = struct {
            a: u8,
            b: u8,
        };
        const a = [_]S{ S{ .a = 1, .b = 2 }, S{ .a = 3, .b = 4 } };
        const sl: []const S = &a;
        if (!(sl.len == 2 and sl[0].a == 1 and sl[1].b == 4)) return 32;
    }
    { // undefined one-possible-value fields normalize independently of stored undef
        const S = struct {
            u: u0,
            a: [3]u0,
            e: NativeEmpty,
        };
        const s = S{ .u = undefined, .a = undefined, .e = undefined };
        if (!(s.u == 0 and s.a[2] == 0 and @TypeOf(s.e) == NativeEmpty)) return 33;
    }
    {
        const a = [_]NativeInner{NativeInner{ .x = 7 }};
        const Wrapped = struct { items: [1]NativeInner };
        const wrapped = Wrapped{ .items = a };
        if (@field(wrapped.items[0], "x") != 7 or @field(@field(wrapped, "items")[0], "x") != 7) return 34;
    }
    return 0;
}
