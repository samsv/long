const std = @import("std");
const VM = @import("vm.zig").VM;
const Value = @import("value.zig").Value;

pub const Function = struct {
    name: []const u8,
    vm: VM,
    upvalues: std.ArrayList(Value),
    args: std.ArrayList([]const u8),

    pub fn deinit(self: *Function, gpa: std.mem.Allocator) void {
        self.vm.deint(gpa);
        defer self.upvalues.deinit(gpa);
        defer self.args.deinit(gpa);

        for (self.upvalues.items) |*up| up.deinit(gpa);
        for (self.args.items) |a| gpa.free(a);
    }

    pub fn format(function: Function, writer: *std.Io.Writer) !void {
        try writer.print("{s}", .{function.name});
    }
};

