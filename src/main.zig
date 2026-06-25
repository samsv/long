const std = @import("std");
const zsv = @import("zsv");

const Io = std.Io;
const Scanner = zsv.scanner.Scanner;
const parser = zsv.parser;
const c = zsv.compiler;
const vm_ = zsv.vm;
const obj = zsv.obj;
const Value = zsv.Value;

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
        const MyList = zsv.List(u32);
        var list = try MyList.init(gpa, &[_]u32{ 1, 2, 3 });
        defer list.deinit(gpa);

        var new_list_0 = try MyList.append(&list, gpa, 4);
        defer new_list_0.deinit(gpa);

        var new_list_1 = try MyList.append(&list, gpa, 5);
        defer new_list_1.deinit(gpa);

        var iter = MyList.Iterator.init(&new_list_0);
        defer iter.deinit(gpa);

        while (iter.next()) |v| {
            std.debug.print("{}\n", .{v});
        }
    }

    {
        var scanner = try Scanner.init("= ==( \"hello, world\") 1.25[5] , . |> \n_hello[man 你好");
        while (scanner.next() catch |err| {
            std.debug.print("{s}\n", .{scanner.err_ctx.?.reason});
            std.debug.print("line: {}\n", .{scanner.err_ctx.?.line});
            return err;
        }) |next| {
            try stdout_writer.print("{f}\n", .{next.kind});
            try stdout_writer.flush();
        }
    }

    {
        var scanner = try Scanner.init(
            // this will fail at runtime, as k is not a global.
            \\y = if x = true do
            \\    k = 5
            \\    l = if k do
            \\        k + 4
            \\    else
            \\        k - 4
            \\    end
            \\    l + 2
            \\else
            \\    z = 5 + 1
            \\    z
            \\end
        );

        var sexpr = try parser.expr(gpa, &scanner, 0);
        defer sexpr.deinit(gpa);
        try stdout_writer.print("{f}\n", .{sexpr});
        try stdout_writer.flush();

        var vm = vm_.VM.init();
        defer vm.deint(gpa);

        var compiler = c.Compiler.init();
        defer compiler.deinit(gpa);

        try compiler.compile(gpa, sexpr, &vm);

        try vm.printInstructions(stdout_writer);
        try stdout_writer.writeByte('\n');
        try stdout_writer.flush();

        try vm.run(gpa);

        try vm.printStack(stdout_writer);
        try stdout_writer.writeByte('\n');

        try vm.printLocals(stdout_writer);
        try stdout_writer.writeByte('\n');

        try vm.printGlobals(stdout_writer);
        try stdout_writer.writeByte('\n');
        try stdout_writer.flush();
    }

    {
        std.debug.print("\n", .{});
        var scanner = try Scanner.init("x = [1, 2, 3]");

        var sexpr = try parser.expr(gpa, &scanner, 0);
        defer sexpr.deinit(gpa);

        var vm = vm_.VM.init();
        defer vm.deint(gpa);

        var compiler = c.Compiler.init();
        defer compiler.deinit(gpa);

        try compiler.compile(gpa, sexpr, &vm);
        try vm.run(gpa);

        try vm.printStack(stdout_writer);
        try stdout_writer.writeByte('\n');
        try stdout_writer.flush();

        std.debug.print("bye\n\n", .{});
    }
}
