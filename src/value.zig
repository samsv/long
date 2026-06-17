const std = @import("std");

const Obj = @import("object.zig").Obj;

pub const Value = union(enum) {
    number: f64,
    boolean: bool,
    nil,

    pub const True: Value = .{ .boolean = true };
    pub const False: Value = .{ .boolean = false };

    pub fn deinit(value: *Value, gpa: std.mem.Allocator) void {
        switch (value.*) {
            .obj => |obj| obj.release(gpa),
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

    pub fn print(value: Value, writer: *std.Io.Writer) !void {
        switch (value) {
            .nil => try writer.writeAll("nil"),
            inline else => |v| try writer.print("{}", .{v}),
        }
    }
};
