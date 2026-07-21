const std = @import("std");
const Compiler = @import("compiler.zig").Compiler;
const Value = @import("value.zig").Value;

test "if" {
    const gpa = std.testing.allocator;

    const test_cases = [_]struct { []const u8, Value }{
        .{ "x = 5", .{ .number = 5 } },
        .{ "y = if x = 8.5 do x end", .{ .number = 8.5 } },
        .{ "1", .{ .number = 1 } },
        .{ "5 * 2.5", .{ .number = 12.5 } },
        .{ "8 / 2", .{ .number = 4 } },
        .{ "3 / 2", .{ .number = 1.5 } },
        .{ "3 - 2", .{ .number = 1 } },
        .{ "if 1 + 2 do 3 - 4 else 5 - 7 end", .{ .number = -1 } },
        .{ "if nil do 3 - 4 else 5 - 7 end", .{ .number = -2 } },
        .{ "if true do 3 - 4 end", .{ .number = -1 } },
        .{ "if false do 3 - 4 end", .nil },
    };

    for (test_cases) |cs| {
        var vm = try Compiler.compile(gpa, cs[0]);
        defer vm.deinit(gpa);

        try vm.run(gpa);

        try std.testing.expectEqual(1, vm.stack.len());
        try std.testing.expectEqual(cs[1], vm.stack.get(0));
    }
}

test "for loop" {
    const gpa = std.testing.allocator;

    var vm = try Compiler.compile(gpa,
        \\ for x in [1, 2, 3] do
        \\      k = x + 2
        \\      k
        \\ end
    );
    defer vm.deinit(gpa);

    try vm.run(gpa);

    try std.testing.expectEqual(1, vm.stack.len());
    try std.testing.expectEqual(Value{ .number = 5 }, vm.stack.get(0));
}

test "functions" {
    const gpa = std.testing.allocator;

    const test_cases = [_]struct { []const u8, Value }{
        .{ "fun f(x) = x + 1 end f(2)", .{ .number = 3 } },
        .{ "fun add(x, y) = x + y end add(3, 4)", .{ .number = 7 } },
        .{ "fun f(x) = x + 1 end f(f(2))", .{ .number = 4 } },
        .{
            \\ fun h(x) =
            \\      k = x + 1
            \\      k
            \\ end
            \\ h(2)
            ,
            .{ .number = 3 },
        },
        .{
            \\fun f() =
            \\  fun g(x) =
            \\      x + 4
            \\  end
            \\
            \\  g
            \\end
            \\ f()(5)
            ,
            .{ .number = 9 },
        },
    };

    for (test_cases) |cs| {
        var vm = try Compiler.compile(gpa, cs[0]);
        defer vm.deinit(gpa);

        try vm.run(gpa);

        try std.testing.expectEqual(1, vm.stack.len());
        try std.testing.expectEqual(cs[1], vm.stack.get(0));
    }

    var vm = try Compiler.compile(gpa, "fun g(x) = x end g([1, 2, 3])");
    defer vm.deinit(gpa);
    try vm.run(gpa);
    try std.testing.expectEqual(1, vm.stack.len());
    try std.testing.expect(vm.stack.get(0) == .obj);
}

test "closures" {
    const gpa = std.testing.allocator;

    const test_cases = [_]struct { []const u8, Value }{
        .{
            \\ fun f(x) =
            \\      fun g[x](y) =
            \\          x + y
            \\      end
            \\      g
            \\ end
            \\ f(4)(5)
            ,
            .{ .number = 9 },
        },
        .{
            \\ x = 5
            \\ fun f[x](y) =
            \\      x + y
            \\ end
            \\ f(4)
            \\ f(5)
            \\ f(9)
            ,
            .{ .number = 14 },
        },
        .{
            \\ fun f(x) =
            \\      fun g[x](y) =
            \\          x + y
            \\      end
            \\      g
            \\ end
            \\ a = f(1)
            \\ b = f(2)
            \\ a(10) + b(10)
            ,
            .{ .number = 23 },
        },
        .{
            \\ fun f(x) =
            \\      fun g[x](y) =
            \\          fun h[x,y](z) =
            \\              x + y + z
            \\          end
            \\          h
            \\      end
            \\      g
            \\ end
            \\ f(1)(2)(3)
            ,
            .{ .number = 6 },
        },
    };

    for (test_cases) |cs| {
        var vm = try Compiler.compile(gpa, cs[0]);
        defer vm.deinit(gpa);

        try vm.run(gpa);

        try std.testing.expectEqual(1, vm.stack.len());
        try std.testing.expectEqual(cs[1], vm.stack.get(0));
    }
}

test "recursion" {
    const gpa = std.testing.allocator;

    var fib_vm = try Compiler.compile(gpa,
        \\ fun fib(y) =
        \\      if y == 0 do 0
        \\      else if y == 1 do 1
        \\      else fib(y - 1) + fib(y - 2)
        \\      end
        \\ end
        \\ fib(10)
    );
    defer fib_vm.deinit(gpa);

    try fib_vm.run(gpa);

    try std.testing.expectEqual(1, fib_vm.stack.len());
    try std.testing.expectEqual(Value{ .number = 55 }, fib_vm.stack.get(0));

    const self_cases = [_][]const u8{
        "fun f(x) = f end f(1)",
        \\ x = [1, 2]
        \\ fun f[x](y) =
        \\      f
        \\ end
        \\ f(1)
        ,
    };

    for (self_cases) |source| {
        var vm = try Compiler.compile(gpa, source);
        defer vm.deinit(gpa);

        try vm.run(gpa);

        try std.testing.expectEqual(1, vm.stack.len());
        try std.testing.expect(vm.stack.get(0) == .obj);
    }
}

test "closure groups" {
    const gpa = std.testing.allocator;

    const test_cases = [_]struct { []const u8, Value }{
        .{
            \\ fun
            \\ | is_even(x) =
            \\      if x == 0 do true
            \\      else is_odd(x - 1)
            \\      end
            \\ | is_odd(x) =
            \\      if x == 0 do false
            \\      else is_even(x - 1)
            \\      end
            \\ end
            \\ is_even(10)
            ,
            Value.True,
        },
        .{
            \\ fun
            \\ | is_even(x) =
            \\      if x == 0 do true
            \\      else is_odd(x - 1)
            \\      end
            \\ | is_odd(x) =
            \\      if x == 0 do false
            \\      else is_even(x - 1)
            \\      end
            \\ end
            \\ is_odd(10)
            ,
            Value.False,
        },
        .{
            \\ x = 5
            \\ y = 7
            \\ fun
            \\ | f[x](a) = x + a
            \\ | g[y](a) = y + a
            \\ end
            \\ f(1) + g(1)
            ,
            .{ .number = 14 },
        },
        .{
            \\ x = 100
            \\ fun
            \\ | f[x](n) = x + g(n)
            \\ | g(n) = n + 1
            \\ end
            \\ f(1)
            ,
            .{ .number = 102 },
        },
        .{
            \\ fun make(x) =
            \\      fun
            \\      | inc[x](n) = x + n
            \\      | dec[x](n) = x - n
            \\      end
            \\ end
            \\ make(10)(3)
            ,
            .{ .number = 7 },
        },
    };

    for (test_cases) |cs| {
        var vm = try Compiler.compile(gpa, cs[0]);
        defer vm.deinit(gpa);

        try vm.run(gpa);

        try std.testing.expectEqual(1, vm.stack.len());
        try std.testing.expectEqual(cs[1], vm.stack.get(0));
    }
}
