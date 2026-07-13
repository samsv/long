const std = @import("std");
const VM = @import("../vm.zig").VM;
const RC = @import("ref_counter.zig").RC;
const Obj = @import("../object.zig").Obj;
const Value = @import("../value.zig").Value;

pub const Function = struct {
    name: []const u8,
    vm: VM,
    args: std.ArrayList([]const u8),

    pub fn deinit(self: *Function, gpa: std.mem.Allocator) void {
        self.vm.deint(gpa);

        for (self.args.items) |a| gpa.free(a);
        self.args.deinit(gpa);
    }

    pub fn format(function: Function, writer: *std.Io.Writer) !void {
        try writer.print("{s}", .{function.name});
    }
};

pub const Closure = struct {
    function: RC(Obj),
    upvalues: std.ArrayList(Value),

    pub fn deinit(self: *Closure, gpa: std.mem.Allocator) void {
        self.function.deinit(gpa);

        for (self.upvalues.items) |*up| up.deinit(gpa);
        self.upvalues.deinit(gpa);
    }

    pub fn getFunction(self: Closure) Function {
        return self.function.getUnwrap().function;
    }

    pub fn format(cls: Closure, writer: *std.Io.Writer) !void {
        try writer.print("{s}", .{cls.function.getUnwrap().function.name});
    }
};
