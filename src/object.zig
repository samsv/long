const std = @import("std");
const RC = @import("ref_counter.zig").RC;
const Value = @import("value.zig").Value;
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

pub const Obj = union(enum) {
    list: List,
    iterator: Iterator,

    pub fn deinit(obj: *Obj, gpa: std.mem.Allocator) void {
        switch (obj.*) {
            inline else => |*o| o.deinit(gpa),
        }
    }

    pub fn initList(gpa: std.mem.Allocator, values: []Value) !Obj {
        return .{
            .list = try List.initOwned(gpa, values),
        };
    }

    pub fn format(obj: Obj, writer: *std.Io.Writer) !void {
        switch (obj) {
            .list => |list| {
                try writer.writeByte('[');

                var iter = list.iterNoBorrow();
                var first = true;
                while (iter.next()) |v| {
                    if (!first)
                        _ = try writer.write(", ")
                    else
                        first = false;
                    try v.format(writer);
                }

                try writer.writeByte(']');
            },
            .iterator => |iter| try iter.format(writer),
        }
    }
};
