fn f() !void {
    if (a) |x| {
        use(x);
    } else |err| {
        return err;
    }
    while (i < 10) : (i += 1) {
        continue;
    }
    outer: while (it.next()) |item| {
        if (item) break :outer;
    } else unreachable;
    for (items, 0..) |item, index| {
        _ = .{ item, index };
    }
    inline for (fields) |*field| field.* = 0;
    defer cleanup();
    errdefer log(0);
    comptime var count = 0;
    nosuspend call();
    label: {
        break :label;
    }
    switch (x) {
        else => {},
    }
    if (a) b() else if (c) d() else e();
    return;
}
