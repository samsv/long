const std = @import("std");

const Obj = @import("object.zig").Obj;

pub const Value = union(enum) {
    number: f64,
    boolean: bool,
    nil,
    obj: *Obj,

    pub const True: Value = .{ .boolean = true };
    pub const False: Value = .{ .boolean = false };

    pub fn deinit(value: *Value, gpa: std.mem.Allocator) void {
        switch (value.*) {
            .obj => |obj| obj.deinit(gpa),
            else => {},
        }
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
        const obj = try gpa.create(Obj);
        obj.* = try Obj.initList(gpa, values);
        return .{
            .obj = obj,
        };
    }

    pub fn format(value: Value, writer: *std.Io.Writer) !void {
        try switch (value) {
            .nil => writer.writeAll("nil"),
            .obj => |o| writer.print("{f}", .{o}),
            inline else => |v| writer.print("{}", .{v}),
        };
    }
};
