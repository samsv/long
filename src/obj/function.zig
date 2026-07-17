const std = @import("std");
const VM = @import("../vm.zig").VM;
const RC = @import("ref_counter.zig").RC;
const Obj = @import("../object.zig").Obj;
const Value = @import("../value.zig").Value;

pub const Function = struct {
    name: []const u8,
    vm: VM,

    pub fn deinit(self: *Function, gpa: std.mem.Allocator) void {
        self.vm.deint(gpa);
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

pub const ClosureMember = struct {
    // The first N values are self and mutually recursive functions
    upvalues: RC(std.ArrayList(Value)),
    index: usize,

    fn deinitUpvalues(vs: *std.ArrayList(Value), gpa: std.mem.Allocator) void {
        for (vs.items) |*value| value.deinit(gpa);
    }

    pub fn deinit(self: *ClosureMember, gpa: std.mem.Allocator) void {
        self.upvalues.deinitWithCb(gpa, deinitUpvalues);
    }

    pub fn getFunction(self: ClosureMember) Function {
        return self.upvalues.getUnwrap().items[self.index].obj.getUnwrap().function;
    }

    pub fn format(cls: ClosureMember, writer: *std.Io.Writer) !void {
        try writer.print("{s}", .{cls.getFunction().name});
    }
};
