const std = @import("std");
const zsv = @import("zsv");

const Io = std.Io;
const Scanner = zsv.scanner.Scanner;
const parser = zsv.parser;
const c = zsv.compiler;
const vm_ = zsv.vm;
const obj = zsv.obj;
const Value = zsv.Value;

fn printVM(vm: vm_.VM, stdout_writer: *std.Io.Writer) !void {
    try vm.printInstructions(stdout_writer);
    try stdout_writer.writeByte('\n');

    try vm.printConstants(stdout_writer);
    try stdout_writer.writeByte('\n');

    try vm.printLocals(stdout_writer);
    try stdout_writer.writeByte('\n');

    try vm.printUpvalues(stdout_writer);
    try stdout_writer.writeByte('\n');

    try vm.printGlobals(stdout_writer);
    try stdout_writer.writeByte('\n');

    try vm.printStack(stdout_writer);
    try stdout_writer.writeByte('\n');
    try stdout_writer.flush();
}

pub fn main(init: std.process.Init) !void {
    const gpa = init.gpa;

    // In order to do I/O operations need an `Io` instance.
    const io = init.io;

    // Stdout is for the actual output of your application, for example if you
    // are implementing gzip, then only the compressed bytes should be sent to
    // stdout, not any debugging messages.
    var stdout_buffer: [1024]u8 = undefined;
    var stdout_file_writer: Io.File.Writer = .init(.stdout(), io, &stdout_buffer);
    const stdout_writer = &stdout_file_writer.interface;

    {
        std.debug.print("\n", .{});
        var vm = try c.Compiler.compile(gpa,
            \\ fun f(x) =
            \\     x + 1
            \\ end
            \\ f(2)
        );
        defer vm.deint(gpa);

        try vm.run(gpa);
        try printVM(vm, stdout_writer);
    }

    {
        const text =
            \\fun f(x) =
            \\  fun g[x](y) =
            \\      x + y
            \\  end
            \\
            \\  g
            \\end
            \\ f(4)(5)
        ;

        var scanner = try Scanner.init(text);

        while (try scanner.peek()) |_| {
            var sexpr = try parser.expr(gpa, &scanner, 0);
            defer sexpr.deinit(gpa);
            try stdout_writer.print("{f}\n", .{sexpr});
            try stdout_writer.flush();
        }

        var vm = try c.Compiler.compile(gpa, text);
        defer vm.deint(gpa);

        try vm.run(gpa);

        const fn_vm = vm.globals.get(0).obj.getUnwrap().closure.getFunction().vm;
        const g_vm = fn_vm.chunk.constants.items[0].obj.getUnwrap().function.vm;

        try printVM(g_vm, stdout_writer);
        try printVM(fn_vm, stdout_writer);
        try printVM(vm, stdout_writer);
    }

    {
        const text =
            \\ x = 5
            \\ fun f[x](y) =
            \\      x + y
            \\ end
            \\ f(4)
            \\ f(5)
            \\ f(9)
        ;

        var scanner = try Scanner.init(text);

        while (try scanner.peek()) |_| {
            var sexpr = try parser.expr(gpa, &scanner, 0);
            defer sexpr.deinit(gpa);
            try stdout_writer.print("{f}\n", .{sexpr});
            try stdout_writer.flush();
        }

        var vm = try c.Compiler.compile(gpa, text);
        defer vm.deint(gpa);

        try vm.run(gpa);
        try printVM(vm, stdout_writer);
    }

    {
        const text =
            \\ fun fib(y) =
            \\      if y == 0 do 0
            \\      else if y == 1 do 1
            \\      else fib(y - 1) + fib(y - 2)
            \\      end
            \\ end
            \\ fib(10)
        ;

        var scanner = try Scanner.init(text);

        while (try scanner.peek()) |_| {
            var sexpr = try parser.expr(gpa, &scanner, 0);
            defer sexpr.deinit(gpa);
            try stdout_writer.print("{f}\n", .{sexpr});
            try stdout_writer.flush();
        }

        var vm = try c.Compiler.compile(gpa, text);
        defer vm.deint(gpa);

        try vm.run(gpa);
        try printVM(vm, stdout_writer);
    }
}
