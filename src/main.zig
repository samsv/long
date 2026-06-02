const std = @import("std");
const Io = std.Io;
const Scanner = @import("scanner.zig").Scanner;

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

    try zsv.printAnotherMessage(stdout_writer);

    try stdout_writer.flush(); // Don't forget to flush!

    var scanner = Scanner.init("\"hello, world\"");
    const next = try scanner.next();
    std.debug.print("{s}\n", .{next.?.kind.literal.string});
}
