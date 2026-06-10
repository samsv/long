const std = @import("std");
const zsv = @import("zsv");

const Io = std.Io;
const Scanner = zsv.scanner.Scanner;
const parser = zsv.parser;
const c = zsv.compiler;
const vm_ = zsv.vm;

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
        var scanner = try Scanner.init("= ==( \"hello, world\") 1.25[5] , . |> \n_hello[man 你好");
        while (scanner.next() catch |err| {
            std.debug.print("{s}\n", .{scanner.err_ctx.?.reason});
            std.debug.print("line: {}\n", .{scanner.err_ctx.?.line});
            return err;
        }) |next| {
            try next.kind.print(stdout_writer);
            try stdout_writer.print("\n", .{});
            try stdout_writer.flush();
        }
    }

    {
        var scanner = try Scanner.init("if x + 5 do y + 1 else z + 1");
        var sexpr = try parser.expr(gpa, &scanner, 0);
        defer sexpr.deinit(gpa);

        var a: std.Io.Writer.Allocating = .init(gpa);
        defer a.deinit();
        try sexpr.print(&a.writer);

        std.debug.print("{s}\n", .{a.written()});
    }

    {
        var a: std.Io.Writer.Allocating = .init(gpa);
        defer a.deinit();

        var scanner = try Scanner.init("y = if x = 5 do x");

        var sexpr = try parser.expr(gpa, &scanner, 0);
        defer sexpr.deinit(gpa);
        try sexpr.print(&a.writer);
        std.debug.print("{s}\n", .{a.written()});

        _ = a.writer.consumeAll();

        var vm = vm_.VM.init();
        defer vm.deint(gpa);

        var compiler = c.Compiler.init();
        defer compiler.deinit(gpa);

        try compiler.compile(gpa, sexpr, &vm);

        try vm.printInstructions(&a.writer);
        std.debug.print("{s}\n", .{a.written()});

        try vm.run(gpa);

        _ = a.writer.consumeAll();
        try vm.printStack(&a.writer);
        std.debug.print("{s}\n", .{a.written()});
    }
}
