const std = @import("std");

const Obj = @import("object.zig").Obj;
const Iterator = @import("object.zig").Iterator;
const RC = @import("ref_counter.zig").RC;
const Chunk = @import("vm.zig").Chunk;

pub const Value = union(enum) {
    number: f64,
    boolean: bool,
    nil,
    obj: RC(Obj),

    pub const True: Value = .{ .boolean = true };
    pub const False: Value = .{ .boolean = false };

    pub fn deinit(value: *Value, gpa: std.mem.Allocator) void {
        switch (value.*) {
            .obj => |*obj| obj.deinit(gpa),
            else => {},
        }
    }

    pub fn borrow(self: *Value) Value {
        return switch (self.*) {
            .obj => |*obj| .{ .obj = obj.borrow() catch unreachable },
            else => self.*,
        };
    }

    pub fn asInt(v: Value, comptime T: type) !T {
        return switch (v) {
            .number => |n| if (n == @trunc(n)) @intFromFloat(n) else error.FloatNotAnInt,
            else => error.TypeNotAnInt,
        };
    }

    pub fn asIntUnsafe(v: Value, comptime T: type) T {
        return @intFromFloat(v.number);
    }

    pub fn isTruthy(v: Value) bool {
        return switch (v) {
            .nil => false,
            .boolean => |b| b,
            else => true,
        };
    }

    pub fn initList(gpa: std.mem.Allocator, values: []Value) !Value {
        var list = try Obj.initList(gpa, values);
        errdefer list.deinit(gpa);

        const obj = try RC(Obj).init(gpa, list);
        return .{ .obj = obj };
    }

    pub fn initFunction(
        gpa: std.mem.Allocator,
        name: []const u8,
        chunk: Chunk,
        upvalues: std.ArrayList(Value),
        args: std.ArrayList([]const u8),
    ) !Value {
        const fun = Obj.initFunction(name, chunk, upvalues, args);
        const obj = try RC(Obj).init(gpa, fun);
        return .{ .obj = obj };
    }

    pub fn format(value: Value, writer: *std.Io.Writer) !void {
        try switch (value) {
            .nil => writer.writeAll("nil"),
            .obj => |o| writer.print("{f}", .{o.getUnwrap()}),
            inline else => |v| writer.print("{}", .{v}),
        };
    }

    pub fn createIterator(value: Value, gpa: std.mem.Allocator) !Value {
        var iter = try Iterator.init(value);
        errdefer iter.deinit(gpa);

        const obj = try RC(Obj).init(gpa, .{ .iterator = iter });
        return .{ .obj = obj };
    }

    pub fn eql(v1: Value, v2: Value) bool {
        return switch (v1) {
            .obj => |o1| switch (v2) {
                .obj => |o2| std.meta.eql(o1.getUnwrap(), o2.getUnwrap()),
                else => false,
            },
            else => std.meta.eql(v1, v2),
        };
    }
};
