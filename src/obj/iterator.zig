const std = @import("std");
const Value = @import("../value.zig").Value;
const List = @import("list.zig").List(Value);

pub const Iterator = union(enum) {
    list: List.Iterator,

    pub fn init(from: Value) !Iterator {
        return switch (from) {
            .obj => |obj| switch (obj.getPtrUnwrap().*) {
                .list => |*l| .{ .list = l.iter() },
                else => error.TypeNotIterable,
            },
            else => error.TypeNotIterable,
        };
    }

    pub fn deinit(self: *Iterator, gpa: std.mem.Allocator) void {
        switch (self.*) {
            inline else => |*iter| iter.deinit(gpa),
        }
    }

    pub fn next(self: *Iterator) Value {
        return switch (self.*) {
            inline else => |*iter| iter.next(),
        } orelse .nil;
    }

    pub fn format(self: Iterator, writer: *std.Io.Writer) !void {
        try writer.print("{s} iterator", .{@tagName(self)});
    }
};
