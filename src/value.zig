const std = @import("std");

pub const Value = union(enum) {
    number: f64,
    boolean: bool,
    nil,

    pub const True: Value = .{ .boolean = true };
    pub const False: Value = .{ .boolean = false };

    pub fn print(value: Value, writer: *std.Io.Writer) !void {
        switch (value) {
            .nil => try writer.writeAll("nil"),
            inline else => |v| try writer.print("{}", .{v}),
        }
    }
};
