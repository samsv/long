const std = @import("std");
const Io = std.Io;
const Scanner = @import("scanner.zig").Scanner;
const parser = @import("parser.zig");

const zsv = @import("zsv");

pub fn main(init: std.process.Init) !void {
    const arena: std.mem.Allocator = init.arena.allocator();

    // Accessing command line arguments:
    const args = try init.minimal.args.toSlice(arena);
    for (args) |arg| {
        std.log.info("arg: {s}", .{arg});
    }

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

    var scanner = try Scanner.init("1 + 2");
    _ = try parser.expr(arena, &scanner, 0);
}
