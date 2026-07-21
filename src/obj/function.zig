const std = @import("std");
const VM = @import("../vm.zig").VM;
const Value = @import("../value.zig").Value;
const RC = @import("ref_counter.zig").RC;

pub const Closure = struct {
    function: *const VM,
    upvalues: std.ArrayList(Value),

    pub fn deinit(self: *Closure, gpa: std.mem.Allocator) void {
        for (self.upvalues.items) |*up| up.deinit(gpa);
        self.upvalues.deinit(gpa);
    }

    pub fn getVM(self: Closure) VM {
        return self.function.*;
    }

    pub fn format(cls: Closure, writer: *std.Io.Writer) !void {
        try writer.print("{s}", .{cls.function.name});
    }
};

pub const ClosureGroup = struct {
    members: []const VM,
    upvalues: std.ArrayList(Value),

    pub fn deinit(self: *ClosureGroup, gpa: std.mem.Allocator) void {
        for (self.upvalues.items) |*up| up.deinit(gpa);
        self.upvalues.deinit(gpa);
    }
};

pub const ClosureMember = struct {
    group: RC(ClosureGroup),
    index: usize,

    pub fn deinit(self: *ClosureMember, gpa: std.mem.Allocator) void {
        self.group.deinit(gpa);
    }

    pub fn getVM(self: ClosureMember) VM {
        return self.group.getUnwrap().members[self.index];
    }

    pub fn format(cls: ClosureMember, writer: *std.Io.Writer) !void {
        try writer.print("{s}", .{cls.group.getUnwrap().members[cls.index].name});
    }
};
