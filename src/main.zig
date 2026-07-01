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
        var scanner = try Scanner.init(
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

        var builder = vm_.VMBuilder.init();

        var compiler = c.Compiler.init();
        defer compiler.deinit(gpa);

        try compiler.compileBuilder(gpa, sexpr, &builder);

        var vm = builder.build();
        defer vm.deint(gpa);

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
        var vm = try c.Compiler.compile(gpa, "for x in [1, 2, 3] do x end");
        defer vm.deint(gpa);

        try vm.run(gpa);

        try vm.printInstructions(stdout_writer);
        try stdout_writer.writeByte('\n');

        try vm.printConstants(stdout_writer);
        try stdout_writer.writeByte('\n');

        try vm.printLocals(stdout_writer);
        try stdout_writer.writeByte('\n');

        try vm.printGlobals(stdout_writer);
        try stdout_writer.writeByte('\n');

        try vm.printStack(stdout_writer);
        try stdout_writer.writeByte('\n');
        try stdout_writer.flush();

        std.debug.print("bye\n\n", .{});
    }
}
